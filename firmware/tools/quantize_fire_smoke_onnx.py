#!/usr/bin/env python3
"""Quantize the lab fire/smoke YOLO ONNX model into an ESP-DL artifact."""

from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Iterable, List

import torch
from PIL import Image
from torch.utils.data import DataLoader, Dataset
from torchvision import transforms

from esp_ppq import QuantizationSettingFactory
from esp_ppq.api import espdl_quantize_onnx


class ImageCalibrationDataset(Dataset):
    def __init__(self, image_dir: Path, height: int, width: int) -> None:
        self.image_dir = image_dir
        self.height = height
        self.width = width
        self.paths = [
            path
            for path in sorted(image_dir.iterdir())
            if path.suffix.lower() in {".jpg", ".jpeg", ".png", ".bmp"}
        ]
        if not self.paths:
            raise FileNotFoundError(f"No calibration images found under: {image_dir}")
        self.transform = transforms.Compose(
            [
                transforms.Resize((height, width)),
                transforms.ToTensor(),
            ]
        )

    def __len__(self) -> int:
        return len(self.paths)

    def __getitem__(self, index: int) -> torch.Tensor:
        image = Image.open(self.paths[index]).convert("RGB")
        return self.transform(image)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Quantize lab fire/smoke ONNX to ESP-DL")
    parser.add_argument("--onnx", type=Path, required=True, help="Path to input ONNX file")
    parser.add_argument("--output", type=Path, required=True, help="Output .espdl path")
    parser.add_argument("--target", default="esp32p4", help="ESP-DL target, e.g. esp32p4")
    parser.add_argument("--input-shape", default="1,3,320,320", help="N,C,H,W input shape")
    parser.add_argument("--calib-dir", type=Path, required=True, help="Calibration image directory")
    parser.add_argument("--calib-steps", type=int, default=16, help="Calibration steps")
    parser.add_argument("--batch-size", type=int, default=1, help="Calibration batch size")
    parser.add_argument("--bits", type=int, default=8, choices=[8, 16], help="Quantization bit width")
    parser.add_argument("--device", default="cpu", choices=["cpu", "cuda"], help="Execution device")
    parser.add_argument("--verbose", type=int, default=1, help="esp-ppq verbosity")
    return parser.parse_args()


def parse_input_shape(input_shape_text: str) -> List[int]:
    parts = [int(part.strip()) for part in input_shape_text.split(",") if part.strip()]
    if len(parts) != 4:
        raise ValueError(f"Expected N,C,H,W input shape, got: {input_shape_text}")
    return parts


def collate_fn(batch: torch.Tensor) -> torch.Tensor:
    if isinstance(batch, list):
        return torch.stack(batch, dim=0)
    return batch


def main() -> int:
    args = parse_args()
    args.onnx = args.onnx.resolve()
    args.output = args.output.resolve()
    args.calib_dir = args.calib_dir.resolve()

    if not args.onnx.exists():
        raise FileNotFoundError(f"ONNX file not found: {args.onnx}")
    if not args.calib_dir.exists():
        raise FileNotFoundError(f"Calibration directory not found: {args.calib_dir}")

    input_shape = parse_input_shape(args.input_shape)
    batch_size, channels, height, width = input_shape
    if channels != 3:
        raise ValueError(f"This script expects RGB input, got channel count: {channels}")

    dataset = ImageCalibrationDataset(args.calib_dir, height=height, width=width)
    dataloader = DataLoader(dataset=dataset, batch_size=args.batch_size, shuffle=False)
    effective_steps = min(args.calib_steps, math.ceil(len(dataset) / args.batch_size))
    if effective_steps <= 0:
        raise ValueError("Calibration steps resolved to zero")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    setting = QuantizationSettingFactory.espdl_setting()

    print(f"ONNX       : {args.onnx}")
    print(f"Output     : {args.output}")
    print(f"Target     : {args.target}")
    print(f"Input shape: {input_shape}")
    print(f"Calib dir  : {args.calib_dir}")
    print(f"Images     : {len(dataset)}")
    print(f"Calib steps: {effective_steps}")
    print(f"Device     : {args.device}")

    espdl_quantize_onnx(
        onnx_import_file=str(args.onnx),
        espdl_export_file=str(args.output),
        calib_dataloader=dataloader,
        calib_steps=effective_steps,
        input_shape=input_shape,
        target=args.target,
        num_of_bits=args.bits,
        collate_fn=collate_fn,
        setting=setting,
        device=args.device,
        error_report=True,
        skip_export=False,
        export_config=True,
        export_test_values=False,
        verbose=args.verbose,
    )

    if not args.output.exists():
        raise FileNotFoundError(f"Conversion finished without creating output: {args.output}")

    print(f"Generated: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
