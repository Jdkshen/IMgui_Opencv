"""
导出 FP16 半精度 ONNX 模型（精度损失极小, 推理速度 ~2x）
用法: py -3.12 export_fp16.py
"""
import onnx
from onnxconverter_common import float16
import os

MODEL_DIR = os.path.join(os.path.dirname(__file__), "..", "models")

models_to_convert = [
    "best.onnx",           # 320×320 缺陷检测
    "best_160.onnx",       # 160×160 (备选)
    "yolo11n.onnx",        # YOLO11 nano
]

for name in models_to_convert:
    src = os.path.join(MODEL_DIR, name)
    if not os.path.exists(src):
        print(f"[跳过] 不存在: {src}")
        continue

    dst = os.path.join(MODEL_DIR, name.replace(".onnx", "_fp16.onnx"))
    print(f"[转换] {name} → {os.path.basename(dst)}")

    model = onnx.load(src)
    model_fp16 = float16.convert_float_to_float16(model)

    onnx.save(model_fp16, dst)
    size_mb = os.path.getsize(dst) / (1024 * 1024)
    print(f"  ✓ 完成, 大小: {size_mb:.1f} MB")
    print(f"  用法: 在工具里选择 {os.path.basename(dst)} 即可, 无需改代码")

print("\n全部完成! 把 *_fp16.onnx 放到 models/ 目录即可在工具中使用")
