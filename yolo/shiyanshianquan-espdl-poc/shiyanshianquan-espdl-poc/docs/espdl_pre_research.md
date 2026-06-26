# ESP-DL / .espdl 预研报告

日期：2026-06-03

## 分支与工作区

- 原仓库目录：`/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan`
- 原工作区状态：`main` 分支存在未提交修改，文件为 `firmware/esp_indoor/components/hazard_infer/hazard_infer.c` 和 `firmware/esp_outdoor/components/ppe_infer/ppe_infer.c`。
- 为避免修改 `main`，本预研创建了独立 Git worktree：`/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc`
- 当前预研分支：`research/espdl-poc`

## 当前仓库实际 ESP-IDF 工程位置

首次结构检查结果显示，当前用户目录本身不是 Git 仓库；Git 仓库位于 `工程文件/shiyanshianquan`。

仓库内存在两个 ESP-IDF app 工程：

- `firmware/esp_indoor`
  - `firmware/esp_indoor/CMakeLists.txt`
  - `firmware/esp_indoor/main/app_main.c`
  - `firmware/esp_indoor/main/CMakeLists.txt`
  - `firmware/esp_indoor/main/idf_component.yml`
- `firmware/esp_outdoor`
  - `firmware/esp_outdoor/CMakeLists.txt`
  - `firmware/esp_outdoor/main/app_main.c`
  - `firmware/esp_outdoor/main/CMakeLists.txt`
  - `firmware/esp_outdoor/main/idf_component.yml`

两个 app 的 `CMakeLists.txt` 都通过 `EXTRA_COMPONENT_DIRS` 引用：

- `../components`
- `components`
- `$ENV{IDF_PATH}/components/mqtt/esp-mqtt`

因此隔离预研组件放置在共享组件目录：`firmware/components/espdl_probe`。

## 当前芯片 target

- `firmware/esp_indoor/sdkconfig` 中找到 `CONFIG_IDF_TARGET="esp32p4"` 和 `CONFIG_IDF_TARGET_ESP32P4=y`。
- `firmware/esp_indoor/dependencies.lock` 中找到 `target: esp32p4`。
- `firmware/esp_outdoor` 未在仓库中找到已生成的 `sdkconfig` 或 `build/project_description.json`，因此 outdoor 的实际已配置 target 未在仓库中找到。
- `firmware/esp_outdoor/sdkconfig.defaults` 与 `firmware/esp_outdoor/main/idf_component.yml` 显示工程面向 ESP32-P4 Function EV Board，并对 `target in [esp32p4]` 启用 `esp_wifi_remote` / `esp_hosted` 依赖。

结论：当前可确认已配置 target 为 indoor 的 `esp32p4`；outdoor 需要执行 `idf.py set-target esp32p4` 或提供生成后的 `sdkconfig` 来确认。

## 当前是否已有 esp-dl

只读检查范围包括：

- `idf_component.yml`
- `managed_components`
- `CMakeLists.txt`
- `dl_model_base.hpp`
- `esp-dl`
- `espdl`
- `.espdl`
- `target_add_aligned_binary_data`
- `dl::Model`

原仓库未发现已接入 ESP-DL 运行时代码，也未发现 `.espdl` 模型文件。仅发现以下文字性线索：

- `firmware/项目介绍.md` 提到后续可替换为 ESP-DL 模型。
- `firmware/tools/download_sh17_weights.py` 说明 `.pt` 权重还不是 ESP32-P4 `.espdl` 文件。
- `firmware/esp_indoor/components/hazard_infer/hazard_infer.c` 文案说明当前 build 没有 ESP-DL runtime 或 `.espdl` artifact。
- `lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/README_model_handoff.md` 提到后续通过 ESP-PPQ / ESP-DL 转换为 `.espdl`。

本分支新增的最小依赖方案：

- 新增 `firmware/components/espdl_probe/idf_component.yml`
- 依赖 `espressif/esp-dl: ^3.3.4`
- 仅对 `target in [esp32p4]` 生效

ESP-DL Component Registry 显示 `espressif/esp-dl` 最新版本为 `3.3.4`，并支持 `esp32p4`。

## .espdl 文件获取方式

仓库内未找到可用 `.espdl` 文件，不能凭空编造模型。

已找到的可转换模型来源：

- `lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx`
- `lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.pt`
- `firmware/models/sh17/yolo9t.pt`
- `runs/detect/*/weights/*.pt`

TODO：

- 需要从 ONNX 或 PyTorch 模型通过 ESP-PPQ 导出 `.espdl`。
- 需要同时导出 `.info` 和 `.json`，用于模型结构、量化和部署核对。
- 需要带 test input/output 的调试版 `.espdl`，否则无法执行 `model->test()` 进行板端正确性验证。
- 本分支约定调试模型放置路径为 `firmware/components/espdl_probe/model/model.espdl`，文件名固定为 `model.espdl` 以匹配 rodata 符号 `_binary_model_espdl_start`。

## 加载方式选择

首选：rodata 嵌入加载。

原因：

- 官方 ESP-DL 文档给出的最小方式是在组件 `CMakeLists.txt` 中使用 `target_add_aligned_binary_data(${COMPONENT_LIB} ${embed_files} BINARY)`。
- 代码中声明 `_binary_model_espdl_start` 对应的 rodata 符号。
- 使用 `dl::Model((const char *)model_espdl, fbs::MODEL_LOCATION_IN_FLASH_RODATA)` 加载。
- 适合作为 POC 验证加载、`model->test()` 和 profile。

备选：

- partition：适合模型较大或需要单独更新模型，后续需新增 partition table 及 `esptool_py_flash_to_partition`。
- sdcard：适合频繁替换模型；indoor 工程已有 microSD mount 逻辑，但本 POC 暂未接入；outdoor 未在仓库中找到 SD card mount 逻辑。

## 本分支新增隔离验证模块

新增组件：`firmware/components/espdl_probe`

默认行为：

- `CONFIG_LABGUARD_ESPDL_PROBE_ENABLE` 默认为 `n`。
- `CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT` 默认为 `n`。
- 默认不加载模型、不运行 test、不做 profile，不改变现有系统行为。

启用方式：

- 通过 `idf.py menuconfig` 打开 `LabGuard ESP-DL probe` 菜单。
- 打开 `Enable isolated ESP-DL probe build`。
- 放入 `firmware/components/espdl_probe/model/model.espdl`。
- 如需上电自动执行，再打开 `Run isolated ESP-DL probe once on boot`。

隔离边界：

- 未修改 `ppe_infer_run`。
- 未修改 `hazard_infer_run`。
- 未修改 `door_fsm_evaluate`。
- 未修改 `risk_fusion_evaluate`。
- 两个 `app_main.c` 只增加默认关闭的 `espdl_probe_run_once()` 条件调用。

## 编译结果

当前机器未在 PATH 中找到 `idf.py`，且 `IDF_PATH` 未设置；在 `/Users/mengxin`、`/opt`、`/usr/local` 常见路径下也未找到 `idf.py` 或 `export.sh`。

已分别在 `firmware/esp_indoor` 与 `firmware/esp_outdoor` 执行 `idf.py build`，结果均为：

```text
zsh:1: command not found: idf.py
```

因此本轮未能完成 ESP-IDF 编译。需要提供或安装 ESP-IDF 环境后再验证：

```sh
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_indoor"
idf.py set-target esp32p4
idf.py menuconfig
idf.py build
```

outdoor：

```sh
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_outdoor"
idf.py set-target esp32p4
idf.py menuconfig
idf.py build
```

## 烧录结果

未执行烧录。阻塞原因：

- 当前机器未找到 `idf.py`。
- 未提供串口设备路径。
- 未提供实际已导出的 `.espdl` 调试模型。

待环境就绪后：

```sh
idf.py -p /dev/tty.usbmodemXXXX flash
```

## 串口日志

未采集串口日志。阻塞原因：

- 当前未执行烧录。
- 未提供串口设备路径。

待环境就绪后：

```sh
idf.py -p /dev/tty.usbmodemXXXX monitor
```

预期探针日志 tag 为 `espdl_probe`。

## model->test() 结果

未执行。阻塞原因：

- 仓库内没有 `.espdl` 文件。
- 需要带 test input/output 的调试版 `.espdl`。
- 当前 ESP-IDF 工具链不可用，无法 build / flash / monitor。

