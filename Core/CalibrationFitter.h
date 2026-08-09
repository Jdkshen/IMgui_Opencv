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
    double meanError = 0.0;
    double maxError = 0.0;
    std::size_t totalImageCount = 0;
    std::size_t successfulImageCount = 0;
    bool passedAcceptance = false;
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
    CalibrationFitResult FitChessboard(const std::vector<cv::Mat>& images,
        cv::Size innerCorners, double squareSize,
        const CalibrationModel& baseModel = CalibrationModel{},
        double rmsAcceptance = 0.5, double maxAcceptance = 1.0);

    nlohmann::json ToJson(const CalibrationModel& model);
    bool FromJson(const nlohmann::json& json, CalibrationModel& model);
    bool Save(const char* path, const CalibrationModel& model);
    bool Load(const char* path, CalibrationModel& model);
    bool SaveDocument(const char* path, const CalibrationModel& model,
        const std::vector<CalibrationSample>& samples);
    bool LoadDocument(const char* path, CalibrationModel& model,
        std::vector<CalibrationSample>& samples);
    bool SaveAcceptanceReport(const char* path, const CalibrationModel& model,
        std::size_t totalImages, std::size_t successfulImages,
        const std::vector<double>& imageErrors, double rmsError, double maxError,
        double rmsAcceptance, double maxAcceptance);
}
