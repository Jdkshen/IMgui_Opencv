#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/core/types.hpp>

enum class GeometryPrimitiveType
{
    Line = 0,
    Rectangle = 1,
    RotatedRectangle = 2,
    Circle = 3,
    Arrow = 4,
    Cross = 5,
    Text = 6,
    Polygon = 7
};

struct GeometryPrimitive
{
    GeometryPrimitiveType type = GeometryPrimitiveType::Line;
    std::string name;
    bool visible = true;
    std::array<int, 4> color = {0, 255, 0, 255}; // RGBA
    int thickness = 2;
    bool filled = false;
    float angle = 0.0f;
    float arrowTipLength = 0.2f;
    int crossSize = 16;
    std::string text = "Text";
    int fontSize = 24;
    std::vector<cv::Point2f> points;
};

const char* GeometryPrimitiveTypeName(GeometryPrimitiveType type);
void NormalizeGeometryPrimitive(GeometryPrimitive& primitive);
nlohmann::json GeometryPrimitiveToJson(const GeometryPrimitive& primitive);
bool GeometryPrimitiveFromJson(const nlohmann::json& json, GeometryPrimitive& primitive);
cv::Point2f GeometryPrimitiveCenter(const GeometryPrimitive& primitive);
std::vector<cv::Point2f> GeometryPrimitiveRotatedCorners(const GeometryPrimitive& primitive);
void TranslateGeometryPrimitive(GeometryPrimitive& primitive, const cv::Point2f& delta);
