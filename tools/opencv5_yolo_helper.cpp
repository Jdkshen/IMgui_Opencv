#define NOMINMAX
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <io.h>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

struct Detection
{
    cv::Rect box;
    int classId = -1;
    float confidence = 0.0f;
};

static std::vector<Detection> PostprocessYolo(const cv::Mat &output, int imageW, int imageH,
                                              int inputSize, float confThreshold, float nmsThreshold);

static std::vector<cv::Mat> RunForward(cv::dnn::Net &net, const cv::Mat &image, int inputSize)
{
    cv::Mat blob = cv::dnn::blobFromImage(
        image,
        1.0 / 255.0,
        cv::Size(inputSize, inputSize),
        cv::Scalar(),
        true,
        false,
        CV_32F);

    net.setInput(blob);
    std::vector<cv::Mat> outputs;
    net.forward(outputs, net.getUnconnectedOutLayersNames());
    return outputs;
}

static void PrintForwardResult(int index, double forwardMs, const std::vector<cv::Mat> &outputs,
                               const cv::Mat &image, int inputSize, float confThreshold,
                               float nmsThreshold, bool printBoxes)
{
    std::cout << "forward[" << index << "]_ms=" << forwardMs
              << " outputs=" << outputs.size();
    if (!outputs.empty())
    {
        std::cout << " shape=";
        for (int d = 0; d < outputs[0].dims; ++d)
        {
            if (d)
                std::cout << "x";
            std::cout << outputs[0].size[d];
        }
        std::vector<Detection> detections = PostprocessYolo(outputs[0], image.cols, image.rows,
                                                            inputSize, confThreshold, nmsThreshold);
        std::cout << " detections=" << detections.size();
        if (printBoxes)
        {
            for (size_t j = 0; j < detections.size() && j < 5; ++j)
            {
                const Detection &det = detections[j];
                std::cout << " det" << j << "=[cls=" << det.classId
                          << " conf=" << det.confidence
                          << " box=" << det.box.x << "," << det.box.y << ","
                          << det.box.width << "," << det.box.height << "]";
            }
        }
    }
    std::cout << "\n";
}

static std::vector<int64_t> ShapeOf(const cv::Mat &mat)
{
    std::vector<int64_t> shape;
    for (int i = 0; i < mat.dims; ++i)
        shape.push_back(mat.size[i]);
    return shape;
}

static cv::Mat LoadRawImage(const std::string &path, int width, int height, int channels)
{
    if (width <= 0 || height <= 0 || (channels != 1 && channels != 3 && channels != 4))
        throw std::runtime_error("invalid raw image metadata");

    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("raw image open failed");

    const size_t expected = (size_t)width * (size_t)height * (size_t)channels;
    std::vector<unsigned char> bytes(expected);
    in.read(reinterpret_cast<char *>(bytes.data()), (std::streamsize)bytes.size());
    if ((size_t)in.gcount() != expected)
        throw std::runtime_error("raw image size mismatch");

    int type = channels == 1 ? CV_8UC1 : (channels == 3 ? CV_8UC3 : CV_8UC4);
    cv::Mat raw(height, width, type, bytes.data());
    cv::Mat bgr;
    if (channels == 1)
        cv::cvtColor(raw, bgr, cv::COLOR_GRAY2BGR);
    else if (channels == 4)
        cv::cvtColor(raw, bgr, cv::COLOR_BGRA2BGR);
    else
        bgr = raw.clone();
    return bgr;
}

static cv::Mat DecodeRawImageBytes(std::vector<unsigned char> &bytes, int width, int height, int channels)
{
    if (width <= 0 || height <= 0 || (channels != 1 && channels != 3 && channels != 4))
        throw std::runtime_error("invalid raw image metadata");

    const size_t expected = (size_t)width * (size_t)height * (size_t)channels;
    if (bytes.size() != expected)
        throw std::runtime_error("raw image size mismatch");

    int type = channels == 1 ? CV_8UC1 : (channels == 3 ? CV_8UC3 : CV_8UC4);
    cv::Mat raw(height, width, type, bytes.data());
    cv::Mat bgr;
    if (channels == 1)
        cv::cvtColor(raw, bgr, cv::COLOR_GRAY2BGR);
    else if (channels == 4)
        cv::cvtColor(raw, bgr, cv::COLOR_BGRA2BGR);
    else
        bgr = raw.clone();
    return bgr;
}

