#!/usr/bin/env python3
"""Export the handoff ONNX model to a real ESP-DL .espdl artifact.

This POC uses the current esp-ppq Python API. It never creates placeholder
model files: success requires esp-ppq to write model.espdl, model.json, and
model.info.
"""

from __future__ import annotations

import argparse
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Iterable

import onnx
import torch
import torchvision.transforms as transforms
from PIL import Image
from torch.utils.data import DataLoader, Dataset

from esp_ppq.api import espdl_quantize_onnx


REPO_ROOT = Path(__file__).resolve().parents[2]
HANDOFF_ROOT = (
    REPO_ROOT
    / "lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001"
    / "lab_fire_smoke_model_handoff_v1"
)
DEFAULT_INPUT = HANDOFF_ROOT / "model" / "best.onnx"
DEFAULT_CALIB_DIR = HANDOFF_ROOT / "predict_samples"
DEFAULT_OUTPUT_DIR = REPO_ROOT / "firmware" / "components" / "espdl_probe" / "model"


class ImageCalibDataset(Dataset[torch.Tensor]):
    def __init__(self, image_paths: Iterable[Path], height: int, width: int) -> None:
        self.image_paths = list(image_paths)
        if not self.image_paths:
            raise FileNotFoundError("No calibration images were found.")
        self.transform = transforms.Compose(
            [
                transforms.Resize((height, width), interpolation=transforms.InterpolationMode.BILINEAR),
                transforms.ToTensor(),
            ]
        )

    def __len__(self) -> int:
        return len(self.image_paths)

    def __getitem__(self, index: int) -> torch.Tensor:
        with Image.open(self.image_paths[index]) as image:
            return self.transform(image.convert("RGB"))


def _image_files(directory: Path) -> list[Path]:
    suffixes = {".jpg", ".jpeg", ".png", ".bmp"}
    return sorted(path for path in directory.iterdir() if path.is_file() and path.suffix.lower() in suffixes)


def _single_onnx_input_shape(path: Path) -> list[int]:
    model = onnx.load(str(path))
    graph_inputs = list(model.graph.input)
    if len(graph_inputs) != 1:
        raise ValueError(f"Expected exactly one ONNX input, found {len(graph_inputs)}.")

    dims: list[int] = []
    for dim in graph_inputs[0].type.tensor_type.shape.dim:
        if dim.dim_value <= 0:
            raise ValueError(f"ONNX input has a dynamic or unknown dimension: {graph_inputs[0].name}")
        dims.append(int(dim.dim_value))

    if len(dims) != 4 or dims[0] != 1 or dims[1] != 3:
        raise ValueError(f"Expected ONNX input shape [1, 3, H, W], found {dims}.")

    return dims


def _normalize_negative_axes(path: Path) -> int:
    model = onnx.load(str(path))
    inferred = onnx.shape_inference.infer_shapes(model)
    ranks: dict[str, int] = {}
    for value_info in list(inferred.graph.input) + list(inferred.graph.value_info) + list(inferred.graph.output):
        shape = value_info.type.tensor_type.shape
        if shape.dim:
            ranks[value_info.name] = len(shape.dim)

    changed = 0
    for node in model.graph.node:
        for attr in node.attribute:
            if attr.name != "axis" or attr.i >= 0:
                continue

            rank = None
            for value_name in list(node.input) + list(node.output):
                rank = ranks.get(value_name)
                if rank is not None:
                    break
            if rank is None:
                raise ValueError(f"Cannot normalize negative axis for {node.name}: tensor rank not found.")

            attr.i = int(attr.i) + rank
            if attr.i < 0:
                raise ValueError(f"Cannot normalize negative axis for {node.name}: rank={rank}, axis={attr.i}.")
            changed += 1

    if changed:
        onnx.save(model, str(path))
    return changed


def _collate_to_device(batch: torch.Tensor | list[torch.Tensor], device: str) -> torch.Tensor:
    if isinstance(batch, torch.Tensor):
        return batch.to(device)
    return torch.stack(batch, dim=0).to(device)


def _artifact_paths(output_dir: Path) -> dict[str, Path]:
    return {
        "espdl": output_dir / "model.espdl",
        "json": output_dir / "model.json",
        "info": output_dir / "model.info",
    }


