#!/usr/bin/env python3
"""
Prepare a small gloves/goggles YOLO dataset from Roboflow PPEs.

Source dataset:
    https://universe.roboflow.com/personal-protective-equipment/ppes-kaxsi

Default kept classes:
    glove, no_glove, goggles, no_goggles

The script can either:
  1. Filter an already downloaded YOLOv8 dataset.
  2. Download Roboflow Universe dataset version 7 first, then filter it.

Examples:
    python tools/prepare_roboflow_ppe_subset.py \
        --source datasets/roboflow-ppes \
        --output datasets/ppes-glove-goggles

    ROBOFLOW_API_KEY=xxxx python tools/prepare_roboflow_ppe_subset.py \
        --download \
        --output datasets/ppes-glove-goggles
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import tempfile
import urllib.parse
import zipfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import requests
import yaml


ROBOFLOW_WORKSPACE = "personal-protective-equipment"
ROBOFLOW_PROJECT = "ppes-kaxsi"
ROBOFLOW_VERSION = 7
ROBOFLOW_FORMAT = "yolov8"

DEFAULT_CLASSES = ["glove", "no_glove", "goggles", "no_goggles"]
IMAGE_SUFFIXES = {
    ".jpg",
    ".jpeg",
    ".png",
    ".bmp",
    ".webp",
    ".tif",
    ".tiff",
}


def normalize_name(name: str) -> str:
    return name.strip().lower().replace("-", "_").replace(" ", "_")


def read_yaml(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise FileNotFoundError(f"Missing data.yaml: {path}")
    with path.open("r", encoding="utf-8") as f:
        loaded = yaml.safe_load(f)
    if not isinstance(loaded, dict):
        raise ValueError(f"Invalid YAML object in {path}")
    return loaded


def load_class_names(data_yaml: dict[str, Any]) -> list[str]:
    names = data_yaml.get("names")
    if isinstance(names, list):
        return [str(name) for name in names]
    if isinstance(names, dict):
        return [str(names[idx]) for idx in sorted(names, key=lambda key: int(key))]
    raise ValueError("data.yaml does not contain a usable 'names' list or dict.")


def find_dataset_root(path: Path) -> Path:
    candidates = [path, *path.glob("**/data.yaml")]
    for candidate in candidates:
        if candidate.is_file() and candidate.name == "data.yaml":
            return candidate.parent
        if candidate.is_dir() and (candidate / "data.yaml").exists():
            return candidate
    raise FileNotFoundError(f"Could not find data.yaml under {path}")


def split_dirs(root: Path) -> list[tuple[str, Path]]:
    found: list[tuple[str, Path]] = []
    for split in ("train", "valid", "val", "test"):
        split_dir = root / split
        if (split_dir / "images").is_dir() and (split_dir / "labels").is_dir():
            yaml_name = "val" if split == "valid" else split
            found.append((yaml_name, split_dir))
    if not found:
        raise FileNotFoundError(
            f"No YOLO split directories found under {root}; expected train/images and train/labels."
        )
    return found


def find_image_for_label(images_dir: Path, label_file: Path) -> Path | None:
    stem = label_file.stem
    for suffix in IMAGE_SUFFIXES:
        candidate = images_dir / f"{stem}{suffix}"
        if candidate.exists():
            return candidate
    matches = [path for path in images_dir.glob(f"{stem}.*") if path.suffix.lower() in IMAGE_SUFFIXES]
    return matches[0] if matches else None


def copy_readme_if_present(source_root: Path, output_root: Path) -> None:
    for name in ("README.dataset.txt", "README.roboflow.txt"):
        source = source_root / name
        if source.exists():
            shutil.copy2(source, output_root / name)


def filter_dataset(source: Path, output: Path, keep_classes: list[str]) -> dict[str, Any]:
    source_root = find_dataset_root(source)
    data = read_yaml(source_root / "data.yaml")
    source_names = load_class_names(data)

    normalized_source = [normalize_name(name) for name in source_names]
    normalized_keep = [normalize_name(name) for name in keep_classes]

    old_to_new: dict[int, int] = {}
    for new_id, wanted_name in enumerate(normalized_keep):
        if wanted_name not in normalized_source:
            raise ValueError(
                f"Class '{keep_classes[new_id]}' not found in source dataset names: {source_names}"
            )
        old_to_new[normalized_source.index(wanted_name)] = new_id

    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    copy_readme_if_present(source_root, output)

    image_counts: Counter[str] = Counter()
    object_counts: dict[str, Counter[str]] = defaultdict(Counter)
    missing_images: list[str] = []

    for split_name, split_dir in split_dirs(source_root):
        src_images = split_dir / "images"
        src_labels = split_dir / "labels"
        dst_images = output / split_name / "images"
        dst_labels = output / split_name / "labels"
        dst_images.mkdir(parents=True, exist_ok=True)
        dst_labels.mkdir(parents=True, exist_ok=True)

        for label_file in sorted(src_labels.glob("*.txt")):
            kept_lines: list[str] = []
            with label_file.open("r", encoding="utf-8") as f:
                for line in f:
                    parts = line.strip().split()
                    if not parts:
                        continue
                    old_class = int(float(parts[0]))
                    if old_class not in old_to_new:
                        continue
                    new_class = old_to_new[old_class]
                    parts[0] = str(new_class)
                    kept_lines.append(" ".join(parts))
                    object_counts[split_name][keep_classes[new_class]] += 1

            if not kept_lines:
                continue

            image = find_image_for_label(src_images, label_file)
            if image is None:
                missing_images.append(str(label_file.relative_to(source_root)))
                continue

            shutil.copy2(image, dst_images / image.name)
            (dst_labels / label_file.name).write_text("\n".join(kept_lines) + "\n", encoding="utf-8")
            image_counts[split_name] += 1

    output_yaml = {
        "path": str(output.resolve()),
        "train": "train/images",
        "val": "val/images" if (output / "val").exists() else "valid/images",
        "test": "test/images" if (output / "test").exists() else None,
        "nc": len(keep_classes),
        "names": keep_classes,
    }
    if output_yaml["test"] is None:
        del output_yaml["test"]
    if "val/images" not in output_yaml.values() and not (output / "valid").exists():
        output_yaml["val"] = output_yaml["train"]

    with (output / "data.yaml").open("w", encoding="utf-8") as f:
        yaml.safe_dump(output_yaml, f, sort_keys=False, allow_unicode=False)

    summary = {
        "source": str(source_root),
        "output": str(output.resolve()),
        "kept_classes": keep_classes,
        "source_classes": source_names,
        "images": dict(image_counts),
        "objects": {split: dict(counts) for split, counts in object_counts.items()},
        "missing_images": missing_images[:20],
        "missing_images_total": len(missing_images),
    }
    (output / "subset_summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    return summary


def download_roboflow_dataset(api_key: str, destination: Path) -> Path:
    destination.mkdir(parents=True, exist_ok=True)
    endpoint = (
        f"https://api.roboflow.com/{ROBOFLOW_WORKSPACE}/{ROBOFLOW_PROJECT}/"
        f"{ROBOFLOW_VERSION}/{ROBOFLOW_FORMAT}"
    )
    response = requests.get(endpoint, params={"api_key": api_key}, timeout=60)
    response.raise_for_status()
    payload = response.json()

    link = (
        payload.get("download")
        or payload.get("link")
        or payload.get("url")
        or payload.get("export", {}).get("link")
        or payload.get("export", {}).get("url")
    )
    if not link:
        raise RuntimeError(f"Roboflow did not return a download link: {payload}")

    archive_path = destination / "ppes-kaxsi-7-yolov8.zip"
    with requests.get(link, stream=True, timeout=60) as download:
        download.raise_for_status()
        with archive_path.open("wb") as f:
            for chunk in download.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    f.write(chunk)

    extract_dir = destination / "ppes-kaxsi-7-yolov8"
    if extract_dir.exists():
        shutil.rmtree(extract_dir)
    with zipfile.ZipFile(archive_path, "r") as zf:
        zf.extractall(extract_dir)
    return find_dataset_root(extract_dir)


def build_download_url(api_key: str) -> str:
    endpoint = (
        f"https://api.roboflow.com/{ROBOFLOW_WORKSPACE}/{ROBOFLOW_PROJECT}/"
        f"{ROBOFLOW_VERSION}/{ROBOFLOW_FORMAT}"
    )
    return endpoint + "?" + urllib.parse.urlencode({"api_key": api_key})


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download/filter Roboflow PPEs into a gloves/goggles YOLO dataset."
    )
    parser.add_argument(
        "--source",
        type=Path,
        help="Already downloaded Roboflow YOLOv8 dataset directory containing data.yaml.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("datasets/ppes-glove-goggles"),
        help="Output directory for the filtered dataset.",
    )
    parser.add_argument(
        "--classes",
        nargs="+",
        default=DEFAULT_CLASSES,
        help="Class names to keep, in the new class order.",
    )
    parser.add_argument(
        "--download",
        action="store_true",
        help="Download Roboflow PPEs version 7 in YOLOv8 format before filtering.",
    )
    parser.add_argument(
        "--download-dir",
        type=Path,
        default=Path("datasets/roboflow_downloads"),
        help="Where downloaded Roboflow archives are stored.",
    )
    parser.add_argument(
        "--api-key",
        default=os.environ.get("ROBOFLOW_API_KEY"),
        help="Roboflow API key. Defaults to ROBOFLOW_API_KEY environment variable.",
    )
    parser.add_argument(
        "--print-download-url",
        action="store_true",
        help="Print the Roboflow REST URL for manual debugging and exit.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.print_download_url:
        if not args.api_key:
            print("Set ROBOFLOW_API_KEY or pass --api-key first.", file=sys.stderr)
            return 2
        print(build_download_url(args.api_key))
        return 0

    if args.download:
        if not args.api_key:
            print("Set ROBOFLOW_API_KEY or pass --api-key to download from Roboflow.", file=sys.stderr)
            return 2
        with tempfile.TemporaryDirectory(prefix="rf-ppe-") as _:
            source = download_roboflow_dataset(args.api_key, args.download_dir)
            summary = filter_dataset(source, args.output, args.classes)
    else:
        if args.source is None:
            print("Pass --source or use --download with ROBOFLOW_API_KEY.", file=sys.stderr)
            return 2
        summary = filter_dataset(args.source, args.output, args.classes)

    print(json.dumps(summary, indent=2, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
