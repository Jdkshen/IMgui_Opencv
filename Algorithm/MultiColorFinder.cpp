#define NOMINMAX
#include "MultiColorFinder.h"
#include "../Log/LogSystem.h"
#include "../Core/ImageUtils.h"
#include "../Core/VisionContext.h"
#include <chrono>
#include <algorithm>
#include <cmath>

extern ImVec4 color;

float g_McfLastTimeMs = 0;
int   g_McfLastCount  = 0;

// ---- 实时预处理预览 ----
extern cv::Mat& gImage;
extern cv::Mat& gPendingUpload;
extern bool& gNeedUpload;

void McfApplyPreview(bool useGray, bool useBinary, int binThresh, const cv::Mat& src)
{
    if (src.empty()) return;
    cv::Mat out = src.clone();
    if (useGray && out.channels() > 1)
        cv::cvtColor(out, out, cv::COLOR_BGR2GRAY);
    if (useBinary)
    {
        if (out.channels() > 1) cv::cvtColor(out, out, cv::COLOR_BGR2GRAY);
        cv::threshold(out, out, binThresh, 255, cv::THRESH_BINARY);
    }
    cv::Mat rgba;
    SafeConvertToRGBA(out, rgba);
    if (!rgba.empty()) { gPendingUpload = rgba; gNeedUpload = true; }
}

static bool IsColorMatch(const uint8_t* pixel, int channels, const ColorPoint& pt)
{
    int db = (int)pixel[0] - pt.b;
    int dg = (int)pixel[1] - pt.g;
    int dr = (int)pixel[2] - pt.r;
    int t = pt.tolerance;
    return db >= -t && db <= t && dg >= -t && dg <= t && dr >= -t && dr <= t;
}