## profile 内存占用

未执行。阻塞原因同上。

探针代码在模型加载后会调用：

- `model->profile_memory()`

## profile 推理耗时

未执行。阻塞原因同上。

探针代码在模型加载后会调用：

- `model->profile_module(CONFIG_LABGUARD_ESPDL_PROBE_SORT_PROFILE_BY_LATENCY)`

## 是否建议进入下一阶段接入

暂不建议进入 PPE / hazard / door / risk-fusion 主链路接入。

进入下一阶段的前置条件：

- ESP-IDF 环境可用，能成功编译 `firmware/esp_indoor` 与 `firmware/esp_outdoor`。
- 已生成 ESP32-P4 可部署的 `.espdl`。
- 已生成 `.info` 与 `.json`。
- 调试版 `.espdl` 包含 test input/output，且 `model->test()` 在板端通过。
- `profile_memory()` 结果证明内存占用在 ESP32-P4 indoor/outdoor 当前任务栈、PSRAM 和 heap 预算内。
- `profile_module()` 结果证明推理耗时满足 1 Hz 主循环或目标帧率。

当前建议：先完成 rodata POC，再决定是否进入 partition/sdcard 模型部署和业务推理替换设计。

## 参考资料

- ESP-DL 官方文档：`https://docs.espressif.com/projects/esp-dl/en/latest/tutorials/how_to_load_test_profile_model.html`
- ESP Component Registry：`https://components.espressif.com/components/espressif/esp-dl`

## 第二轮环境检查结果

第二轮执行日期：2026-06-03

当前 worktree：`/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc`

当前分支检查：

```text
$ git branch --show-current
research/espdl-poc
```

`git status` 结果：

```text
On branch research/espdl-poc
Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   firmware/esp_indoor/main/CMakeLists.txt
	modified:   firmware/esp_indoor/main/app_main.c
	modified:   firmware/esp_outdoor/main/CMakeLists.txt
	modified:   firmware/esp_outdoor/main/app_main.c

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/
	firmware/components/espdl_probe/

no changes added to commit (use "git add" and/or "git commit -a")
```

### ESP-IDF 环境检查结果

执行命令：

```sh
which idf.py
```

完整结果：

```text
idf.py not found
```

执行命令：

```sh
echo "$IDF_PATH"
```

完整结果为空行：

```text

```

执行命令：

```sh
find "$HOME" -maxdepth 4 -name export.sh -o -name idf.py 2>/dev/null | sort | head -50
```

完整结果为空：

```text
```

结论：第二轮仍未找到 `idf.py`，也未找到可 source 的 `export.sh`。按照本轮要求，未继续改源码，也未执行 ESP-IDF build。

### indoor build 结果

未执行。

阻塞原因：`which idf.py` 未找到 `idf.py`，`IDF_PATH` 为空，`$HOME` 四层内未找到 `export.sh` 或 `idf.py`。

### outdoor build 结果

未执行。

阻塞原因：同 indoor，当前 shell 环境缺少 ESP-IDF 命令入口。

### 模型文件检查结果

执行命令：

```sh
find . -type f \( -name "*.onnx" -o -name "*.pt" -o -name "*.espdl" -o -name "*.info" -o -name "*.json" \) | sort
```

关键模型相关结果：

```text
./firmware/models/sh17/yolo9t.pt
./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx
./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.pt
./runs/detect/big_can_yolo26n/weights/best.pt
./runs/detect/big_can_yolo26n/weights/last.pt
./runs/detect/big_can_yolov8n/weights/best.pt
./runs/detect/big_can_yolov8n/weights/last.pt
./runs/detect/small_can_yolov8n/weights/best.pt
./runs/detect/small_can_yolov8n/weights/last.pt
```

同时该命令还命中了以下非 ESP-PPQ 模型交付物类型：

- `./firmware/esp_indoor/build/**/*.json`：已有 indoor build 目录中的 CMake / sdkconfig / flasher 描述文件。
- `./firmware/esp_indoor/managed_components/espressif__cjson/**/*.json`：cJSON 组件测试和元数据文件。
- `./firmware/esp_indoor/managed_components/espressif__cjson/cJSON/tests/unity/release/*.info`：cJSON 测试 release 信息文件。
- `./web/dashboard/node_modules/**/*.json`、`./web/dashboard/package*.json`：前端依赖和包描述文件。
- `./web/mobile/node_modules/**/*.json`、`./web/mobile/package*.json`：前端依赖和包描述文件。
- `./.vscode/settings.json`：编辑器配置。

按实际输出确认：

- 存在 `best.onnx`：`./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx`
- 存在 `best.pt`：`./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.pt`
- 未在仓库中找到 `.espdl`。
- 未在仓库中找到 ESP-PPQ 导出的模型 `.info`。
- 未在仓库中找到 ESP-PPQ 导出的模型 `.json`。

### 是否已经具备导出 .espdl 的输入模型

已经具备输入模型候选：

- ONNX 输入候选：`./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx`
- PyTorch 输入候选：`./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.pt`
- 其他 PyTorch 权重：`./firmware/models/sh17/yolo9t.pt` 与 `./runs/detect/*/weights/*.pt`

尚不具备板端 ESP-DL 验证所需交付物：

- 真实 `model.espdl`
- ESP-PPQ 导出的 `.info`
- ESP-PPQ 导出的 `.json`
- 带 test input/output 的调试版 `.espdl`

### 下一步需要人工执行或补充的内容

1. 提供 ESP-IDF 环境入口：需要能在当前 shell 中通过 `which idf.py` 找到 `idf.py`，或提供实际 `export.sh` 路径。
2. 在 ESP-IDF 环境恢复后，执行 indoor：

```sh
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_indoor"
idf.py set-target esp32p4
idf.py build
```

3. 再执行 outdoor：

```sh
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_outdoor"
idf.py set-target esp32p4
idf.py build
```

4. 使用实际模型通过 ESP-PPQ 导出 `.espdl`、`.info`、`.json`，并提供带 test input/output 的调试版 `.espdl`。
5. 将调试版 `.espdl` 放入 `firmware/components/espdl_probe/model/model.espdl` 后，再打开 `CONFIG_LABGUARD_ESPDL_PROBE_ENABLE` 和 `CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT` 做 rodata 加载、`model->test()`、`profile_memory()`、`profile_module()`。

### 第二轮是否建议进入主链路接入

不建议。

原因：当前尚未满足进入主链路接入的必要条件：

- ESP-IDF build 未通过，实际本轮未能执行。
- 仓库中不存在真实 `model.espdl`。
- `model->test()` 未执行。
- `profile_memory()` 无结果。
- `profile_module()` 无结果。

## 第三轮预研推进结果

第三轮执行日期：2026-06-03

当前 worktree：`/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc`

当前分支：

```text
research/espdl-poc
```

### ESP-IDF 环境恢复结果

第二轮按 `$HOME` 四层搜索没有找到 ESP-IDF。第三轮根据仓库内 `firmware/esp_indoor/dependencies.lock` 中的 `idf: 6.0.1` 和 `target: esp32p4`，在仓库外安装 ESP-IDF：

```text
/Users/mengxin/.codex-labguard/esp-idf-v6.0.1
```

安装过程使用实际 tag：

```text
v6.0.1
```

由于本机 GitHub HTTPS 克隆曾出现 LibreSSL `SSL_ERROR_SYSCALL`，实际 clone / submodule 更新使用过：

```sh
git -c http.version=HTTP/1.1
```

`./install.sh esp32p4` 已完成。安装后发现 ESP-IDF 自身 `components/spiffs/spiffs` 子模块只有 `.git` 指针、缺少 `src`，因此在 ESP-IDF 安装目录内执行：

```sh
git -c http.version=HTTP/1.1 submodule update --init --recursive --force components/spiffs/spiffs
```

补齐后确认 `components/spiffs/spiffs/src/spiffs.h` 等文件存在。

source 环境后结果：

```text
/Users/mengxin/.codex-labguard/esp-idf-v6.0.1/tools/idf.py
IDF_PATH=/Users/mengxin/.codex-labguard/esp-idf-v6.0.1
ESP-IDF v6.0.1
```

ESP-PPQ / PPQ 检查结果：

