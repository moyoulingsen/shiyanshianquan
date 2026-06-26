#!/bin/sh
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
MODEL_DIR="$ROOT/firmware/components/espdl_probe/model"

print_header() {
    printf '\n== %s ==\n' "$1"
}

print_header "Repository"
printf 'root=%s\n' "$ROOT"
if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'branch=%s\n' "$(git -C "$ROOT" branch --show-current 2>/dev/null || true)"
    git -C "$ROOT" status --short --branch --untracked-files=normal
else
    printf 'git=not found at root\n'
fi

print_header "ESP-IDF"
if command -v idf.py >/dev/null 2>&1; then
    printf 'idf.py=%s\n' "$(command -v idf.py)"
    idf.py --version || true
else
    printf 'idf.py=not found in PATH\n'
fi
printf 'IDF_PATH=%s\n' "${IDF_PATH:-}"

print_header "Model artifacts"
for name in model.espdl model.info model.json; do
    path="$MODEL_DIR/$name"
    if [ -s "$path" ]; then
        bytes=$(wc -c < "$path" | tr -d ' ')
        printf '%s=present bytes=%s\n' "$path" "$bytes"
        shasum -a 256 "$path" 2>/dev/null || true
    else
        printf '%s=missing-or-empty\n' "$path"
    fi
done

print_header "Project configuration"
for app in esp_indoor esp_outdoor; do
    cfg="$ROOT/firmware/$app/sdkconfig"
    printf '%s\n' "$app"
    if [ -f "$cfg" ]; then
        grep -E 'CONFIG_IDF_TARGET=|CONFIG_ESPTOOLPY_FLASHSIZE=|CONFIG_ESPTOOLPY_FLASHSIZE_[0-9A-Z]+=|CONFIG_PARTITION_TABLE_' "$cfg" || true
        grep -E 'CONFIG_LABGUARD_ESPDL_PROBE_' "$cfg" || true
    else
        printf 'sdkconfig=not found\n'
    fi
done

print_header "Serial candidates"
ls /dev/cu.* 2>/dev/null | sort || true
ls /dev/tty.* 2>/dev/null | sort || true

print_header "Preflight summary"
printf 'This script only reports blockers. It does not flash, build, or change sdkconfig.\n'
if grep -q 'CONFIG_ESPTOOLPY_FLASHSIZE="2MB"' "$ROOT/firmware/esp_indoor/sdkconfig" 2>/dev/null; then
    printf 'indoor_flash_blocker=current sdkconfig says 2MB flash\n'
fi
if grep -q 'CONFIG_ESPTOOLPY_FLASHSIZE="2MB"' "$ROOT/firmware/esp_outdoor/sdkconfig" 2>/dev/null; then
    printf 'outdoor_flash_blocker=current sdkconfig says 2MB flash\n'
fi
printf 'manual_next_step=confirm physical flash size and board serial port before flash/monitor\n'
