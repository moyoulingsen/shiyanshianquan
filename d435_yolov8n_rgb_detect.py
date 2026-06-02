import argparse
import glob
import os
import time
from pathlib import Path

import cv2
import numpy as np
import torch
from ultralytics import YOLO


PROJECT_DIR = Path(__file__).resolve().parent
DEFAULT_BIG_MODEL = PROJECT_DIR / "runs/detect/big_can_yolov8n/weights/best.pt"
DEFAULT_SMALL_MODEL = PROJECT_DIR / "runs/detect/small_can_yolov8n/weights/best.pt"
DEFAULT_SAVE_DIR = PROJECT_DIR / "runs/detect/d435_rgb_yolov8n"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run big_can_yolov8n and small_can_yolov8n on RealSense D435 RGB frames."
    )
    parser.add_argument("--big-model", default=str(DEFAULT_BIG_MODEL), help="Path to big_can YOLOv8n weights")
    parser.add_argument("--small-model", default=str(DEFAULT_SMALL_MODEL), help="Path to small_can YOLOv8n weights")
    parser.add_argument(
        "--source",
        default="auto",
        help="Camera source. Use auto, a device path like /dev/video8, or an integer camera index.",
    )
    parser.add_argument("--width", type=int, default=640, help="RGB stream width")
    parser.add_argument("--height", type=int, default=480, help="RGB stream height")
    parser.add_argument("--fps", type=int, default=30, help="RGB stream FPS")
    parser.add_argument("--conf", type=float, default=0.35, help="Confidence threshold")
    parser.add_argument("--imgsz", type=int, default=640, help="YOLO inference image size")
    parser.add_argument("--device", default="auto", help="YOLO device: auto, 0, cpu, etc.")
    parser.add_argument("--show-size", type=int, default=1100, help="Preview max width")
    parser.add_argument("--save-dir", default=str(DEFAULT_SAVE_DIR), help="Directory for saved annotated frames")
    parser.add_argument("--oneshot", action="store_true", help="Save one annotated frame and exit")
    parser.add_argument("--warmup-frames", type=int, default=10, help="Frames to skip before detecting")
    return parser.parse_args()


def resolve_source(source: str):
    if source != "auto":
        if source.isdigit():
            return int(source)
        return source

    by_id_patterns = [
        "/dev/v4l/by-id/*RealSense*video-index0",
        "/dev/v4l/by-id/*RealSense*video-index2",
    ]
    for pattern in by_id_patterns:
        matches = sorted(glob.glob(pattern))
        if matches:
            return os.path.realpath(matches[0])

    for index in (8, 6, 4, 0):
        path = Path(f"/dev/video{index}")
        if path.exists():
            return str(path)

    return 0


def open_camera(source, width: int, height: int, fps: int):
    cap = cv2.VideoCapture(source, cv2.CAP_V4L2)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open camera source: {source}")

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"YUYV"))
    cap.set(cv2.CAP_PROP_CONVERT_RGB, 1)
    return cap


def fit_preview(image: np.ndarray, max_width: int) -> np.ndarray:
    h, w = image.shape[:2]
    if w <= max_width:
        return image
    scale = max_width / w
    return cv2.resize(image, (int(w * scale), int(h * scale)))


def draw_label(image: np.ndarray, text: str, x: int, y: int, color: tuple[int, int, int]):
    font = cv2.FONT_HERSHEY_SIMPLEX
    font_scale = 0.58
    thickness = 2
    (text_w, text_h), baseline = cv2.getTextSize(text, font, font_scale, thickness)
    y = max(y, text_h + baseline + 6)
    cv2.rectangle(image, (x, y - text_h - baseline - 7), (x + text_w + 8, y + 3), color, -1)
    cv2.putText(image, text, (x + 4, y - baseline - 2), font, font_scale, (0, 0, 0), thickness, cv2.LINE_AA)


def load_detector(model_path: str, label: str, color: tuple[int, int, int]):
    path = Path(model_path).expanduser()
    if not path.exists():
        raise FileNotFoundError(f"{label} model not found: {path}")

    model = YOLO(str(path))
    print(f"Loaded {label}: {path}")
    print(f"  names: {model.names}")
    return {"model": model, "label": label, "color": color}


def detect_and_draw(frame: np.ndarray, detector, conf: float, imgsz: int, device: str) -> int:
    result = detector["model"].predict(frame, conf=conf, imgsz=imgsz, device=device, verbose=False)[0]
    if result.boxes is None or len(result.boxes) == 0:
        return 0

    color = detector["color"]
    label = detector["label"]
    boxes = result.boxes.xyxy.cpu().numpy()
    scores = result.boxes.conf.cpu().numpy()

    for box, score in zip(boxes, scores):
        x1, y1, x2, y2 = [int(round(v)) for v in box]
        cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
        draw_label(frame, f"{label} {score:.2f}", x1, y1, color)

    return len(boxes)


def draw_status(frame: np.ndarray, fps: float, counts: dict[str, int], source):
    text = f"D435 RGB  FPS:{fps:.1f}  big:{counts['big_can']}  small:{counts['small_can']}  src:{source}"
    cv2.rectangle(frame, (8, 8), (min(frame.shape[1] - 8, 660), 42), (20, 20, 20), -1)
    cv2.putText(frame, text, (16, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (245, 245, 245), 2, cv2.LINE_AA)


def save_frame(save_dir: Path, frame: np.ndarray, prefix: str = "d435_rgb") -> Path:
    save_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    path = save_dir / f"{prefix}_{stamp}.jpg"
    index = 2
    while path.exists():
        path = save_dir / f"{prefix}_{stamp}_{index}.jpg"
        index += 1
    cv2.imwrite(str(path), frame)
    return path


def main():
    args = parse_args()
    save_dir = Path(args.save_dir)
    source = resolve_source(args.source)
    device = "0" if args.device == "auto" and torch.cuda.is_available() else args.device
    if device == "auto":
        device = "cpu"

    detectors = [
        load_detector(args.big_model, "big_can", (0, 190, 255)),
        load_detector(args.small_model, "small_can", (80, 220, 80)),
    ]

    cap = open_camera(source, args.width, args.height, args.fps)
    print(f"Opened D435 RGB source: {source}")
    print("Colors: big_can = orange, small_can = green")
    print("Press q or ESC to quit, s to save current annotated frame.")

    last_time = time.monotonic()
    fps = 0.0
    frame_index = 0

    try:
        while True:
            ok, frame = cap.read()
            if not ok or frame is None:
                time.sleep(0.02)
                continue

            frame_index += 1
            if frame_index <= args.warmup_frames:
                continue

            counts = {}
            for detector in detectors:
                counts[detector["label"]] = detect_and_draw(frame, detector, args.conf, args.imgsz, device)

            now = time.monotonic()
            dt = now - last_time
            last_time = now
            if dt > 0:
                fps = 0.9 * fps + 0.1 * (1.0 / dt) if fps else 1.0 / dt

            draw_status(frame, fps, counts, source)

            if args.oneshot:
                saved = save_frame(save_dir, frame)
                print(f"Saved annotated frame: {saved}")
                break

            cv2.imshow("D435 RGB YOLOv8n - big_can + small_can", fit_preview(frame, args.show_size))
            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):
                break
            if key == ord("s"):
                saved = save_frame(save_dir, frame)
                print(f"Saved annotated frame: {saved}")
    finally:
        cap.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
