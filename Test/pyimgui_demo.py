"""
Dear PyGui YOLO 实时检测 Demo
pip install dearpygui onnxruntime opencv-python numpy
"""
import dearpygui.dearpygui as dpg
import onnxruntime as ort
import cv2
import numpy as np
from pathlib import Path

# ============================================================
# 配置
# ============================================================
MODEL_PATH = Path(__file__).parent.parent / "models" / "best.onnx"
CLASSES_PATH = Path(__file__).parent.parent / "models" / "检测模型.txt"

if not MODEL_PATH.exists():
    # 回退
    MODEL_PATH = Path(__file__).parent.parent / "models" / "yolo11n.onnx"
    CLASSES_PATH = None

# ============================================================
# YOLO 检测器
# ============================================================
class YOLODetector:
    def __init__(self):
        self.sess = None
        self.input_name = ""
        self.output_name = ""
        self.input_size = (640, 640)
        self.classes = ["defect"]
        self.conf = 0.5
        self.nms = 0.4
        self.last_ms = 0.0

    def load(self, model_path, classes_path=None):
        model_path = Path(model_path)
        if classes_path:
            classes_path = Path(classes_path)
            if classes_path.exists():
                self.classes = [l.strip() for l in classes_path.read_text(encoding="utf-8").splitlines() if l.strip()]

        # 与 C++ 版一致：开启全部优化 + 多线程
        opts = ort.SessionOptions()
        opts.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        opts.intra_op_num_threads = 0  # 自动检测CPU核心
        opts.inter_op_num_threads = 1
        opts.execution_mode = ort.ExecutionMode.ORT_PARALLEL

        self.sess = ort.InferenceSession(str(model_path), opts, providers=["CPUExecutionProvider"])
        inp = self.sess.get_inputs()[0]
        out = self.sess.get_outputs()[0]
        self.input_name = inp.name
        self.output_name = out.name
        shape = inp.shape
        self.input_size = (shape[3], shape[2])  # (W, H)
        print(f"模型: {model_path.name}, 输入: {self.input_size}, 类别: {len(self.classes)}")

    def detect(self, image: np.ndarray) -> list[dict]:
        import time
        t0 = time.perf_counter()

        h, w = image.shape[:2]
        iw, ih = self.input_size
        blob = cv2.dnn.blobFromImage(image, 1/255.0, (iw, ih), swapRB=True, crop=False)
        outputs = self.sess.run([self.output_name], {self.input_name: blob})[0]

        results = self._postprocess(outputs, image.shape[1], image.shape[0])
        self.last_ms = (time.perf_counter() - t0) * 1000
        return results

    def _postprocess(self, output: np.ndarray, img_w: int, img_h: int) -> list[dict]:
        output = np.squeeze(output)
        if output.shape[0] <= 200 and output.shape[1] > output.shape[0]:
            output = output.T

        # 向量化：一次性提取所有框
        boxes_xywh = output[:, :4]
        scores_all = output[:, 4:]
        cls_ids = scores_all.argmax(axis=1)
        scores = scores_all[np.arange(len(scores_all)), cls_ids]

        mask = scores >= self.conf
        if not mask.any():
            return []

        boxes = boxes_xywh[mask]
        scores = scores[mask]
        cls_ids = cls_ids[mask]

        # cxcywh → xywh + 缩放
        scale_x = img_w / self.input_size[0]
        scale_y = img_h / self.input_size[1]
        boxes[:, 0] = (boxes[:, 0] - boxes[:, 2] / 2) * scale_x
        boxes[:, 1] = (boxes[:, 1] - boxes[:, 3] / 2) * scale_y
        boxes[:, 2] *= scale_x
        boxes[:, 3] *= scale_y
        boxes = boxes.astype(np.int32)

        # NMS
        indices = cv2.dnn.NMSBoxes(boxes.tolist(), scores.tolist(), self.conf, self.nms)
        return [
            {"x": int(boxes[i][0]), "y": int(boxes[i][1]), "w": int(boxes[i][2]), "h": int(boxes[i][3]),
             "score": float(scores[i]), "class": self.classes[cls_ids[i]] if cls_ids[i] < len(self.classes) else f"cls{cls_ids[i]}"}
            for i in indices
        ]


