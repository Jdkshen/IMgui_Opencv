#include "HardwareCameraPolicy.h"

#include <opencv2/core.hpp>

namespace HardwareCameraPolicy
{
cv::Mat OrientFrame(const cv::Mat& frame, int orientation)
{
    cv::Mat orientedFrame;
    switch (orientation)
    {
    case 1: cv::rotate(frame, orientedFrame, cv::ROTATE_90_CLOCKWISE); break;
    case 2: cv::rotate(frame, orientedFrame, cv::ROTATE_180); break;
    case 3: cv::rotate(frame, orientedFrame, cv::ROTATE_90_COUNTERCLOCKWISE); break;
    case 4: cv::flip(frame, orientedFrame, 1); break;
    case 5: cv::flip(frame, orientedFrame, 0); break;
    default: break;
    }
    return orientedFrame.empty() ? frame : orientedFrame;
}

DeviceOperationResult ValidateTrigger(const CameraCapabilities& capabilities,
    const CameraTriggerConfig& trigger)
{
    if (trigger.mode == CameraTriggerMode::Continuous &&
        !capabilities.softwareTrigger && !capabilities.hardwareTrigger)
    {
        return {true, "continuous acquisition"};
    }
    if (trigger.mode == CameraTriggerMode::Software &&
        !capabilities.softwareTrigger)
    {
        return {false, "selected camera does not support software trigger"};
    }
    if ((trigger.mode == CameraTriggerMode::HardwareLine1 ||
         trigger.mode == CameraTriggerMode::HardwareLine2) &&
        !capabilities.hardwareTrigger)
    {
        return {false, "selected camera does not support hardware line trigger"};
    }
    return {true, {}};
}

DeviceOperationResult ValidatePtp(const CameraCapabilities& capabilities,
    bool enabled)
{
    if (!enabled)
        return {true, "PTP disabled"};
    return capabilities.ptp
        ? DeviceOperationResult{true, {}}
        : DeviceOperationResult{false, "selected camera does not support PTP"};
}

DeviceOperationResult ValidateBufferPolicy(
    const CameraCapabilities& capabilities, CameraBufferPolicy policy)
{
    if (policy == CameraBufferPolicy::Sequential)
        return {true, "sequential frame buffer"};
    return capabilities.queueControl
        ? DeviceOperationResult{true, {}}
        : DeviceOperationResult{false,
            "selected camera does not support frame buffer control"};
}
}