static std::vector<Detection> PostprocessYolo(const cv::Mat &output, int imageW, int imageH,
                                              int inputSize, float confThreshold, float nmsThreshold)
{
    std::vector<Detection> detections;
    if (output.empty())
        return detections;

    cv::Mat out = output.isContinuous() ? output : output.clone();
    const float *data = out.ptr<float>();
    std::vector<int64_t> shape = ShapeOf(out);
    if (!data || shape.empty())
        return detections;

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
        return detections;
    }

    if (shape.size() == 3 && shape[2] == 6 && shape[1] <= 500)
    {
        std::vector<cv::Rect> boxes;
        std::vector<float> confidences;
        std::vector<int> classIds;
        const int n = (int)shape[1];
        const float scaleX = (float)imageW / (float)inputSize;
        const float scaleY = (float)imageH / (float)inputSize;
        for (int i = 0; i < n; ++i)
        {
            const float conf = data[i * 6 + 4];
            if (conf < confThreshold)
                continue;
            int x1 = (int)(data[i * 6 + 0] * scaleX);
            int y1 = (int)(data[i * 6 + 1] * scaleY);
            int x2 = (int)(data[i * 6 + 2] * scaleX);
            int y2 = (int)(data[i * 6 + 3] * scaleY);
            boxes.emplace_back(cv::Point(x1, y1), cv::Point(x2, y2));
            confidences.push_back(conf);
            classIds.push_back((int)data[i * 6 + 5]);
        }
        std::vector<int> indices;
        cv::dnn::NMSBoxes(boxes, confidences, confThreshold, nmsThreshold, indices);
        for (int idx : indices)
            detections.push_back({boxes[idx], classIds[idx], confidences[idx]});
        return detections;
    }

    bool transposed = false;
    if (dim0 <= 200 && dim1 > dim0)
        transposed = false;
    else if (dim1 <= 200 && dim0 > dim1)
        transposed = true;
    else
        transposed = dim0 > dim1;

    const int cCount = transposed ? dim1 : dim0;
    const int nCount = transposed ? dim0 : dim1;
    int clsStart = transposed ? 5 : 4;
    int classCount = transposed ? (cCount - 5) : (cCount - 4);
    if (classCount <= 0)
        return detections;
    if (classCount > 80)
        classCount = 80;

    const bool hasObj = clsStart > 4;
    const float scaleX = (float)imageW / (float)inputSize;
    const float scaleY = (float)imageH / (float)inputSize;
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> classIds;

    for (int i = 0; i < nCount; ++i)
    {
        float cx, cy, w, h;
        if (!transposed)
        {
            cx = data[0 * nCount + i];
            cy = data[1 * nCount + i];
            w = data[2 * nCount + i];
            h = data[3 * nCount + i];
        }
        else
        {
            cx = data[i * cCount + 0];
            cy = data[i * cCount + 1];
            w = data[i * cCount + 2];
            h = data[i * cCount + 3];
        }

        const float obj = hasObj ? (transposed ? data[i * cCount + 4] : data[4 * nCount + i]) : 1.0f;
        float bestConf = 0.0f;
        int bestClass = -1;
        for (int c = 0; c < classCount; ++c)
        {
            float conf = transposed ? data[i * cCount + clsStart + c] : data[(clsStart + c) * nCount + i];
            conf *= obj;
            if (conf > bestConf)
            {
                bestConf = conf;
                bestClass = c;
            }
        }
        if (bestConf < confThreshold)
            continue;

        int left = (int)((cx - w * 0.5f) * scaleX);
        int top = (int)((cy - h * 0.5f) * scaleY);
        boxes.emplace_back(left, top, (int)(w * scaleX), (int)(h * scaleY));
        confidences.push_back(bestConf);
        classIds.push_back(bestClass);
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, confThreshold, nmsThreshold, indices);
    for (int idx : indices)
        detections.push_back({boxes[idx], classIds[idx], confidences[idx]});
    return detections;
}

