#define NOMINMAX
#include "GeometryDrawTool.h"

#include "../Core/VisionContext.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgproc.hpp>

namespace
{
cv::Point ToPoint(const cv::Point2f& point)
{
    return cv::Point(cvRound(point.x), cvRound(point.y));
}

cv::Rect Bounds(const std::vector<cv::Point2f>& points)
{
    if (points.empty())
        return {};
    float minX = points[0].x;
    float maxX = points[0].x;
    float minY = points[0].y;
    float maxY = points[0].y;
    for (const cv::Point2f& point : points)
    {
        minX = (std::min)(minX, point.x);
        maxX = (std::max)(maxX, point.x);
        minY = (std::min)(minY, point.y);
        maxY = (std::max)(maxY, point.y);
    }
    const int left = static_cast<int>(std::floor(minX));
    const int top = static_cast<int>(std::floor(minY));
    const int right = static_cast<int>(std::ceil(maxX));
    const int bottom = static_cast<int>(std::ceil(maxY));
    return cv::Rect(left, top, (std::max)(0, right - left),
                    (std::max)(0, bottom - top));
}

std::vector<cv::Point> IntegerPoints(const std::vector<cv::Point2f>& points)
{
    std::vector<cv::Point> result;
    result.reserve(points.size());
    for (const cv::Point2f& point : points)
        result.push_back(ToPoint(point));
    return result;
}

void AddRegion(ToolResult& result, const GeometryPrimitive& primitive)
{
    ToolResult::Region region;
    region.label = primitive.name;
    region.score = 1.0f;
    std::vector<cv::Point2f> contour;
    if (primitive.type == GeometryPrimitiveType::Rectangle && primitive.points.size() >= 2)
    {
        const float left = (std::min)(primitive.points[0].x, primitive.points[1].x);
        const float right = (std::max)(primitive.points[0].x, primitive.points[1].x);
        const float top = (std::min)(primitive.points[0].y, primitive.points[1].y);
        const float bottom = (std::max)(primitive.points[0].y, primitive.points[1].y);
        contour = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
        region.area = (right - left) * (bottom - top);
        region.center = {(left + right) * 0.5f, (top + bottom) * 0.5f};
        region.width = right - left;
        region.height = bottom - top;
    }
    else if (primitive.type == GeometryPrimitiveType::RotatedRectangle)
    {
        contour = GeometryPrimitiveRotatedCorners(primitive);
        if (contour.size() == 4)
        {
            region.center = GeometryPrimitiveCenter(primitive);
            region.width = std::abs(primitive.points[1].x - primitive.points[0].x);
            region.height = std::abs(primitive.points[1].y - primitive.points[0].y);
            region.area = region.width * region.height;
            region.angle = primitive.angle;
        }
    }
    else if (primitive.type == GeometryPrimitiveType::Circle && primitive.points.size() >= 2)
    {
        const cv::Point2f center = primitive.points[0];
        const float radius = static_cast<float>(cv::norm(primitive.points[1] - center));
        for (int angle = 0; angle < 360; angle += 10)
        {
            const float radians = angle * static_cast<float>(CV_PI / 180.0);
            contour.emplace_back(center.x + radius * std::cos(radians),
                                 center.y + radius * std::sin(radians));
        }
        region.center = center;
        region.width = radius * 2.0f;
        region.height = radius * 2.0f;
        region.area = static_cast<float>(CV_PI * radius * radius);
    }
    else if (primitive.type == GeometryPrimitiveType::Polygon && primitive.points.size() >= 3)
    {
        contour = primitive.points;
        region.center = GeometryPrimitiveCenter(primitive);
        region.area = static_cast<float>(std::abs(cv::contourArea(contour)));
    }
    else if (primitive.type == GeometryPrimitiveType::Cross && !primitive.points.empty())
    {
        const cv::Point2f center = primitive.points[0];
        const float radius = static_cast<float>(primitive.crossSize);
        contour = {{center.x - radius, center.y - radius},
                   {center.x + radius, center.y - radius},
                   {center.x + radius, center.y + radius},
                   {center.x - radius, center.y + radius}};
        region.center = center;
    }
    if (contour.empty())
        return;
    region.contour = IntegerPoints(contour);
    region.bbox = Bounds(contour);
    result.regions.push_back(std::move(region));
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                    static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), count);
    return result;
}

