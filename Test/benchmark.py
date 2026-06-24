"""Compare Python YOLO inference speed with the local demo assets."""

from pathlib import Path
import time

import cv2

ROOT = Path(__file__).resolve().parents[1]
TEST_DIR = ROOT / "Test"

import sys
sys.path.insert(0, str(TEST_DIR))

from pyimgui_demo import YOLODetector

MODEL = ROOT / "models" / "best.onnx"
CLASSES = ROOT / "models" / "classes.txt"
IMG = ROOT / "assets" / "images" / "defect_0000_orig.jpg"


def main() -> int:
    missing = [p for p in (MODEL, CLASSES, IMG) if not p.exists()]
    if missing:
        print("Missing benchmark inputs:")
        for path in missing:
            print(f"  {path}")
        return 1

    detector = YOLODetector()
    detector.load(str(MODEL), str(CLASSES))
    img = cv2.imread(str(IMG))
    if img is None:
        print(f"Failed to read image: {IMG}")
        return 1

    for _ in range(10):
        detector.detect(img)

    runs = 50
    start = time.perf_counter()
    for _ in range(runs):
        detector.detect(img)
    avg_ms = (time.perf_counter() - start) / runs * 1000

    print(f"Python YOLO average: {avg_ms:.1f} ms over {runs} runs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
