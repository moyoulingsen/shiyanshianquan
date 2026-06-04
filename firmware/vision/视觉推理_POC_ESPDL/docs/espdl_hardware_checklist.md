# ESP-DL hardware checklist

Date: 2026-06-04

This checklist is for the isolated ESP-DL probe only. It is not a main-chain
inference integration checklist.

## Required before flashing

Confirm the physical board and connection:

```text
1. ESP32-P4 board is connected over USB/JTAG or UART.
2. The actual serial port is known.
3. The actual flash capacity is known.
4. If SD-card mode is used, /sdcard/model.espdl is present on the card.
5. The model file on SD-card matches the branch artifact hash:
   3e24bf2391675d5a69c802455d470693ffc029533dfda494198a213d6a0804ea
```

Current command output did not identify a board serial port:

```text
/dev/cu.Bluetooth-Incoming-Port
/dev/cu.debug-console
/dev/tty.Bluetooth-Incoming-Port
/dev/tty.debug-console
```

Do not use these as the board port without manual confirmation.

A7 preflight after sourcing ESP-IDF v6.0.1 still reported only the same serial
candidates. It also confirmed both app `sdkconfig` files still contain:

```text
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="2MB"
```

Do not flash until command output or the user confirms a real ESP32-P4 port and
the actual physical flash capacity.

## Indoor-specific notes

Repository facts:

```text
firmware/esp_indoor/main/app_main.c
SD_CARD_MOUNT_POINT "/sdcard"
init_sd_card()
```

The indoor probe call is placed after the existing SD-card initialization when
`CONFIG_LABGUARD_ESPDL_PROBE_RUN_ON_BOOT=y`.

For SD-card probe validation, manually place:

```text
/sdcard/model.espdl
```

## Outdoor-specific notes

No SD-card mount logic was found in the outdoor repository path during this
pre-research. Outdoor SD-card mode can compile, but board-side loading is not
validated until an outdoor mount path is provided or implemented in an isolated
way.

## Commands after the size blocker is resolved

Indoor:

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_indoor"
idf.py -p <confirmed-board-port> flash monitor
```

Outdoor:

```sh
source "$HOME/.codex-labguard/esp-idf-v6.0.1/export.sh"
cd "/Users/mengxin/Desktop/桌面文档/嵌入式芯片大赛/工程文件/shiyanshianquan-espdl-poc/firmware/esp_outdoor"
idf.py -p <confirmed-board-port> flash monitor
```

Expected probe log tag:

```text
espdl_probe
```

Required board-side evidence before any main-chain integration:

```text
model->test() passed
model->profile_memory()
model->profile_module(...)
```

A7 status:

```text
flashed: no
serial log collected: no
model->test(): not executed
profile_memory(): not executed
profile_module(): not executed
main-chain integration: not allowed
```