ToolResult MultiColorFinder::Execute(VisionContext& ctx)
{
    ToolResult result;
    result.toolName = "多点找色";

    if (points.empty())
    {
        result.success = false;
        result.message = "请至少添加1个颜色点";
        LogSystem::Add(LOG_WARN, "多点找色: 颜色点为空");
        return result;
    }

    cv::Mat src = ctx.image;
    if (src.empty())
    {
        result.success = false;
        result.message = "请先加载图片";
        return result;
    }

    // ---- 主图 + 参考图预处理（灰度/二值化） ----
    cv::Mat refProc = refImage.clone();
    if (imgUseGray && src.channels() > 1)
    {
        cv::cvtColor(src, src, cv::COLOR_BGR2GRAY);
        if (!refProc.empty() && refProc.channels() > 1)
            cv::cvtColor(refProc, refProc, cv::COLOR_BGR2GRAY);
    }
    if (imgUseBinary)
    {
        if (src.channels() > 1) cv::cvtColor(src, src, cv::COLOR_BGR2GRAY);
        cv::threshold(src, src, imgBinThresh, 255, cv::THRESH_BINARY);
        if (!refProc.empty())
        {
            if (refProc.channels() > 1) cv::cvtColor(refProc, refProc, cv::COLOR_BGR2GRAY);
            cv::threshold(refProc, refProc, imgBinThresh, 255, cv::THRESH_BINARY);
        }
    }

    // 从预处理后的参考图重取颜色点值
    std::vector<ColorPoint> ptsProc = points;
    if (!refProc.empty())
    {
        for (auto& cp : ptsProc)
        {
            int px = cp.x + refAnchorX;
            int py = cp.y + refAnchorY;
            if ((unsigned)px < (unsigned)refProc.cols && (unsigned)py < (unsigned)refProc.rows)
            {
                const int ch = refProc.channels();
                const uchar* p = refProc.ptr<uchar>(py) + (size_t)px * ch;
                if (ch == 1)
                {
                    cp.b = cp.g = cp.r = p[0];
                }
                else if (ch >= 3)
                {
                    cp.b = p[0]; cp.g = p[1]; cp.r = p[2];
                }
            }
        }
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    // 确定搜索区域 — HasROI要求selectedROI>=0，未选中时自动用第一个ROI
    cv::Rect searchRect(0, 0, src.cols, src.rows);
    if (useROI)
    {
        if (ctx.HasROI())
            searchRect = ctx.GetActiveROIRect() & cv::Rect(0, 0, src.cols, src.rows);
        else if (!ctx.rois.empty())
        {
            // 有ROI但未选中：自动取第一个
            searchRect = ctx.rois[0].ToCvRect() & cv::Rect(0, 0, src.cols, src.rows);
        }
    }

    // 计算所有点的包围盒，用于边界检查
    int minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (const auto& pt : ptsProc)
    {
        if (pt.x < minX) minX = pt.x;
        if (pt.x > maxX) maxX = pt.x;
        if (pt.y < minY) minY = pt.y;
        if (pt.y > maxY) maxY = pt.y;
    }

    int nc = src.channels();
    std::vector<cv::Point> matches;
    const int nPts = (int)ptsProc.size();

    // 遍历搜索区域
    int startY = std::max(searchRect.y, searchRect.y - minY);
    int endY   = std::min(searchRect.y + searchRect.height, src.rows - maxY);
    int startX = std::max(searchRect.x, searchRect.x - minX);
    int endX   = std::min(searchRect.x + searchRect.width, src.cols - maxX);

    // 最佳部分匹配（仅记录位置+匹配数，不存mask避免每像素分配内存）
    cv::Point bestPartial(-1, -1);
    int bestPartialCount = 0;

    for (int y = startY; y < endY; y++)
    {
        const uint8_t* row = src.ptr<uint8_t>(y);
        for (int x = startX; x < endX; x++)
        {
            // 快速路径：逐点检测，遇不匹配立即跳出
            int matched = 0;
            for (int pi = 0; pi < nPts; pi++)
            {
                const auto& pt = ptsProc[pi];
                int px = x + pt.x, py = y + pt.y;
                if ((unsigned)px >= (unsigned)src.cols || (unsigned)py >= (unsigned)src.rows)
                    break;
                const uint8_t* pixel = src.ptr<uint8_t>(py) + px * nc;
                if (IsColorMatch(pixel, nc, pt))
                    matched++;
                else
                    break;
            }
            if (matched == nPts)
            {
                matches.push_back(cv::Point(x, y));
            }
            else if (matched > bestPartialCount)
            {
                // 候选部分匹配：需要精确统计全部点匹配数
                int exact = 0;
                for (int pi = 0; pi < nPts; pi++)
                {
                    const auto& pt = ptsProc[pi];
                    int px = x + pt.x, py = y + pt.y;
                    if ((unsigned)px >= (unsigned)src.cols || (unsigned)py >= (unsigned)src.rows) continue;
                    const uint8_t* pixel = src.ptr<uint8_t>(py) + px * nc;
                    if (IsColorMatch(pixel, nc, pt)) exact++;
                }
                if (exact > bestPartialCount)
                {
                    bestPartialCount = exact;
                    bestPartial = cv::Point(x, y);
                }
            }
        }
    }

    // 无完全匹配时回退到最佳部分匹配
    bool isPartial = false;
    if (matches.empty() && bestPartialCount > 0 && bestPartialCount < nPts)
    {
        matches.push_back(bestPartial);
        isPartial = true;
    }

    // NMS 去重
    if (minDist > 0 && matches.size() > 1)
    {
        std::vector<cv::Point> filtered;
        float d2 = minDist * minDist;
        for (const auto& m : matches)
        {
            bool keep = true;
            for (const auto& f : filtered)
            {
                float dx = (float)(m.x - f.x), dy = (float)(m.y - f.y);
                if (dx * dx + dy * dy < d2) { keep = false; break; }
            }
            if (keep) filtered.push_back(m);
        }
        matches.swap(filtered);
    }

    if ((int)matches.size() > maxResults)
        matches.resize(maxResults);

    // 填充结果
    int refW = refImage.empty() ? 0 : refImage.cols;
    int refH = refImage.empty() ? 0 : refImage.rows;
    for (int i = 0; i < (int)matches.size(); i++)
    {
        const auto& anchor = matches[i];
        ToolResult::Region reg;
        // bbox = 参考图ROI在匹配位置，锚点在参考图中的位置为(refAnchorX, refAnchorY)
        reg.bbox = cv::Rect(anchor.x - refAnchorX,
                            anchor.y - refAnchorY,
                            refW > 0 ? refW : crossSize * 2,
                            refH > 0 ? refH : crossSize * 2);

        int matchedCnt = nPts;
        std::vector<bool> ptMask; // 仅在部分匹配时分配一次

        if (isPartial && i == 0)
        {
            ptMask.resize(nPts, false);
            matchedCnt = 0;
            for (int pi = 0; pi < nPts; pi++)
            {
                const auto& pt = ptsProc[pi];
                int px = anchor.x + pt.x, py = anchor.y + pt.y;
                if ((unsigned)px < (unsigned)src.cols && (unsigned)py < (unsigned)src.rows)
                {
                    const uint8_t* pixel = src.ptr<uint8_t>(py) + px * nc;
                    if (IsColorMatch(pixel, nc, pt)) { ptMask[pi] = true; matchedCnt++; }
                }
            }
        }

        reg.score = (float)matchedCnt / nPts;
        reg.area  = (float)matchedCnt;
        reg.label = (isPartial && i == 0)
            ? cv::format("部分 %d/%d (%d,%d)", matchedCnt, nPts, anchor.x, anchor.y)
            : cv::format("#%d (%d,%d)", i + 1, anchor.x, anchor.y);

        if (isPartial && i == 0)
        {
            for (int pi = 0; pi < nPts; pi++)
                if (ptMask[pi])
                    reg.contour.push_back(cv::Point(anchor.x + ptsProc[pi].x, anchor.y + ptsProc[pi].y));
            for (int pi = 0; pi < nPts; pi++)
                if (!ptMask[pi])
                    reg.contour.push_back(cv::Point(anchor.x + ptsProc[pi].x, anchor.y + ptsProc[pi].y));
        }
        else
        {
            for (const auto& cp : ptsProc)
                reg.contour.push_back(cv::Point(anchor.x + cp.x, anchor.y + cp.y));
        }
        result.regions.push_back(reg);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

    result.success = true;
    result.message = cv::format("%d个匹配, %.3fms", (int)matches.size(), ms);
    if (isPartial)
        LogSystem::Add(LOG_WARN, "多点找色:无完全匹配,最佳部分 %d/%d %.3fms", bestPartialCount, nPts, ms);
    else
        LogSystem::Add(LOG_INFO, color, "多点找色:%d个匹配 %.3fms", (int)matches.size(), ms);

    return result;
}

nlohmann::json MultiColorFinder::Save() const
{
    nlohmann::json j;
    j["imgUseGray"]   = imgUseGray;
    j["imgUseBinary"] = imgUseBinary;
    j["imgBinThresh"] = imgBinThresh;
    j["useROI"] = useROI;
    j["maxResults"] = maxResults;
    j["minDist"] = minDist;
    j["crossSize"] = crossSize;
    j["crossThick"] = crossThick;
    j["markColor"] = { markColor[0], markColor[1], markColor[2] };
    j["roiX"] = roiX; j["roiY"] = roiY; j["roiW"] = roiW; j["roiH"] = roiH;

    // 参考图序列化为 base64 PNG
    if (!refImage.empty())
    {
        std::vector<uchar> buf;
        cv::imencode(".png", refImage, buf);
        j["refImage"] = nlohmann::json::binary_t(buf);
        j["refAnchorX"] = refAnchorX;
        j["refAnchorY"] = refAnchorY;
    }

    j["points"] = nlohmann::json::array();
    for (const auto& p : points)
    {
        j["points"].push_back({ {"x", p.x}, {"y", p.y}, {"b", p.b}, {"g", p.g}, {"r", p.r}, {"tolerance", p.tolerance} });
    }
    return j;
}

void MultiColorFinder::Load(const nlohmann::json& j)
{
    imgUseGray  = j.value("imgUseGray", false);
    imgUseBinary= j.value("imgUseBinary", false);
    imgBinThresh= j.value("imgBinThresh", 128);
    useROI      = j.value("useROI", false);
    maxResults  = j.value("maxResults", 50);
    minDist     = j.value("minDist", 5.0f);
    crossSize   = j.value("crossSize", 10);
    crossThick  = j.value("crossThick", 2);
    roiX = j.value("roiX", 0); roiY = j.value("roiY", 0);
    roiW = j.value("roiW", 0); roiH = j.value("roiH", 0);
    if (j.contains("markColor") && j["markColor"].is_array() && j["markColor"].size() >= 3)
        markColor = cv::Scalar(j["markColor"][0], j["markColor"][1], j["markColor"][2]);

    // 加载参考图
    refImage.release();
    if (j.contains("refImage") && j["refImage"].is_binary())
    {
        auto& bin = j["refImage"].get_binary();
        std::vector<uchar> buf(bin.begin(), bin.end());
        refImage = cv::imdecode(buf, cv::IMREAD_COLOR);
        refAnchorX = j.value("refAnchorX", 0);
        refAnchorY = j.value("refAnchorY", 0);
    }

    points.clear();
    if (j.contains("points") && j["points"].is_array())
    {
        for (const auto& pj : j["points"])
        {
            ColorPoint cp;
            cp.x = pj.value("x", 0);
            cp.y = pj.value("y", 0);
            cp.b = pj.value("b", 0);
            cp.g = pj.value("g", 0);
            cp.r = pj.value("r", 0);
            cp.tolerance = pj.value("tolerance", 10);
            points.push_back(cp);
        }
    }
}
