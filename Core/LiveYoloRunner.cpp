#include "LiveYoloRunner.h"

#include "LegacyAppState.h"
#include "ROIState.h"
#include "ToolChainState.h"
#include "UIStateBridge.h"
#include "VideoCapture.h"
#include "VisionContext.h"
#include "../Algorithm/OpenCVYoloDetector.h"
#include "../Algorithm/YOLODetector.h"
#include "../Log/LogSystem.h"

#include <chrono>
#include <deque>

// =====================================================
// 实时 YOLO 性能统计（滑动窗口平均）
// =====================================================
namespace
{
struct LiveYoloPerfStats
{
    static constexpr int WarmupFrames = 5;   // 预热帧数（排除前几帧的 JIT/缓存延迟）
    static constexpr int WindowFrames = 100; // 滑动窗口大小

    int framesSeen = 0;
    std::deque<float> total;   // 总耗时滑动窗口
    std::deque<float> pre;     // 预处理耗时窗口
    std::deque<float> inf;     // 推理耗时窗口
    std::deque<float> post;    // 后处理耗时窗口
    float totalSum = 0.0f;
    float preSum = 0.0f;
    float infSum = 0.0f;
    float postSum = 0.0f;

    void Reset()
    {
        framesSeen = 0;
        total.clear();
        pre.clear();
        inf.clear();
        post.clear();
        totalSum = preSum = infSum = postSum = 0.0f;
    }

    bool Add(float totalMs, float preMs, float infMs, float postMs)
    {
        framesSeen++;
        if (framesSeen <= WarmupFrames)
            return false;

        Push(total, totalSum, totalMs);
        Push(pre, preSum, preMs);
        Push(inf, infSum, infMs);
        Push(post, postSum, postMs);
        return true;
    }

    int Count() const { return (int)total.size(); }
    float AvgTotal() const { return Avg(totalSum); }
    float AvgPre() const { return Avg(preSum); }
    float AvgInf() const { return Avg(infSum); }
    float AvgPost() const { return Avg(postSum); }

private:
    void Push(std::deque<float>& q, float& sum, float value)
    {
        q.push_back(value);
        sum += value;
        if ((int)q.size() > WindowFrames)
        {
            sum -= q.front();
            q.pop_front();
        }
    }

    float Avg(float sum) const
    {
        return total.empty() ? 0.0f : sum / (float)total.size();
    }
};

// 从工具实例或全局 ROI 列表中选取实时 YOLO 搜索区域
cv::Rect SelectLiveYoloSearchRect(const ToolInstance& it, const cv::Mat& image)
{
    if (image.empty())
        return {};

    // 优先使用工具自身 ROI，否则使用全局 ROI
    const auto& globalROIs = ROIState::ReadOnlyItems();
    const std::vector<ROI>* rois = !it.searchROIs.empty() ? &it.searchROIs : &globalROIs;
    if (!rois || rois->empty())
        return {};

    int roiIndex = 0;
    if (rois == &globalROIs)
        roiIndex = ROIState::SelectIndexFor(*rois);

    cv::Rect roi = (*rois)[roiIndex].ToCvRect();
    roi &= cv::Rect(0, 0, image.cols, image.rows);  // 裁剪到图像范围内
    return (roi.width > 0 && roi.height > 0) ? roi : cv::Rect();
}
}

