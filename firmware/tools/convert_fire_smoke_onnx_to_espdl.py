#!/usr/bin/env python3
"""
Convert the lab fire/smoke ONNX model into an ESP-DL .espdl artifact.

This script is a project wrapper around Espressif's esp-ppq tooling. By
default it invokes the repo-local quantization script that knows the expected
model path, calibration-image directory, and output location for this project.
"""

from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
FIRMWARE_DIR = SCRIPT_DIR.parent
REPO_ROOT = FIRMWARE_DIR.parent
DEFAULT_ONNX = REPO_ROOT / (
    "lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/"
    "lab_fire_smoke_model_handoff_v1/model/best.onnx"
)
DEFAULT_OUTPUT = FIRMWARE_DIR / "models/p4/lab_fire_smoke.espdl"
DEFAULT_CALIB_DIR = REPO_ROOT / (
    "lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/"
    "lab_fire_smoke_model_handoff_v1/predict_samples"
)
DEFAULT_QUANT_SCRIPT = SCRIPT_DIR / "quantize_fire_smoke_onnx.py"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert lab fire/smoke ONNX to ESP-DL format.")
    parser.add_argument("--onnx", type=Path, default=DEFAULT_ONNX, help="Path to best.onnx")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="Output .espdl path")
    parser.add_argument(
        "--target",
        default="esp32p4",
        choices=["esp32p4", "esp32s3", "esp32s31", "c"],
        help="ESP target used by esp-ppq during export.",
    )
    parser.add_argument(
        "--input-shape",
        default="1,3,320,320",
        help="Model input tensor shape as comma-separated N,C,H,W.",
    )
    parser.add_argument(
        "--calib-dir",
        type=Path,
        default=DEFAULT_CALIB_DIR,
        help="Calibration image directory.",
    )
    parser.add_argument(
        "--calib-steps",
        type=int,
        default=16,
        help="Number of calibration steps to run.",
    )
    parser.add_argument(
        "--quant-script",
        type=Path,
        default=DEFAULT_QUANT_SCRIPT,
        help="Python quantization script that exports the .espdl artifact.",
    )
    parser.add_argument(
        "--device",
        default="cpu",
        choices=["cpu", "cuda"],
        help="Device passed to the quantization script.",
    )
    parser.add_argument(
        "--verbose",
        type=int,
        default=1,
        help="Verbosity passed to the quantization script.",
    )
    return parser.parse_args()


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def main() -> int:
    args = parse_args()
    args.onnx = args.onnx.resolve()
    args.output = args.output.resolve()
    args.calib_dir = args.calib_dir.resolve()
    args.quant_script = args.quant_script.resolve()

    if not args.onnx.exists():
        print(f"ONNX file not found: {args.onnx}", file=sys.stderr)
        return 1

    if not args.calib_dir.exists():
        print(f"Calibration directory not found: {args.calib_dir}", file=sys.stderr)
        return 1

    if not args.quant_script.exists():
        print(f"Quantization script not found: {args.quant_script}", file=sys.stderr)
        return 1

    if importlib.util.find_spec("esp_ppq") is None:
        print("esp-ppq is not installed in the current Python environment.", file=sys.stderr)
        print("Activate the Python 3.12 project venv first, for example:", file=sys.stderr)
        print("  source .venv-espdl/bin/activate", file=sys.stderr)
        print("  python -m pip install torch torchvision torchaudio esp-ppq", file=sys.stderr)
        return 1

    ensure_parent(args.output)

    command = [
        sys.executable,
        str(args.quant_script),
        "--onnx",
        str(args.onnx),
        "--output",
        str(args.output),
        "--target",
        args.target,
        "--input-shape",
        args.input_shape,
        "--calib-dir",
        str(args.calib_dir),
        "--calib-steps",
        str(args.calib_steps),
        "--device",
        args.device,
        "--verbose",
        str(args.verbose),
    ]

    print("Running:", " ".join(command), flush=True)
    completed = subprocess.run(command, check=False)
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
