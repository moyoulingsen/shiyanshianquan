#include "indoor_camera_capture.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/isp.h"
#include "driver/i2c_master.h"
#include "esp_cache.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"
#include "esp_check.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_sccb_i2c.h"
#include "esp_sccb_intf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_reader.h"

static const char *TAG = "indoor_camera";
static bool s_ready;

// Pin assignment matches the ESP32-P4 HMI subboard fly-wires the indoor
// hardware is built with (5V, GND, PWM26, RST27).
#define LCD_BACKLIGHT_GPIO       GPIO_NUM_26
#define LCD_RESET_GPIO           GPIO_NUM_27
#define LCD_BACKLIGHT_ON_LEVEL   1

// MIPI PHY LDO channel/voltage as used by the official mipi_isp_dsi example.
#define MIPI_PHY_LDO_CHAN_ID     3
#define MIPI_PHY_LDO_VOLTAGE_MV  2500

// EK79007 panel timings, copied from
// $IDF_PATH/examples/peripherals/camera/common_components/dsi_init.
// Refresh rate = 48 MHz / (10+120+120+1024) / (1+20+10+600) = 60 Hz.
#define DSI_DPI_CLK_MHZ          48
#define DSI_H_RES                1024
#define DSI_V_RES                600
#define DSI_HSYNC                10
#define DSI_HBP                  120
#define DSI_HFP                  120
#define DSI_VSYNC                1
#define DSI_VBP                  20
#define DSI_VFP                  10

#define CSI_LANE_BITRATE_MBPS    200  // line_rate = pclk * 4
#define CSI_SCCB_FREQ_HZ         (100 * 1000)
#define CAM_FORMAT_NAME          "MIPI_2lane_24Minput_RAW8_1024x600_30fps"

#define FRAME_BUFFER_PIXEL_BYTES 2
#define FRAME_BUFFER_SIZE        (DSI_H_RES * DSI_V_RES * FRAME_BUFFER_PIXEL_BYTES)

static esp_cam_ctlr_handle_t s_cam_handle;
static esp_cam_ctlr_trans_t  s_cam_trans;

static bool IRAM_ATTR on_camera_get_new_vb(esp_cam_ctlr_handle_t handle,
                                           esp_cam_ctlr_trans_t *trans,
                                           void *user_data)
{
    (void)handle;
    esp_cam_ctlr_trans_t *seed = (esp_cam_ctlr_trans_t *)user_data;
    trans->buffer = seed->buffer;
    trans->buflen = seed->buflen;
    return false;
}

static bool IRAM_ATTR on_camera_trans_finished(esp_cam_ctlr_handle_t handle,
                                               esp_cam_ctlr_trans_t *trans,
                                               void *user_data)
{
    (void)handle;
    (void)trans;
    (void)user_data;
    return false;
}

