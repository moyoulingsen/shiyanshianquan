#!/usr/bin/env python3
"""
Download lightweight SH17 PPE detector weights from GitHub releases.

SH17 repository:
    https://github.com/ahmadmughees/SH17dataset

Useful classes include:
    Glasses, Gloves, Face-mask-medical, Helmet, Medical-suit, Safety-suit

These .pt weights are not ESP32-P4 .espdl files yet, but yolo8n/yolo9t are
small enough to use as a practical starting point for testing and conversion.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import requests


BASE_URL = "https://github.com/ahmadmughees/SH17dataset/releases/download/v1"

WEIGHTS = {
    "yolo8n": ("yolo8n.pt", 6_252_377),
    "yolo8s": ("yolo8s.pt", 22_529_625),
    "yolo9t": ("yolo9t.pt", 4_647_738),
    "yolo9s": ("yolo9s.pt", 15_242_042),
    "yolo10n": ("yolo10n.pt", 5_771_812),
    "yolo10s": ("yolo10s.pt", 16_549_100),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Download SH17 YOLO PPE weights.")
    parser.add_argument(
        "--model",
        choices=sorted(WEIGHTS),
        default="yolo8n",
        help="Weight variant to download. yolo8n is the safest first test.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("models/sh17"),
        help="Directory where the .pt file will be saved.",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=120,
        help="HTTP timeout in seconds.",
    )
    return parser.parse_args()


def download(url: str, target: Path, timeout: int) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = target.with_suffix(target.suffix + ".part")
    headers = {}
    existing = temp.stat().st_size if temp.exists() else 0
    if existing:
        headers["Range"] = f"bytes={existing}-"

    with requests.get(url, headers=headers, stream=True, timeout=(15, timeout)) as response:
        if response.status_code == 416:
            temp.rename(target)
            return
        response.raise_for_status()

        mode = "ab" if response.status_code == 206 and existing else "wb"
        with temp.open(mode) as f:
            for chunk in response.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    f.write(chunk)

    temp.rename(target)


def main() -> int:
    args = parse_args()
    filename, expected_size = WEIGHTS[args.model]
    target = args.output_dir / filename
    url = f"{BASE_URL}/{filename}"

    if target.exists() and target.stat().st_size == expected_size:
        print(f"Already downloaded: {target}")
        return 0

    print(f"Downloading {url}", flush=True)
    print(f"Target: {target}", flush=True)
    download(url, target, args.timeout)

    actual_size = target.stat().st_size
    if actual_size != expected_size:
        print(
            f"Downloaded size looks unusual: expected {expected_size}, got {actual_size}",
            file=sys.stderr,
        )
        return 1

    print(f"Saved: {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