```text
esp-ppq: not found
ppq: not found
python3 -m pip show ppq esp-ppq: no installed package found
```

当前 Python 环境中找到的相关包：

```text
torch                     2.9.1
torchvision               0.24.1
ultralytics               8.3.228
ultralytics-thop          2.0.18
```

### ESP-IDF v6.0.1 MQTT 配置修正

第三轮首次 indoor 配置失败，原因是两个工程顶层 `CMakeLists.txt` 都引用了不存在的路径：

```text
$IDF_PATH/components/mqtt/esp-mqtt
```

ESP-IDF v6.0.1 实际输出显示该目录不存在：

```text
/Users/mengxin/.codex-labguard/esp-idf-v6.0.1/components/mqtt
```

ESP-IDF v6.0.1 本地迁移文档 `docs/en/migration-guides/release-6.x/6.0/protocols.rst` 写明 ESP-MQTT 已从 ESP-IDF 移除，作为托管组件 `espressif/mqtt` 提供；ESP-IDF 自带示例 `examples/protocols/mqtt/main/idf_component.yml` 使用：

```yaml
espressif/mqtt: "^1.0.0"
```

本分支因此做最小 build 配置修正：

- 从 `firmware/esp_indoor/CMakeLists.txt` 移除旧 `$IDF_PATH/components/mqtt/esp-mqtt`。
- 从 `firmware/esp_outdoor/CMakeLists.txt` 移除旧 `$IDF_PATH/components/mqtt/esp-mqtt`。
- 新增 `firmware/components/labguard_net/idf_component.yml`，声明 `espressif/mqtt: "^1.0.0"`。
- 将 `firmware/components/labguard_net/CMakeLists.txt` 中的 CMake 依赖名从 `esp-mqtt` 改为实际 managed component 名 `espressif__mqtt`。

未修改以下主链路函数：

- `ppe_infer_run`
- `hazard_infer_run`
- `door_fsm_evaluate`
- `risk_fusion_evaluate`

### indoor build 结果

为避免仓库内已有 tracked `build/` 产物被 `idf.py set-target` 清理污染，最终使用离仓库 build 目录：

```sh
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_indoor"
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
idf.py -B /tmp/espdl_indoor_build_esp32p4 build
```

构建目标由 `firmware/esp_indoor/sdkconfig` 确认为：

```text
esp32p4
```

build 通过。关键输出：

```text
NOTICE: [5/16] espressif/esp-dl (3.3.4)
NOTICE: [14/16] espressif/mqtt (1.0.0)
[99%] Built target __idf_espressif__esp-dl
[99%] Built target __idf_espdl_probe
Generated /private/tmp/espdl_indoor_build_esp32p4/labguard_indoor.bin
labguard_indoor.bin binary size 0x10e150 bytes. Smallest app partition is 0x177000 bytes. 0x68eb0 bytes (28%) free.
Project build complete.
```

说明：

- ESP-DL runtime 已进入 indoor 构建图并完成编译。
- `espdl_probe` 默认关闭路径已完成编译。
- 没有真实 `model.espdl`，因此未启用 rodata 模型嵌入，`espdl_probe_espdl.cpp` 未作为启用态模型路径验证。

### outdoor build 结果

outdoor 原仓库未跟踪 `sdkconfig`。第三轮按要求执行 `set-target esp32p4`，生成了：

```text
firmware/esp_outdoor/sdkconfig
firmware/esp_outdoor/dependencies.lock
```

这两个文件当前被仓库规则忽略，未出现在普通 `git status --short --untracked-files=normal` 中。

最终使用离仓库 build 目录：

```sh
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_outdoor"
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
idf.py -B /tmp/espdl_outdoor_build_esp32p4 set-target esp32p4
idf.py -B /tmp/espdl_outdoor_build_esp32p4 build
```

build 通过。关键输出：

```text
Set Target to: esp32p4, new sdkconfig will be created.
NOTICE: [4/12] espressif/esp-dl (3.3.4)
NOTICE: [10/12] espressif/mqtt (1.0.0)
[100%] Built target __idf_espressif__esp-dl
[100%] Built target __idf_espdl_probe
Generated /private/tmp/espdl_outdoor_build_esp32p4/labguard_outdoor.bin
labguard_outdoor.bin binary size 0xe4220 bytes. Smallest app partition is 0x100000 bytes. 0x1bde0 bytes (11%) free.
Project build complete.
```

说明：

- ESP-DL runtime 已进入 outdoor 构建图并完成编译。
- `espdl_probe` 默认关闭路径已完成编译。
- 当前 outdoor app partition 剩余 11%，后续嵌入真实 `.espdl` 时需要重点关注分区尺寸；如模型较大，应优先评估 partition 或 sdcard 方案。

### ESP-DL API 核对

根据实际下载的 `espressif/esp-dl (3.3.4)` 文件：

- `firmware/esp_indoor/managed_components/espressif__esp-dl/dl/model/include/dl_model_base.hpp`
- `firmware/esp_indoor/managed_components/espressif__esp-dl/fbs_loader/include/fbs_model.hpp`

确认存在：

```text
dl::Model(const char *rodata_address_or_partition_label_or_path,
          fbs::model_location_type_t location = fbs::MODEL_LOCATION_IN_FLASH_RODATA,
          ...)
esp_err_t Model::test()
void Model::profile_memory()
void Model::profile_module(bool sort_module_by_latency = false)
fbs::MODEL_LOCATION_IN_FLASH_RODATA
```

`Model::test()` 源码中明确提示：如果模型输入缺少测试数据，需要在 esp-ppq 导出 `.espdl` 时启用 `export_test_values`。

### 模型文件检查结果

第三轮执行原始命令：

```sh
find . -type f \( -name "*.onnx" -o -name "*.pt" -o -name "*.espdl" -o -name "*.info" -o -name "*.json" \) | sort
```

由于第三轮已下载 ESP-IDF managed components，且仓库有前端 `node_modules`，该原始命令会额外命中大量依赖元数据、build 元数据和 package JSON。用于判断模型交付物时，额外执行了排除 `build` / `managed_components` / `node_modules` 的收敛检查，关键结果：

```text
./firmware/models/sh17/yolo9t.pt
./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx
./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.pt
./runs/detect/big_can_yolo26n/weights/best.pt
./runs/detect/big_can_yolo26n/weights/last.pt
./runs/detect/big_can_yolov8n/weights/best.pt
./runs/detect/big_can_yolov8n/weights/last.pt
./runs/detect/small_can_yolov8n/weights/best.pt
./runs/detect/small_can_yolov8n/weights/last.pt
```

按实际输出确认：

- 存在 `best.onnx`：`./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx`
- 存在 `best.pt`：`./lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.pt`
- 未在仓库中找到 `.espdl`。
- 未在仓库中找到 ESP-PPQ 导出的模型 `.info`。
- 未在仓库中找到 ESP-PPQ 导出的模型 `.json`。

### .espdl 转换入口

新增预检入口：

```text
firmware/tools/espdl_export_poc.sh
```

默认输入模型使用仓库中真实存在的 ONNX：

```text
lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx
```

默认输出目录：

```text
firmware/components/espdl_probe/model
```

该脚本不会生成或伪造 `.espdl`。本轮运行结果：

```text
ESP-DL export POC preflight
expected_espdl=.../firmware/components/espdl_probe/model/model.espdl
expected_info=.../firmware/components/espdl_probe/model/model.info
expected_json=.../firmware/components/espdl_probe/model/model.json
ERROR: esp-ppq/ppq command not found in PATH.
TODO: install official ESP-PPQ, then export a real ESP32-P4 .espdl model.
TODO: export model.espdl, model.info, model.json, and enable export_test_values for model->test().
```

### 烧录结果

未执行烧录。

阻塞原因：

- 未提供实际 ESP32-P4 板卡串口路径。
- 仓库中没有真实 `model.espdl`，当前只能验证默认关闭探针的 build。

待具备串口后，indoor 可在对应工程目录执行：

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
idf.py -B /tmp/espdl_indoor_build_esp32p4 -p /dev/tty.usbmodemXXXX flash
```

outdoor：

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
idf.py -B /tmp/espdl_outdoor_build_esp32p4 -p /dev/tty.usbmodemXXXX flash
```

### 串口日志

未采集串口日志。

阻塞原因：