bool DrawUtf8Text(cv::Mat& image, const GeometryPrimitive& primitive)
{
    if (primitive.points.empty() || primitive.text.empty() || image.type() != CV_8UC3)
        return false;
    const std::wstring text = Utf8ToWide(primitive.text);
    if (text.empty())
        return false;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.cols;
    info.bmiHeader.biHeight = -image.rows;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return false;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits)
    {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(dc);
        return false;
    }
    cv::Mat dib(image.rows, image.cols, CV_8UC4, bits);
    cv::cvtColor(image, dib, cv::COLOR_BGR2BGRA);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    HFONT font = CreateFontW(-primitive.fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(primitive.color[0], primitive.color[1], primitive.color[2]));
    const cv::Point origin = ToPoint(primitive.points[0]);
    const BOOL drawn = TextOutW(dc, origin.x, origin.y, text.data(),
                                static_cast<int>(text.size()));
    if (oldFont) SelectObject(dc, oldFont);
    if (font) DeleteObject(font);
    SelectObject(dc, oldBitmap);
    cv::cvtColor(dib, image, cv::COLOR_BGRA2BGR);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return drawn != FALSE;
}

bool DrawOpaque(cv::Mat& image, const GeometryPrimitive& item)
{
    const cv::Scalar color(item.color[2], item.color[1], item.color[0]);
    const int thickness = item.filled ? cv::FILLED : item.thickness;
    switch (item.type)
    {
    case GeometryPrimitiveType::Line:
        if (item.points.size() < 2) return false;
        cv::line(image, ToPoint(item.points[0]), ToPoint(item.points[1]), color,
                 item.thickness, cv::LINE_AA);
        return true;
    case GeometryPrimitiveType::Rectangle:
        if (item.points.size() < 2) return false;
        cv::rectangle(image, ToPoint(item.points[0]), ToPoint(item.points[1]), color,
                      thickness, cv::LINE_AA);
        return true;
    case GeometryPrimitiveType::RotatedRectangle:
    {
        const std::vector<cv::Point2f> cornersF = GeometryPrimitiveRotatedCorners(item);
        if (cornersF.size() != 4) return false;
        const std::vector<cv::Point> corners = IntegerPoints(cornersF);
        if (item.filled)
            cv::fillConvexPoly(image, corners, color, cv::LINE_AA);
        else
            cv::polylines(image, corners, true, color, item.thickness, cv::LINE_AA);
        return true;
    }
    case GeometryPrimitiveType::Circle:
        if (item.points.size() < 2) return false;
        cv::circle(image, ToPoint(item.points[0]),
                   (std::max)(1, cvRound(cv::norm(item.points[1] - item.points[0]))),
                   color, thickness, cv::LINE_AA);
        return true;
    case GeometryPrimitiveType::Arrow:
        if (item.points.size() < 2) return false;
        cv::arrowedLine(image, ToPoint(item.points[0]), ToPoint(item.points[1]), color,
                        item.thickness, cv::LINE_AA, 0, item.arrowTipLength);
        return true;
    case GeometryPrimitiveType::Cross:
        if (item.points.empty()) return false;
        cv::drawMarker(image, ToPoint(item.points[0]), color, cv::MARKER_CROSS,
                       item.crossSize * 2, item.thickness, cv::LINE_AA);
        return true;
    case GeometryPrimitiveType::Text:
        return DrawUtf8Text(image, item);
    case GeometryPrimitiveType::Polygon:
    {
        if (item.points.size() < 3) return false;
        const std::vector<cv::Point> points = IntegerPoints(item.points);
        if (item.filled)
            cv::fillPoly(image, std::vector<std::vector<cv::Point>>{points}, color, cv::LINE_AA);
        else
            cv::polylines(image, points, true, color, item.thickness, cv::LINE_AA);
        return true;
    }
    }
    return false;
}

