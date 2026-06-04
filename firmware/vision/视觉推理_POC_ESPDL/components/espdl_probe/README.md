# Isolated ESP-DL probe

This component is an isolated ESP-DL / `.espdl` proof of concept for the
LabGuard ESP-IDF apps.

It is default-off:

```text
CONFIG_LABGUARD_ESPDL_PROBE_ENABLE is not set
CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT is not set
```

It does not connect ESP-DL to these business inference paths:

```text
ppe_infer_run
hazard_infer_run
door_fsm_evaluate
risk_fusion_evaluate
```

Supported POC load modes:

```text
rodata:
  target_add_aligned_binary_data(...)
  _binary_model_espdl_start
  dl::Model(..., fbs::MODEL_LOCATION_IN_FLASH_RODATA)

sdcard:
  CONFIG_LABGUARD_ESPDL_PROBE_SDCARD_PATH
  dl::Model(..., fbs::MODEL_LOCATION_IN_SDCARD)
```

Current exported artifacts:

```text
model/model.espdl
model/model.info
model/model.json
```

A7 verified artifact summary:

```text
model/model.espdl bytes=3603872 sha256=3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea
model/model.info  bytes=22341686 sha256=5b20d881dd702e0ee9a1deb92095d77588eb161a9ed35436b589f1f0c462832e
model/model.json  bytes=984482 sha256=9ac07eddcdcd2f871e3a0a0fd31efc7144f37d01d729a14a71d5062947ce538b
```

Current A7 status:

```text
default-off indoor build: passes
default-off outdoor build: passes
ESP-DL enabled SD-card min build: fails app_check_size
current flash config: CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
current serial port: not confirmed
flash/monitor: not performed
model->test/profile: not executed
```

Do not flash this POC until the actual board serial port and actual flash size
are confirmed. Do not claim board-side validation until the isolated probe has
logged `model->test()`, `profile_memory()`, and `profile_module()` results.

Use the SD-card size POC overlays only in isolated build directories:

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
cd firmware/esp_indoor
cmake -S . -B /tmp/espdl_indoor_sdcard_poc \
  -DSDKCONFIG=/tmp/espdl_indoor_sdcard_poc.sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.espdl_sdcard_poc" \
  -DIDF_TARGET=esp32p4
cmake --build /tmp/espdl_indoor_sdcard_poc
```

Do not enable this component for the normal demo flow until board-side
`model->test()`, `profile_memory()`, and `profile_module()` logs are captured.