- 未执行烧录。
- 未提供串口路径。
- 未提供真实 `.espdl` 调试模型。

预期探针日志 tag 仍为：

```text
espdl_probe
```

### model->test() 结果

未执行。

阻塞原因：

- 仓库内没有真实 `.espdl` 文件。
- ESP-PPQ / PPQ 命令当前未安装。
- 未生成带 `export_test_values` 的调试版 `.espdl`。
- 未烧录、未采集板端日志。

### profile 内存占用

未执行。

阻塞原因同 `model->test()`。探针代码保留调用：

```text
model->profile_memory()
```

### profile 推理耗时

未执行。

阻塞原因同 `model->test()`。探针代码保留调用：

```text
model->profile_module(CONFIG_LABGUARD_ESPDL_PROBE_SORT_PROFILE_BY_LATENCY)
```

### 第三轮是否建议进入主链路接入

不建议。

当前已经满足：

- ESP-IDF v6.0.1 环境可 source。
- indoor build 通过。
- outdoor build 通过。
- `espressif/esp-dl (3.3.4)` 可被两个工程解析并编译。
- 默认关闭的 `espdl_probe` 可被两个工程编译。

当前仍未满足进入主链路接入的必要条件：

- 未存在真实 `model.espdl`。
- `model->test()` 未通过。
- `profile_memory()` 没有板端结果。
- `profile_module()` 没有板端结果。
- 未烧录，未采集串口日志。

结论：A 线可以继续做 `.espdl` 导出和 isolated probe 板端验证，但不能进入 `ppe_infer_run`、`hazard_infer_run`、`door_fsm_evaluate`、`risk_fusion_evaluate` 主链路接入。

## 第四轮预研记录（2026-06-04）

### 当前分支和 worktree

当前 worktree：

```text
/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc
```

当前分支：

```text
research/espdl-poc
```

本轮仍只修改 `research/espdl-poc` 分支，未切换或修改 `main`。

### ESP-IDF 和 ESP-PPQ 环境

ESP-IDF 环境：

```text
IDF_PATH=/Users/mengxin/.codex-labguard/esp-idf-v6.0.1
which idf.py=/Users/mengxin/.codex-labguard/esp-idf-v6.0.1/tools/idf.py
idf.py --version=ESP-IDF v6.0.1
```

source 使用的实际路径：

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
```

ESP-PPQ 环境：

```text
python=/Users/mengxin/.codex-labguard/esp-ppq-py311-venv/bin/python
esp-ppq=1.3.3
```

`python3` 在当前机器上为 Python `3.14.0`，本轮实际使用 `python3.11` 创建的 venv，避免 ESP-PPQ 在 Python 3.14 下不可用。

### .espdl 转换入口和真实导出结果

本轮新增并验证 Python 转换入口：

```text
firmware/tools/espdl_export_poc.py
firmware/tools/espdl_export_poc.sh
```

实际输入模型来自仓库内 handoff 目录：

```text
lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx
lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.pt
```

实际 ONNX 信息：

```text
input=images [1, 3, 320, 320]
output=output0 [1, 6, 2100]
opset=13
```

校准图片目录：

```text
lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/predict_samples
```

校准图片数量：

```text
50
```

转换脚本 dry-run 输出确认：

```text
target=esp32p4
bits=8
device=cpu
export_test_values=True
onnx_input_shape=[1, 3, 320, 320]
calibration_images=50
effective_calib_steps=8
```

本轮已经生成真实 ESP-DL artifact，未伪造 `model.espdl`：

```text
firmware/components/espdl_probe/model/model.espdl size=3603872
firmware/components/espdl_probe/model/model.json size=984482
firmware/components/espdl_probe/model/model.info size=22341686
```

SHA256：

```text
model.espdl 3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea
model.json  9ac07eddcdcd2f871e3a0a0fd31efc7144f37d01d729a14a71d5062947ce538b
model.info  5b20d881dd702e0ee9a1deb92095d77588eb161a9ed35436b589f1f0c462832e
```

转换中遇到并处理的实际问题：

- ESP-PPQ v1.3.3 没有独立 `esp-ppq` CLI，本轮使用 `esp_ppq.api.espdl_quantize_onnx`。
- YOLOv8 DFL reshape 固定 batch=1，batch size 4 会在 `/model.22/dfl/Reshape` 失败；转换脚本默认 batch size 改为 `1`。
- 原始 ONNX 中 `/model.22/Concat` 和 `/model.22/Concat_1` 使用 `axis=-1`，ESP-PPQ 导出阶段在 layout pattern 中失败；脚本只在临时 ONNX 副本中将 rank 3 的 `axis=-1` 规范化为 `axis=2`，不修改原始 ONNX。

### 模型文件检查结果

本轮检查确认：

- 已存在真实 `best.onnx`。
- 已存在真实 `best.pt`。
- 已存在真实 `firmware/components/espdl_probe/model/model.espdl`。
- 已存在真实 `firmware/components/espdl_probe/model/model.info`。
- 已存在真实 `firmware/components/espdl_probe/model/model.json`。

另发现已有 `.pt` 权重：

```text
firmware/models/sh17/yolo9t.pt
runs/detect/big_can_yolo26n/weights/best.pt
runs/detect/big_can_yolo26n/weights/last.pt
runs/detect/big_can_yolov8n/weights/best.pt
runs/detect/big_can_yolov8n/weights/last.pt
runs/detect/small_can_yolov8n/weights/best.pt
runs/detect/small_can_yolov8n/weights/last.pt
```

当前已经具备导出 `.espdl` 的输入模型，并已完成一次真实导出。

### 默认关闭 probe 的编译结果

indoor 默认关闭 probe build 通过：

```text
Built target __idf_espressif__esp-dl
Built target __idf_espdl_probe
Generated /private/tmp/espdl_indoor_build_esp32p4/labguard_indoor.bin
labguard_indoor.bin binary size 0x10e150 bytes
Smallest app partition is 0x177000 bytes
0x68eb0 bytes (28%) free
```

outdoor 默认关闭 probe build 通过：

```text
Built target __idf_espressif__esp-dl
Built target __idf_espdl_probe
Generated /private/tmp/espdl_outdoor_build_esp32p4/labguard_outdoor.bin
labguard_outdoor.bin binary size 0xe4220 bytes
Smallest app partition is 0x100000 bytes
0x1bde0 bytes (11%) free
```

结论：默认关闭 `CONFIG_LABGUARD_ESPDL_PROBE_ENABLE` / `CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT` 时，两个工程仍可 build，默认系统行为不改变。

### 启用 rodata probe 的编译结果

本轮使用临时 sdkconfig 启用以下实际配置：

```text
CONFIG_LABGUARD_ESPDL_PROBE_ENABLE=y
CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT=y
CONFIG_LABGUARD_ESPDL_PROBE_SORT_PROFILE_BY_LATENCY=y
```

indoor 启用 rodata probe 后，ESP-DL 和模型嵌入链路编译、链接、生成 bin 均成功：

```text
Generating ../../model.espdl.S
Building ASM object ... model.espdl.S.obj
Building CXX object ... espdl_probe_espdl.cpp.obj
Built target __idf_espdl_probe
Built target labguard_indoor.elf
Generated /private/tmp/espdl_indoor_probe_build_esp32p4/labguard_indoor.bin
```

最终失败在 app partition 尺寸检查：

```text
Error: app partition is too small for binary labguard_indoor.bin size 0x5e4100:
  - Part 'factory' 0/0 @ 0x10000 size 0x177000 (overflow 0x46d100)
```

outdoor 启用 rodata probe 后，ESP-DL 和模型嵌入链路同样编译、链接、生成 bin 成功：

```text
Generating ../../model.espdl.S
Building ASM object ... model.espdl.S.obj
Building CXX object ... espdl_probe_espdl.cpp.obj
Built target __idf_espdl_probe
Built target labguard_outdoor.elf
Generated /private/tmp/espdl_outdoor_probe_build_esp32p4/labguard_outdoor.bin
```

最终失败在 app partition 尺寸检查：

```text
Error: app partition is too small for binary labguard_outdoor.bin size 0x5b9c20:
  - Part 'factory' 0/0 @ 0x10000 size 0x100000 (overflow 0x4b9c20)
