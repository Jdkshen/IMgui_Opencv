#pragma once

#include "HardwareAdapters.h"

#include <opencv2/core/mat.hpp>

namespace HardwareCameraPolicy
{
    cv::Mat OrientFrame(const cv::Mat& frame, int orientation);
    DeviceOperationResult ValidateTrigger(const CameraCapabilities& capabilities,
        const CameraTriggerConfig& trigger);
    DeviceOperationResult ValidatePtp(const CameraCapabilities& capabilities,
        bool enabled);
    DeviceOperationResult ValidateBufferPolicy(
        const CameraCapabilities& capabilities, CameraBufferPolicy policy);
}
