# ESP-DL A7 handoff

Date: 2026-06-04

Worktree:

```text
/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc
```

Branch:

```text
research/espdl-poc
```

## Current A-line status

The ESP-DL / `.espdl` work is paused at the isolated POC layer.

```text
default-off build: passes for indoor and outdoor
ESP-DL enabled build: blocked by current app partition size
flash: not performed
serial log: not collected
model->test(): not executed
profile_memory(): not executed
profile_module(): not executed
main-chain integration: not allowed yet
```

The blocker is not the probe wrapper itself. Sixth-round size attribution
showed the major increase comes from ESP-DL runtime, C++ runtime, and
`fbs_model`.

## Real artifacts

The current real ESP-DL artifacts are:

```text
firmware/components/espdl_probe/model/model.espdl
size: 3603872 bytes
sha256: 3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea

firmware/components/espdl_probe/model/model.info
size: 22341686 bytes
sha256: 5b20d881dd702e0ee9a1deb92095d77588eb161a9ed35436b589f1f0c462832e

firmware/components/espdl_probe/model/model.json
size: 984482 bytes
sha256: 9ac07eddcdcd2f871e3a0a0fd31efc7144f37d01d729a14a71d5062947ce538b
```

A7 tracking check:

```text
git ls-files firmware/components/espdl_probe/model/model.espdl firmware/components/espdl_probe/model/model.info firmware/components/espdl_probe/model/model.json
```

returned no tracked files, and:

```text
git check-ignore -v firmware/components/espdl_probe/model/model.espdl firmware/components/espdl_probe/model/model.info firmware/components/espdl_probe/model/model.json || true
```

returned no ignore rule. These large artifacts are currently untracked and not
ignored. Do not `git add` them without an explicit project decision.

## Build evidence

A7 default-off build commands used the report-recorded ESP-IDF environment:

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
```

Indoor:

```text
command: idf.py -B /tmp/espdl_a7_indoor_default build
result: Project build complete
labguard_indoor.bin binary size 0x10e150 bytes
smallest app partition 0x177000 bytes
free 0x68eb0 bytes (28%)
```

Outdoor:

```text
command: idf.py -B /tmp/espdl_a7_outdoor_default build
result: Project build complete
labguard_outdoor.bin binary size 0xe4220 bytes
smallest app partition 0x100000 bytes
free 0x1bde0 bytes (11%)
```

Sixth-round ESP-DL enabled lower-bound results remain the current blocker:

```text
indoor SD-card min POC:
labguard_indoor.bin size 0x259590
partition size 0x177000
overflow 0xe2590

outdoor SD-card min POC:
labguard_outdoor.bin size 0x234690
partition size 0x100000
overflow 0x134690
```

## Current blockers

Confirmed generated `sdkconfig` state:

```text
firmware/esp_indoor/sdkconfig:
CONFIG_IDF_TARGET="esp32p4"
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp_large.csv"
# CONFIG_LABGUARD_ESPDL_PROBE_ENABLE is not set

firmware/esp_outdoor/sdkconfig:
CONFIG_IDF_TARGET="esp32p4"
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
CONFIG_PARTITION_TABLE_SINGLE_APP=y
CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp.csv"
# CONFIG_LABGUARD_ESPDL_PROBE_ENABLE is not set
```

Current serial candidates:

```text
/dev/cu.Bluetooth-Incoming-Port
/dev/cu.debug-console
/dev/tty.Bluetooth-Incoming-Port
/dev/tty.debug-console
```

No command output currently identifies a real ESP32-P4 board serial port.

## What must not be claimed

Do not claim any of the following until there is board-side evidence:

```text
flashed
serial log collected
model->test() passed
profile_memory() result available
profile_module() result available
main-chain integration is ready
```

Do not connect ESP-DL to these functions in this POC state:

```text
ppe_infer_run
hazard_infer_run
door_fsm_evaluate
risk_fusion_evaluate
```

## Manual inputs needed from the user

Before the next board-side validation round, the user must provide:

```text
1. Actual ESP32-P4 board serial port path.
2. Plug-before and plug-after outputs for ls /dev/cu.* and ls /dev/tty.*.
3. Actual flash size read from esptool.py or idf.py output.
4. Whether a non-default POC partition table is allowed in this branch.
5. If using indoor SD-card mode, confirmation that /sdcard/model.espdl is present.
```

## Next-round gate

The next engineering round may proceed to board validation only if all of these
are true:

```text
actual flash capacity is confirmed
actual serial port is confirmed
ESP-DL enabled isolated probe build passes app_check_size
```

Only after those gates pass may the workflow continue to flash, monitor, and
collect `model->test()`, `profile_memory()`, and `profile_module()` logs.