static int RunServer(cv::dnn::Net &net, int inputSize)
{
    std::cout << "READY\n"
              << std::flush;
    std::string command;
    while (std::cin >> command)
    {
        if (command == "QUIT")
        {
            std::cout << "BYE\n"
                      << std::flush;
            return 0;
        }
        if (command == "RUNB")
        {
            int rawW = 0;
            int rawH = 0;
            int rawChannels = 0;
            float confThreshold = 0.25f;
            float nmsThreshold = 0.45f;
            int repeat = 1;
            size_t byteCount = 0;
            std::cin >> rawW >> rawH >> rawChannels >> confThreshold >> nmsThreshold >> repeat >> byteCount;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            repeat = std::max(1, repeat);

            try
            {
                std::vector<unsigned char> bytes(byteCount);
                std::cin.read(reinterpret_cast<char *>(bytes.data()), (std::streamsize)bytes.size());
                if ((size_t)std::cin.gcount() != byteCount)
                    throw std::runtime_error("binary image read failed");

                cv::Mat image = DecodeRawImageBytes(bytes, rawW, rawH, rawChannels);
                std::cout << "image=<pipe> " << image.cols << "x" << image.rows
                          << " channels=" << image.channels()
                          << " bytes=" << byteCount
                          << " conf=" << confThreshold << " nms=" << nmsThreshold
                          << " repeat=" << repeat << "\n";
                for (int i = 0; i < repeat; ++i)
                {
                    auto t0 = std::chrono::steady_clock::now();
                    std::vector<cv::Mat> outputs = RunForward(net, image, inputSize);
                    auto t1 = std::chrono::steady_clock::now();
                    PrintForwardResult(i,
                                       std::chrono::duration<double, std::milli>(t1 - t0).count(),
                                       outputs, image, inputSize, confThreshold, nmsThreshold, true);
                }
                std::cout << "END\n"
                          << std::flush;
            }
            catch (const cv::Exception &e)
            {
                std::cout << "opencv_error: " << e.what() << "\nEND\n"
                          << std::flush;
            }
            catch (const std::exception &e)
            {
                std::cout << "error: " << e.what() << "\nEND\n"
                          << std::flush;
            }
            continue;
        }
        if (command != "RUN")
        {
            std::cout << "ERROR unknown_command\nEND\n"
                      << std::flush;
            continue;
        }

        std::string rawPath;
        int rawW = 0;
        int rawH = 0;
        int rawChannels = 0;
        float confThreshold = 0.25f;
        float nmsThreshold = 0.45f;
        int repeat = 1;
        std::cin >> rawPath >> rawW >> rawH >> rawChannels >> confThreshold >> nmsThreshold >> repeat;
        repeat = std::max(1, repeat);

        try
        {
            cv::Mat image = LoadRawImage(rawPath, rawW, rawH, rawChannels);
            std::cout << "image=" << rawPath << " " << image.cols << "x" << image.rows
                      << " channels=" << image.channels()
                      << " conf=" << confThreshold << " nms=" << nmsThreshold
                      << " repeat=" << repeat << "\n";
            for (int i = 0; i < repeat; ++i)
            {
                auto t0 = std::chrono::steady_clock::now();
                std::vector<cv::Mat> outputs = RunForward(net, image, inputSize);
                auto t1 = std::chrono::steady_clock::now();
                PrintForwardResult(i,
                                   std::chrono::duration<double, std::milli>(t1 - t0).count(),
                                   outputs, image, inputSize, confThreshold, nmsThreshold, true);
            }
            std::cout << "END\n"
                      << std::flush;
        }
        catch (const cv::Exception &e)
        {
            std::cout << "opencv_error: " << e.what() << "\nEND\n"
                      << std::flush;
        }
        catch (const std::exception &e)
        {
            std::cout << "error: " << e.what() << "\nEND\n"
                      << std::flush;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    if (argc < 2)
    {
        std::cerr << "usage: opencv5_yolo_helper.exe <model.onnx> [repeat] [engine:auto|classic|new] [input_size] [--raw-bgr file width height channels conf nms]\n";
        return 2;
    }

    const std::string modelPath = argv[1];
    const int repeat = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 3;
    int engine = cv::dnn::ENGINE_CLASSIC;
    if (argc >= 4)
    {
        std::string engineName = argv[3];
        if (engineName == "auto")
            engine = cv::dnn::ENGINE_AUTO;
        else if (engineName == "new")
            engine = cv::dnn::ENGINE_NEW;
        else
            engine = cv::dnn::ENGINE_CLASSIC;
    }
    const int inputSize = argc >= 5 ? std::max(32, std::atoi(argv[4])) : 320;
    const bool serverMode = argc >= 3 && std::string(argv[2]) == "--server";
    std::string rawPath;
    int rawW = 0;
    int rawH = 0;
    int rawChannels = 0;
    float confThreshold = 0.25f;
    float nmsThreshold = 0.45f;
    if (!serverMode && argc >= 12 && std::string(argv[5]) == "--raw-bgr")
    {
        rawPath = argv[6];
        rawW = std::atoi(argv[7]);
        rawH = std::atoi(argv[8]);
        rawChannels = std::atoi(argv[9]);
        confThreshold = (float)std::atof(argv[10]);
        nmsThreshold = (float)std::atof(argv[11]);
    }

    try
    {
        std::cout << "OpenCV " << CV_VERSION << "\n";
        std::cout << "model=" << modelPath << "\n";
        std::cout << "engine=" << engine << "\n";
        std::cout << "input_size=" << inputSize << "\n";

        auto load0 = std::chrono::steady_clock::now();
        cv::dnn::Net net = cv::dnn::readNetFromONNX(modelPath, engine);
        auto load1 = std::chrono::steady_clock::now();
        if (net.empty())
        {
            std::cerr << "error: net is empty\n";
            return 3;
        }

        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

        if (serverMode)
        {
            std::cout << "load_ms="
                      << std::chrono::duration<double, std::milli>(load1 - load0).count()
                      << "\n";
            return RunServer(net, inputSize);
        }

        cv::Mat image;
        if (!rawPath.empty())
        {
            image = LoadRawImage(rawPath, rawW, rawH, rawChannels);
            std::cout << "image=" << rawPath << " " << image.cols << "x" << image.rows
                      << " channels=" << image.channels()
                      << " conf=" << confThreshold << " nms=" << nmsThreshold << "\n";
        }
        else
        {
            image = cv::Mat(inputSize, inputSize, CV_8UC3, cv::Scalar(32, 32, 32));
            cv::rectangle(image, cv::Rect(inputSize / 4, inputSize / 5, inputSize / 3, inputSize / 2),
                          cv::Scalar(210, 210, 210), cv::FILLED);
            cv::circle(image, cv::Point(inputSize / 2, inputSize / 2), inputSize / 8,
                       cv::Scalar(60, 130, 220), cv::FILLED);
            std::cout << "image=synthetic\n";
        }

        std::cout << "load_ms="
                  << std::chrono::duration<double, std::milli>(load1 - load0).count()
                  << "\n";

        for (int i = 0; i < repeat; ++i)
        {
            auto t0 = std::chrono::steady_clock::now();
            std::vector<cv::Mat> outputs = RunForward(net, image, inputSize);
            auto t1 = std::chrono::steady_clock::now();

            PrintForwardResult(i,
                               std::chrono::duration<double, std::milli>(t1 - t0).count(),
                               outputs, image, inputSize, confThreshold, nmsThreshold, !rawPath.empty());
        }
        return 0;
    }
    catch (const cv::Exception &e)
    {
        std::cerr << "opencv_error: " << e.what() << "\n";
        return 10;
    }
    catch (const std::exception &e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 11;
    }
}
