#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEFAULT_PYTHON="$HOME/.codex-labguard/esp-ppq-py311-venv/bin/python"
PYTHON=${PYTHON:-$DEFAULT_PYTHON}

if [ ! -x "$PYTHON" ]; then
    printf 'ERROR: Python with ESP-PPQ is not executable: %s\n' "$PYTHON" >&2
    printf 'TODO: create the ESP-PPQ Python 3.11 venv or set PYTHON=/path/to/python.\n' >&2
    exit 3
fi

exec "$PYTHON" "$SCRIPT_DIR/espdl_export_poc.py" "$@"
