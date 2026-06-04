# LabGuard ESP-DL Probe

`espdl_probe` 是 LabGuard 固件里的**隔离 ESP-DL 探针组件**，用于在不改动 PPE / hazard / door / risk-fusion 业务逻辑的前提下，单独验证 `.espdl` 模型能否在 ESP32-P4 工程中加载和执行自检。

它现在位于正式公共组件目录：

```text
firmware/components/espdl_probe
```

两块板的工程都通过 `firmware/<node>/CMakeLists.txt` 中的 `../components` 自动发现这个组件；主程序只有在 `CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT=y` 时才会调用 `espdl_probe_run_once()`。

## 默认状态

默认关闭：

```text
CONFIG_LABGUARD_ESPDL_PROBE_ENABLE is not set
CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT is not set
```

关闭时组件只编译一个很小的 C wrapper，不链接 ESP-DL C++ runtime，也不会增加主业务流程风险。

## 它不会做什么

这个组件**不直接接入**这些业务路径：

```text
ppe_infer_run
hazard_infer_run
door_fsm_evaluate
risk_fusion_evaluate
```

正式火焰/烟雾业务入口仍然是：

```text
firmware/esp_indoor/components/hazard_infer
```

## 支持的模型加载模式

### 1. rodata

把组件目录下的模型嵌入 app 镜像：

```text
model/model.espdl
target_add_aligned_binary_data(...)
_binary_model_espdl_start
dl::Model(..., fbs::MODEL_LOCATION_IN_FLASH_RODATA)
```

优点是路径简单；缺点是 app 分区会直接增加 `.espdl` 模型大小。

### 2. SD card

从 SD 卡路径加载：

```text
CONFIG_LABGUARD_ESPDL_PROBE_SDCARD_PATH
dl::Model(..., fbs::MODEL_LOCATION_IN_SDCARD)
```

这个模式更接近当前室内 `hazard_infer` 的真实部署方式，但仍然会链接 ESP-DL runtime。

## 当前模型产物

组件内保留了 POC 导出的真实产物：

```text
model/model.espdl
model/model.info
model/model.json
```

A7 交接记录中的摘要：

```text
model/model.espdl bytes=3603872 sha256=3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea
model/model.info  bytes=22341686 sha256=5b20d881dd702e0ee9a1deb92095d77588eb161a9ed35436b589f1f0c462832e
model/model.json  bytes=984482 sha256=9ac07eddcdcd2f871e3a0a0fd31efc7144f37d01d729a14a71d5062947ce538b
```

这些文件体积较大，是否提交到远端仓库需要单独决定。

## 如何启用一次性探针

建议只在隔离构建目录里启用，不要直接污染正常演示配置。

室内 SD 卡 POC 示例：

```bash
cd firmware/esp_indoor
cmake -S . -B /tmp/espdl_indoor_sdcard_poc \
  -DSDKCONFIG=/tmp/espdl_indoor_sdcard_poc.sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;../vision/视觉推理_POC_ESPDL/configs/sdkconfig.defaults.esp_indoor.espdl_sdcard_poc" \
  -DIDF_TARGET=esp32p4
cmake --build /tmp/espdl_indoor_sdcard_poc
```

启用后，启动时会调用：

```c
espdl_probe_run_once();
```

并尝试打印：

```text
model->test()
model->profile_memory()
model->profile_module()
```

## 当前已知边界

- 板端 `model->test()` / profile 日志还需要实机采集后才能宣称跑通。
- ESP-DL runtime 会显著增加镜像体积，分区方案需要结合真实 flash size 调整。
- Outdoor 工程目前没有 SD 卡挂载逻辑，SD 卡 probe 更适合先在 indoor 工程验证。
- 业务推理应优先走 `hazard_infer`，本组件只负责隔离验证。