static esp_err_t init_backlight_and_reset(void)
{
    const gpio_config_t rst_cfg = {
        .pin_bit_mask = BIT64(LCD_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst_cfg), TAG, "configure LCD reset GPIO failed");
    gpio_set_level(LCD_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    const gpio_config_t bk_cfg = {
        .pin_bit_mask = BIT64(LCD_BACKLIGHT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bk_cfg), TAG, "configure LCD backlight GPIO failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_BACKLIGHT_GPIO, LCD_BACKLIGHT_ON_LEVEL),
                        TAG, "set LCD backlight failed");

    ESP_LOGI(TAG, "LCD reset/backlight done (RST=GPIO%d, BL=GPIO%d)",
             LCD_RESET_GPIO, LCD_BACKLIGHT_GPIO);
    return ESP_OK;
}

static esp_err_t init_mipi_ldo(void)
{
    esp_ldo_channel_handle_t ldo_handle = NULL;
    const esp_ldo_channel_config_t cfg = {
        .chan_id = MIPI_PHY_LDO_CHAN_ID,
        .voltage_mv = MIPI_PHY_LDO_VOLTAGE_MV,
    };
    return esp_ldo_acquire_channel(&cfg, &ldo_handle);
}

static esp_err_t init_dsi_panel(esp_lcd_panel_handle_t *out_panel, void **out_fb)
{
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    const esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .lane_bit_rate_mbps = 1000,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), TAG, "new dsi bus failed");

    esp_lcd_panel_io_handle_t dbi_io = NULL;
    const esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io),
                        TAG, "new dbi io failed");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .num_fbs = 1,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DSI_DPI_CLK_MHZ,
        .virtual_channel = 0,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .video_timing = {
            .h_size = DSI_H_RES,
            .v_size = DSI_V_RES,
            .hsync_back_porch = DSI_HBP,
            .hsync_pulse_width = DSI_HSYNC,
            .hsync_front_porch = DSI_HFP,
            .vsync_back_porch = DSI_VBP,
            .vsync_pulse_width = DSI_VSYNC,
            .vsync_front_porch = DSI_VFP,
        },
    };
    ek79007_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ek79007(dbi_io, &panel_cfg, out_panel),
                        TAG, "new ek79007 panel failed");

    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(*out_panel, 1, out_fb),
                        TAG, "get frame buffer failed");
    return ESP_OK;
}

static esp_err_t configure_sc2336_on_shared_bus(i2c_master_bus_handle_t bus)
{
    esp_cam_sensor_device_t *cam = NULL;
    esp_cam_sensor_config_t cam_cfg = {
        .reset_pin = -1,
        .pwdn_pin = -1,
        .xclk_pin = -1,
    };

    // Walk every registered sensor probe, but the only sensor compiled in is
    // SC2336 (per sdkconfig.defaults), so this is short.
    for (esp_cam_sensor_detect_fn_t *p = &__esp_cam_sensor_detect_fn_array_start;
         p < &__esp_cam_sensor_detect_fn_array_end; ++p) {
        sccb_i2c_config_t sccb_cfg = {
            .scl_speed_hz = CSI_SCCB_FREQ_HZ,
            .device_address = p->sccb_addr,
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        };
        ESP_RETURN_ON_ERROR(sccb_new_i2c_io(bus, &sccb_cfg, &cam_cfg.sccb_handle),
                            TAG, "create sccb io failed");
        cam_cfg.sensor_port = p->port;
        cam = (*(p->detect))(&cam_cfg);
        if (cam) {
            if (p->port != ESP_CAM_SENSOR_MIPI_CSI) {
                ESP_LOGE(TAG, "detected camera on non-MIPI port");
                return ESP_ERR_INVALID_STATE;
            }
            break;
        }
        esp_sccb_del_i2c_io(cam_cfg.sccb_handle);
        cam_cfg.sccb_handle = NULL;
    }

    if (cam == NULL) {
        ESP_LOGE(TAG, "no SC2336 detected on SCCB bus");
        return ESP_ERR_NOT_FOUND;
    }

    esp_cam_sensor_format_array_t fmt_array = {0};
    esp_cam_sensor_query_format(cam, &fmt_array);
    const esp_cam_sensor_format_t *parray = fmt_array.format_array;
    const esp_cam_sensor_format_t *cur_fmt = NULL;
    for (int i = 0; i < fmt_array.count; i++) {
        if (strcmp(parray[i].name, CAM_FORMAT_NAME) == 0) {
            cur_fmt = &parray[i];
            break;
        }
    }
    if (cur_fmt == NULL) {
        ESP_LOGE(TAG, "SC2336 format '%s' not supported", CAM_FORMAT_NAME);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_RETURN_ON_ERROR(esp_cam_sensor_set_format(cam, cur_fmt), TAG, "set sensor format failed");

    int enable_flag = 1;
    ESP_RETURN_ON_ERROR(esp_cam_sensor_ioctl(cam, ESP_CAM_SENSOR_IOC_S_STREAM, &enable_flag),
                        TAG, "sensor start stream failed");
    ESP_LOGI(TAG, "SC2336 configured: %s", cur_fmt->name);
    return ESP_OK;
}

static esp_err_t init_csi_and_isp(void *frame_buffer)
{
    s_cam_trans.buffer = frame_buffer;
    s_cam_trans.buflen = FRAME_BUFFER_SIZE;

    const esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id = 0,
        .h_res = DSI_H_RES,
        .v_res = DSI_V_RES,
        .lane_bit_rate_mbps = CSI_LANE_BITRATE_MBPS,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RAW8,
        .data_lane_num = 2,
        .byte_swap_en = false,
        .queue_items = 1,
    };
    ESP_RETURN_ON_ERROR(esp_cam_new_csi_ctlr(&csi_cfg, &s_cam_handle), TAG, "csi ctlr new failed");

    const esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = on_camera_get_new_vb,
        .on_trans_finished = on_camera_trans_finished,
    };
    ESP_RETURN_ON_ERROR(esp_cam_ctlr_register_event_callbacks(s_cam_handle, &cbs, &s_cam_trans),
                        TAG, "register csi callbacks failed");
    ESP_RETURN_ON_ERROR(esp_cam_ctlr_enable(s_cam_handle), TAG, "csi enable failed");

    isp_proc_handle_t isp_proc = NULL;
    const esp_isp_processor_cfg_t isp_cfg = {
        .clk_hz = 80 * 1000 * 1000,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RGB565,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = DSI_H_RES,
        .v_res = DSI_V_RES,
    };
    ESP_RETURN_ON_ERROR(esp_isp_new_processor(&isp_cfg, &isp_proc), TAG, "isp new failed");
    ESP_RETURN_ON_ERROR(esp_isp_enable(isp_proc), TAG, "isp enable failed");
    return ESP_OK;
}