```

结论：rodata 方式的 `target_add_aligned_binary_data`、`_binary_model_espdl_start`、`dl::Model(... MODEL_LOCATION_IN_FLASH_RODATA)` 编译链路已经验证可达；当前阻塞点不是 C++ 或 ESP-DL API，而是模型嵌入 app 后超过现有 factory app partition。

### 烧录结果

未执行烧录。

阻塞原因：

- 启用 rodata probe 的 indoor/outdoor bin 均超过当前 app partition，`idf.py build` 在 `app_check_size` 阶段失败。
- 未在仓库或命令输出中获得实际 ESP32-P4 板卡串口路径。

### 串口日志

未采集串口日志。

阻塞原因：

- 启用 rodata probe 的镜像尚不能通过当前分区表尺寸检查。
- 未提供实际串口路径。

### model->test() 结果

未执行。

当前已具备带 `export_test_values=True` 导出的真实 `model.espdl`，但由于 rodata 启用版镜像超过 app partition，尚未烧录到板端执行 `model->test()`。

### profile 内存占用

未执行。

探针代码仍保留调用：

```text
model->profile_memory()
```

但由于当前 rodata 启用版无法通过 app partition 检查，尚无板端 profile 内存日志。

### profile 推理耗时

未执行。

探针代码仍保留调用：

```text
model->profile_module(CONFIG_LABGUARD_ESPDL_PROBE_SORT_PROFILE_BY_LATENCY)
```

但由于当前 rodata 启用版无法通过 app partition 检查，尚无板端 profile 耗时日志。

### 第四轮是否建议进入主链路接入

不建议。

当前已经满足：

- ESP-IDF v6.0.1 环境可用。
- ESP-PPQ v1.3.3 环境可用。
- 已存在真实 `best.onnx` 和 `best.pt`。
- 已生成真实 `model.espdl`、`model.info`、`model.json`。
- indoor/outdoor 默认关闭 probe build 通过。
- 启用 rodata probe 时，ESP-DL、模型 rodata 嵌入、`espdl_probe_espdl.cpp` 编译链接路径可达。

当前仍未满足进入主链路接入的必要条件：

- 启用 rodata probe 后 app 超过当前 partition，尚不能烧录。
- `model->test()` 未在板端通过。
- `profile_memory()` 没有板端结果。
- `profile_module()` 没有板端结果。
- 未采集串口日志。

下一步建议优先做二选一：

1. 调整 partition/flash 布局，给启用 rodata 的 app 足够空间后再烧录验证。
2. 将 `.espdl` 从 app rodata 改为单独 model partition 或 sdcard 加载，避免 app partition 被 3.4 MB 模型撑爆。

在上述板端验证完成之前，仍不要接入 `ppe_infer_run`、`hazard_infer_run`、`door_fsm_evaluate`、`risk_fusion_evaluate`。

## 第五轮预研记录（2026-06-04）：SD-card 加载模式与尺寸阻塞复查

### 执行上下文

当前 worktree：

```text
/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc
```

当前分支：

```text
research/espdl-poc
```

本轮继续保持隔离验证，不接入或修改以下主链路函数：

```text
ppe_infer_run
hazard_infer_run
door_fsm_evaluate
risk_fusion_evaluate
```

### ESP-IDF 工程位置和 target

当前仓库内实际 ESP-IDF 工程位置仍为：

```text
firmware/esp_indoor
firmware/esp_outdoor
```

本轮实际使用 target：

```text
esp32p4
```

ESP-IDF 环境：

```text
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
ESP-IDF v6.0.1
```

### 当前是否已有 ESP-DL

当前工程已经存在 ESP-DL 管理组件和隔离 probe 依赖：

```text
firmware/esp_indoor/managed_components/espressif__esp-dl
firmware/esp_outdoor/managed_components/espressif__esp-dl
firmware/components/espdl_probe/idf_component.yml
```

隔离 probe 的依赖声明：

```text
espressif/esp-dl: ^3.3.4
```

ESP-DL 本地头文件中已确认存在以下模型加载位置枚举：

```text
MODEL_LOCATION_IN_FLASH_RODATA
MODEL_LOCATION_IN_FLASH_PARTITION
MODEL_LOCATION_IN_SDCARD
```

### .espdl 文件获取方式

本轮没有凭空创建模型文件。真实 `.espdl` 来自仓库内已有 ONNX，经 ESP-PPQ v1.3.3 导出：

```text
输入 ONNX:
lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx

输入 PT:
lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.pt

输出:
firmware/components/espdl_probe/model/model.espdl
firmware/components/espdl_probe/model/model.info
firmware/components/espdl_probe/model/model.json
```

输出文件大小：

```text
model.espdl size=3603872
model.json  size=984482
model.info  size=22341686
```

SHA256：

```text
model.espdl 3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea
model.json  9ac07eddcdcd2f871e3a0a0fd31efc7144f37d01d729a14a71d5062947ce538b
model.info  5b20d881dd702e0ee9a1deb92095d77588eb161a9ed35436b589f1f0c462832e
```

转换入口：

```text
firmware/tools/espdl_export_poc.py
firmware/tools/espdl_export_poc.sh
```

### 加载方式选择：rodata / partition / sdcard

当前隔离 probe 支持以下实际路径：

```text
rodata:
  target_add_aligned_binary_data(...)
  _binary_model_espdl_start
  dl::Model(..., fbs::MODEL_LOCATION_IN_FLASH_RODATA)

sdcard:
  CONFIG_LABGUARD_ESPDL_PROBE_SDCARD_PATH
  默认路径 /sdcard/model.espdl
  dl::Model(..., fbs::MODEL_LOCATION_IN_SDCARD)
