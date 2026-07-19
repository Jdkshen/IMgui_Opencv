#pragma once

#include "CalibrationModel.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

struct CalibrationSample
{
    cv::Point2d pixel;
    cv::Point2d world;
};

struct CalibrationFitResult
{
    bool success = false;
    CalibrationModel model;
    double rmsError = 0.0;
    double maxError = 0.0;
    std::vector<double> residuals;
    std::string message;
};

namespace CalibrationFitter
{
    CalibrationFitResult FitScale(const std::vector<CalibrationSample>& samples);
    CalibrationFitResult FitHomography(const std::vector<CalibrationSample>& samples,
        double reprojectionThreshold = 3.0);
    CalibrationFitResult Evaluate(const CalibrationModel& model,
        const std::vector<CalibrationSample>& samples);

    nlohmann::json ToJson(const CalibrationModel& model);
    bool FromJson(const nlohmann::json& json, CalibrationModel& model);
    bool Save(const char* path, const CalibrationModel& model);
    bool Load(const char* path, CalibrationModel& model);
    bool SaveDocument(const char* path, const CalibrationModel& model,
        const std::vector<CalibrationSample>& samples);
    bool LoadDocument(const char* path, CalibrationModel& model,
        std::vector<CalibrationSample>& samples);
}
