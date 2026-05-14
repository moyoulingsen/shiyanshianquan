#!/usr/bin/env python3
"""
Run the Hexmon YOLO PPE detector on a still image.

Model:
    https://huggingface.co/Hexmon/vyra-yolo-ppe-detection

Example:
    python tools/ppe_detect_hexmon.py image.jpg --save-annotated out.jpg
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from huggingface_hub import hf_hub_download
from ultralytics import YOLO


DEFAULT_MODEL = "hf://Hexmon/vyra-yolo-ppe-detection/best.pt"

# The Hexmon model uses names such as "Gloves", "Goggles",
# "NO-Gloves", "NO-Goggles", and "Person".
# SH17 uses "Glasses" and "Gloves", so "glasses" is treated as eye PPE.
POSITIVE_GLOVES = {"gloves", "glove"}
NEGATIVE_GLOVES = {"no-gloves", "no_gloves", "no glove", "no gloves"}
POSITIVE_GOGGLES = {"goggles", "goggle", "glasses", "safety glasses", "safety_glasses"}
NEGATIVE_GOGGLES = {"no-goggles", "no_goggles", "no goggle", "no goggles"}
PERSON_CLASSES = {"person"}


def normalize_class_name(name: str) -> str:
    return name.strip().lower()


def box_to_record(result: Any, box: Any) -> dict[str, Any]:
    cls_id = int(box.cls[0])
    class_name = result.names[cls_id]
    xyxy = box.xyxy[0].tolist()
    return {
        "class_id": cls_id,
        "class_name": class_name,
        "confidence": round(float(box.conf[0]), 4),
        "box_xyxy": [round(float(v), 2) for v in xyxy],
    }


def summarize_detections(detections: list[dict[str, Any]]) -> dict[str, Any]:
    has_person = False
    has_gloves = False
    has_no_gloves = False
    has_goggles = False
    has_no_goggles = False

    best: dict[str, dict[str, Any] | None] = {
        "person": None,
        "gloves": None,
        "no_gloves": None,
        "goggles": None,
        "no_goggles": None,
    }

    def remember(key: str, item: dict[str, Any]) -> None:
        current = best[key]
        if current is None or item["confidence"] > current["confidence"]:
            best[key] = item

    for item in detections:
        name = normalize_class_name(item["class_name"])
        if name in PERSON_CLASSES:
            has_person = True
            remember("person", item)
        elif name in POSITIVE_GLOVES:
            has_gloves = True
            remember("gloves", item)
        elif name in NEGATIVE_GLOVES:
            has_no_gloves = True
            remember("no_gloves", item)
        elif name in POSITIVE_GOGGLES:
            has_goggles = True
            remember("goggles", item)
        elif name in NEGATIVE_GOGGLES:
            has_no_goggles = True
            remember("no_goggles", item)

    return {
        "person_detected": has_person,
        # Conservative rule: if the model sees both positive and negative
        # evidence, prefer the negative class so access control fails closed.
        "wearing_gloves": has_gloves and not has_no_gloves,
        "wearing_goggles": has_goggles and not has_no_goggles,
        "raw_flags": {
            "has_gloves": has_gloves,
            "has_no_gloves": has_no_gloves,
            "has_goggles": has_goggles,
            "has_no_goggles": has_no_goggles,
        },
        "best_detection": best,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Detect gloves and goggles in one image with the Hexmon YOLO PPE model."
    )
    parser.add_argument("image", type=Path, nargs="?", help="Path to the input image.")
    parser.add_argument(
        "--model",
        default=DEFAULT_MODEL,
        help=f"YOLO model path or Hugging Face path. Default: {DEFAULT_MODEL}",
    )
    parser.add_argument(
        "--conf",
        type=float,
        default=0.35,
        help="Confidence threshold. Raise it to reduce false positives.",
    )
    parser.add_argument(
        "--imgsz",
        type=int,
        default=640,
        help="Inference image size. 640 is a common YOLO default.",
    )
    parser.add_argument(
        "--device",
        default=None,
        help="Inference device, for example 'cpu', '0', or 'cuda:0'. Default lets Ultralytics choose.",
    )
    parser.add_argument(
        "--json-out",
        type=Path,
        default=None,
        help="Optional path to save JSON results.",
    )
    parser.add_argument(
        "--save-annotated",
        type=Path,
        default=None,
        help="Optional path to save the image with detection boxes drawn on it.",
    )
    parser.add_argument(
        "--show-classes",
        action="store_true",
        help="Print all class names baked into the loaded model and exit.",
    )
    return parser.parse_args()


def resolve_model_path(model: str) -> str:
    if not model.startswith("hf://"):
        return model

    repo_and_file = model[len("hf://") :]
    repo_id, filename = repo_and_file.rsplit("/", 1)
    return hf_hub_download(repo_id=repo_id, filename=filename)


def main() -> int:
    args = parse_args()

    if args.image is None and not args.show_classes:
        print("Image path is required unless --show-classes is used.", file=sys.stderr)
        return 2

    if args.image is not None and not args.image.exists():
        print(f"Image not found: {args.image}", file=sys.stderr)
        return 2

    model = YOLO(resolve_model_path(args.model))

    if args.show_classes:
        print(json.dumps(model.names, ensure_ascii=False, indent=2))
        return 0

    results = model.predict(
        source=str(args.image),
        conf=args.conf,
        imgsz=args.imgsz,
        device=args.device,
        verbose=False,
    )

    if not results:
        print("No result returned by model.", file=sys.stderr)
        return 1

    result = results[0]
    detections = [box_to_record(result, box) for box in result.boxes]
    summary = summarize_detections(detections)
    output = {
        "image": str(args.image),
        "model": args.model,
        "confidence_threshold": args.conf,
        "image_size": args.imgsz,
        "summary": summary,
        "detections": detections,
    }

    print(json.dumps(output, ensure_ascii=False, indent=2))

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n")

    if args.save_annotated is not None:
        import cv2

        args.save_annotated.parent.mkdir(parents=True, exist_ok=True)
        annotated = result.plot()
        cv2.imwrite(str(args.save_annotated), annotated)
        print(f"Annotated image saved to: {args.save_annotated}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