```

partition 方式仅在 ESP-DL 头文件中确认存在 `MODEL_LOCATION_IN_FLASH_PARTITION` 枚举；本轮未在仓库中实现 partition 加载，因为当前 2MB flash 配置和 app partition 尺寸已经不足以容纳启用 ESP-DL runtime 的验证镜像，继续添加 model partition 没有实际烧录价值。

indoor 工程中已找到并使用现有 SD 卡挂载路径：

```text
firmware/esp_indoor/main/app_main.c
SD_CARD_MOUNT_POINT "/sdcard"
init_sd_card()
```

indoor 的 run-on-boot probe 调用已放在 `event_log_init(NULL); init_sd_card();` 之后，便于 SD-card 模式加载 `/sdcard/model.espdl`。

outdoor 工程中未在仓库中找到 SD-card 挂载逻辑；因此 outdoor 的 SD-card probe 只能验证编译路径，不能确认板端 SD-card 加载路径可用。

### 默认关闭 probe 的编译结果

默认关闭以下配置时，现有系统行为不改变：

```text
CONFIG_LABGUARD_ESPDL_PROBE_ENABLE is not set
CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT is not set
```

indoor 默认关闭 build 通过：

```text
build dir: /tmp/espdl_indoor_build_esp32p4_sdmode_recheck
labguard_indoor.bin binary size 0x10e150 bytes
Smallest app partition is 0x177000 bytes
0x68eb0 bytes (28%) free
```

outdoor 默认关闭 build 通过：

```text
build dir: /tmp/espdl_outdoor_build_esp32p4_sdmode_recheck
labguard_outdoor.bin binary size 0xe4220 bytes
Smallest app partition is 0x100000 bytes
0x1bde0 bytes (11%) free
```

### 启用 rodata probe 的编译结果

indoor 启用 rodata probe 后，模型 rodata 嵌入、C++ probe 编译和链接路径可达，但最终 app 尺寸检查失败：

```text
labguard_indoor.bin size 0x5e4100
factory app partition size 0x177000
overflow 0x46d100
```

outdoor 启用 rodata probe 后，模型 rodata 嵌入、C++ probe 编译和链接路径可达，但最终 app 尺寸检查失败：

```text
labguard_outdoor.bin size 0x5b9c20
factory app partition size 0x100000
overflow 0x4b9c20
```

### 启用 SD-card probe 的编译结果

SD-card 模式不把 `.espdl` 嵌入 app，仅链接 ESP-DL runtime 和隔离 probe。

indoor 启用 SD-card probe 后，编译、链接、生成 bin 可达，但最终 app 尺寸检查失败：

```text
build dir: /tmp/espdl_indoor_probe_sdcard_build_esp32p4
labguard_indoor.bin size 0x2743b0
factory app partition size 0x177000
overflow 0xfd3b0
```

outdoor 启用 SD-card probe 后，编译、链接、生成 bin 可达，但最终 app 尺寸检查失败：

```text
build dir: /tmp/espdl_outdoor_probe_sdcard_build_esp32p4
labguard_outdoor.bin size 0x249ed0
factory app partition size 0x100000
overflow 0x149ed0
```

### 最小化 SD-card probe 复查结果

本轮使用临时 sdkconfig 复查，不修改仓库内主配置：

```text
CONFIG_LABGUARD_ESPDL_PROBE_ENABLE=y
CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT=y
CONFIG_LABGUARD_ESPDL_PROBE_LOAD_SDCARD=y
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
```

并根据 ESP-DL 实际 Kconfig 将以下 pixel conversion 选项在临时 sdkconfig 中关闭：

```text
CONFIG_PIX_CVT_RGB565_TO_RGB565_SUPPORT is not set
CONFIG_PIX_CVT_RGB565_TO_RGB888_SUPPORT is not set
CONFIG_PIX_CVT_RGB565_TO_GRAY_SUPPORT is not set
CONFIG_PIX_CVT_RGB565_TO_HSV_SUPPORT is not set
CONFIG_PIX_CVT_RGB888_TO_RGB888_SUPPORT is not set
CONFIG_PIX_CVT_RGB888_TO_RGB565_SUPPORT is not set
CONFIG_PIX_CVT_RGB888_TO_GRAY_SUPPORT is not set
CONFIG_PIX_CVT_RGB888_TO_HSV_SUPPORT is not set
CONFIG_PIX_CVT_GRAY_TO_GRAY_SUPPORT is not set
CONFIG_PIX_CVT_HSV_TO_HSV_MASK_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_RGB888_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_RGB565_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_GRAY_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_HSV_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_YUV_SUPPORT is not set
```

indoor 最小化 SD-card probe 仍失败于 app 尺寸：

```text
build dir: /tmp/espdl_indoor_probe_sdcard_min_build_esp32p4
labguard_indoor.bin size 0x259590
factory app partition size 0x177000
overflow 0xe2590
```

outdoor 最小化 SD-card probe 仍失败于 app 尺寸。本次 outdoor 临时使用 ESP-IDF 的 `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` 复查，因此 app partition 为 `0x177000`；原始 outdoor 仓库配置仍为 `CONFIG_PARTITION_TABLE_SINGLE_APP=y`，app partition 为 `0x100000`。

```text
build dir: /tmp/espdl_outdoor_probe_sdcard_min_build_esp32p4
labguard_outdoor.bin size 0x234690
factory app partition size 0x177000
overflow 0xbd690
```

`idf.py size-components --format json` 在 ESP-IDF v6.0.1 中不是合法格式；命令输出确认可用格式为：

```text
default, text, csv, json2, tree, raw
```

随后直接使用 ESP-IDF 自带 size 工具读取已生成的 `.map` 文件，绕开 `app_check_size`：

```text
/Users/mengxin/.codex-labguard/esp-idf-v6.0.1/tools/idf_size.py
```

indoor 最小化 SD-card probe 的 archive 体积归因中，最大项为：

```text
libespressif__esp-dl.a  Total Size 1097839
libstdc++.a             Total Size 297177
libesp_stdio.a          Total Size 193480
libespressif__esp_hosted.a Total Size 147021
liblwip.a               Total Size 96521
```

outdoor 最小化 SD-card probe 的 archive 体积归因中，最大项为：

```text
libespressif__esp-dl.a  Total Size 1095639
libstdc++.a             Total Size 297177
libesp_stdio.a          Total Size 170792
libespressif__esp_hosted.a Total Size 147021
liblwip.a               Total Size 96521
```

与默认关闭 probe 的 build map 对比，最小化 SD-card probe 的二进制净增量为：

```text
indoor:  0x259590 - 0x10e150 = 0x14b440，十进制 1356864
outdoor: 0x234690 - 0x0e4220 = 0x150470，十进制 1377392
```

diff 归因中新增最大的实际库为：

```text
indoor:
libespressif__esp-dl.a +1097839
libstdc++.a            +295571
libfbs_model.a         +25868

outdoor:
libespressif__esp-dl.a +1095639
libstdc++.a            +295571
libfbs_model.a         +25868
```

结论：SD-card 模式下即使不嵌入 `model.espdl`，主要新增尺寸来自 ESP-DL runtime 和 C++ runtime，不是 `espdl_probe` 自身；继续微调 probe 代码无法补回当前 app partition 缺口。

### 当前 flash / partition 约束

当前仓库配置中确认：

```text
firmware/esp_indoor/sdkconfig:
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp_large.csv"

firmware/esp_outdoor/sdkconfig:
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
CONFIG_PARTITION_TABLE_SINGLE_APP=y
CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp.csv"
```

ESP-IDF v6.0.1 默认分区文件中确认：

```text
partitions_singleapp_large.csv: factory app size 1500K
partitions_singleapp.csv:       factory app size 1M
```

结论：当前 2MB flash 配置下，rodata 模式无法容纳 3.4MB 的 `model.espdl`；即使切到 SD-card 加载，不嵌入模型，仅启用 ESP-DL runtime 后 app 也超过当前 app partition。

### 烧录结果

未执行烧录。

阻塞原因：

```text
启用 ESP-DL probe 的 indoor/outdoor 镜像均未通过 app_check_size。
未在本轮命令输出中获得实际串口路径。
```

### 串口日志

未采集串口日志。

阻塞原因：

```text
启用 ESP-DL probe 的镜像尚不能通过当前 partition 尺寸检查。
未提供实际串口路径。
```

本轮串口候选检查命令输出：

```text
ls /dev/cu.* 2>/dev/null | sort
/dev/cu.Bluetooth-Incoming-Port
/dev/cu.debug-console

ls /dev/tty.* 2>/dev/null | sort
/dev/tty.Bluetooth-Incoming-Port
/dev/tty.debug-console
```

当前未看到可明确识别为 ESP32-P4 板卡的 USB/JTAG 串口路径；不能安全执行 `idf.py -p <port> flash monitor`。

### model->test() 结果

未执行。

当前已经具备带 test values 的真实 `model.espdl`，但尚未生成可烧录的启用 ESP-DL probe 镜像。

### profile 内存占用

未执行。

隔离 probe 中保留 `model->profile_memory()` 调用，但当前无板端日志。

### profile 推理耗时

未执行。

隔离 probe 中保留 `model->profile_module(CONFIG_LABGUARD_ESPDL_PROBE_SORT_PROFILE_BY_LATENCY)` 调用，但当前无板端日志。

### 第五轮是否建议进入主链路接入

不建议进入主链路接入。

进入主链路接入的必要条件当前只满足一项：

```text
已存在真实 model.espdl: 是
ESP-IDF 启用 ESP-DL probe build 通过: 否
model->test() 通过: 否，未执行
profile_memory() 有结果: 否，未执行
profile_module() 有结果: 否，未执行
```

下一步需要人工确认或补充：

```text
1. 确认实际 ESP32-P4 模块 flash 容量是否大于当前 sdkconfig 的 2MB。
2. 确认是否允许为预研分支调整 flash size 和 partition table。
3. 若继续走 SD-card 模式，需将真实 model.espdl 放到 indoor 板卡 /sdcard/model.espdl。
4. 提供或确认实际串口路径后，才能执行 idf.py -p <port> flash monitor。
5. outdoor 若要做 SD-card 模式板端验证，需要先提供或实现 outdoor 的 SD-card 挂载路径；当前仓库未找到。
```

在完成上述尺寸和板端验证前，继续保持本分支为隔离 ESP-DL POC，不进入 `ppe_infer_run`、`hazard_infer_run`、`door_fsm_evaluate`、`risk_fusion_evaluate`。

## 第六轮预研记录（2026-06-04）：size 阻塞收敛、分区预案与硬件准备

### 执行上下文

当前 worktree：

```text
/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc
```

当前分支：

```text
research/espdl-poc
```

本轮继续保持隔离验证，不接入或修改以下主链路函数：

```text
ppe_infer_run
hazard_infer_run
door_fsm_evaluate
risk_fusion_evaluate
```

### ESP-IDF 环境检查

实际 source 路径仍为：

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
```

