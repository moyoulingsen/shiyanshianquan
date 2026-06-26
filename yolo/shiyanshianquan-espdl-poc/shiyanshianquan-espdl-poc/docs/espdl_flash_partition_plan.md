# ESP-DL flash and partition plan

Date: 2026-06-04

Worktree:

```text
/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc
```

Branch:

```text
research/espdl-poc
```

## Current confirmed constraints

The current ESP-IDF apps are:

```text
firmware/esp_indoor
firmware/esp_outdoor
```

Both generated `sdkconfig` files currently target:

```text
esp32p4
```

Current flash configuration found in the generated `sdkconfig` files:

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

ESP-IDF v6.0.1 default partition sizes confirmed by build output:

```text
indoor factory app partition:  0x177000
outdoor factory app partition: 0x100000
```

Current real ESP-DL artifact:

```text
firmware/components/espdl_probe/model/model.espdl
size: 3603872 bytes
sha256: 3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea
```

## Build size facts

Default-off builds still fit current partitions:

```text
indoor default-off:
labguard_indoor.bin size 0x10e150
partition size 0x177000
free 0x68eb0

outdoor default-off:
labguard_outdoor.bin size 0xe4220
partition size 0x100000
free 0x1bde0
```

A7 recheck with `idf.py -B /tmp/espdl_a7_*_default build` produced the same
default-off size results and completed successfully for both apps.

Earlier rodata probe builds linked successfully, then failed at app size check:

```text
indoor rodata probe:
labguard_indoor.bin size 0x5e4100
partition size 0x177000
overflow 0x46d100

outdoor rodata probe:
labguard_outdoor.bin size 0x5b9c20
partition size 0x100000
overflow 0x4b9c20
```

SD-card probe mode avoids embedding `model.espdl`, but still links ESP-DL and
C++ runtime. The sixth round lower-bound measurements still exceeded the
current app partitions:

```text
indoor SD-card min:
labguard_indoor.bin size 0x259590
partition size 0x177000
overflow 0xe2590

outdoor SD-card min, measured against current single-app partition:
labguard_outdoor.bin size 0x234690
partition size 0x100000
overflow 0x134690
```

## Deployment options

### Option A: rodata model

Required app partition must contain the existing app, ESP-DL runtime, C++
runtime, probe code, and the `3.4 MB` `.espdl` blob.

Observed rodata binary sizes:

```text
indoor:  0x5e4100
outdoor: 0x5b9c20
```

This option is the simplest for API validation, but it requires a larger flash
device and a larger factory app partition than the current `2MB` configuration.

### Option B: flash model partition

This keeps the model outside the app partition and would load through
`fbs::MODEL_LOCATION_IN_FLASH_PARTITION`.

Minimum required changes after hardware flash size is confirmed:

```text
1. Create a non-default partition table with a larger app partition.
2. Add a separate model data partition large enough for model.espdl plus margin.
3. Add a probe-only Kconfig option for partition label.
4. Add CMake flash target support for the model partition.
5. Keep the existing main inference paths untouched until board validation passes.
```

This branch does not create a concrete partition CSV yet because the physical
board flash capacity has not been confirmed by command output or user-provided
hardware data.

### Option C: SD-card model

This keeps `model.espdl` on `/sdcard/model.espdl`.

Known repository facts:

```text
indoor:
SD_CARD_MOUNT_POINT "/sdcard"
init_sd_card()

outdoor:
no SD-card mount logic found in the repository
```

SD-card still needs a larger app partition than the current configuration
because ESP-DL runtime and C++ runtime add about `1.3 MB` to the app.

## Recommendation

Do not force the current `2MB` config to pass by deleting business code.

Do not change the default flash size or default partition table in this branch
until the actual physical ESP32-P4 board flash size is confirmed. A larger app
partition may be evaluated only as a non-default POC configuration after that
confirmation.

Recommended next local step after hardware confirmation:

```text
1. Confirm the actual ESP32-P4 board flash size.
2. If flash is larger than 2MB, create a non-default POC partition table.
3. First validate indoor SD-card probe, because indoor already mounts /sdcard.
4. Capture serial logs for model->test(), profile_memory(), and profile_module().
5. Only after that, decide whether rodata, flash partition, or SD-card is the
   correct final deployment mode.
```

Until these steps pass, ESP-DL should remain an isolated POC component.
