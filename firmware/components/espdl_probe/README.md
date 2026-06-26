# Isolated ESP-DL Probe

This component validates that an ESP-DL `.espdl` model can be loaded and profiled
on the ESP32-P4 board without wiring model output into the LabGuard risk path.

Default behavior:

```text
CONFIG_LABGUARD_ESPDL_PROBE_ENABLE is not set
CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT is not set
```

When enabled, the default load path is:

```text
/sdcard/model.espdl
```

The probe runs only these checks:

```text
dl::Model load
model->test()
model->profile_memory()
model->profile_module()
```

It does not call camera inference, `risk_fusion_evaluate()`, or actuator logic.