source 后确认：

```text
which idf.py=/Users/mengxin/.codex-labguard/esp-idf-v6.0.1/tools/idf.py
idf.py --version=ESP-IDF v6.0.1
IDF_PATH=/Users/mengxin/.codex-labguard/esp-idf-v6.0.1
```

本机工具补充事实：

```text
cmake=/opt/homebrew/bin/cmake
ninja not found
make=/usr/bin/make
```

因此本轮非默认临时 POC 构建使用 CMake `Unix Makefiles` 生成器，同时把 `SDKCONFIG` 指向 `/tmp`，避免写入工程默认 `sdkconfig`。

### 当前真实模型 artifact

当前真实 ESP-DL artifact 仍存在，未凭空创建：

```text
firmware/components/espdl_probe/model/model.espdl bytes=3603872
firmware/components/espdl_probe/model/model.info  bytes=22341686
firmware/components/espdl_probe/model/model.json  bytes=984482
```

SHA256：

```text
model.espdl 3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea
model.info  5b20d881dd702e0ee9a1deb92095d77588eb161a9ed35436b589f1f0c462832e
model.json  9ac07eddcdcd2f871e3a0a0fd31efc7144f37d01d729a14a71d5062947ce538b
```

### 默认关闭 build 复查结果

默认关闭 probe 时，两个工程仍可 build，现有系统行为不改变：

```text
CONFIG_LABGUARD_ESPDL_PROBE_ENABLE is not set
CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT is not set
```

indoor 默认关闭 build：

```text
build dir: /tmp/espdl_a6_indoor_default
factory app partition: 0x177000
labguard_indoor.bin binary size 0x10e150
free 0x68eb0 (28%)
Project build complete
```

outdoor 默认关闭 build：

```text
build dir: /tmp/espdl_a6_outdoor_default
factory app partition: 0x100000
labguard_outdoor.bin binary size 0xe4220
free 0x1bde0 (11%)
Project build complete
```

### 非默认 SD-card 最小 POC 配置

新增非默认 overlay，不会被普通 build 自动使用：

```text
firmware/esp_indoor/sdkconfig.defaults.espdl_sdcard_poc
firmware/esp_outdoor/sdkconfig.defaults.espdl_sdcard_poc
```

实际启用配置：

```text
CONFIG_LABGUARD_ESPDL_PROBE_ENABLE=y
CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT=y
CONFIG_LABGUARD_ESPDL_PROBE_LOAD_SDCARD=y
CONFIG_LABGUARD_ESPDL_PROBE_SDCARD_PATH="/sdcard/model.espdl"
CONFIG_LABGUARD_ESPDL_PROBE_SORT_PROFILE_BY_LATENCY=y
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
```

并使用 ESP-DL 实际 Kconfig 名称关闭 pixel conversion 选项，用于测量当前 POC 尺寸下限：

```text
CONFIG_PIX_CVT_RGB565_TO_RGB565_SUPPORT is not set
CONFIG_PIX_CVT_RGB565_TO_RGB888_SUPPORT is not set
CONFIG_PIX_CVT_RGB565_TO_GRAY_SUPPORT is not set
CONFIG_PIX_CVT_RGB565_TO_HSV_SUPPORT is not set
CONFIG_PIX_CVT_RGB888_TO_RGB888_SUPPORT is not set
CONFIG_PIX_CVT_RGB888_TO_RGB565_SUPPORT is not set
CONFIG_PIX_CVT_RGB888_TO_GRAY_SUPPORT is not set
CONFIG_PIX_CVT_RGB888_TO_HSV_SUPPORT is not set
CONFIG_PIX_CVT_GRAY_TO_GRAY_SUPPORT is not set
CONFIG_PIX_CVT_HSV_TO_HSV_MASK_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_RGB888_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_RGB565_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_GRAY_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_HSV_SUPPORT is not set
CONFIG_PIX_CVT_YUV_TO_YUV_SUPPORT is not set
```

indoor 临时构建命令：

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_indoor"
cmake -S . -B /tmp/espdl_a6_indoor_sdcard_min_make -G "Unix Makefiles" \
  -DSDKCONFIG=/tmp/espdl_a6_indoor_sdcard_min_make.sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.espdl_sdcard_poc" \
  -DIDF_TARGET=esp32p4
cmake --build /tmp/espdl_a6_indoor_sdcard_min_make -j 8
```

outdoor 临时构建命令同理，build dir 为：

```text
/tmp/espdl_a6_outdoor_sdcard_min_make
```

### SD-card 最小 POC build 结果

indoor 输出确认加载了非默认 overlay：

```text
Loading defaults file .../firmware/esp_indoor/sdkconfig.defaults
Loading defaults file .../firmware/esp_indoor/sdkconfig.defaults.espdl_sdcard_poc
factory,app,factory,0x10000,1500K
Built target __idf_espressif__esp-dl
Built target __idf_espdl_probe
Generated /tmp/espdl_a6_indoor_sdcard_min_make/labguard_indoor.bin
Error: app partition is too small for binary labguard_indoor.bin size 0x259590:
  - Part 'factory' 0/0 @ 0x10000 size 0x177000 (overflow 0xe2590)
```

outdoor 输出确认加载了非默认 overlay：

```text
Loading defaults file .../firmware/esp_outdoor/sdkconfig.defaults
Loading defaults file .../firmware/esp_outdoor/sdkconfig.defaults.espdl_sdcard_poc
factory,app,factory,0x10000,1M
Built target __idf_espressif__esp-dl
Built target __idf_espdl_probe
Generated /tmp/espdl_a6_outdoor_sdcard_min_make/labguard_outdoor.bin
Error: app partition is too small for binary labguard_outdoor.bin size 0x234690:
  - Part 'factory' 0/0 @ 0x10000 size 0x100000 (overflow 0x134690)
```

结论：SD-card 模式不嵌入 `model.espdl`，但在当前真实 app partition 下仍不能通过 size check。

### size 归因

二进制净增量：

```text
indoor:  0x259590 - 0x10e150 = 0x14b440, decimal 1356864
outdoor: 0x234690 - 0x0e4220 = 0x150470, decimal 1377392
```

`idf_size.py --archives` 中 SD-card 最小 POC 最大项：

```text
indoor:
libespressif__esp-dl.a  Total Size 1097839
libstdc++.a             Total Size 297177
libesp_stdio.a          Total Size 193480
libespressif__esp_hosted.a Total Size 147021
liblwip.a               Total Size 96521
libfbs_model.a          Total Size 25868
libespdl_probe.a        小于前 80 行主要项，非尺寸主因

outdoor:
libespressif__esp-dl.a  Total Size 1095639
libstdc++.a             Total Size 297177
libesp_stdio.a          Total Size 170792
libespressif__esp_hosted.a Total Size 147021
liblwip.a               Total Size 96521
libfbs_model.a          Total Size 25868
libespdl_probe.a        Total Size 524
```

与默认关闭 build 的 diff 归因中最大新增项：

```text
indoor:
libespressif__esp-dl.a +1097839
libstdc++.a            +295571
libfbs_model.a         +25868

