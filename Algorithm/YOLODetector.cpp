#define NOMINMAX
#include "YOLODetector.h"
#include "../Log/LogSystem.h"

#include <fstream>
#include <algorithm>

namespace YOLODetector
{

static cv::dnn::Net        s_Net;
static std::vector<std::string> s_Classes;
static bool                s_Loaded = false;
static int                 s_InputW = 640;
static int                 s_InputH = 640;

// =====================================================
// 加载模型
// =====================================================
bool LoadModel(const std::string& onnxPath, const std::string& classesPath)
{
    Unload();

    // 加载类别名称
    std::ifstream f(classesPath);
    if (!f.is_open())
    {
        LogSystem::Add(LOG_ERROR, "YOLO: 无法打开类别文件: %s", classesPath.c_str());
        return false;
    }
    s_Classes.clear();
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty())
            s_Classes.push_back(line);
    }
    f.close();

    // 加载 ONNX 模型
    try
    {
        s_Net = cv::dnn::readNetFromONNX(onnxPath);

        // 尝试使用 OpenCL 后端加速（如果有的话）
        // s_Net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        // s_Net.setPreferableTarget(cv::dnn::DNN_TARGET_OPENCL);
        // 默认 CPU 后端
    }
    catch (const cv::Exception& e)
    {
        LogSystem::Add(LOG_ERROR, "YOLO: 加载模型失败: %s", e.what());
        s_Classes.clear();
        return false;
    }

    s_Loaded = true;
    LogSystem::Add(LOG_INFO, "YOLO 模型已加载: %zu 个类别, 输入 %dx%d",
        s_Classes.size(), s_InputW, s_InputH);
    return true;
}

bool IsLoaded() { return s_Loaded; }

// =====================================================
// 预处理：resize + 归一化 + blob
// =====================================================
static cv::Mat Preprocess(const cv::Mat& image, cv::Rect roi)
{
    cv::Mat crop;
    if (roi.width > 0 && roi.height > 0)
        crop = image(roi).clone();
    else
        crop = image.clone();

    // YOLOv8 输入: (1, 3, 640, 640), RGB, 归一化到 [0,1]
    cv::Mat blob = cv::dnn::blobFromImage(crop, 1.0 / 255.0,
        cv::Size(s_InputW, s_InputH), cv::Scalar(), true, false);
    return blob;
}

// =====================================================
// 后处理：解析 YOLOv8 输出
// YOLOv8 输出 shape: (1, 84, 8400) = (1, 4+80, 8400)
// 每列: [cx, cy, w, h, class_0_conf, ..., class_79_conf]
// =====================================================
static std::vector<DetectedObject> Postprocess(const cv::Mat& output,
    float confThreshold, float nmsThreshold, cv::Rect roi)
{
    std::vector<DetectedObject> detections;
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> classIds;

    // output: [1, 84, 8400]
    int numClasses = (int)s_Classes.size();
    int numDetections = output.size[2];  // 8400
    int dimsPerDetection = output.size[1]; // 84 = 4 + numClasses

    float scaleX = (float)s_InputW / (roi.width  > 0 ? roi.width  : s_InputW);
    float scaleY = (float)s_InputH / (roi.height > 0 ? roi.height : s_InputH);

    const float* data = (const float*)output.data;

    for (int i = 0; i < numDetections; i++)
    {
        const float* row = data + i * dimsPerDetection;

        // 找最大置信度的类别
        float maxConf = 0.0f;
        int bestClass = -1;
        for (int c = 0; c < numClasses; c++)
        {
            float conf = row[4 + c];
            if (conf > maxConf)
            {
                maxConf = conf;
                bestClass = c;
            }
        }

        if (maxConf < confThreshold) continue;

        // 解析边界框 (cx, cy, w, h 归一化到 0~1)
        float cx = row[0];
        float cy = row[1];
        float w  = row[2];
        float h  = row[3];

        // 转换到原始图像坐标
        int left   = (int)((cx - w * 0.5f) / scaleX + roi.x);
        int top    = (int)((cy - h * 0.5f) / scaleY + roi.y);
        int width  = (int)(w / scaleX);
        int height = (int)(h / scaleY);

        boxes.push_back(cv::Rect(left, top, width, height));
        confidences.push_back(maxConf);
        classIds.push_back(bestClass);
    }

    // NMS
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, confThreshold, nmsThreshold, indices);

    for (int idx : indices)
    {
        DetectedObject obj;
        obj.box        = boxes[idx];
        obj.classId    = classIds[idx];
        obj.confidence = confidences[idx];
        obj.className  = (classIds[idx] >= 0 && classIds[idx] < (int)s_Classes.size())
                         ? s_Classes[classIds[idx]] : "unknown";
        detections.push_back(obj);
    }

    return detections;
}

