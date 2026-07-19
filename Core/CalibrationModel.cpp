#include "CalibrationModel.h"

#include <opencv2/geometry/3d.hpp>

#include <cmath>
#include <vector>

bool CalibrationModel::HasValidScale() const
{
    return std::isfinite(scaleX) && std::isfinite(scaleY) && scaleX > 0.0 && scaleY > 0.0;
}

bool CalibrationModel::HasValidCamera() const
{
    return std::isfinite(fx) && std::isfinite(fy) && fx > 0.0 && fy > 0.0;
}

bool CalibrationModel::HasValidHomography() const
{
    const double determinant = cv::determinant(cv::Mat(pixelToWorldHomography));
    return std::isfinite(determinant) && std::abs(determinant) > 1.0e-12;
}

cv::Point2d CalibrationModel::UndistortPixel(cv::Point2d pixel) const
{
    if (!distortionEnabled || !HasValidCamera())
        return pixel;

    const cv::Matx33d cameraMatrix(
        fx, 0.0, cx,
        0.0, fy, cy,
        0.0, 0.0, 1.0);
    const cv::Vec<double, 5> coefficients(k1, k2, p1, p2, k3);
    std::vector<cv::Point2d> source = {pixel};
    std::vector<cv::Point2d> destination;
    cv::undistortPoints(source, destination, cameraMatrix, coefficients,
        cv::noArray(), cameraMatrix);
    return destination.empty() ? pixel : destination.front();
}

cv::Point2d CalibrationModel::PixelToWorld(cv::Point2d pixel) const
{
    pixel = UndistortPixel(pixel);
    if (!enabled)
        return pixel;

    if (homographyEnabled && HasValidHomography())
    {
        const cv::Vec3d projected = pixelToWorldHomography * cv::Vec3d(pixel.x, pixel.y, 1.0);
        if (std::isfinite(projected[2]) && std::abs(projected[2]) > 1.0e-12)
            return {projected[0] / projected[2], projected[1] / projected[2]};
    }

    if (!HasValidScale())
        return pixel;
    return {
        worldOrigin.x + (pixel.x - pixelOrigin.x) * scaleX,
        worldOrigin.y + (pixel.y - pixelOrigin.y) * scaleY,
    };
}

double CalibrationModel::Distance(cv::Point2d first, cv::Point2d second) const
{
    const cv::Point2d worldFirst = PixelToWorld(first);
    const cv::Point2d worldSecond = PixelToWorld(second);
    return cv::norm(worldSecond - worldFirst);
}