outdoor:
libespressif__esp-dl.a +1095639
libstdc++.a            +295571
libfbs_model.a         +25868
```

结论：当前 size 阻塞来自 ESP-DL runtime 和 C++ runtime；继续微调 `espdl_probe` 自身无法解决当前 `2MB` flash / app partition 不足。

### 新增分区和硬件准备文档

新增：

```text
docs/espdl_flash_partition_plan.md
docs/espdl_hardware_checklist.md
firmware/tools/espdl_probe_preflight.sh
firmware/components/espdl_probe/README.md
```

`docs/espdl_flash_partition_plan.md` 记录：

```text
rodata / flash partition / sdcard 三种部署选项
当前 2MB flash 配置和 app partition 尺寸
rodata 与 SD-card 的实际 size 阻塞
未确认物理 flash 容量前，不创建默认分区表修改
```

`docs/espdl_hardware_checklist.md` 记录：

```text
需要确认 ESP32-P4 板卡串口
需要确认实际 flash 容量
SD-card 模式需要 /sdcard/model.espdl
板端必须采集 model->test(), profile_memory(), profile_module() 日志
```

预检脚本执行结果摘要：

```text
branch=research/espdl-poc
idf.py=not found in PATH
IDF_PATH=
model.espdl=present bytes=3603872
model.info=present bytes=22341686
model.json=present bytes=984482
indoor: CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
outdoor: CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
serial candidates:
/dev/cu.Bluetooth-Incoming-Port
/dev/cu.debug-console
/dev/tty.Bluetooth-Incoming-Port
/dev/tty.debug-console
manual_next_step=confirm physical flash size and board serial port before flash/monitor
```

该脚本只报告阻塞点，不执行 build、flash 或 `sdkconfig` 修改。

### 烧录结果

未执行烧录。

阻塞原因：

```text
SD-card 最小 POC 在 indoor/outdoor 当前真实 app partition 下仍未通过 app_check_size。
当前命令输出未发现可明确识别为 ESP32-P4 板卡的串口路径。
尚未确认物理板卡实际 flash 容量是否大于 sdkconfig 当前 2MB。
```

### 串口日志

未采集串口日志。

当前只看到：

```text
/dev/cu.Bluetooth-Incoming-Port
/dev/cu.debug-console
/dev/tty.Bluetooth-Incoming-Port
/dev/tty.debug-console
```

不能安全把这些路径当作 ESP32-P4 板卡串口。

### model->test() 结果

未执行。

当前已经具备真实 `model.espdl`，但尚未生成可烧录的启用 ESP-DL probe 镜像。

### profile 内存占用

未执行。

阻塞原因同 `model->test()`；无板端 `model->profile_memory()` 日志。

### profile 推理耗时

未执行。

阻塞原因同 `model->test()`；无板端 `model->profile_module()` 日志。

### 第六轮是否建议进入主链路接入

不建议进入主链路接入。

进入主链路接入的必要条件当前状态：

```text
ESP-IDF 默认关闭 build 通过: 是
启用 ESP-DL probe build 通过 app_check_size: 否
已存在真实 model.espdl: 是
model->test() 通过: 否，未执行
profile_memory() 有结果: 否，未执行
profile_module() 有结果: 否，未执行
```

下一步只需要人工补充：

```text
1. 确认实际 ESP32-P4 板卡 flash 容量。
2. 提供或确认实际板卡串口路径。
3. 若走 indoor SD-card 模式，将 model.espdl 放到板卡 /sdcard/model.espdl。
4. 若 flash 容量大于 2MB，确认是否允许在本预研分支新增非默认 POC 分区表并切到更大 app partition。
```

在上述条件满足并拿到板端 `model->test()`、`profile_memory()`、`profile_module()` 日志前，继续保持 ESP-DL 为隔离 POC，不进入 `ppe_infer_run`、`hazard_infer_run`、`door_fsm_evaluate`、`risk_fusion_evaluate`。

## 第七轮：本地收敛、preflight 与硬件门禁

执行日期：2026-06-04

当前 worktree：

```text
/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc
```

当前分支：

```text
research/espdl-poc
```

### ESP-IDF 环境复核

实际环境入口：

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
```

复核输出：

```text
which idf.py: /Users/mengxin/.codex-labguard/esp-idf-v6.0.1/tools/idf.py
idf.py --version: ESP-IDF v6.0.1
IDF_PATH: /Users/mengxin/.codex-labguard/esp-idf-v6.0.1
which ninja: ninja not found
which cmake: /opt/homebrew/bin/cmake
cmake version: 4.3.2
```

`idf.py -B` 实际调用的 CMake generator 为：

```text
Unix Makefiles
```

### artifact 复核

```text
firmware/components/espdl_probe/model/model.espdl
bytes=3603872
sha256=3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea

firmware/components/espdl_probe/model/model.info
bytes=22341686
sha256=5b20d881dd702e0ee9a1deb92095d77588eb161a9ed35436b589f1f0c462832e

firmware/components/espdl_probe/model/model.json
bytes=984482
sha256=9ac07eddcdcd2f871e3a0a0fd31efc7144f37d01d729a14a71d5062947ce538b
```

`git ls-files` 对以上三个 artifact 无输出；`git check-ignore -v` 也无输出。结论：当前三个大型 artifact 是未跟踪且未被 ignore 的文件，不应在未明确项目策略前直接提交。

### 隔离边界复核

针对以下路径执行 `git diff` 无输出：

```text
firmware/esp_indoor/components/hazard_infer
firmware/esp_outdoor/components/ppe_infer
firmware/esp_outdoor/components/door_fsm
firmware/esp_indoor/components/risk_fusion
```

仓库中能搜索到这些函数的原始定义和调用：

```text
hazard_infer_run
risk_fusion_evaluate
ppe_infer_run
door_fsm_evaluate
```

但本轮未修改这些函数所在组件。`espdl_probe` 仍为隔离 POC。

### 默认配置复核

indoor：

```text
CONFIG_IDF_TARGET="esp32p4"
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp_large.csv"
# CONFIG_LABGUARD_ESPDL_PROBE_ENABLE is not set
```

outdoor：

```text
CONFIG_IDF_TARGET="esp32p4"
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
CONFIG_PARTITION_TABLE_SINGLE_APP=y
CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp.csv"
# CONFIG_LABGUARD_ESPDL_PROBE_ENABLE is not set
```

非默认 SD-card POC overlay 仍为独立文件：

```text
firmware/esp_indoor/sdkconfig.defaults.espdl_sdcard_poc
firmware/esp_outdoor/sdkconfig.defaults.espdl_sdcard_poc
```

默认 `sdkconfig.defaults` 未自动包含这些 overlay。

### default-off build 复核

indoor 命令：

```sh
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_indoor"
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
idf.py -B /tmp/espdl_a7_indoor_default build
```

结果：

```text
labguard_indoor.bin binary size 0x10e150 bytes.
Smallest app partition is 0x177000 bytes.
0x68eb0 bytes (28%) free.
Project build complete.
```

outdoor 命令：

```sh
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_outdoor"
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
idf.py -B /tmp/espdl_a7_outdoor_default build
```

结果：

```text
labguard_outdoor.bin binary size 0xe4220 bytes.
Smallest app partition is 0x100000 bytes.
0x1bde0 bytes (11%) free.
Project build complete.
```

### preflight 运行结果

脚本：

```text
firmware/tools/espdl_probe_preflight.sh
```

`sh -n` 通过。脚本内容为只读检查，不执行 build、flash、monitor，不修改 `sdkconfig` 或分区表。

在已 source ESP-IDF v6.0.1 环境后运行结果摘要：

```text
branch=research/espdl-poc
idf.py=/Users/mengxin/.codex-labguard/esp-idf-v6.0.1/tools/idf.py
ESP-IDF v6.0.1
IDF_PATH=/Users/mengxin/.codex-labguard/esp-idf-v6.0.1
model.espdl=present bytes=3603872 sha256=3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea
model.info=present bytes=22341686 sha256=5b20d881dd702e0ee9a1deb92095d77588eb161a9ed35436b589f1f0c462832e
model.json=present bytes=984482 sha256=9ac07eddcdcd2f871e3a0a0fd31efc7144f37d01d729a14a71d5062947ce538b
indoor: CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
outdoor: CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
```

串口候选仍只有：

```text
/dev/cu.Bluetooth-Incoming-Port
/dev/cu.debug-console
/dev/tty.Bluetooth-Incoming-Port
/dev/tty.debug-console
```

未在命令输出中找到明确 ESP32-P4 板卡串口。

### 本轮新增/更新文档

新增：

```text
docs/espdl_a7_handoff.md
```

更新：

```text
docs/espdl_pre_research.md
docs/espdl_flash_partition_plan.md
docs/espdl_hardware_checklist.md
firmware/components/espdl_probe/README.md
```

### 第七轮当前结论

```text
工程改动应暂停在 isolated POC 层。
不允许进入主链路接入。
不允许烧录。
不允许伪造串口日志。
不允许伪造 model->test/profile 结果。
```

继续工程验证的准入条件：

```text
1. 用户提供实际 ESP32-P4 串口路径。
2. 用户提供实际 flash size 读取结果。
3. 用户确认是否允许使用非默认 POC 分区表。
4. ESP-DL enabled isolated probe build 必须先通过 app_check_size。
5. 之后才允许 flash、monitor，并记录 model->test/profile 日志。
```
