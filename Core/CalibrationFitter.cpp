#include "CalibrationFitter.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <cctype>

namespace
{
CalibrationFitResult BuildFitResult(const CalibrationModel& model,
    const std::vector<CalibrationSample>& samples)
{
    CalibrationFitResult result;
    result.model = model;
    if (samples.empty())
    {
        result.message = "至少需要一个标定点";
        return result;
    }

    double squareSum = 0.0;
    for (const CalibrationSample& sample : samples)
    {
        const cv::Point2d error = model.PixelToWorld(sample.pixel) - sample.world;
        const double distance = cv::norm(error);
        result.residuals.push_back(distance);
        squareSum += distance * distance;
        result.maxError = (std::max)(result.maxError, distance);
    }
    result.rmsError = std::sqrt(squareSum / samples.size());
    result.success = std::isfinite(result.rmsError);
    result.message = result.success ? "标定拟合完成" : "标定残差无效";
    return result;
}

bool IsFiniteMatrix(const cv::Mat& matrix)
{
    if (matrix.empty() || matrix.rows != 3 || matrix.cols != 3)
        return false;
    for (int row = 0; row < matrix.rows; ++row)
        for (int column = 0; column < matrix.cols; ++column)
            if (!std::isfinite(matrix.at<double>(row, column)))
                return false;
    return true;
}
}