cv::Mat ToBgr(const cv::Mat& source)
{
    if (source.empty())
        return {};
    cv::Mat image;
    if (source.depth() != CV_8U)
        source.convertTo(image, CV_8U);
    else
        image = source;
    cv::Mat bgr;
    if (image.channels() == 1)
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
    else if (image.channels() == 4)
        cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
    else if (image.channels() == 3)
        bgr = image.clone();
    return bgr;
}
}

bool GeometryDrawTool::DrawPrimitive(cv::Mat& image, const GeometryPrimitive& primitive)
{
    if (!primitive.visible || primitive.color[3] <= 0 || image.empty())
        return false;
    GeometryPrimitive normalized = primitive;
    NormalizeGeometryPrimitive(normalized);
    if (normalized.color[3] >= 255)
        return DrawOpaque(image, normalized);
    cv::Mat overlay = image.clone();
    if (!DrawOpaque(overlay, normalized))
        return false;
    const double alpha = normalized.color[3] / 255.0;
    cv::addWeighted(overlay, alpha, image, 1.0 - alpha, 0.0, image);
    return true;
}

ToolResult GeometryDrawTool::Execute(VisionContext& ctx)
{
    ToolResult result;
    result.toolName = GetName();
    if (ctx.image.empty())
    {
        result.success = false;
        result.message = "请先加载图片";
        return result;
    }
    result.debugImage = ToBgr(ctx.image);
    if (result.debugImage.empty())
    {
        result.success = false;
        result.message = "不支持的图像格式";
        return result;
    }

    int rendered = 0;
    for (GeometryPrimitive primitive : primitives)
    {
        NormalizeGeometryPrimitive(primitive);
        if (!DrawPrimitive(result.debugImage, primitive))
            continue;
        ++rendered;
        if (primitive.type == GeometryPrimitiveType::Line ||
            primitive.type == GeometryPrimitiveType::Arrow)
        {
            ToolResult::Line line;
            line.p1 = ToPoint(primitive.points[0]);
            line.p2 = ToPoint(primitive.points[1]);
            const float dx = static_cast<float>(line.p2.x - line.p1.x);
            const float dy = static_cast<float>(line.p2.y - line.p1.y);
            line.length = std::hypot(dx, dy);
            line.angle = static_cast<float>(std::atan2(dy, dx) * 180.0 / CV_PI);
            result.lines.push_back(line);
        }
        else if (primitive.type == GeometryPrimitiveType::Text && !primitive.points.empty())
        {
            ToolResult::TextItem text;
            text.text = primitive.text;
            text.box = cv::Rect(ToPoint(primitive.points[0]),
                cv::Size((std::max)(primitive.fontSize,
                    static_cast<int>(primitive.text.size()) * primitive.fontSize / 2),
                    primitive.fontSize));
            text.confidence = 1.0f;
            result.texts.push_back(std::move(text));
        }
        else
        {
            AddRegion(result, primitive);
        }
    }
    result.success = true;
    result.message = "OK";
    result.measurements.push_back({"geometryRendered", static_cast<double>(rendered), "count"});
    return result;
}

nlohmann::json GeometryDrawTool::Save() const
{
    nlohmann::json items = nlohmann::json::array();
    for (const GeometryPrimitive& primitive : primitives)
        items.push_back(GeometryPrimitiveToJson(primitive));
    return {{"type", GetType()}, {"primitives", std::move(items)}};
}

void GeometryDrawTool::Load(const nlohmann::json& json)
{
    primitives.clear();
    if (!json.contains("primitives") || !json["primitives"].is_array())
        return;
    for (const auto& item : json["primitives"])
    {
        GeometryPrimitive primitive;
        if (GeometryPrimitiveFromJson(item, primitive))
            primitives.push_back(std::move(primitive));
    }
}