static void camera_capture_task(void *arg)
{
    (void)arg;
    while (true) {
        esp_err_t ret = esp_cam_ctlr_receive(s_cam_handle, &s_cam_trans, ESP_CAM_CTLR_MAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_cam_ctlr_receive: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

esp_err_t indoor_camera_capture_init(void)
{
    i2c_master_bus_handle_t i2c_bus = sensor_reader_get_i2c_bus();
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "shared I2C bus is unavailable; sensor_reader must be initialized first");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(init_backlight_and_reset(), TAG, "backlight/reset init failed");
    ESP_RETURN_ON_ERROR(init_mipi_ldo(), TAG, "MIPI PHY LDO acquire failed");

    esp_lcd_panel_handle_t dpi_panel = NULL;
    void *frame_buffer = NULL;
    ESP_RETURN_ON_ERROR(init_dsi_panel(&dpi_panel, &frame_buffer), TAG, "DSI panel init failed");

    ESP_RETURN_ON_ERROR(configure_sc2336_on_shared_bus(i2c_bus), TAG, "SC2336 configuration failed");
    ESP_RETURN_ON_ERROR(init_csi_and_isp(frame_buffer), TAG, "CSI/ISP init failed");

    // Reset the DPI panel and pre-fill the frame buffer to white so the user
    // sees a clean handover from "backlight only" to "live video".
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(dpi_panel), TAG, "DPI panel reset failed");
    memset(frame_buffer, 0xFF, FRAME_BUFFER_SIZE);
    esp_cache_msync(frame_buffer, FRAME_BUFFER_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    ESP_RETURN_ON_ERROR(esp_cam_ctlr_start(s_cam_handle), TAG, "csi start failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(dpi_panel), TAG, "DPI panel init failed");

    BaseType_t ok = xTaskCreatePinnedToCore(camera_capture_task, "cam_rx", 4096, NULL, 6, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create camera capture task");
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(TAG, "indoor camera pipeline ready: SC2336 -> CSI -> ISP -> DSI(EK79007)");
    return ESP_OK;
}

bool indoor_camera_capture_is_ready(void)
{
    return s_ready;
}
