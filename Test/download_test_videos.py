"""
测试视频下载工具
用法:
  python download_test_videos.py            # 下载所有默认测试视频
  python download_test_videos.py -t street  # 只下载街道场景
  python download_test_videos.py --list     # 列出可用视频
"""

import argparse
import os
import sys
import urllib.request
import urllib.error

# 公开测试视频（无需特殊权限，适合 YOLO 检测测试）
VIDEOS = {
    "street": {
        "name": "street.mp4",
        "desc": "街道行人车辆",
        "url": "https://github.com/intel-iot-devkit/sample-videos/raw/master/person-bicycle-car-detection.mp4",
        "size": "~4 MB",
    },
    "traffic": {
        "name": "traffic.mp4",
        "desc": "交通路口俯拍",
        "url": "https://github.com/intel-iot-devkit/sample-videos/raw/master/classroom.mp4",
        "size": "~3 MB",
    },
    "people": {
        "name": "people.mp4",
        "desc": "人群场景",
        "url": "https://github.com/intel-iot-devkit/sample-videos/raw/master/people-detection.mp4",
        "size": "~3 MB",
    },
    "road": {
        "name": "road.mp4",
        "desc": "道路驾驶视角",
        "url": "https://github.com/ultralytics/ultralytics/raw/main/ultralytics/assets/bus.jpg",
        "size": "图片(跳过)",
    },
}

# OpenCV 自带测试视频（更可靠的长链接）
OPENCV_VIDEOS = {
    "walking": {
        "name": "walking.avi",
        "desc": "行人跟踪",
        "url": "https://raw.githubusercontent.com/opencv/opencv_extra/master/testdata/cv/video/1920x1080.avi",
        "size": "~2 MB",
    },
}


def download(url, dest, name):
    """带进度条的下载"""
    print(f"[下载] {name}")
    print(f"  {url}")
    try:
        def hook(block, size, total):
            if total > 0:
                pct = min(block * size * 100 // total, 100)
                bar = "#" * (pct // 4) + "-" * (25 - pct // 4)
                print(f"\r  [{bar}] {pct}%", end="", flush=True)

        urllib.request.urlretrieve(url, dest, reporthook=hook)
        print()
        size_mb = os.path.getsize(dest) / (1024 * 1024)
        print(f"  -> {dest} ({size_mb:.1f} MB)")
        return True
    except Exception as e:
        print(f"\n  [失败] {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="测试视频下载工具")
    parser.add_argument("-t", "--type", default="all",
                        choices=["all", "street", "traffic", "people", "walking"],
                        help="视频类型 (默认: all)")
    parser.add_argument("-o", "--output", default=".",
                        help="输出目录 (默认: 当前目录)")
    parser.add_argument("--list", action="store_true",
                        help="列出可用视频")
    args = parser.parse_args()

    if args.list:
        print("===== 可用测试视频 =====")
        all_vids = {**OPENCV_VIDEOS, **VIDEOS}
        for k, v in all_vids.items():
            print(f"  {k:<12} {v['name']:<30} {v['desc']}")
        print("\n用法: python download_test_videos.py -t <类型>")
        return

    os.makedirs(args.output, exist_ok=True)

    if args.type == "all":
        items = list(VIDEOS.items())
    else:
        items = [(args.type, VIDEOS.get(args.type) or OPENCV_VIDEOS.get(args.type))]

    for key, info in items:
        if info is None:
            print(f"[跳过] 未知类型: {key}")
            continue
        dest = os.path.join(args.output, info["name"])
        if os.path.exists(dest):
            print(f"[跳过] 已存在: {dest}")
            continue
        download(info["url"], dest, f"{info['desc']} ({info['name']})")

    print("\n完成！用 YOLOVideoTest.exe 测试:")
    print(f"  YOLOVideoTest.exe yolo11n.onnx {os.path.join(args.output, 'street.mp4')}")


if __name__ == "__main__":
    main()