// =====================================================
// LiveYoloRunner::Update — 实时 YOLO 检测主循环
// 流程：
//   1. 检查是否启用实时检测 + 是否有图像
//   2. 从工具链获取模型路径、置信度/NMS 阈值、ROI
//   3. 根据类型选择后端（YOLODetector=ONNX GPU / OpenCVYoloDetector=DNN CPU）
//   4. 执行推理 → 计时 → 滑动窗口统计 → 日志输出
//   5. 发布检测结果到叠加层和统一结果列表
// =====================================================
namespace LiveYoloRunner
{
void Update()
{
    // 1. 前置检查：实时检测开关 + 图像有效性
    if (!ToolChainState::YoloLiveDetect() || gImage.empty())
        return;

    static std::string s_LiveModelTag;
    static std::string s_LiveStatsKey;
    static LiveYoloPerfStats s_LiveStats;
    static auto s_FpsTimer = std::chrono::steady_clock::now();

    if (!VideoCapture::IsOpen())
    {
        ToolChainState::SetYoloLiveDetect(false);
        ToolChainState::SetYoloLiveInstanceIndex(-1);
        g_YoloShowOverlay = false;
        g_YoloOverlays.clear();
        gContext.ClearUnifiedResults();
        return;
    }

    float confTh = 0.5f;
    float nmsTh = 0.4f;
    cv::Rect roi;
    auto& tools = ToolChainState::Tools();
    int idx = ToolChainState::YoloLiveInstanceIndex();
    int liveType = (idx >= 0 && idx < (int)tools.size()) ? tools[idx].type : -1;
    if (idx >= 0 && idx < (int)tools.size() && (liveType == 4 || liveType == 11))
    {
        auto& it = tools[idx];
        confTh = it.yoloConfThreshold;
        nmsTh = it.yoloNmsThreshold;
        auto pos = it.yoloModelPath.rfind('/');
        auto pos2 = it.yoloModelPath.rfind('\\');
        if (pos2 != std::string::npos && (pos == std::string::npos || pos2 > pos))
            pos = pos2;
        s_LiveModelTag = (pos != std::string::npos) ? it.yoloModelPath.substr(pos + 1) : it.yoloModelPath;
        auto dot = s_LiveModelTag.rfind('.');
        if (dot != std::string::npos)
            s_LiveModelTag = s_LiveModelTag.substr(0, dot);
        roi = SelectLiveYoloSearchRect(it, gImage);
    }
    else
    {
        for (auto& it : tools)
        {
            if (it.type == 4)
            {
                confTh = it.yoloConfThreshold;
                nmsTh = it.yoloNmsThreshold;
                break;
            }
        }
    }

    std::vector<DetectedObject> objs;
    auto t0 = std::chrono::steady_clock::now();
    bool canDetect = true;
    if (liveType == 11)
    {
        auto& it = tools[idx];
        if (it.yoloModelPath.empty() || !OpenCVYoloDetector::LoadModel(it.yoloModelPath, it.yoloClassesPath))
        {
            ToolChainState::SetYoloLiveDetect(false);
            ToolChainState::SetYoloLiveInstanceIndex(-1);
            LogSystem::Add(LOG_WARN, "YOLO OpenCV DNN: 实时测试已停止，请先选择有效 ONNX 模型");
            canDetect = false;
        }
        else
        {
            objs = OpenCVYoloDetector::Detect(gImage, confTh, nmsTh, roi);
        }
    }
    else
    {
        if (!YOLODetector::IsLoaded())
        {
            ToolChainState::SetYoloLiveDetect(false);
            ToolChainState::SetYoloLiveInstanceIndex(-1);
            LogSystem::Add(LOG_WARN, "YOLO: 实时检测已停止，请先加载模型");
            canDetect = false;
        }
        else
        {
            objs = YOLODetector::Detect(gImage, confTh, nmsTh, roi);
        }
    }
    if (!canDetect)
        return;

    auto now = std::chrono::steady_clock::now();
    const float measuredFrameMs = std::chrono::duration<float, std::milli>(now - t0).count();
    float totalMs = measuredFrameMs;
    float preMs = 0.0f;
    float infMs = 0.0f;
    float postMs = 0.0f;
    if (liveType == 11)
    {
        totalMs = OpenCVYoloDetector::g_OpenCVYoloTotalMs;
        preMs = OpenCVYoloDetector::g_OpenCVYoloPreMs;
        infMs = OpenCVYoloDetector::g_OpenCVYoloInfMs;
        postMs = OpenCVYoloDetector::g_OpenCVYoloPostMs;
    }
    else
    {
        totalMs = g_YoloDetailTotalMs > 0.0f ? g_YoloDetailTotalMs : measuredFrameMs;
        preMs = g_YoloDetailPreMs;
        infMs = g_YoloDetailInfMs;
        postMs = g_YoloDetailPostMs;
    }
    ToolChainState::SetYoloLiveFrameMs(totalMs);

    const std::string statsKey = std::to_string(liveType) + "|" + std::to_string(idx) + "|" + s_LiveModelTag;
    if (statsKey != s_LiveStatsKey)
    {
        s_LiveStatsKey = statsKey;
        s_LiveStats.Reset();
        s_FpsTimer = now;
    }
    const bool hasStableStats = s_LiveStats.Add(totalMs, preMs, infMs, postMs);

    float elapsed = std::chrono::duration<float>(now - s_FpsTimer).count();
    if (elapsed >= 0.5f && hasStableStats)
    {
        const float avgTotal = s_LiveStats.AvgTotal();
        const float fps = avgTotal > 0.0f ? 1000.0f / avgTotal : 0.0f;
        const char* liveBaseName = liveType == 11 ? "YOLO OpenCV DNN" : "YOLO";
        const std::string liveName = (idx >= 0 && idx < (int)tools.size())
            ? ToolInstanceLogName(liveBaseName, tools[idx].label)
            : std::string(liveBaseName);
        LogSystem::Add(LOG_INFO, ImVec4(0.5f, 1, 0.5f, 1),
            "%s(%s): %.1f fps avg %.3fms/帧 当前 %d 个目标 | 预 %.3f 推 %.3f 后 %.3f | warmup %d 帧已排除 n=%d",
            liveName.c_str(),
            liveType == 11 ? "OpenCV CPU" : YOLODetector::GetBackendName(),
            fps, avgTotal, (int)objs.size(),
            s_LiveStats.AvgPre(), s_LiveStats.AvgInf(), s_LiveStats.AvgPost(),
            LiveYoloPerfStats::WarmupFrames, s_LiveStats.Count());
        s_FpsTimer = now;
    }

    for (auto& o : objs)
        o.className = "[" + s_LiveModelTag + "] " + o.className;
    g_YoloOverlays = std::move(objs);
    g_YoloShowOverlay = true;

    ToolResult tr;
    const char* resultBaseName = liveType == 11 ? "YOLO OpenCV 5.0" : "YOLO";
    tr.toolName = (idx >= 0 && idx < (int)tools.size())
        ? ToolInstanceLogName(resultBaseName, tools[idx].label)
        : std::string(resultBaseName);
    tr.success = true;
    for (const auto& o : g_YoloOverlays)
    {
        ToolResult::Detection d;
        d.box = o.box;
        d.label = o.className;
        d.score = o.confidence;
        tr.detections.push_back(d);
    }
    gContext.ClearUnifiedResults();
    gContext.unifiedResults.push_back(std::move(tr));
}
}