namespace CalibrationFitter
{
CalibrationFitResult FitScale(const std::vector<CalibrationSample>& samples)
{
    CalibrationFitResult result;
    if (samples.size() < 2)
    {
        result.message = "X/Y 比例标定至少需要两个点";
        return result;
    }

    cv::Point2d pixelMean;
    cv::Point2d worldMean;
    for (const auto& sample : samples)
    {
        pixelMean += sample.pixel;
        worldMean += sample.world;
    }
    pixelMean *= 1.0 / samples.size();
    worldMean *= 1.0 / samples.size();

    double pixelVarianceX = 0.0;
    double pixelVarianceY = 0.0;
    double covarianceX = 0.0;
    double covarianceY = 0.0;
    for (const auto& sample : samples)
    {
        const cv::Point2d pixel = sample.pixel - pixelMean;
        const cv::Point2d world = sample.world - worldMean;
        pixelVarianceX += pixel.x * pixel.x;
        pixelVarianceY += pixel.y * pixel.y;
        covarianceX += pixel.x * world.x;
        covarianceY += pixel.y * world.y;
    }
    if (pixelVarianceX <= 1.0e-12 || pixelVarianceY <= 1.0e-12)
    {
        result.message = "标定点缺少 X 或 Y 方向变化";
        return result;
    }

    CalibrationModel model;
    model.enabled = true;
    model.scaleX = covarianceX / pixelVarianceX;
    model.scaleY = covarianceY / pixelVarianceY;
    model.pixelOrigin = pixelMean;
    model.worldOrigin = worldMean;
    if (!model.HasValidScale())
    {
        result.message = "拟合出的 X/Y 比例无效";
        return result;
    }
    return BuildFitResult(model, samples);
}

CalibrationFitResult FitChessboard(const std::vector<cv::Mat>& images,
    cv::Size innerCorners, double squareSize, const CalibrationModel& baseModel,
    double rmsAcceptance, double maxAcceptance)
{
    CalibrationFitResult result;
    result.totalImageCount = images.size();
    if (innerCorners.width < 2 || innerCorners.height < 2 || squareSize <= 0.0)
    {
        result.message = "棋盘格参数无效";
        return result;
    }

    std::vector<cv::Point3f> objectTemplate;
    objectTemplate.reserve(static_cast<std::size_t>(innerCorners.area()));
    for (int row = 0; row < innerCorners.height; ++row)
        for (int column = 0; column < innerCorners.width; ++column)
            objectTemplate.emplace_back(static_cast<float>(column * squareSize),
                static_cast<float>(row * squareSize), 0.0f);

    std::vector<std::vector<cv::Point2f>> imagePoints;
    std::vector<std::vector<cv::Point3f>> objectPoints;
    cv::Size imageSize;
    for (const cv::Mat& image : images)
    {
        if (image.empty())
            continue;
        if (imageSize.empty())
            imageSize = image.size();
        if (image.size() != imageSize)
            continue;
        cv::Mat gray;
        if (image.channels() == 1)
            gray = image;
        else
            cv::cvtColor(image, gray,
                image.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        std::vector<cv::Point2f> corners;
        const bool found = cv::findChessboardCorners(gray, innerCorners, corners,
            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE |
            cv::CALIB_CB_FAST_CHECK);
        if (!found)
            continue;
        cv::cornerSubPix(gray, corners, cv::Size(5, 5), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::COUNT,
                40, 0.001));
        imagePoints.push_back(std::move(corners));
        objectPoints.push_back(objectTemplate);
    }
    if (imagePoints.size() < 3)
    {
        result.successfulImageCount = imagePoints.size();
        result.message = "至少需要 3 张成功识别棋盘格的图像；当前成功 " +
            std::to_string(imagePoints.size()) + " 张";
        return result;
    }

    cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat distortion = cv::Mat::zeros(1, 5, CV_64F);
    std::vector<cv::Mat> rotations;
    std::vector<cv::Mat> translations;
    const double rms = cv::calibrateCamera(objectPoints, imagePoints, imageSize,
        cameraMatrix, distortion, rotations, translations);
    if (!std::isfinite(rms))
    {
        result.message = "棋盘格标定求解失败";
        return result;
    }

    result.model = baseModel;
    result.model.distortionEnabled = true;
    result.model.fx = cameraMatrix.at<double>(0, 0);
    result.model.fy = cameraMatrix.at<double>(1, 1);
    result.model.cx = cameraMatrix.at<double>(0, 2);
    result.model.cy = cameraMatrix.at<double>(1, 2);
    result.model.k1 = distortion.at<double>(0, 0);
    result.model.k2 = distortion.at<double>(0, 1);
    result.model.p1 = distortion.at<double>(0, 2);
    result.model.p2 = distortion.at<double>(0, 3);
    result.model.k3 = distortion.total() > 4 ? distortion.at<double>(0, 4) : 0.0;
    result.rmsError = rms;
    result.maxError = 0.0;
    for (std::size_t index = 0; index < imagePoints.size(); ++index)
    {
        std::vector<cv::Point2f> projected;
        cv::projectPoints(objectPoints[index], rotations[index], translations[index],
            cameraMatrix, distortion, projected);
        const double error = cv::norm(imagePoints[index], projected, cv::NORM_L2) /
            std::sqrt(static_cast<double>(projected.size()));
        result.residuals.push_back(error);
        result.maxError = (std::max)(result.maxError, error);
    }
    result.successfulImageCount = imagePoints.size();
    if (!result.residuals.empty())
    {
        result.meanError = std::accumulate(result.residuals.begin(),
            result.residuals.end(), 0.0) / result.residuals.size();
    }
    result.passedAcceptance = result.rmsError <= (std::max)(0.0, rmsAcceptance) &&
        result.maxError <= (std::max)(0.0, maxAcceptance);
    result.success = true;
    result.message = "棋盘格标定完成：成功 " + std::to_string(imagePoints.size()) +
        "/" + std::to_string(images.size()) + " 张";
    return result;
}

CalibrationFitResult FitHomography(const std::vector<CalibrationSample>& samples,
    double reprojectionThreshold)
{
    CalibrationFitResult result;
    if (samples.size() < 4)
    {
        result.message = "透视标定至少需要四个点";
        return result;
    }

    std::vector<cv::Point2f> pixelPoints;
    std::vector<cv::Point2f> worldPoints;
    pixelPoints.reserve(samples.size());
    worldPoints.reserve(samples.size());
    for (const auto& sample : samples)
    {
        pixelPoints.emplace_back(static_cast<float>(sample.pixel.x), static_cast<float>(sample.pixel.y));
        worldPoints.emplace_back(static_cast<float>(sample.world.x), static_cast<float>(sample.world.y));
    }
    cv::Mat homography = cv::findHomography(pixelPoints, worldPoints, cv::RANSAC,
        (std::max)(0.0, reprojectionThreshold));
    if (homography.empty())
    {
        result.message = "透视矩阵拟合失败";
        return result;
    }
    homography.convertTo(homography, CV_64F);
    if (!IsFiniteMatrix(homography))
    {
        result.message = "透视矩阵包含无效数值";
        return result;
    }

    CalibrationModel model;
    model.enabled = true;
    model.homographyEnabled = true;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            model.pixelToWorldHomography(row, column) = homography.at<double>(row, column);
    return BuildFitResult(model, samples);
}

CalibrationFitResult Evaluate(const CalibrationModel& model,
    const std::vector<CalibrationSample>& samples)
{
    return BuildFitResult(model, samples);
}

nlohmann::json ToJson(const CalibrationModel& model)
{
    std::vector<double> homography(model.pixelToWorldHomography.val,
        model.pixelToWorldHomography.val + 9);
    return {
        {"enabled", model.enabled},
        {"scaleX", model.scaleX}, {"scaleY", model.scaleY},
        {"pixelOriginX", model.pixelOrigin.x}, {"pixelOriginY", model.pixelOrigin.y},
        {"worldOriginX", model.worldOrigin.x}, {"worldOriginY", model.worldOrigin.y},
        {"homographyEnabled", model.homographyEnabled}, {"homography", homography},
        {"distortionEnabled", model.distortionEnabled},
        {"fx", model.fx}, {"fy", model.fy}, {"cx", model.cx}, {"cy", model.cy},
        {"k1", model.k1}, {"k2", model.k2}, {"p1", model.p1}, {"p2", model.p2}, {"k3", model.k3}
    };
}

bool FromJson(const nlohmann::json& json, CalibrationModel& model)
{
    if (!json.is_object())
        return false;
    model.enabled = json.value("enabled", false);
    model.scaleX = json.value("scaleX", 1.0);
    model.scaleY = json.value("scaleY", 1.0);
    model.pixelOrigin.x = json.value("pixelOriginX", 0.0);
    model.pixelOrigin.y = json.value("pixelOriginY", 0.0);
    model.worldOrigin.x = json.value("worldOriginX", 0.0);
    model.worldOrigin.y = json.value("worldOriginY", 0.0);
    model.homographyEnabled = json.value("homographyEnabled", false);
    const auto homography = json.value("homography", std::vector<double>());
    if (homography.size() == 9)
        std::copy(homography.begin(), homography.end(), model.pixelToWorldHomography.val);
    model.distortionEnabled = json.value("distortionEnabled", false);
    model.fx = json.value("fx", 1.0); model.fy = json.value("fy", 1.0);
    model.cx = json.value("cx", 0.0); model.cy = json.value("cy", 0.0);
    model.k1 = json.value("k1", 0.0); model.k2 = json.value("k2", 0.0);
    model.p1 = json.value("p1", 0.0); model.p2 = json.value("p2", 0.0);
    model.k3 = json.value("k3", 0.0);
    return model.HasValidScale() || (model.homographyEnabled && model.HasValidHomography());
}

bool Save(const char* path, const CalibrationModel& model)
{
    if (!path || !path[0])
        return false;
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;
    file << ToJson(model).dump(2);
    return static_cast<bool>(file);
}

bool Load(const char* path, CalibrationModel& model)
{
    if (!path || !path[0])
        return false;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    nlohmann::json json;
    try
    {
        file >> json;
    }
    catch (...)
    {
        return false;
    }
    return FromJson(json, model);
}

bool SaveDocument(const char* path, const CalibrationModel& model,
    const std::vector<CalibrationSample>& samples)
{
    if (!path || !path[0])
        return false;
    nlohmann::json sampleValues = nlohmann::json::array();
    for (const CalibrationSample& sample : samples)
    {
        sampleValues.push_back({
            {"pixelX", sample.pixel.x}, {"pixelY", sample.pixel.y},
            {"worldX", sample.world.x}, {"worldY", sample.world.y}
        });
    }
    const nlohmann::json document = {
        {"kind", "vision_calibration"}, {"version", 1},
        {"model", ToJson(model)}, {"samples", std::move(sampleValues)}
    };
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;
    file << document.dump(2);
    return static_cast<bool>(file);
}

bool LoadDocument(const char* path, CalibrationModel& model,
    std::vector<CalibrationSample>& samples)
{
    if (!path || !path[0])
        return false;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    nlohmann::json document;
    try
    {
        file >> document;
    }
    catch (...)
    {
        return false;
    }

    // Model-only files from the earlier API remain load-compatible.
    const nlohmann::json& modelJson = document.contains("model")
        ? document["model"] : document;
    CalibrationModel loadedModel;
    if (!FromJson(modelJson, loadedModel))
        return false;

    std::vector<CalibrationSample> loadedSamples;
    if (document.contains("samples") && document["samples"].is_array())
    {
        for (const auto& value : document["samples"])
        {
            CalibrationSample sample;
            sample.pixel.x = value.value("pixelX", 0.0);
            sample.pixel.y = value.value("pixelY", 0.0);
            sample.world.x = value.value("worldX", 0.0);
            sample.world.y = value.value("worldY", 0.0);
            loadedSamples.push_back(sample);
        }
    }
    model = loadedModel;
    samples = std::move(loadedSamples);
    return true;
}

bool SaveAcceptanceReport(const char* path, const CalibrationModel& model,
    std::size_t totalImages, std::size_t successfulImages,
    const std::vector<double>& imageErrors, double rmsError, double maxError,
    double rmsAcceptance, double maxAcceptance)
{
    if (!path || !*path)
        return false;
    const bool passed = successfulImages >= 3 && rmsError <= rmsAcceptance &&
        maxError <= maxAcceptance;
    std::string filePath(path);
    std::string extension;
    const std::size_t dot = filePath.find_last_of('.');
    if (dot != std::string::npos)
    {
        extension = filePath.substr(dot);
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output << std::setprecision(10);
    if (extension == ".csv")
    {
        output << "metric,value,unit\n"
               << "acceptance," << (passed ? "PASS" : "FAIL") << ",\n"
               << "total_images," << totalImages << ",count\n"
               << "successful_images," << successfulImages << ",count\n"
               << "rms_error," << rmsError << ",px\n"
               << "maximum_error," << maxError << ",px\n"
               << "rms_limit," << rmsAcceptance << ",px\n"
               << "maximum_limit," << maxAcceptance << ",px\n"
               << "fx," << model.fx << ",px\n"
               << "fy," << model.fy << ",px\n"
               << "cx," << model.cx << ",px\n"
               << "cy," << model.cy << ",px\n"
               << "k1," << model.k1 << ",\n"
               << "k2," << model.k2 << ",\n"
               << "p1," << model.p1 << ",\n"
               << "p2," << model.p2 << ",\n"
               << "k3," << model.k3 << ",\n";
        for (std::size_t index = 0; index < imageErrors.size(); ++index)
            output << "image_" << (index + 1) << "_error," << imageErrors[index]
                   << ",px\n";
    }
    else
    {
        nlohmann::json report = {
            {"acceptance", passed ? "PASS" : "FAIL"},
            {"totalImages", totalImages}, {"successfulImages", successfulImages},
            {"rmsErrorPixels", rmsError}, {"maximumErrorPixels", maxError},
            {"rmsAcceptancePixels", rmsAcceptance},
            {"maximumAcceptancePixels", maxAcceptance},
            {"imageErrorsPixels", imageErrors}, {"calibration", ToJson(model)}
        };
        output << report.dump(2);
    }
    return output.good();
}
}
