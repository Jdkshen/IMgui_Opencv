"""Generate rotated/shifted test images from a local video asset."""

from pathlib import Path
import random

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
VIDEO_PATH = ROOT / "models" / "industrial_defect_dataset.mp4"
OUT_DIR = ROOT / "assets" / "images"


def main() -> int:
    if not VIDEO_PATH.exists():
        print(f"Missing input video: {VIDEO_PATH}")
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    random.seed(42)
    np.random.seed(42)

    cap = cv2.VideoCapture(str(VIDEO_PATH))
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    print(f"video: {total_frames} frames, {fps:.1f} fps")

    if total_frames <= 0:
        print("No frames available")
        return 1

    sample_interval = max(1, total_frames // 12)
    frame_indices = list(range(0, total_frames, sample_interval))

    count = 0
    for frame_index in frame_indices:
        cap.set(cv2.CAP_PROP_POS_FRAMES, frame_index)
        ok, frame = cap.read()
        if not ok or frame is None:
            continue

        height, width = frame.shape[:2]
        for variant in range(3):
            img = frame.copy()
            if variant == 0:
                suffix = "orig"
            elif variant == 1:
                angle = random.uniform(-15, 15)
                matrix = cv2.getRotationMatrix2D((width / 2, height / 2), angle, 1.0)
                img = cv2.warpAffine(img, matrix, (width, height), borderMode=cv2.BORDER_REPLICATE)
                suffix = f"rot{angle:+.0f}"
            else:
                angle = random.uniform(-12, 12)
                scale = random.uniform(0.92, 1.05)
                dx = random.randint(-25, 25)
                dy = random.randint(-25, 25)
                matrix = cv2.getRotationMatrix2D((width / 2, height / 2), angle, scale)
                matrix[0, 2] += dx
                matrix[1, 2] += dy
                img = cv2.warpAffine(img, matrix, (width, height), borderMode=cv2.BORDER_REPLICATE)
                suffix = f"rot{angle:+.0f}_off{dx:+d}_{dy:+d}"

            out_path = OUT_DIR / f"defect_{frame_index:04d}_{suffix}.jpg"
            cv2.imwrite(str(out_path), img)
            count += 1

    cap.release()
    print(f"generated {count} test images in {OUT_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
