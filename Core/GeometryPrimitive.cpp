#include "GeometryPrimitive.h"

#include <algorithm>
#include <cmath>

const char* GeometryPrimitiveTypeName(GeometryPrimitiveType type)
{
    switch (type)
    {
    case GeometryPrimitiveType::Line: return "直线";
    case GeometryPrimitiveType::Rectangle: return "矩形";
    case GeometryPrimitiveType::RotatedRectangle: return "旋转矩形";
    case GeometryPrimitiveType::Circle: return "圆";
    case GeometryPrimitiveType::Arrow: return "箭头";
    case GeometryPrimitiveType::Cross: return "十字标记";
    case GeometryPrimitiveType::Text: return "文字";
    case GeometryPrimitiveType::Polygon: return "多边形";
    default: return "图形";
    }
}

void NormalizeGeometryPrimitive(GeometryPrimitive& primitive)
{
    const int type = std::clamp(static_cast<int>(primitive.type), 0, 7);
    primitive.type = static_cast<GeometryPrimitiveType>(type);
    if (primitive.name.empty())
        primitive.name = GeometryPrimitiveTypeName(primitive.type);
    for (int& channel : primitive.color)
        channel = std::clamp(channel, 0, 255);
    primitive.thickness = std::clamp(primitive.thickness, 1, 64);
    if (!std::isfinite(primitive.angle))
        primitive.angle = 0.0f;
    while (primitive.angle > 180.0f) primitive.angle -= 360.0f;
    while (primitive.angle <= -180.0f) primitive.angle += 360.0f;
    primitive.arrowTipLength = std::clamp(primitive.arrowTipLength, 0.01f, 0.95f);
    primitive.crossSize = std::clamp(primitive.crossSize, 1, 4096);
    primitive.fontSize = std::clamp(primitive.fontSize, 6, 512);
}

nlohmann::json GeometryPrimitiveToJson(const GeometryPrimitive& primitive)
{
    nlohmann::json points = nlohmann::json::array();
    for (const cv::Point2f& point : primitive.points)
        points.push_back({point.x, point.y});
    return {
        {"type", static_cast<int>(primitive.type)}, {"name", primitive.name},
        {"visible", primitive.visible}, {"color", primitive.color},
        {"thickness", primitive.thickness}, {"filled", primitive.filled},
        {"angle", primitive.angle}, {"arrowTipLength", primitive.arrowTipLength},
        {"crossSize", primitive.crossSize}, {"text", primitive.text},
        {"fontSize", primitive.fontSize}, {"points", std::move(points)}};
}

bool GeometryPrimitiveFromJson(const nlohmann::json& json, GeometryPrimitive& primitive)
{
    if (!json.is_object())
        return false;
    GeometryPrimitive value;
    value.type = static_cast<GeometryPrimitiveType>(json.value("type", 0));
    value.name = json.value("name", std::string());
    value.visible = json.value("visible", true);
    value.thickness = json.value("thickness", 2);
    value.filled = json.value("filled", false);
    value.angle = json.value("angle", 0.0f);
    value.arrowTipLength = json.value("arrowTipLength", 0.2f);
    value.crossSize = json.value("crossSize", 16);
    value.text = json.value("text", std::string("Text"));
    value.fontSize = json.value("fontSize", 24);
    if (json.contains("color") && json["color"].is_array())
    {
        const std::size_t count = (std::min)(json["color"].size(), value.color.size());
        for (std::size_t index = 0; index < count; ++index)
            if (json["color"][index].is_number_integer())
                value.color[index] = json["color"][index].get<int>();
    }
    if (json.contains("points") && json["points"].is_array())
    {
        for (const auto& point : json["points"])
            if (point.is_array() && point.size() >= 2 &&
                point[0].is_number() && point[1].is_number())
                value.points.emplace_back(point[0].get<float>(), point[1].get<float>());
    }
    NormalizeGeometryPrimitive(value);
    primitive = std::move(value);
    return true;
}

cv::Point2f GeometryPrimitiveCenter(const GeometryPrimitive& primitive)
{
    if (primitive.points.empty())
        return {};
    if (primitive.type != GeometryPrimitiveType::Polygon && primitive.points.size() >= 2)
        return (primitive.points[0] + primitive.points[1]) * 0.5f;
    cv::Point2f center;
    for (const cv::Point2f& point : primitive.points)
        center += point;
    return center * (1.0f / static_cast<float>(primitive.points.size()));
}

std::vector<cv::Point2f> GeometryPrimitiveRotatedCorners(const GeometryPrimitive& primitive)
{
    if (primitive.type != GeometryPrimitiveType::RotatedRectangle ||
        primitive.points.size() < 2)
        return {};
    const cv::Point2f center = GeometryPrimitiveCenter(primitive);
    const cv::Size2f size(std::abs(primitive.points[1].x - primitive.points[0].x),
                          std::abs(primitive.points[1].y - primitive.points[0].y));
    if (size.width < 1.0f || size.height < 1.0f)
        return {};
    cv::Point2f corners[4];
    cv::RotatedRect(center, size, primitive.angle).points(corners);
    return std::vector<cv::Point2f>(corners, corners + 4);
}

void TranslateGeometryPrimitive(GeometryPrimitive& primitive, const cv::Point2f& delta)
{
    for (cv::Point2f& point : primitive.points)
        point += delta;
}
