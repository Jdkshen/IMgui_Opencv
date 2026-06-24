#define NOMINMAX
#include "OpenCVYoloDetector.h"
#include "../Log/LogSystem.h"

#include <opencv2/dnn.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>

namespace OpenCVYoloDetector
{
    static cv::dnn::Net s_Net;
    static std::string s_ModelPath;
    static std::string s_ClassesPath;
    static std::vector<std::string> s_Classes;
    static int s_InputW = 320;
    static int s_InputH = 320;

    float g_OpenCVYoloPreMs = 0.0f;
    float g_OpenCVYoloInfMs = 0.0f;
    float g_OpenCVYoloPostMs = 0.0f;
    float g_OpenCVYoloTotalMs = 0.0f;

    static std::vector<std::string> DefaultCocoClasses()
    {
        return {
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
    }

    static std::vector<std::string> LoadClasses(const std::string& path)
    {
        std::vector<std::string> classes;
        if (!path.empty())
        {
            std::ifstream f(path);
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty())
                    classes.push_back(line);
            }
        }
        return classes.empty() ? DefaultCocoClasses() : classes;
    }

    bool LoadModel(const std::string& onnxPath, const std::string& classesPath)
    {
        if (onnxPath.empty())
            return false;

        if (!s_Net.empty() && s_ModelPath == onnxPath && s_ClassesPath == classesPath)
            return true;

        try
        {
            cv::dnn::Net net = cv::dnn::readNetFromONNX(onnxPath);
            if (net.empty())
            {
                LogSystem::Add(LOG_ERROR, "YOLO OpenCV DNN: 模型加载为空");
                return false;
            }

            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

            s_Net = std::move(net);
            s_ModelPath = onnxPath;
            s_ClassesPath = classesPath;
            s_Classes = LoadClasses(classesPath);
            LogSystem::Add(LOG_INFO, "YOLO OpenCV DNN: 已加载 %s, %zu 类别",
                onnxPath.c_str(), s_Classes.size());
            return true;
        }
        catch (const cv::Exception& e)
        {
            LogSystem::Add(LOG_ERROR, "YOLO OpenCV DNN: 加载失败 - %s", e.what());
            return false;
        }
    }

    bool IsLoaded()
    {
        return !s_Net.empty();
    }

    const std::string& GetModelPath()
    {
        return s_ModelPath;
    }

    static std::vector<int64_t> ShapeOf(const cv::Mat& m)
    {
        std::vector<int64_t> shape;
        if (m.dims > 0)
        {
            shape.reserve((size_t)m.dims);
            for (int i = 0; i < m.dims; ++i)
                shape.push_back(m.size[i]);
        }
        else
        {
            shape = {m.rows, m.cols};
        }
        return shape;
    }

    static std::vector<DetectedObject> Postprocess(const float* data, const std::vector<int64_t>& shape,
        float confThreshold, float nmsThreshold, cv::Rect roi)
    {
        std::vector<DetectedObject> detections;
        if (!data || shape.empty())
            return detections;

        if (shape.size() == 3 && shape[2] == 6 && shape[1] <= 500)
        {
            int N = (int)shape[1];
            float scaleX = (float)roi.width / s_InputW;
            float scaleY = (float)roi.height / s_InputH;
            for (int i = 0; i < N; ++i)
            {
                float conf = data[i * 6 + 4];
                int cls = (int)data[i * 6 + 5];
                if (conf < confThreshold || cls < 0)
                    continue;

                DetectedObject obj;
                int x1 = (int)(data[i * 6 + 0] * scaleX + roi.x);
                int y1 = (int)(data[i * 6 + 1] * scaleY + roi.y);
                int x2 = (int)(data[i * 6 + 2] * scaleX + roi.x);
                int y2 = (int)(data[i * 6 + 3] * scaleY + roi.y);
                obj.box = cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2));
                obj.classId = cls;
                obj.confidence = conf;
                obj.className = cls < (int)s_Classes.size() ? s_Classes[cls] : "unknown";
                detections.push_back(obj);
            }
            return detections;
        }

        int dim0 = 0;
        int dim1 = 0;
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
            LogSystem::Add(LOG_ERROR, "YOLO OpenCV DNN: 输出维度=%zu, 需2或3维", shape.size());
            return detections;
        }

        bool transposed = false;
        if (dim0 <= 200 && dim1 > dim0)
            transposed = false;
        else if (dim1 <= 200 && dim0 > dim1)
            transposed = true;
        else
            transposed = dim0 > dim1;

        int C = transposed ? dim1 : dim0;
        int N = transposed ? dim0 : dim1;
        int modelClasses = transposed ? (C - 5) : (C - 4);
        if (modelClasses <= 0)
            return detections;

        int numClasses = (int)s_Classes.size();
        if (numClasses <= 0 || numClasses > modelClasses)
            numClasses = modelClasses;

        int clsStart = transposed ? 5 : 4;
        if (C - clsStart - numClasses > 0 && clsStart + numClasses < C)
        {
            for (int tryStart = clsStart + 1; tryStart <= C - numClasses; ++tryStart)
            {
                if (tryStart + numClasses == C)
                {
                    clsStart = tryStart;
                    break;
                }
            }
        }

        float scaleX = (float)roi.width / s_InputW;
        float scaleY = (float)roi.height / s_InputH;
        bool hasObj = clsStart > 4;

        std::vector<cv::Rect> boxes;
        std::vector<float> confidences;
        std::vector<int> classIds;

        for (int i = 0; i < N; ++i)
        {
            float cx, cy, w, h;
            if (!transposed)
            {
                cx = data[0 * N + i];
                cy = data[1 * N + i];
                w = data[2 * N + i];
                h = data[3 * N + i];
            }
            else
            {
                cx = data[i * C + 0];
                cy = data[i * C + 1];
                w = data[i * C + 2];
                h = data[i * C + 3];
            }

            float obj = hasObj ? (transposed ? data[i * C + 4] : data[4 * N + i]) : 1.0f;
            float maxConf = 0.0f;
            int bestClass = -1;
            for (int c = 0; c < numClasses; ++c)
            {
                float conf = transposed ? data[i * C + clsStart + c] : data[(clsStart + c) * N + i];
                conf *= obj;
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
            boxes.emplace_back(left, top, (int)(w * scaleX), (int)(h * scaleY));
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
            obj.className = (obj.classId >= 0 && obj.classId < (int)s_Classes.size()) ? s_Classes[obj.classId] : "unknown";
            detections.push_back(obj);
        }
        return detections;
    }

    std::vector<DetectedObject> Detect(const cv::Mat& image, float confThreshold, float nmsThreshold, cv::Rect roi)
    {
        try {
            if (s_Net.empty() || image.empty())
                return {};

            auto total0 = std::chrono::steady_clock::now();
            if (roi.width <= 0 || roi.height <= 0)
                roi = cv::Rect(0, 0, image.cols, image.rows);
            else
                roi &= cv::Rect(0, 0, image.cols, image.rows);
            if (roi.width <= 0 || roi.height <= 0)
                return {};

            cv::Mat crop = image(roi);
            auto t0 = std::chrono::steady_clock::now();
            cv::Mat blob = cv::dnn::blobFromImage(crop, 1.0 / 255.0, cv::Size(s_InputW, s_InputH),
                cv::Scalar(), true, false, CV_32F);
            auto t1 = std::chrono::steady_clock::now();

            s_Net.setInput(blob);
            std::vector<cv::Mat> outputs;
            s_Net.forward(outputs, s_Net.getUnconnectedOutLayersNames());
            auto t2 = std::chrono::steady_clock::now();

            std::vector<DetectedObject> result;
            if (!outputs.empty())
            {
                cv::Mat out = outputs[0];
                if (!out.isContinuous())
                    out = out.clone();
                result = Postprocess(out.ptr<float>(), ShapeOf(out), confThreshold, nmsThreshold, roi);
            }
            auto t3 = std::chrono::steady_clock::now();

            g_OpenCVYoloPreMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
            g_OpenCVYoloInfMs = std::chrono::duration<float, std::milli>(t2 - t1).count();
            g_OpenCVYoloPostMs = std::chrono::duration<float, std::milli>(t3 - t2).count();
            g_OpenCVYoloTotalMs = std::chrono::duration<float, std::milli>(t3 - total0).count();
            return result;
        }
        catch (const cv::Exception& e) {
            LogSystem::Add(LOG_ERROR, "YOLO OpenCV DNN: inference failed - %s", e.what());
        }
        catch (const std::exception& e) {
            LogSystem::Add(LOG_ERROR, "YOLO OpenCV DNN: inference failed - %s", e.what());
        }
        catch (...) {
            LogSystem::Add(LOG_ERROR, "YOLO OpenCV DNN: inference failed with an unknown error");
        }
        g_OpenCVYoloPreMs = 0.0f;
        g_OpenCVYoloInfMs = 0.0f;
        g_OpenCVYoloPostMs = 0.0f;
        g_OpenCVYoloTotalMs = 0.0f;
        return {};
    }

    void Unload()
    {
        s_Net = cv::dnn::Net();
        s_ModelPath.clear();
        s_ClassesPath.clear();
        s_Classes.clear();
        g_OpenCVYoloPreMs = 0.0f;
        g_OpenCVYoloInfMs = 0.0f;
        g_OpenCVYoloPostMs = 0.0f;
        g_OpenCVYoloTotalMs = 0.0f;
    }
}
