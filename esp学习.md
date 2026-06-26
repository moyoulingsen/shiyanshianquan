# ESP 学习：LabGuard 烧录命令

这份记录只写本项目常用的 ESP-IDF 编译、配置、烧录和串口监视命令。下面命令默认仓库路径是：

```bash
/home/lijiaolong/labguard/shiyanshianquan
```

## 1. 进入固件目录

所有 `idf.py` 命令都在 `firmware` 目录里执行：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
```

## 2. 加载 ESP-IDF 环境

每次新开终端后，先执行一次：

```bash
. /home/lijiaolong/esp/esp-idf/export.sh
```

如果终端能识别 `idf.py`，说明环境已经加载成功：

```bash
idf.py --version
```

## 3. 设置芯片目标

本项目当前默认目标是 ESP32-P4，`firmware/sdkconfig.defaults` 里也是：

```text
CONFIG_IDF_TARGET="esp32p4"
```

首次编译或换过构建目录时执行：

```bash
idf.py set-target esp32p4
```

## 4. 配置 Wi-Fi 和 MQTT

打开配置界面：

```bash
idf.py menuconfig
```

重点确认这些配置：

```text
CONFIG_LABGUARD_WIFI_SSID="你的WiFi名称"
CONFIG_LABGUARD_WIFI_PASSWORD="你的WiFi密码"
CONFIG_LABGUARD_MQTT_URI="mqtt://你的电脑IP:1884"
```

`你的电脑IP` 可以先在仓库根目录启动联调脚本查看：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan
./run_labguard_stack.sh
```

脚本启动后终端会打印给板子用的 MQTT 地址，通常类似：

```text
mqtt://192.168.x.x:1884
```

## 5. 查找开发板串口

常见串口是 `/dev/ttyACM0` 或 `/dev/ttyUSB0`。插上开发板后查看：

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

如果显示 `No such file or directory`，说明当前没有识别到对应串口，先检查 USB 线、开发板供电、驱动和权限。

## 6. 编译固件

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
. /home/lijiaolong/esp/esp-idf/export.sh
idf.py build
```

## 7. 烧录并打开串口监视

把 `<串口>` 换成实际串口，例如 `/dev/ttyACM0`：

```bash
idf.py -p <串口> flash monitor
```

常用示例：

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

退出串口监视：

```text
Ctrl + ]
```

## 8. 一条命令完成常规烧录

如果串口就是 `/dev/ttyACM0`，可以直接执行：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
. /home/lijiaolong/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

如果已经设置过 `set-target`，以后一般只需要：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
. /home/lijiaolong/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash monitor
```

## 9. 只打开串口监视

固件已经烧录过，只想看日志：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
. /home/lijiaolong/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 monitor
```

## 10. 擦除后重新烧录

如果 Wi-Fi、NVS、MQTT 配置异常，或者想清空旧数据，可以先擦除整片 Flash：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
. /home/lijiaolong/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash monitor
```

## 11. 烧录后联调顺序

推荐顺序：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan
./run_labguard_stack.sh
```

然后另开一个终端烧录或监视板子：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
. /home/lijiaolong/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash monitor
```

这样板子启动后会连接到电脑上的 MQTT broker，网页端和移动端就能收到数据。

## 12. 常见问题

`idf.py: command not found`：

```bash
. /home/lijiaolong/esp/esp-idf/export.sh
```

找不到串口：

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

串口被占用时，先关闭其他 `monitor`、串口桥或占用串口的程序，再重新烧录。

MQTT 连不上时，先确认 `menuconfig` 里的 `CONFIG_LABGUARD_MQTT_URI` 是否是当前电脑 IP，并确认电脑已经运行：

```bash
./run_labguard_stack.sh
```

出现 Python 环境不一致时：

```text
python is currently active in the environment while the project was configured with ...
Run 'idf.py fullclean' to start again.
```

这是因为上一次构建用的是另一个 Python 版本，例如之前是 Python 3.12，现在终端里 `(base)` conda 环境变成了 Python 3.13。优先退出 conda，再重新加载 ESP-IDF：

```bash
conda deactivate
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
. /home/lijiaolong/esp/esp-idf/export.sh
idf.py build
```

如果还是提示同样错误，清理旧构建目录后重新编译：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
. /home/lijiaolong/esp/esp-idf/export.sh
idf.py fullclean
idf.py build
```

`fullclean` 会删除当前固件的旧编译缓存，不会删除源码。