// =====================================================
// 执行检测
// =====================================================
std::vector<DetectedObject> Detect(const cv::Mat& image,
    float confThreshold, float nmsThreshold, cv::Rect roi)
{
    if (!s_Loaded || image.empty()) return {};

    // 限定 ROI 在图像范围内
    if (roi.width <= 0 || roi.height <= 0)
        roi = cv::Rect(0, 0, image.cols, image.rows);
    else
        roi &= cv::Rect(0, 0, image.cols, image.rows);

    cv::Mat blob = Preprocess(image, roi);
    s_Net.setInput(blob);

    // 前向推理
    std::vector<cv::Mat> outputs;
    s_Net.forward(outputs, s_Net.getUnconnectedOutLayersNames());

    if (outputs.empty()) return {};

    // YOLOv8 只有一个输出层，shape: (1, 84, 8400)
    // 需要 transpose 到 (1, 8400, 84) 格式便于解析
    cv::Mat output = outputs[0];
    if (output.size[1] == 8400 && output.size[2] == (int)(4 + s_Classes.size()))
    {
        // 已经是 (1, 84, 8400)，保持不变，Postprocess 会按列处理
    }

    return Postprocess(output, confThreshold, nmsThreshold, roi);
}

// =====================================================
// 绘制检测框
// =====================================================
void DrawDetections(cv::Mat& image,
    const std::vector<DetectedObject>& objects, bool drawLabel)
{
    static const cv::Scalar s_Colors[] = {
        {255,   0,   0}, {  0, 255,   0}, {  0,   0, 255},
        {255, 255,   0}, {255,   0, 255}, {  0, 255, 255},
        {128,   0, 128}, {128, 128,   0}, {  0, 128, 128},
        {255, 128,   0}, {128,   0, 255}, {255,   0, 128},
    };
    constexpr int nColors = sizeof(s_Colors) / sizeof(s_Colors[0]);

    for (const auto& obj : objects)
    {
        cv::Scalar color = s_Colors[obj.classId % nColors];

        // 边框
        int thickness = (int)((std::max)(1.0f, 2.0f * (std::min)(image.cols, image.rows) / 640.0f));
        cv::rectangle(image, obj.box, color, thickness);

        // 标签
        if (drawLabel)
        {
            char label[128];
            snprintf(label, sizeof(label), "%s %.0f%%",
                obj.className.c_str(), obj.confidence * 100.0f);

            int baseline = 0;
            cv::Size textSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                0.5, 1, &baseline);

            cv::Point labelPos(obj.box.x, obj.box.y - 5);
            if (labelPos.y < textSize.height + 5)
                labelPos.y = obj.box.y + textSize.height + 5;

            cv::rectangle(image,
                cv::Rect(labelPos.x, labelPos.y - textSize.height,
                    textSize.width + 4, textSize.height + 4),
                color, cv::FILLED);
            cv::putText(image, label,
                cv::Point(labelPos.x + 2, labelPos.y),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255),
                1, cv::LINE_AA);
        }
    }
}

// =====================================================
// 释放
// =====================================================
void Unload()
{
    s_Net = cv::dnn::Net(); // 释放网络
    s_Classes.clear();
    s_Loaded = false;
}

} // namespace YOLODetector
