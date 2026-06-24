"""
YOLO11 模型下载工具
用法:
  python download_yolo11.py              # 下载 yolo11n.onnx (默认)
  python download_yolo11.py -m s         # 下载 yolo11s.onnx
  python download_yolo11.py -m all       # 下载全部 n/s/m/l/x
  python download_yolo11.py -m n -o ./models  # 指定输出目录
  python download_yolo11.py --list       # 列出可用模型
"""

import argparse
import sys
import os
from pathlib import Path

MODELS = {
    "n":  {"name": "yolo11n", "size": "5.3 MB",  "mAP": "39.5", "params": "2.6M"},
    "s":  {"name": "yolo11s", "size": "18.4 MB", "mAP": "47.0", "params": "9.4M"},
    "m":  {"name": "yolo11m", "size": "38.9 MB", "mAP": "51.5", "params": "20.1M"},
    "l":  {"name": "yolo11l", "size": "48.9 MB", "mAP": "53.4", "params": "25.3M"},
    "x":  {"name": "yolo11x", "size": "54.5 MB", "mAP": "54.7", "params": "56.9M"},
}

VARIANT_DESC = {"n": "Nano", "s": "Small", "m": "Medium", "l": "Large", "x": "XLarge"}


def print_list():
    """列出所有可用模型"""
    print("=" * 65)
    print(f"{'变体':<6} {'模型名':<12} {'大小':<10} {'mAP':<8} {'参数量':<10}")
    print("-" * 65)
    for k, v in MODELS.items():
        print(f"{k.upper():<6} {v['name']:<12} {v['size']:<10} {v['mAP']:<8} {v['params']:<10}")
    print("=" * 65)
    print("\n用法: python download_yolo11.py -m <变体>  (例如: -m n / -m s / -m all)")


def export_onnx(model_name: str, output_dir: str, imgsz: int = 640) -> str:
    """使用 ultralytics 导出 ONNX"""
    try:
        from ultralytics import YOLO
    except ImportError:
        print("[ERROR] 需要安装 ultralytics: pip install ultralytics")
        return ""

    print(f"[下载] {model_name}.pt ...")
    model = YOLO(f"{model_name}.pt")  # 自动下载

    onnx_path = os.path.join(output_dir, f"{model_name}.onnx")
    print(f"[导出] -> {onnx_path} (imgsz={imgsz})")

    success = model.export(format="onnx", imgsz=imgsz, simplify=True, opset=12)

    if isinstance(success, str):
        # 检查实际输出路径
        expected = f"{model_name}.onnx"
        if os.path.exists(expected):
            os.makedirs(output_dir, exist_ok=True)
            target = os.path.join(output_dir, expected)
            if os.path.abspath(expected) != os.path.abspath(target):
                import shutil
                shutil.move(expected, target)
            onnx_path = target
        print(f"[完成] {onnx_path}")
        return onnx_path
    else:
        print("[失败] 导出返回异常")
        return ""


def main():
    parser = argparse.ArgumentParser(description="YOLO11 模型下载 & ONNX 导出工具")
    parser.add_argument("-m", "--model",  default="n",
                        choices=["n", "s", "m", "l", "x", "all"],
                        help="模型变体 (默认: n)")
    parser.add_argument("-o", "--output", default=".",
                        help="输出目录 (默认: 当前目录)")
    parser.add_argument("--imgsz", default=640, type=int,
                        help="导出图像大小 (默认: 640)")
    parser.add_argument("--list", action="store_true",
                        help="列出可用模型")
    args = parser.parse_args()

    if args.list:
        print_list()
        return

    os.makedirs(args.output, exist_ok=True)
    output_dir = os.path.abspath(args.output)

    variants = list(MODELS.keys()) if args.model == "all" else [args.model]

    print(f"[输出目录] {output_dir}")
    print(f"[图像尺寸] {args.imgsz}x{args.imgsz}")
    print()

    results = []
    for v in variants:
        model_name = MODELS[v]["name"]
        print(f"[{VARIANT_DESC.get(v, v)}] {model_name} — {MODELS[v]['size']}")
        path = export_onnx(model_name, output_dir, args.imgsz)
        if path:
            size_mb = os.path.getsize(path) / (1024 * 1024)
            results.append((model_name, path, size_mb))
            print(f"  -> {path} ({size_mb:.1f} MB)")
        print()

    print("=" * 50)
    print("下载完成!")
    if results:
        print(f"\n导出的模型 ({len(results)} 个):")
        for name, path, size in results:
            print(f"  {name}.onnx  ({size:.1f} MB)  -> {path}")
        print(f"\n在你的工具中加载: 选择 {results[0][0]}.onnx 即可")

        # 自动生成 COCO 类别文件
        try:
            import yaml
            import ultralytics
            cfg_dir = os.path.join(os.path.dirname(ultralytics.__file__), "cfg", "datasets")
            coco_yaml = os.path.join(cfg_dir, "coco.yaml")
            if os.path.exists(coco_yaml):
                with open(coco_yaml, encoding="utf-8") as f:
                    data = yaml.safe_load(f)
                names = data.get("names", {})
                classes_path = os.path.join(output_dir, "coco_classes.txt")
                with open(classes_path, "w", encoding="utf-8") as f:
                    for i in range(len(names)):
                        f.write(names[i] + "\n")
                print(f"\n[类别文件] {classes_path} ({len(names)} 个类别)")
        except Exception as e:
            pass  # 非必须


if __name__ == "__main__":
    main()