# ============================================================
# Dear PyGui App
# ============================================================
class App:
    def __init__(self):
        self.detector = YOLODetector()
        self.video_path = None
        self.video_frame_idx = 0
        self.current_image = None
        self.offset_x = 0.0
        self.video_tag = "video_texture"
        self._cap = None
        self.total_frames = 0

    def run(self):
        dpg.create_context()
        dpg.create_viewport(title="YOLO 实时检测 (Py)", width=1280, height=800)
        self._build_ui()
        dpg.setup_dearpygui()
        dpg.show_viewport()

        # 加载模型
        self.detector.load(MODEL_PATH, CLASSES_PATH)

        # 加载测试视频或图片
        test_video = Path(__file__).parent.parent / "models" / "工业缺陷_多角度测试.mp4"
        if test_video.exists():
            self._load_video(test_video)
        else:
            # 加载单张图片
            test_img = Path(__file__).parent.parent / "assets" / "images" / "test.jpg"
            if test_img.exists():
                self.current_image = cv2.imread(str(test_img))
                self._update_texture()

        while dpg.is_dearpygui_running():
            self._update()
            dpg.render_dearpygui_frame()
        dpg.destroy_context()

    def _build_ui(self):
        # 左侧控制面板
        with dpg.window(label="控制面板", width=300, height=800, no_close=True):
            dpg.add_text("YOLO 检测")
            dpg.add_slider_float(label="置信度", default_value=0.5, min_value=0.1, max_value=1.0,
                                 callback=lambda s, a: setattr(self.detector, "conf", a))
            dpg.add_slider_float(label="滚动补偿(X)", default_value=0, min_value=-100, max_value=100,
                                 callback=lambda s, a: setattr(self, "offset_x", a))
            dpg.add_text("", tag="fps_text")

            dpg.add_separator()
            dpg.add_button(label="打开视频", callback=self._open_video)
            dpg.add_button(label="执行检测", callback=self._single_detect)

            with dpg.group(horizontal=True):
                dpg.add_button(label="上一帧", callback=self._prev_frame)
                dpg.add_button(label="下一帧", callback=self._next_frame)
                dpg.add_text("0 / 0", tag="frame_info")

        # 右侧图像显示（窗口 + 纹理 + 叠加层 只创建一次）
        with dpg.window(label="图像预览", width=950, height=750, no_close=True, tag="img_win"):
            with dpg.texture_registry():
                dpg.add_raw_texture(1920, 1080, np.zeros((1080, 1920, 4), dtype=np.float32),
                                    tag=self.video_tag, format=dpg.mvFormat_Float_rgba)
            dpg.add_image(self.video_tag, tag="img_display")
            dpg.add_drawlist(tag="overlay_draw", width=1920, height=1080)

    def _load_video(self, path: Path):
        self._cap = cv2.VideoCapture(str(path))
        self.total_frames = int(self._cap.get(cv2.CAP_PROP_FRAME_COUNT))
        self.video_path = path
        self.video_frame_idx = 0
        self._read_frame()
        print(f"视频: {self.total_frames}帧")
        dpg.set_value("frame_info", f"0 / {self.total_frames}")

    def _read_frame(self):
        if self._cap is None:
            return
        self._cap.set(cv2.CAP_PROP_POS_FRAMES, self.video_frame_idx)
        ret, frame = self._cap.read()
        if ret:
            if frame.shape[1] > 1920:
                scale = 1920 / frame.shape[1]
                frame = cv2.resize(frame, (1920, int(frame.shape[0] * scale)))
            self.current_image = frame
        else:
            self.video_frame_idx = 0
            self._cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
            ret, frame = self._cap.read()
            if ret:
                self.current_image = frame

    def _open_video(self):
        from tkinter import filedialog
        path = filedialog.askopenfilename(filetypes=[("Video", "*.mp4 *.avi")])
        if path:
            if self._cap is not None:
                self._cap.release()
            self._load_video(Path(path))

    def _prev_frame(self):
        if self._cap is not None:
            self.video_frame_idx = max(0, self.video_frame_idx - 1)
            self._read_frame()
            dpg.set_value("frame_info", f"{self.video_frame_idx} / {self.total_frames}")

    def _next_frame(self):
        if self._cap is not None:
            self.video_frame_idx = min(self.total_frames - 1, self.video_frame_idx + 1)
            self._read_frame()
            dpg.set_value("frame_info", f"{self.video_frame_idx} / {self.total_frames}")

    def _single_detect(self):
        if self.current_image is not None:
            objs = self.detector.detect(self.current_image)
            print(f"检测: {len(objs)} 目标 | {self.detector.last_ms:.1f}ms")
            for o in objs:
                print(f"  {o['class']} {o['score']:.2f} [{o['x']},{o['y']} {o['w']}x{o['h']}]")

    def _update_texture(self):
        if self.current_image is None:
            return
        h, w = self.current_image.shape[:2]
        rgba = cv2.cvtColor(self.current_image, cv2.COLOR_BGR2RGBA).astype(np.float32) / 255.0
        rgba = np.ascontiguousarray(rgba)
        dpg.set_value(self.video_tag, rgba)

    def _draw_overlays(self, objs: list[dict]):
        if not dpg.does_item_exist("overlay_draw"):
            return
        dpg.delete_item("overlay_draw", children_only=True)
        for o in objs:
            x = o["x"] + int(self.offset_x)
            y = o["y"]
            w_box = o["w"]
            h_box = o["h"]
            dpg.draw_rectangle((x, y), (x + w_box, y + h_box), color=(0, 255, 0, 255), thickness=2, parent="overlay_draw")
            dpg.draw_text((x + 3, y - 18), f"{o['class']} {o['score']:.2f}", color=(255, 255, 255, 255), size=14, parent="overlay_draw")

    def _update(self):
        if self._cap is not None and self.current_image is not None:
            self._update_texture()

            # 自动检测
            objs = self.detector.detect(self.current_image)
            self._draw_overlays(objs)
            dpg.set_value("fps_text", f"FPS: {1000/self.detector.last_ms:.1f} ({self.detector.last_ms:.0f}ms) | {len(objs)}个")

            # 前进一帧
            self.video_frame_idx = (self.video_frame_idx + 1) % self.total_frames
            self._read_frame()
            dpg.set_value("frame_info", f"{self.video_frame_idx} / {self.total_frames}")


if __name__ == "__main__":
    App().run()
