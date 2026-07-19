#include "OpenCvCameraAdapter.h"

#include <opencv2/videoio.hpp>

#include <algorithm>
#include <charconv>
#include <utility>

namespace
{
int CaptureBackend(const std::string& name)
{
    if (name == "dshow") return cv::CAP_DSHOW;
    if (name == "msmf") return cv::CAP_MSMF;
    if (name == "ffmpeg") return cv::CAP_FFMPEG;
    if (name == "gstreamer") return cv::CAP_GSTREAMER;
    return cv::CAP_ANY;
}

bool ParseCameraIndex(const std::string& address, int& index)
{
    if (address.empty())
        return false;
    const char* begin = address.data();
    const char* end = begin + address.size();
    const auto parsed = std::from_chars(begin, end, index);
    return parsed.ec == std::errc{} && parsed.ptr == end && index >= 0;
}

class OpenCvCaptureBackend final : public ICameraCaptureBackend
{
public:
    DeviceOperationResult Open(const DeviceEndpoint& endpoint) override
    {
        Close();
        if (endpoint.address.empty())
            return {false, "camera address is empty"};

        const int timeout = (std::max)(1, endpoint.timeoutMs);
        capture_.set(cv::CAP_PROP_OPEN_TIMEOUT_MSEC, timeout);
        capture_.set(cv::CAP_PROP_READ_TIMEOUT_MSEC, timeout);
        const int backend = CaptureBackend(endpoint.resource);
        int cameraIndex = -1;
        const bool opened = ParseCameraIndex(endpoint.address, cameraIndex)
            ? capture_.open(cameraIndex, backend)
            : capture_.open(endpoint.address, backend);
        if (!opened)
            return {false, "OpenCV could not open camera source: " + endpoint.address};

        capture_.set(cv::CAP_PROP_READ_TIMEOUT_MSEC, timeout);
        return {true, "OpenCV camera connected"};
    }

    void Close() override
    {
        capture_.release();
    }

    DeviceOperationResult Read(cv::Mat& frame, int timeoutMs) override
    {
        frame.release();
        if (!capture_.isOpened())
            return {false, "OpenCV camera is not open"};
        capture_.set(cv::CAP_PROP_READ_TIMEOUT_MSEC, (std::max)(1, timeoutMs));
        if (!capture_.read(frame) || frame.empty())
            return {false, "OpenCV camera returned no frame"};
        return {true, "OpenCV camera frame captured"};
    }

private:
    cv::VideoCapture capture_;
};
}

OpenCvCameraAdapter::OpenCvCameraAdapter(
    std::unique_ptr<ICameraCaptureBackend> backend)
    : backend_(backend ? std::move(backend)
                       : std::make_unique<OpenCvCaptureBackend>())
{
}

OpenCvCameraAdapter::~OpenCvCameraAdapter()
{
    Disconnect();
}

const char* OpenCvCameraAdapter::AdapterName() const
{
    return "OpenCV Camera";
}

DeviceOperationResult OpenCvCameraAdapter::Connect(const DeviceEndpoint& endpoint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    backend_->Close();
    streaming_ = false;
    state_ = DeviceConnectionState::Connecting;
    DeviceOperationResult result = backend_->Open(endpoint);
    if (!result.success)
        return Fail(std::move(result.message), true);

    state_ = DeviceConnectionState::Connected;
    lastError_.clear();
    return {true, "OpenCV camera connected"};
}

void OpenCvCameraAdapter::Disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    streaming_ = false;
    backend_->Close();
    state_ = DeviceConnectionState::Disconnected;
}

DeviceConnectionState OpenCvCameraAdapter::ConnectionState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string OpenCvCameraAdapter::LastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

DeviceOperationResult OpenCvCameraAdapter::GrabFrame(cv::Mat& frame, int timeoutMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    frame.release();
    if (state_ != DeviceConnectionState::Connected)
        return Fail("OpenCV camera is not connected");

    DeviceOperationResult result = backend_->Read(frame, (std::max)(1, timeoutMs));
    if (!result.success)
        return Fail(std::move(result.message));
    if (frame.empty())
        return Fail("OpenCV camera returned an empty frame");
    return result;
}

DeviceOperationResult OpenCvCameraAdapter::StartStream()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected)
        return Fail("OpenCV camera is not connected");
    streaming_ = true;
    return {true, "OpenCV camera stream started"};
}

void OpenCvCameraAdapter::StopStream()
{
    std::lock_guard<std::mutex> lock(mutex_);
    streaming_ = false;
}

bool OpenCvCameraAdapter::IsStreaming() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return streaming_;
}

DeviceOperationResult OpenCvCameraAdapter::Fail(std::string message, bool fault)
{
    lastError_ = std::move(message);
    if (fault)
    {
        backend_->Close();
        streaming_ = false;
        state_ = DeviceConnectionState::Fault;
    }
    return {false, lastError_};
}
