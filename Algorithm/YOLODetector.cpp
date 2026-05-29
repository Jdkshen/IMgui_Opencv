#define NOMINMAX
#define ORT_API_MANUAL_INIT
#include "YOLODetector.h"
#include "../Log/LogSystem.h"

#include <onnxruntime/onnxruntime_cxx_api.h>
#include <fstream>
#include <algorithm>
#include <windows.h>

namespace YOLODetector
{

// ---- ONNX Runtime 环境（全局单例）----
static HMODULE                 s_OrtDll = nullptr;
static Ort::Env*              s_Env = nullptr;
static Ort::Session*          s_Session = nullptr;
static Ort::AllocatorWithDefaultOptions* s_Allocator = nullptr;
static Ort::MemoryInfo*       s_MemInfo = nullptr;  // 全部延迟初始化

static std::vector<std::string> s_Classes;
static std::string            s_InputName;
static std::string            s_OutputName;
static bool                   s_Loaded = false;
static int                    s_InputW = 640;
static int                    s_InputH = 640;

// =====================================================
// 加载模型
// =====================================================
bool LoadModel(const std::string& onnxPath, const std::string& classesPath)
{
    Unload();

    // 加载类别名称（未提供或打不开则使用 COCO 默认 80 类）
    s_Classes.clear();
    if (!classesPath.empty())
    {
        std::ifstream f(classesPath);
        if (f.is_open())
        {
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty())
                    s_Classes.push_back(line);
            }
            f.close();
        }
        else
        {
            LogSystem::Add(LOG_WARN, "YOLO: 无法打开类别文件，使用 COCO 默认类别");
        }
    }
    if (s_Classes.empty())
    {
        s_Classes = {
            "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
            "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
            "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe",
            "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard",
            "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
            "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl",
            "banana", "apple", "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza",
            "donut", "cake", "chair", "couch", "potted plant", "bed", "dining table", "toilet",
            "tv", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven",
            "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors",
            "teddy bear", "hair drier", "toothbrush"
        };
        LogSystem::Add(LOG_INFO, "YOLO: 使用 COCO 默认 %zu 个类别", s_Classes.size());
    }

    // 检查文件是否存在
    {
        std::ifstream testFile(onnxPath, std::ios::binary | std::ios::ate);
        if (!testFile.is_open())
        {
            LogSystem::Add(LOG_ERROR, "YOLO: 模型文件不存在: %s", onnxPath.c_str());
            s_Classes.clear();
            return false;
        }
        auto fileSize = testFile.tellg();
        testFile.close();
        if (fileSize < 1024)
        {
            LogSystem::Add(LOG_ERROR, "YOLO: 模型文件太小 (%lld bytes)，可能损坏", (long long)fileSize);
            s_Classes.clear();
            return false;
        }
        LogSystem::Add(LOG_INFO, "YOLO: 模型文件大小 %.1f MB", fileSize / 1048576.0);
    }

    // 加载 ONNX 模型 — 使用 ONNX Runtime
    try
    {
        // 初始化 ORT 环境（仅首次）——手动加载 DLL + 获取 API
        if (!s_Env)
        {
            // 1) 加载 DLL
            s_OrtDll = LoadLibraryW(L"onnxruntime.dll");
            if (!s_OrtDll)
            {
                DWORD err = GetLastError();
                LogSystem::Add(LOG_ERROR, "YOLO(ORT): 无法加载 onnxruntime.dll (err=%lu)", err);
                s_Classes.clear();
                return false;
            }

            // 2) 获取 OrtGetApiBase 函数
            typedef const OrtApiBase* (ORT_API_CALL* PFN_OrtGetApiBase)(void);
            auto pfnGetApiBase = (PFN_OrtGetApiBase)GetProcAddress(s_OrtDll, "OrtGetApiBase");
            if (!pfnGetApiBase)
            {
                LogSystem::Add(LOG_ERROR, "YOLO(ORT): 找不到 OrtGetApiBase 导出函数");
                FreeLibrary(s_OrtDll); s_OrtDll = nullptr;
                s_Classes.clear();
                return false;
            }

            // 3) 初始化 C++ API
            const OrtApi* api = pfnGetApiBase()->GetApi(ORT_API_VERSION);
            if (!api)
            {
                LogSystem::Add(LOG_ERROR, "YOLO(ORT): GetApi 返回 nullptr");
                FreeLibrary(s_OrtDll); s_OrtDll = nullptr;
                s_Classes.clear();
                return false;
            }
            Ort::InitApi(api);

            // 4) 创建 Env、Allocator 和 MemoryInfo
            s_Env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "YOLODetector");
            s_Allocator = new Ort::AllocatorWithDefaultOptions();
            s_MemInfo = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

            LogSystem::Add(LOG_INFO, "YOLO(ORT): 运行时初始化成功");
        }

        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        opts.SetIntraOpNumThreads(4);

        std::wstring wPath(onnxPath.begin(), onnxPath.end());
        s_Session = new Ort::Session(*s_Env, wPath.c_str(), opts);

        // 获取输入/输出名称
        auto inName = s_Session->GetInputNameAllocated(0, *s_Allocator);
        s_InputName = inName.get();
        auto outName = s_Session->GetOutputNameAllocated(0, *s_Allocator);
        s_OutputName = outName.get();

        LogSystem::Add(LOG_INFO, "YOLO(ORT): 输入=%s, 输出=%s", s_InputName.c_str(), s_OutputName.c_str());
    }
    catch (const Ort::Exception& e)
    {
        LogSystem::Add(LOG_ERROR, "YOLO(ORT): 加载失败 - %s (code=%d)", e.what(), e.GetOrtErrorCode());
        delete s_Session; s_Session = nullptr;
        s_Classes.clear();
        return false;
    }
    catch (const std::exception& e)
    {
        LogSystem::Add(LOG_ERROR, "YOLO(ORT): 加载失败(std) - %s", e.what());
        delete s_Session; s_Session = nullptr;
        s_Classes.clear();
        return false;
    }
    catch (...)
    {
        LogSystem::Add(LOG_ERROR, "YOLO(ORT): 加载时发生未知异常");
        delete s_Session; s_Session = nullptr;
        s_Classes.clear();
        return false;
    }

    s_Loaded = true;
    LogSystem::Add(LOG_INFO, "YOLO(ORT) 模型已加载: %zu 个类别, 输入 %dx%d",
        s_Classes.size(), s_InputW, s_InputH);
    return true;
}

bool IsLoaded() { return s_Loaded && s_Session != nullptr; }

// =====================================================
// 预处理：resize + 归一化 + blob (保留 OpenCV 做图像处理)
// =====================================================
static cv::Mat Preprocess(const cv::Mat& image, cv::Rect roi)
{
    // 严格守卫：空图直接返回
    if (image.empty() || image.data == nullptr)
        return {};

    cv::Mat crop;
    if (roi.width > 0 && roi.height > 0)
        crop = image(roi).clone();
    else
        crop = image.clone();

    if (crop.empty())
        return {};

    // YOLO 输入: (1, 3, H, W), RGB, 归一化到 [0,1]
    cv::Mat blob = cv::dnn::blobFromImage(crop, 1.0 / 255.0,
        cv::Size(s_InputW, s_InputH), cv::Scalar(), true, false);
    return blob;
}

// =====================================================
// 后处理：解析 YOLO 输出（从 float* 指针）
// =====================================================
static std::vector<DetectedObject> Postprocess(
    const float* data, const std::vector<int64_t>& shape,
    float confThreshold, float nmsThreshold, cv::Rect roi)
{
    std::vector<DetectedObject> detections;
    if (!data) return {};

    // 确定 C, N：shape 为 [1, C, N] 或 [C, N]
    int C, N;
    if (shape.size() == 3)      { C = (int)shape[1]; N = (int)shape[2]; }
    else if (shape.size() == 2) { C = (int)shape[0]; N = (int)shape[1]; }
    else { LogSystem::Add(LOG_ERROR, "YOLO: 输出维度=%zu, 需2或3维", shape.size()); return {}; }

    if (N <= 0 || N > 100000 || C < 5)
    {
        LogSystem::Add(LOG_ERROR, "YOLO: 输出形状异常 [C=%d N=%d]", C, N);
        return {};
    }

    int numClasses = (int)s_Classes.size();
    int modelClasses = C - 4;
    if (modelClasses <= 0) return {};
    if (numClasses > modelClasses) numClasses = modelClasses;

    float scaleX = (float)(roi.width  > 0 ? roi.width  : s_InputW) / s_InputW;
    float scaleY = (float)(roi.height > 0 ? roi.height : s_InputH) / s_InputH;

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> classIds;

    for (int i = 0; i < N; i++)
    {
        float cx = data[0 * N + i];
        float cy = data[1 * N + i];
        float w  = data[2 * N + i];
        float h  = data[3 * N + i];

        float maxConf = 0.0f;
        int bestClass = -1;
        for (int c = 0; c < numClasses; c++)
        {
            float conf = data[(4 + c) * N + i];
            if (conf > maxConf) { maxConf = conf; bestClass = c; }
        }

        if (maxConf < confThreshold) continue;

        int left   = (int)((cx - w * 0.5f) * scaleX + roi.x);
        int top    = (int)((cy - h * 0.5f) * scaleY + roi.y);
        int width  = (int)(w * scaleX);
        int height = (int)(h * scaleY);

        boxes.push_back(cv::Rect(left, top, width, height));
        confidences.push_back(maxConf);
        classIds.push_back(bestClass);
    }

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
    if (!s_Loaded || !s_Session || image.empty()) return {};

    try
    {
        if (roi.width <= 0 || roi.height <= 0)
            roi = cv::Rect(0, 0, image.cols, image.rows);
        else
            roi &= cv::Rect(0, 0, image.cols, image.rows);

        if (roi.width <= 0 || roi.height <= 0)
        {
            LogSystem::Add(LOG_ERROR, "YOLO: 无效的 ROI 区域");
            return {};
        }

        // 预处理
        cv::Mat blob = Preprocess(image, roi);
        if (blob.empty())
        {
            LogSystem::Add(LOG_ERROR, "YOLO: 预处理生成空 blob");
            return {};
        }

        // ONNX Runtime 推理
        std::vector<int64_t> inputShape = {1, 3, s_InputH, s_InputW};
        size_t inputSize = 1 * 3 * s_InputH * s_InputW;

        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            *s_MemInfo, (float*)blob.data, inputSize,
            inputShape.data(), inputShape.size());

        const char* inNames[]  = { s_InputName.c_str() };
        const char* outNames[] = { s_OutputName.c_str() };

        auto outputs = s_Session->Run(Ort::RunOptions{ nullptr },
            inNames, &inputTensor, 1, outNames, 1);

        if (outputs.empty())
        {
            LogSystem::Add(LOG_WARN, "YOLO: 推理输出为空");
            return {};
        }

        // 解析输出形状和数据
        auto& outTensor = outputs[0];
        auto outShape = outTensor.GetTensorTypeAndShapeInfo().GetShape();
        const float* outData = outTensor.GetTensorData<float>();

        return Postprocess(outData, outShape, confThreshold, nmsThreshold, roi);
    }
    catch (const Ort::Exception& e)
    {
        LogSystem::Add(LOG_ERROR, "YOLO(ORT) 推理异常: %s (code=%d)", e.what(), e.GetOrtErrorCode());
        return {};
    }
    catch (const std::exception& e)
    {
        LogSystem::Add(LOG_ERROR, "YOLO(ORT) 异常(std): %s", e.what());
        return {};
    }
    catch (...)
    {
        LogSystem::Add(LOG_ERROR, "YOLO(ORT): 推理时发生未知异常");
        return {};
    }
}

// =====================================================
// 绘制检测框
// =====================================================
void DrawDetections(cv::Mat& image,
    const std::vector<DetectedObject>& objects, bool drawLabel)
{
    // 严格守卫：空图或无目标直接返回
    if (image.empty() || objects.empty())
        return;

    static const cv::Scalar s_Colors[] = {
        {  0, 255,   0},   // 亮绿
        {  0, 165, 255},   // 橙
        {255,   0, 255},   // 品红
        {  0, 255, 255},   // 青
        {255, 255,   0},   // 天蓝
        {128,   0, 255},   // 橙红
        {255,   0, 128},   // 紫
        { 50, 205,  50},   // 酸橙绿
        {255, 105, 180},   // 粉
        { 30, 144, 255},   // 道奇蓝
        {255, 215,   0},   // 金
        {138,  43, 226},   // 蓝紫
    };
    constexpr int nColors = sizeof(s_Colors) / sizeof(s_Colors[0]);

    for (const auto& obj : objects)
    {
        cv::Scalar color = s_Colors[obj.classId % nColors];

        // 边框（加粗 ×1.5）
        int thickness = (int)((std::max)(2.0f, 3.0f * (std::min)(image.cols, image.rows) / 640.0f));
        cv::rectangle(image, obj.box, color, thickness);

        // 标签
        if (drawLabel)
        {
            char label[128];
            snprintf(label, sizeof(label), "%s %.2f (%d,%d)",
                obj.className.c_str(), obj.confidence,
                obj.box.x + obj.box.width / 2, obj.box.y + obj.box.height / 2);

            int baseline = 0;
            double fontScale = (std::max)(0.8, 1.2 * (std::min)(image.cols, image.rows) / 640.0);
            int fontThick = (int)((std::max)(1.0, 2.0 * (std::min)(image.cols, image.rows) / 640.0));
            cv::Size textSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                fontScale, fontThick, &baseline);

            cv::Point labelPos(obj.box.x, obj.box.y - 5);
            if (labelPos.y < textSize.height + 5)
                labelPos.y = obj.box.y + textSize.height + 5;

            cv::rectangle(image,
                cv::Rect(labelPos.x, labelPos.y - textSize.height,
                    textSize.width + 4, textSize.height + 4),
                color, cv::FILLED);
            cv::putText(image, label,
                cv::Point(labelPos.x + 2, labelPos.y),
                cv::FONT_HERSHEY_SIMPLEX, fontScale, cv::Scalar(255, 255, 255),
                fontThick, cv::LINE_AA);
        }
    }
}

// =====================================================
// 释放
// =====================================================
void Unload()
{
    delete s_Session;   s_Session = nullptr;
    delete s_MemInfo;   s_MemInfo = nullptr;
    delete s_Allocator; s_Allocator = nullptr;
    delete s_Env;       s_Env = nullptr;
    if (s_OrtDll) { FreeLibrary(s_OrtDll); s_OrtDll = nullptr; }
    s_Classes.clear();
    s_InputName.clear();
    s_OutputName.clear();
    s_Loaded = false;
}

} // namespace YOLODetector
