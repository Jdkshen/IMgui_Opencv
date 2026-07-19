#define NOMINMAX
#define ORT_API_MANUAL_INIT
#include "YOLODetector.h"
#include "../Core/RealtimeDetectionState.h"
#include "../Log/LogSystem.h"

#include <onnxruntime/onnxruntime_cxx_api.h>
#include <fstream>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <windows.h>
#include <chrono>

namespace YOLODetector
{

    // ---- UTF-8 → 宽字符 (Windows 中文路径支持) ----
    static std::wstring Utf8ToWide(const std::string &utf8)
    {
        if (utf8.empty())
            return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
        if (len <= 0)
            return {};
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), w.data(), len);
        return w;
    }

    // ---- 模型缓存：按路径保存 Session，切换免重建 ----
    struct CachedSession
    {
        Ort::Session *session = nullptr;
        std::vector<std::string> classes;
        std::string inputName, outputName;
        std::string backendName = "CPU";
        int inputW = 640, inputH = 640;

        CachedSession() = default;
        CachedSession(CachedSession &&o) noexcept
            : session(o.session), classes(std::move(o.classes)),
              inputName(std::move(o.inputName)), outputName(std::move(o.outputName)),
              backendName(std::move(o.backendName)),
              inputW(o.inputW), inputH(o.inputH)
        {
            o.session = nullptr;
        }
        CachedSession &operator=(CachedSession &&o) noexcept
        {
            if (this != &o)
            {
                delete session;
                session = o.session;
                o.session = nullptr;
                classes = std::move(o.classes);
                inputName = std::move(o.inputName);
                outputName = std::move(o.outputName);
                backendName = std::move(o.backendName);
                inputW = o.inputW;
                inputH = o.inputH;
            }
            return *this;
        }
        ~CachedSession() { delete session; }
    };

    // ---- ONNX Runtime 环境（全局单例，生命周期=进程）----
    static HMODULE s_OrtDll = nullptr;
    static Ort::Env *s_Env = nullptr;
    static Ort::AllocatorWithDefaultOptions *s_Allocator = nullptr;
    static Ort::MemoryInfo *s_MemInfo = nullptr;
    static bool s_OrtReady = false;

    // ---- 当前激活的模型 ----
    static CachedSession *s_Active = nullptr;
    static std::unordered_map<std::string, CachedSession> s_Cache;
    static bool s_LoggedProviders = false;

    static void LogAvailableProvidersOnce()
    {
        if (s_LoggedProviders)
            return;
        s_LoggedProviders = true;

        try
        {
            std::string joined;
            for (const auto& provider : Ort::GetAvailableProviders())
            {
                if (!joined.empty())
                    joined += ", ";
                joined += provider;
            }
            LogSystem::Add(LOG_INFO, "YOLO(ORT): 可用后端: %s", joined.empty() ? "(none)" : joined.c_str());
        }
        catch (const std::exception& e)
        {
            LogSystem::Add(LOG_WARN, "YOLO(ORT): 查询可用后端失败 - %s", e.what());
        }
        catch (...)
        {
            LogSystem::Add(LOG_WARN, "YOLO(ORT): 查询可用后端失败，发生未知异常");
        }
    }

    static bool TryAppendCudaProvider(Ort::SessionOptions& opts)
    {
        try
        {
            OrtCUDAProviderOptions cudaOptions{};
            cudaOptions.device_id = 0;
            opts.AppendExecutionProvider_CUDA(cudaOptions);
            LogSystem::Add(LOG_INFO, "YOLO(ORT): 启用 CUDA GPU 加速");
            return true;
        }
        catch (const Ort::Exception& e)
        {
            LogSystem::Add(LOG_WARN, "YOLO(ORT): CUDA 不可用 - %s (code=%d)", e.what(), e.GetOrtErrorCode());
        }
        catch (const std::exception& e)
        {
            LogSystem::Add(LOG_WARN, "YOLO(ORT): CUDA 不可用(std) - %s", e.what());
        }
        catch (...)
        {
            LogSystem::Add(LOG_WARN, "YOLO(ORT): CUDA 不可用，发生未知异常");
        }
        return false;
    }

    static bool TryAppendDmlProvider(Ort::SessionOptions& opts)
    {
        try
        {
            opts.AppendExecutionProvider("DML");
            LogSystem::Add(LOG_INFO, "YOLO(ORT): 启用 DML GPU 加速");
            return true;
        }
        catch (const Ort::Exception& e)
        {
            LogSystem::Add(LOG_WARN, "YOLO(ORT): DML 不可用 - %s (code=%d)", e.what(), e.GetOrtErrorCode());
        }
        catch (const std::exception& e)
        {
            LogSystem::Add(LOG_WARN, "YOLO(ORT): DML 不可用(std) - %s", e.what());
        }
        catch (...)
        {
            LogSystem::Add(LOG_WARN, "YOLO(ORT): DML 不可用，发生未知异常");
        }
        return false;
    }

    // =====================================================
    // 加载模型（缓存：同路径只加载一次）
    // =====================================================
    bool LoadModel(const std::string &onnxPath, const std::string &classesPath, bool useGPU)
    {
        // ---- 1. 初始化 ORT 环境（仅首次）----
        if (!s_OrtReady)
        {
            s_OrtDll = LoadLibraryW(L"onnxruntime.dll");
            if (!s_OrtDll)
            {
                LogSystem::Add(LOG_ERROR, "YOLO(ORT): 无法加载 onnxruntime.dll (err=%lu)", GetLastError());
                return false;
            }
            typedef const OrtApiBase *(ORT_API_CALL * PFN_OrtGetApiBase)(void);
            auto pfn = (PFN_OrtGetApiBase)GetProcAddress(s_OrtDll, "OrtGetApiBase");
            if (!pfn)
            {
                LogSystem::Add(LOG_ERROR, "YOLO(ORT): 找不到 OrtGetApiBase");
                return false;
            }
            const OrtApi *api = pfn()->GetApi(ORT_API_VERSION);
            if (!api)
            {
                LogSystem::Add(LOG_ERROR, "YOLO(ORT): GetApi 返回 nullptr");
                return false;
            }
            Ort::InitApi(api);
            s_Env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "YOLODetector");
            s_Allocator = new Ort::AllocatorWithDefaultOptions();
            s_MemInfo = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
            s_OrtReady = true;
            LogSystem::Add(LOG_INFO, "YOLO(ORT): 运行时初始化成功");
        }
        LogAvailableProvidersOnce();

        // ---- 2. 查缓存（区分 GPU/CPU） ----
        std::string cacheKey = onnxPath + (useGPU ? "|DML" : "|CPU");
        auto it = s_Cache.find(cacheKey);
        if (it != s_Cache.end())
        {
            s_Active = &it->second;
            // 重新加载类别文件（可能换了）
            LogSystem::Add(LOG_INFO, "YOLO(ORT): 切换类别文件=\"%s\"", classesPath.c_str());
            if (!classesPath.empty())
            {
                std::ifstream f(classesPath);
                if (f.is_open())
                {
                    s_Active->classes.clear();
                    std::string line;
                    while (std::getline(f, line))
                        if (!line.empty())
                            s_Active->classes.push_back(line);
                }
            }
            else
            {
                // 没设类别文件 → 重置为默认 COCO 80 类
                s_Active->classes = {
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
                    "teddy bear", "hair drier", "toothbrush"};
            }
            LogSystem::Add(LOG_INFO, "YOLO(ORT): 从缓存切换: %zu 个类别, 后端=%s",
                           s_Active->classes.size(), s_Active->backendName.c_str());
            return true;
        }

        // ---- 3. 加载新模型 ----
        // 3a. 类别
        std::vector<std::string> classes;
        if (!classesPath.empty())
        {
            std::ifstream f(Utf8ToWide(classesPath));
            if (f.is_open())
            {
                std::string line;
                while (std::getline(f, line))
                    if (!line.empty())
                        classes.push_back(line);
            }
            else
                LogSystem::Add(LOG_WARN, "YOLO: 无法打开类别文件，使用 COCO 默认");
        }
        if (classes.empty())
        {
            classes = {
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
                "teddy bear", "hair drier", "toothbrush"};
        }

        // 3b. 加载 Session
        try
        {
            Ort::SessionOptions opts;
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            opts.SetIntraOpNumThreads(0);    // 自动检测 CPU 核心数
            opts.SetInterOpNumThreads(1);    // YOLO 单模型推理
            opts.SetExecutionMode(ExecutionMode::ORT_PARALLEL);  // 并行执行
            std::string backendName = "CPU";
            if (useGPU)
            {
                // 优先 CUDA（NVIDIA），其次 DML（通用 DX12）
                if (TryAppendCudaProvider(opts))
                    backendName = "CUDA";
                else if (TryAppendDmlProvider(opts))
                    backendName = "DML";
                else
                    LogSystem::Add(LOG_WARN, "YOLO(ORT): GPU 不可用，回退 CPU");
            }

            std::wstring wPath = Utf8ToWide(onnxPath);
            auto *sess = new Ort::Session(*s_Env, wPath.c_str(), opts);

            auto inName = sess->GetInputNameAllocated(0, *s_Allocator);
            auto outName = sess->GetOutputNameAllocated(0, *s_Allocator);

            int iw = 640, ih = 640;
            Ort::TypeInfo inType = sess->GetInputTypeInfo(0);
            auto inShape = inType.GetTensorTypeAndShapeInfo().GetShape();
            if (inShape.size() >= 4 && inShape[2] > 0 && inShape[3] > 0)
            {
                ih = (int)inShape[2];
                iw = (int)inShape[3];
            }

            // 缓存
            CachedSession cs;
            cs.session = sess;
            cs.classes = std::move(classes);
            cs.inputName = inName.get();
            cs.outputName = outName.get();
            cs.inputW = iw;
            cs.inputH = ih;
            cs.backendName = backendName;

            auto [iter, ok] = s_Cache.emplace(cacheKey, std::move(cs));
            s_Active = &iter->second;
        }
        catch (const Ort::Exception &e)
        {
            LogSystem::Add(LOG_ERROR, "YOLO(ORT): 加载失败 - %s (code=%d)", e.what(), e.GetOrtErrorCode());
            return false;
        }
        catch (const std::exception &e)
        {
            LogSystem::Add(LOG_ERROR, "YOLO(ORT): 加载失败(std) - %s", e.what());
            return false;
        }
        catch (...)
        {
            LogSystem::Add(LOG_ERROR, "YOLO(ORT): 加载时发生未知异常");
            return false;
        }

        LogSystem::Add(LOG_INFO, "YOLO(ORT) 已缓存: %zu 类别 %dx%d 后端=%s",
                       s_Active->classes.size(), s_Active->inputW, s_Active->inputH,
                       s_Active->backendName.c_str());
        return true;
    }

    bool IsLoaded() { return s_Active && s_Active->session; }
    const char* GetBackendName()
    {
        return IsLoaded() ? s_Active->backendName.c_str() : "未加载";
    }

    const std::string &GetModelPath()
    {
        static std::string empty;
        if (!s_Active)
            return empty;
        for (auto &[k, v] : s_Cache)
            if (&v == s_Active)
                return k;
        return empty;
    }

    // =====================================================
    // 预处理：resize + 归一化 + blob (保留 OpenCV 做图像处理)
    // =====================================================
    static cv::Mat Preprocess(const cv::Mat &image, cv::Rect roi)
    {
        // 严格守卫：空图直接返回
        if (image.empty() || image.data == nullptr)
            return {};

        // 直接用 ROI 裁剪视图，避免 clone（blobFromImage 内部会拷贝）
        cv::Mat crop = (roi.width > 0 && roi.height > 0) ? image(roi) : image;

        if (crop.empty())
            return {};

        // YOLO 输入: (1, 3, H, W), RGB, 归一化到 [0,1]
        cv::Mat blob = cv::dnn::blobFromImage(crop, 1.0 / 255.0,
                                              cv::Size(s_Active->inputW, s_Active->inputH), cv::Scalar(), true, false);
        return blob;
    }

    // =====================================================
    // 后处理：自动适配 YOLOv5 [N,C] 和 YOLOv8 [C,N] 两种格式
    // =====================================================
    static std::vector<DetectedObject> Postprocess(
        const float *data, const std::vector<int64_t> &shape,
        float confThreshold, float nmsThreshold, cv::Rect roi)
    {
        std::vector<DetectedObject> detections;
        if (!data)
            return {};

        // =====================================================
        // YOLOv26 NMS-Free 格式: [1, 300, 6] → [x1,y1,x2,y2,conf,cls]
        // =====================================================
        if (shape.size() == 3 && shape[2] == 6 && shape[1] <= 500)
        {
            int N = (int)shape[1];
            float scaleX = (float)(roi.width > 0 ? roi.width : s_Active->inputW) / s_Active->inputW;
            float scaleY = (float)(roi.height > 0 ? roi.height : s_Active->inputH) / s_Active->inputH;

            for (int i = 0; i < N; i++)
            {
                float x1 = data[i * 6 + 0];
                float y1 = data[i * 6 + 1];
                float x2 = data[i * 6 + 2];
                float y2 = data[i * 6 + 3];
                float conf = data[i * 6 + 4];
                int cls = (int)data[i * 6 + 5];

                if (conf < confThreshold || cls < 0)
                    continue;

                int left = (int)(x1 * scaleX + roi.x);
                int top = (int)(y1 * scaleY + roi.y);
                int width = (int)((x2 - x1) * scaleX);
                int height = (int)((y2 - y1) * scaleY);

                DetectedObject obj;
                obj.box = cv::Rect(left, top, width, height);
                obj.classId = cls;
                obj.confidence = conf;
                if (cls < (int)s_Active->classes.size())
                    obj.className = s_Active->classes[cls];
                detections.push_back(obj);
            }
            return detections;
        }

        // 解析 C, N
        int dim0, dim1;
        if (shape.size() == 3)
        {
            dim0 = (int)shape[1];
            dim1 = (int)shape[2];
        }
        else if (shape.size() == 2)
        {
            dim0 = (int)shape[0];
            dim1 = (int)shape[1];
        }
        else
        {
            LogSystem::Add(LOG_ERROR, "YOLO: 输出维度=%zu, 需2或3维", shape.size());
            return {};
        }

        if (dim0 <= 0 || dim1 <= 0 || dim0 > 100000 || dim1 > 100000)
        {
            LogSystem::Add(LOG_ERROR, "YOLO: 输出形状异常 [%d %d]", dim0, dim1);
            return {};
        }

        // 自动检测格式：C 总是 ≤200 的小维度，N 是成千上万的网格数
        // YOLOv8 [1, C, N]: dim0=小(C), dim1=大(N)
        // YOLOv5 [1, N, C]: dim0=大(N), dim1=小(C)
        bool transposed; // false=[C,N] YOLOv8, true=[N,C] YOLOv5
        if (dim0 <= 200 && dim1 > dim0)
            transposed = false; // [C, N] YOLOv8: 第一维小=类别维
        else if (dim1 <= 200 && dim0 > dim1)
            transposed = true; // [N, C] YOLOv5: 第二维小=类别维
        else
            transposed = (dim0 > dim1); // 兜底：大的那维是N

        int C, N;
        if (!transposed)
        {
            C = dim0;
            N = dim1;
        } // [C, N] YOLOv8: C=4+cls
        else
        {
            N = dim0;
            C = dim1;
        } // [N, C] YOLOv5: C=5+cls

        int modelClasses = transposed ? (C - 5) : (C - 4);
        if (modelClasses <= 0)
            return {};

        int numClasses = (int)s_Active->classes.size();
        if (numClasses <= 0 || numClasses > modelClasses)
            numClasses = modelClasses;

        // 自动检测 class score 起始位置
        // YOLOv8: 可能 4(无obj) 或 5(有obj), YOLOv5: 通常 5
        int clsStart = transposed ? 5 : 4;
        if (C - clsStart - numClasses > 0 && clsStart + numClasses < C)
        {
            // C 比预期大，尝试往后找 — 比如 C=6 但 cls=1，说明 index 4 是 objectness
            for (int tryStart = clsStart + 1; tryStart <= C - numClasses; tryStart++)
            {
                if (tryStart + numClasses == C)
                {
                    clsStart = tryStart;
                    break;
                }
            }
        }

        float scaleX = (float)(roi.width > 0 ? roi.width : s_Active->inputW) / s_Active->inputW;
        float scaleY = (float)(roi.height > 0 ? roi.height : s_Active->inputH) / s_Active->inputH;

        std::vector<cv::Rect> boxes;
        std::vector<float> confidences;
        std::vector<int> classIds;

        // 如果有 objectness 通道，用它乘 class score
        bool hasObj = (clsStart > 4);

        for (int i = 0; i < N; i++)
        {
            float cx, cy, w, h;

            if (!transposed)
            {
                // YOLOv8: [C, N] — data[col * N + row]
                cx = data[0 * N + i];
                cy = data[1 * N + i];
                w = data[2 * N + i];
                h = data[3 * N + i];
            }
            else
            {
                // YOLOv5: [N, C] — data[row * C + col]
                cx = data[i * C + 0];
                cy = data[i * C + 1];
                w = data[i * C + 2];
                h = data[i * C + 3];
            }

            float maxConf = 0.0f;
            int bestClass = -1;

            // 有 objectness 通道时先读取
            float obj = 1.0f;
            if (hasObj)
            {
                obj = transposed ? data[i * C + 4] : data[4 * N + i];
            }

            for (int c = 0; c < numClasses; c++)
            {
                float conf = transposed
                                 ? data[i * C + clsStart + c]
                                 : data[(clsStart + c) * N + i];
                conf *= obj; // objectness × class_score
                if (conf > maxConf)
                {
                    maxConf = conf;
                    bestClass = c;
                }
            }

            if (maxConf < confThreshold)
                continue;

            int left = (int)((cx - w * 0.5f) * scaleX + roi.x);
            int top = (int)((cy - h * 0.5f) * scaleY + roi.y);
            int width = (int)(w * scaleX);
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
            obj.box = boxes[idx];
            obj.classId = classIds[idx];
            obj.confidence = confidences[idx];
            obj.className = (classIds[idx] >= 0 && classIds[idx] < (int)s_Active->classes.size())
                                ? s_Active->classes[classIds[idx]]
                                : "unknown";
            detections.push_back(obj);
        }

        return detections;
    }

    // =====================================================
    // 执行检测
    // =====================================================
    std::vector<DetectedObject> Detect(const cv::Mat &image,
                                       float confThreshold, float nmsThreshold, cv::Rect roi)
    {
        if (!s_Active || !s_Active->session || image.empty())
            return {};

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
            auto t0 = std::chrono::steady_clock::now();
            cv::Mat blob = Preprocess(image, roi);
            auto t1 = std::chrono::steady_clock::now();
            if (blob.empty())
            {
                LogSystem::Add(LOG_ERROR, "YOLO: 预处理生成空 blob");
                return {};
            }

            // ONNX Runtime 推理
            std::vector<int64_t> inputShape = {1, 3, s_Active->inputH, s_Active->inputW};
            size_t inputSize = 1 * 3 * s_Active->inputH * s_Active->inputW;

            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                *s_MemInfo, (float *)blob.data, inputSize,
                inputShape.data(), inputShape.size());

            const char *inNames[] = {s_Active->inputName.c_str()};
            const char *outNames[] = {s_Active->outputName.c_str()};

            auto outputs = s_Active->session->Run(Ort::RunOptions{nullptr},
                                                  inNames, &inputTensor, 1, outNames, 1);
            auto t2 = std::chrono::steady_clock::now();

            if (outputs.empty())
            {
                LogSystem::Add(LOG_WARN, "YOLO: 推理输出为空");
                return {};
            }

            // 解析输出形状和数据
            auto &outTensor = outputs[0];
            auto outShape = outTensor.GetTensorTypeAndShapeInfo().GetShape();
            const float *outData = outTensor.GetTensorData<float>();

            auto result = Postprocess(outData, outShape, confThreshold, nmsThreshold, roi);

            // 输出分步耗时供外部读取
            auto t3 = std::chrono::steady_clock::now();
            RealtimeDetectionState::Performance stats;
            stats.preprocessMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
            stats.inferenceMs = std::chrono::duration<float, std::milli>(t2 - t1).count();
            stats.postprocessMs = std::chrono::duration<float, std::milli>(t3 - t2).count();
            stats.totalMs = std::chrono::duration<float, std::milli>(t3 - t0).count();
            RealtimeDetectionState::SetStats(stats);

            return result;
        }
        catch (const Ort::Exception &e)
        {
            LogSystem::Add(LOG_ERROR, "YOLO(ORT) 推理异常: %s (code=%d)", e.what(), e.GetOrtErrorCode());
            return {};
        }
        catch (const std::exception &e)
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
    void DrawDetections(cv::Mat &image,
                        const std::vector<DetectedObject> &objects, bool drawLabel)
    {
        // 严格守卫：空图或无目标直接返回
        if (image.empty() || objects.empty())
            return;

        static const cv::Scalar s_Colors[] = {
            {0, 255, 0},     // 亮绿
            {0, 165, 255},   // 橙
            {255, 0, 255},   // 品红
            {0, 255, 255},   // 青
            {255, 255, 0},   // 天蓝
            {128, 0, 255},   // 橙红
            {255, 0, 128},   // 紫
            {50, 205, 50},   // 酸橙绿
            {255, 105, 180}, // 粉
            {30, 144, 255},  // 道奇蓝
            {255, 215, 0},   // 金
            {138, 43, 226},  // 蓝紫
        };
        constexpr int nColors = sizeof(s_Colors) / sizeof(s_Colors[0]);

        for (const auto &obj : objects)
        {
            cv::Scalar color = s_Colors[obj.classId % nColors];

            // 边框（加粗 ×1.5）
            int thickness = (int)((std::max)(2.0f, 3.0f * (std::min)(image.cols, image.rows) / 640.0f));
            cv::rectangle(image, obj.box, color, thickness);

            // 标签（ASCII 用 OpenCV，中文用 GDI）
            if (drawLabel)
            {
                char label[128];
                snprintf(label, sizeof(label), "%s %.2f (%d,%d)",
                         obj.className.c_str(), obj.confidence,
                         obj.box.x + obj.box.width / 2, obj.box.y + obj.box.height / 2);

                // 检测是否为纯 ASCII（Hershey 字体可渲染）
                bool isAscii = true;
                for (const char *p = label; *p; ++p)
                {
                    if ((unsigned char)*p > 127)
                    {
                        isAscii = false;
                        break;
                    }
                }

                double fontScale = (std::max)(0.8, 1.2 * (std::min)(image.cols, image.rows) / 640.0);
                int fontThick = (int)((std::max)(1.0, 2.0 * (std::min)(image.cols, image.rows) / 640.0));

                if (isAscii)
                {
                    // ===== ASCII：OpenCV putText =====
                    int baseline = 0;
                    cv::Size ts = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                                  fontScale, fontThick, &baseline);

                    int lx = obj.box.x;
                    int ly = obj.box.y - 5;
                    if (ly < ts.height + 5)
                        ly = obj.box.y + ts.height + 5;

                    cv::rectangle(image,
                                  cv::Rect(lx, ly - ts.height, ts.width + 4, ts.height + 4),
                                  color, cv::FILLED);
                    cv::putText(image, label,
                                cv::Point(lx + 2, ly),
                                cv::FONT_HERSHEY_SIMPLEX, fontScale, cv::Scalar(255, 255, 255),
                                fontThick, cv::LINE_AA);
                }
                else
                {
                    // ===== 非 ASCII：GDI 渲染中文 =====
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, label, -1, nullptr, 0);
                    if (wlen <= 1)
                        continue;
                    std::wstring wtext(wlen, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, label, -1, &wtext[0], wlen);
                    wtext.resize(wlen - 1);

                    int fontHeight = (int)(20.0 * fontScale);
                    int fontWeight = (fontThick <= 1) ? FW_NORMAL : FW_BOLD;

                    HDC hdcScreen = GetDC(nullptr);
                    HDC hdcMem = CreateCompatibleDC(hdcScreen);
                    HFONT hFont = CreateFontW(-fontHeight, 0, 0, 0, fontWeight,
                                              FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"SimSun");
                    HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

                    SIZE sz;
                    GetTextExtentPoint32W(hdcMem, wtext.c_str(), (int)wtext.size(), &sz);
                    int bw = sz.cx + 4, bh = sz.cy + 4;

                    int lx = obj.box.x;
                    int ly = obj.box.y - 5;
                    if (ly < bh + 5)
                        ly = obj.box.y + bh + 5;

                    cv::rectangle(image, cv::Rect(lx, ly - bh, bw, bh), color, cv::FILLED);

                    int roiX = (std::max)(0, lx);
                    int roiY = (std::max)(0, ly - bh);
                    int roiW = (std::min)(bw, image.cols - roiX);
                    int roiH = (std::min)(bh, image.rows - roiY);
                    if (roiW > 0 && roiH > 0)
                    {
                        BITMAPINFO bmi = {};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = bw;
                        bmi.bmiHeader.biHeight = -bh;
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;

                        void *bits = nullptr;
                        HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
                        if (hBmp && bits)
                        {
                            HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);
                            RECT rc = {0, 0, bw, bh};
                            HBRUSH hBr = CreateSolidBrush(RGB(color[2], color[1], color[0]));
                            FillRect(hdcMem, &rc, hBr);
                            DeleteObject(hBr);
                            SetTextColor(hdcMem, RGB(255, 255, 255));
                            SetBkMode(hdcMem, TRANSPARENT);
                            TextOutW(hdcMem, 2, 2, wtext.c_str(), (int)wtext.size());

                            cv::Mat roi = image(cv::Rect(roiX, roiY, roiW, roiH));
                            for (int dy = 0; dy < roiH; dy++)
                            {
                                uchar *src = (uchar *)bits + dy * bw * 4;
                                uchar *dst = roi.ptr<uchar>(dy);
                                for (int dx = 0; dx < roiW; dx++)
                                {
                                    dst[dx * 3] = src[dx * 4];
                                    dst[dx * 3 + 1] = src[dx * 4 + 1];
                                    dst[dx * 3 + 2] = src[dx * 4 + 2];
                                }
                            }
                            SelectObject(hdcMem, hOldBmp);
                        }
                        if (hBmp)
                            DeleteObject(hBmp);
                    }
                    SelectObject(hdcMem, hOldFont);
                    DeleteObject(hFont);
                    DeleteDC(hdcMem);
                    ReleaseDC(nullptr, hdcScreen);
                }
            }
        }
    }

    // =====================================================
    // 释放
    // =====================================================
    void Unload()
    {
        for (auto &kv : s_Cache)
            delete kv.second.session;
        s_Cache.clear();
        s_Active = nullptr;
        delete s_MemInfo;
        s_MemInfo = nullptr;
        delete s_Allocator;
        s_Allocator = nullptr;
        delete s_Env;
        s_Env = nullptr;
        if (s_OrtDll)
        {
            FreeLibrary(s_OrtDll);
            s_OrtDll = nullptr;
        }
        s_OrtReady = false;
    }

} // namespace YOLODetector

// 分步耗时（每次检测后更新，供外部读取）