def _print_artifacts(paths: dict[str, Path]) -> None:
    for name, path in paths.items():
        if path.exists():
            print(f"{name}={path} size={path.stat().st_size}")
        else:
            print(f"{name}=missing:{path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export a real ESP32-P4 .espdl POC model with ESP-PPQ.")
    parser.add_argument("input_model", nargs="?", type=Path, help="Optional .onnx input model path.")
    parser.add_argument("output_dir", nargs="?", type=Path, help="Optional output directory for model artifacts.")
    parser.add_argument("--input", dest="input_override", type=Path, help="Explicit .onnx input model path.")
    parser.add_argument("--calib-dir", type=Path, default=DEFAULT_CALIB_DIR, help="Directory of calibration images.")
    parser.add_argument("--output-dir", dest="output_override", type=Path, help="Directory for model.espdl/json/info.")
    parser.add_argument("--target", default="esp32p4", help="ESP-DL target chip, e.g. esp32p4.")
    parser.add_argument("--bits", type=int, default=8, choices=(8, 16), help="Quantization bit width.")
    parser.add_argument("--batch-size", type=int, default=1, help="Calibration batch size.")
    parser.add_argument("--calib-steps", type=int, default=8, help="Calibration steps.")
    parser.add_argument("--device", default="cpu", help="ESP-PPQ execution device.")
    parser.add_argument("--error-report", action="store_true", help="Enable ESP-PPQ quantization error reports.")
    parser.add_argument("--dry-run", action="store_true", help="Check inputs and print the planned export only.")
    parser.add_argument(
        "--no-export-test-values",
        action="store_true",
        help="Disable embedded test values. Leave unset for board-side model->test().",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_model = args.input_override or args.input_model or DEFAULT_INPUT
    output_dir = args.output_override or args.output_dir or DEFAULT_OUTPUT_DIR
    input_model = input_model.resolve()
    output_dir = output_dir.resolve()
    calib_dir = args.calib_dir.resolve()

    print("ESP-DL export POC")
    print(f"input_model={input_model}")
    print(f"calib_dir={calib_dir}")
    print(f"output_dir={output_dir}")
    print(f"target={args.target}")
    print(f"bits={args.bits}")
    print(f"device={args.device}")
    print(f"export_test_values={not args.no_export_test_values}")

    if input_model.suffix.lower() != ".onnx":
        raise ValueError("This POC conversion entry currently supports the actual handoff .onnx path only.")
    if not input_model.is_file():
        raise FileNotFoundError(f"Input ONNX model not found: {input_model}")
    if not calib_dir.is_dir():
        raise FileNotFoundError(f"Calibration image directory not found: {calib_dir}")

    input_shape = _single_onnx_input_shape(input_model)
    _, channels, height, width = input_shape
    image_paths = _image_files(calib_dir)
    if not image_paths:
        raise FileNotFoundError(f"No calibration images found in {calib_dir}")

    dataset = ImageCalibDataset(image_paths, height=height, width=width)
    dataloader = DataLoader(dataset=dataset, batch_size=args.batch_size, shuffle=False)
    calib_steps = min(args.calib_steps, len(dataloader))
    if calib_steps <= 0:
        raise ValueError("Calibration steps resolved to zero.")

    artifacts = _artifact_paths(output_dir)
    print(f"onnx_input_shape={input_shape}")
    print(f"calibration_images={len(image_paths)}")
    print(f"effective_calib_steps={calib_steps}")
    _print_artifacts(artifacts)

    if args.dry_run:
        return 0

    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="labguard_espdl_export_") as temp_dir_name:
        work_onnx = Path(temp_dir_name) / input_model.name
        shutil.copy2(input_model, work_onnx)
        normalized_axes = _normalize_negative_axes(work_onnx)
        print(f"normalized_negative_axes={normalized_axes}")

        espdl_quantize_onnx(
            onnx_import_file=str(work_onnx),
            espdl_export_file=str(artifacts["espdl"]),
            calib_dataloader=dataloader,
            calib_steps=calib_steps,
            input_shape=input_shape,
            target=args.target,
            num_of_bits=args.bits,
            collate_fn=lambda batch: _collate_to_device(batch, args.device),
            device=args.device,
            error_report=args.error_report,
            skip_export=False,
            export_config=True,
            export_test_values=not args.no_export_test_values,
            verbose=1,
            metadata_props={
                "source_model": input_model.name,
                "target": args.target,
                "bits": str(args.bits),
                "input_shape": "x".join(str(dim) for dim in input_shape),
                "calibration_images": str(len(image_paths)),
                "calibration_source": calib_dir.name,
            },
        )

    _print_artifacts(artifacts)
    missing = [name for name, path in artifacts.items() if not path.is_file()]
    if missing:
        raise RuntimeError(f"ESP-PPQ export finished but required artifacts are missing: {', '.join(missing)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise
