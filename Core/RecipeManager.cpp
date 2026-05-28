#include "RecipeManager.h"
#include "DX12Context.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/TemplateMatch.h"
#include "../UI/ROIManager.h"
#include "../UI/DockSpaceHost.h"

#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// 外部变量引用
extern std::string pendingPath;
extern bool gUseGray;
extern int  gThresholdValue;
extern bool gThresholdBinaryInv;
extern int  gBlurSize;
extern int  gCannyLow, gCannyHigh;
extern float gBrightness, gContrast;
extern int  gProcessMode;
extern PipelineState gPipe;

namespace RecipeManager
{

// ===================== 保存 =====================
bool Save(const char* filepath, const RecipeData& data)
{
    json j;

    j["name"] = data.name;
    j["imagePath"] = data.imagePath;
    j["templateImage"] = data.templateImage;

    // 阈值参数
    json& th = j["threshold"];
    th["useGray"]         = data.threshold.useGray;
    th["thresholdValue"]  = data.threshold.thresholdValue;
    th["binaryInv"]       = data.threshold.binaryInv;
    th["blurSize"]        = data.threshold.blurSize;
    th["cannyLow"]        = data.threshold.cannyLow;
    th["cannyHigh"]       = data.threshold.cannyHigh;
    th["brightness"]      = data.threshold.brightness;
    th["contrast"]        = data.threshold.contrast;
    th["processMode"]     = data.threshold.processMode;
    th["pipeBlur"]        = data.threshold.pipeBlur;
    th["pipeThreshold"]   = data.threshold.pipeThreshold;
    th["pipeCanny"]       = data.threshold.pipeCanny;
    th["pipeBlurSize"]    = data.threshold.pipeBlurSize;
    th["pipeThresholdVal"] = data.threshold.pipeThresholdVal;
    th["pipeCannyLow"]    = data.threshold.pipeCannyLow;
    th["pipeCannyHigh"]   = data.threshold.pipeCannyHigh;

    // 模板匹配参数
    json& tm = j["templateMatch"];
    tm["method"]         = data.tmMatch.method;
    tm["searchMode"]     = data.tmMatch.searchMode;
    tm["maxResults"]     = data.tmMatch.maxResults;
    tm["maxImageDim"]    = data.tmMatch.maxImageDim;
    tm["matchThreshold"] = data.tmMatch.matchThreshold;
    tm["enableRotation"] = data.tmMatch.enableRotation;
    tm["rotationStart"]  = data.tmMatch.rotationStart;
    tm["rotationEnd"]    = data.tmMatch.rotationEnd;
    tm["rotationStep"]   = data.tmMatch.rotationStep;

    // ROI 数组
    json& rois = j["rois"] = json::array();
    for (const auto& r : data.rois)
    {
        rois.push_back({
            {"startX", r.startX}, {"startY", r.startY},
            {"endX",   r.endX},   {"endY",   r.endY},
            {"type",   r.type}
        });
    }

    // 工具实例数组
    json& tools = j["tools"] = json::array();
    for (const auto& t : data.tools)
    {
        json tj;
        tj["type"]           = t.type;
        tj["templateFile"]   = t.templateFile;
        tj["enableRotation"] = t.enableRotation;
        tj["rotationStart"]  = t.rotationStart;
        tj["rotationEnd"]    = t.rotationEnd;
        tj["rotationStep"]   = t.rotationStep;
        tj["maxResults"]     = t.maxResults;
        tj["matchThreshold"] = t.matchThreshold;
        tj["maxImageDim"]    = t.maxImageDim;
        tj["nmsThreshold"]   = t.nmsThreshold;
        tj["searchMode"]     = t.searchMode;
        tj["tplGray"]        = t.tplGray;
        tj["tplBinary"]      = t.tplBinary;
        tj["tplBinThresh"]   = t.tplBinThresh;
        tj["tplEdge"]        = t.tplEdge;
        tj["tplEdgeLow"]     = t.tplEdgeLow;
        tj["tplEdgeHigh"]    = t.tplEdgeHigh;
        tj["imgUseGray"]     = t.imgUseGray;
        tj["imgEnableThreshold"] = t.imgEnableThreshold;
        tj["imgThreshold"]   = t.imgThreshold;
        tj["cannyLow"]       = t.cannyLow;
        tj["cannyHigh"]      = t.cannyHigh;
        tj["edgeUseGray"]    = t.edgeUseGray;
        tj["dbgUseGray"]     = t.dbgUseGray;
        tj["dbgEnableBlur"]  = t.dbgEnableBlur;
        tj["dbgBlurSize"]    = t.dbgBlurSize;
        tj["dbgEnableThresh"] = t.dbgEnableThresh;
        tj["dbgThreshold"]   = t.dbgThreshold;
        tj["dbgEnableCanny"] = t.dbgEnableCanny;
        tj["dbgCannyLow"]    = t.dbgCannyLow;
        tj["dbgCannyHigh"]   = t.dbgCannyHigh;

        // 搜索 ROI 子数组
        json& srois = tj["searchROIs"] = json::array();
        for (const auto& r : t.searchROIs)
        {
            srois.push_back({
                {"startX", r.startX}, {"startY", r.startY},
                {"endX",   r.endX},   {"endY",   r.endY},
                {"type",   r.type}
            });
        }
        tools.push_back(tj);
    }

    // 写入文件
    std::ofstream f(filepath);
    if (!f)
    {
        LogSystem::Add(LOG_ERROR, "RecipeManager: 无法写入 %s", filepath);
        return false;
    }
    f << j.dump(2);

    LogSystem::Add(LOG_INFO, color, "[Save] 已写入 %zu 个工具实例", data.tools.size());

    // 保存模板图像（与配方同目录、同名 .png）
    if (!data.templateImage.empty())
    {
        std::string tplPath(filepath);
        size_t slash = tplPath.find_last_of("\\/");
        tplPath = (slash != std::string::npos)
            ? tplPath.substr(0, slash + 1) + data.templateImage
            : data.templateImage;

        if (TemplateMatch::SaveTemplate(tplPath.c_str()))
            LogSystem::Add(LOG_INFO, "模板图像已保存: %s", tplPath.c_str());
    }

    // 保存每个工具实例的模板图像
    for (size_t ti = 0; ti < data.tools.size(); ti++)
    {
        const auto& t = data.tools[ti];
        if (t.templateFile.empty()) continue;
        std::string tplPath(filepath);
        size_t slash = tplPath.find_last_of("\\/");
        tplPath = (slash != std::string::npos)
            ? tplPath.substr(0, slash + 1) + t.templateFile
            : t.templateFile;

        if (ti < UI::g_ToolInstances.size() && !UI::g_ToolInstances[ti].templateImg.empty())
        {
            cv::Mat& tpl = UI::g_ToolInstances[ti].templateImg;
            if (cv::imwrite(tplPath, tpl))
                LogSystem::Add(LOG_INFO, "工具模板已保存: %s", tplPath.c_str());
        }
    }

    LogSystem::Add(LOG_INFO, "配方已保存: %s", filepath);
    return true;
}

// ===================== 加载 =====================
static std::string g_LastRecipePath;

bool Load(const char* filepath, RecipeData& data)
{
    g_LastRecipePath = filepath;

    std::ifstream f(filepath);
    if (!f)
    {
        LogSystem::Add(LOG_ERROR, "RecipeManager: 无法打开 %s", filepath);
        return false;
    }

    json j;
    try { f >> j; }
    catch (const json::parse_error& e)
    {
        LogSystem::Add(LOG_ERROR, "RecipeManager: JSON 解析失败 %s (byte %zu)", e.what(), e.byte);
        return false;
    }

    data.name          = j.value("name", "");
    data.imagePath     = j.value("imagePath", "");
    data.templateImage = j.value("templateImage", "");

    // 阈值参数
    if (j.contains("threshold"))
    {
        auto& th = j["threshold"];
        data.threshold.useGray          = th.value("useGray", false);
        data.threshold.thresholdValue   = th.value("thresholdValue", 128);
        data.threshold.binaryInv        = th.value("binaryInv", false);
        data.threshold.blurSize         = th.value("blurSize", 1);
        data.threshold.cannyLow         = th.value("cannyLow", 50);
        data.threshold.cannyHigh        = th.value("cannyHigh", 150);
        data.threshold.brightness       = th.value("brightness", 0.0f);
        data.threshold.contrast         = th.value("contrast", 1.0f);
        data.threshold.processMode      = th.value("processMode", 0);
        data.threshold.pipeBlur         = th.value("pipeBlur", false);
        data.threshold.pipeThreshold    = th.value("pipeThreshold", false);
        data.threshold.pipeCanny        = th.value("pipeCanny", false);
        data.threshold.pipeBlurSize     = th.value("pipeBlurSize", 5);
        data.threshold.pipeThresholdVal = th.value("pipeThresholdVal", 128);
        data.threshold.pipeCannyLow     = th.value("pipeCannyLow", 50);
        data.threshold.pipeCannyHigh    = th.value("pipeCannyHigh", 150);
    }

    // 模板匹配参数
    if (j.contains("templateMatch"))
    {
        auto& tm = j["templateMatch"];
        data.tmMatch.method         = tm.value("method", 5);
        data.tmMatch.searchMode     = tm.value("searchMode", 0);
        data.tmMatch.maxResults     = tm.value("maxResults", 10);
        data.tmMatch.maxImageDim    = tm.value("maxImageDim", 1000);
        data.tmMatch.matchThreshold = tm.value("matchThreshold", 0.75f);
        data.tmMatch.enableRotation = tm.value("enableRotation", false);
        data.tmMatch.rotationStart  = tm.value("rotationStart", -5);
        data.tmMatch.rotationEnd    = tm.value("rotationEnd", 5);
        data.tmMatch.rotationStep   = tm.value("rotationStep", 5);
    }

    // ROI 数组
    data.rois.clear();
    if (j.contains("rois") && j["rois"].is_array())
    {
        for (const auto& r : j["rois"])
        {
            RecipeROI roi;
            roi.startX = r.value("startX", 0.0f);
            roi.startY = r.value("startY", 0.0f);
            roi.endX   = r.value("endX", 0.0f);
            roi.endY   = r.value("endY", 0.0f);
            roi.type   = r.value("type", 0);
            data.rois.push_back(roi);
        }
    }

    // 工具实例数组
    data.tools.clear();
    if (j.contains("tools") && j["tools"].is_array())
    {
        for (const auto& tj : j["tools"])
        {
            RecipeToolInstance t;
            t.type             = tj.value("type", 0);
            t.templateFile     = tj.value("templateFile", "");
            t.enableRotation   = tj.value("enableRotation", false);
            t.rotationStart    = tj.value("rotationStart", -45);
            t.rotationEnd      = tj.value("rotationEnd", 45);
            t.rotationStep     = tj.value("rotationStep", 1);
            t.maxResults       = tj.value("maxResults", 5);
            t.matchThreshold   = tj.value("matchThreshold", 0.7f);
            t.maxImageDim      = tj.value("maxImageDim", 1000);
            t.nmsThreshold     = tj.value("nmsThreshold", 0.3f);
            t.searchMode       = tj.value("searchMode", 0);
            t.tplGray          = tj.value("tplGray", false);
            t.tplBinary        = tj.value("tplBinary", false);
            t.tplBinThresh     = tj.value("tplBinThresh", 128);
            t.tplEdge          = tj.value("tplEdge", false);
            t.tplEdgeLow       = tj.value("tplEdgeLow", 50);
            t.tplEdgeHigh      = tj.value("tplEdgeHigh", 150);
            t.imgUseGray       = tj.value("imgUseGray", false);
            t.imgEnableThreshold = tj.value("imgEnableThreshold", false);
            t.imgThreshold     = tj.value("imgThreshold", 128);
            t.cannyLow         = tj.value("cannyLow", 50);
            t.cannyHigh        = tj.value("cannyHigh", 150);
            t.edgeUseGray      = tj.value("edgeUseGray", false);
            t.dbgUseGray       = tj.value("dbgUseGray", false);
            t.dbgEnableBlur    = tj.value("dbgEnableBlur", false);
            t.dbgBlurSize      = tj.value("dbgBlurSize", 5);
            t.dbgEnableThresh  = tj.value("dbgEnableThresh", false);
            t.dbgThreshold     = tj.value("dbgThreshold", 128);
            t.dbgEnableCanny   = tj.value("dbgEnableCanny", false);
            t.dbgCannyLow      = tj.value("dbgCannyLow", 50);
            t.dbgCannyHigh     = tj.value("dbgCannyHigh", 150);

            // 搜索 ROI 子数组
            if (tj.contains("searchROIs") && tj["searchROIs"].is_array())
            {
                for (const auto& r : tj["searchROIs"])
                {
                    RecipeROI rr;
                    rr.startX = r.value("startX", 0.0f);
                    rr.startY = r.value("startY", 0.0f);
                    rr.endX   = r.value("endX", 0.0f);
                    rr.endY   = r.value("endY", 0.0f);
                    rr.type   = r.value("type", 0);
                    t.searchROIs.push_back(rr);
                }
            }
            data.tools.push_back(t);
        }
    }

    // 加载模板图像
    if (!data.templateImage.empty())
    {
        std::string tplPath(filepath);
        size_t slash = tplPath.find_last_of("\\/");
        tplPath = (slash != std::string::npos)
            ? tplPath.substr(0, slash + 1) + data.templateImage
            : data.templateImage;

        if (TemplateMatch::LoadTemplate(tplPath.c_str()))
            LogSystem::Add(LOG_INFO, "模板图像已加载: %s", tplPath.c_str());
    }

    LogSystem::Add(LOG_INFO, color, "[Load] 解析到 %zu 个工具实例", data.tools.size());
    LogSystem::Add(LOG_INFO, color, "配方已加载: %s (ROI: %zu, 工具: %zu)", data.name.c_str(), data.rois.size(), data.tools.size());
    return true;
}

// ===================== 列出所有配方 =====================
std::vector<std::string> List(const char* exeDir)
{
    std::vector<std::string> result;
    std::string dir = exeDir ? std::string(exeDir) + "recipes\\" : "recipes\\";
    CreateDirectoryA(dir.c_str(), nullptr);

    WIN32_FIND_DATAA fd;
    std::string pattern = dir + "*.recipe";
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return result;

    do {
        std::string name = fd.cFileName;
        name = name.substr(0, name.rfind('.'));
        result.push_back(name);
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    return result;
}

// ===================== 从当前环境抓取参数 =====================
RecipeData Capture(const char* name)
{
    RecipeData d;
    d.name = name;
    if (!pendingPath.empty()) d.imagePath = pendingPath;

    // 阈值
    d.threshold.useGray         = gUseGray;
    d.threshold.thresholdValue  = gThresholdValue;
    d.threshold.binaryInv       = gThresholdBinaryInv;
    d.threshold.blurSize        = gBlurSize;
    d.threshold.cannyLow        = gCannyLow;
    d.threshold.cannyHigh       = gCannyHigh;
    d.threshold.brightness      = gBrightness;
    d.threshold.contrast        = gContrast;
    d.threshold.processMode     = gProcessMode;

    d.threshold.pipeBlur        = gPipe.enableBlur;
    d.threshold.pipeThreshold   = gPipe.enableThreshold;
    d.threshold.pipeCanny       = gPipe.enableCanny;
    d.threshold.pipeBlurSize    = gPipe.blurSize;
    d.threshold.pipeThresholdVal = gPipe.threshold;
    d.threshold.pipeCannyLow    = gPipe.cannyLow;
    d.threshold.pipeCannyHigh   = gPipe.cannyHigh;

    // 模板匹配
    d.tmMatch.searchMode      = g_TMSearchMode;
    d.tmMatch.maxResults      = g_TMMaxResults;
    d.tmMatch.maxImageDim     = g_TMMaxImageDim;
    d.tmMatch.matchThreshold  = g_TMMatchThreshold;
    d.tmMatch.enableRotation  = g_TMEnableRotation;
    d.tmMatch.rotationStart   = g_TMRotationStart;
    d.tmMatch.rotationEnd     = g_TMRotationEnd;
    d.tmMatch.rotationStep    = g_TMRotationStep;

    // ROI
    d.rois.clear();
    for (const auto& roi : UI::gROIs)
    {
        RecipeROI r;
        r.startX = roi.start.x;
        r.startY = roi.start.y;
        r.endX   = roi.end.x;
        r.endY   = roi.end.y;
        r.type   = roi.type;
        d.rois.push_back(r);
    }

    d.templateImage = d.name + ".png";

    // 工具实例
    d.tools.clear();
    LogSystem::Add(LOG_INFO, color, "[Capture] g_ToolInstances 共 %zu 个实例",
        UI::g_ToolInstances.size());
    for (size_t ti = 0; ti < UI::g_ToolInstances.size(); ti++)
    {
        const auto& src = UI::g_ToolInstances[ti];
        RecipeToolInstance t;
        t.type = src.type;
        if (!src.templateImg.empty())
            t.templateFile = d.name + "_tpl" + std::to_string(ti) + ".png";

        t.enableRotation   = src.enableRotation;
        t.rotationStart    = src.rotationStart;
        t.rotationEnd      = src.rotationEnd;
        t.rotationStep     = src.rotationStep;
        t.maxResults       = src.maxResults;
        t.matchThreshold   = src.matchThreshold;
        t.maxImageDim      = src.maxImageDim;
        t.nmsThreshold     = src.nmsThreshold;
        t.searchMode       = src.searchMode;
        t.tplGray          = src.tplGray;
        t.tplBinary        = src.tplBinary;
        t.tplBinThresh     = src.tplBinThresh;
        t.tplEdge          = src.tplEdge;
        t.tplEdgeLow       = src.tplEdgeLow;
        t.tplEdgeHigh      = src.tplEdgeHigh;
        t.imgUseGray       = src.imgUseGray;
        t.imgEnableThreshold = src.imgEnableThreshold;
        t.imgThreshold     = src.imgThreshold;
        t.cannyLow         = src.cannyLow;
        t.cannyHigh        = src.cannyHigh;
        t.edgeUseGray      = src.edgeUseGray;
        t.dbgUseGray       = src.dbgUseGray;
        t.dbgEnableBlur    = src.dbgEnableBlur;
        t.dbgBlurSize      = src.dbgBlurSize;
        t.dbgEnableThresh  = src.dbgEnableThresh;
        t.dbgThreshold     = src.dbgThreshold;
        t.dbgEnableCanny   = src.dbgEnableCanny;
        t.dbgCannyLow      = src.dbgCannyLow;
        t.dbgCannyHigh     = src.dbgCannyHigh;

        for (const auto& roi : src.searchROIs)
        {
            RecipeROI r;
            r.startX = roi.start.x; r.startY = roi.start.y;
            r.endX   = roi.end.x;   r.endY   = roi.end.y;
            r.type   = roi.type;
            t.searchROIs.push_back(r);
        }

        d.tools.push_back(t);
    }

    return d;
}

// ===================== 应用配方到当前环境 =====================
void Apply(const RecipeData& data)
{
    if (!data.imagePath.empty())
        pendingPath = data.imagePath;

    // 阈值参数
    gUseGray            = data.threshold.useGray;
    gThresholdValue     = data.threshold.thresholdValue;
    gThresholdBinaryInv = data.threshold.binaryInv;
    gBlurSize           = data.threshold.blurSize;
    gCannyLow           = data.threshold.cannyLow;
    gCannyHigh          = data.threshold.cannyHigh;
    gBrightness         = data.threshold.brightness;
    gContrast           = data.threshold.contrast;
    gProcessMode        = data.threshold.processMode;

    gPipe.enableBlur      = data.threshold.pipeBlur;
    gPipe.enableThreshold = data.threshold.pipeThreshold;
    gPipe.enableCanny     = data.threshold.pipeCanny;
    gPipe.blurSize        = data.threshold.pipeBlurSize;
    gPipe.threshold       = data.threshold.pipeThresholdVal;
    gPipe.cannyLow        = data.threshold.pipeCannyLow;
    gPipe.cannyHigh       = data.threshold.pipeCannyHigh;

    // 模板匹配
    g_TMSearchMode      = data.tmMatch.searchMode;
    g_TMMaxResults      = data.tmMatch.maxResults;
    g_TMMaxImageDim     = data.tmMatch.maxImageDim;
    g_TMMatchThreshold  = data.tmMatch.matchThreshold;
    g_TMEnableRotation  = data.tmMatch.enableRotation;
    g_TMRotationStart   = data.tmMatch.rotationStart;
    g_TMRotationEnd     = data.tmMatch.rotationEnd;
    g_TMRotationStep    = data.tmMatch.rotationStep;

    // ROI
    UI::gROIs.clear();
    for (const auto& r : data.rois)
    {
        ROI roi;
        roi.start = ImVec2(r.startX, r.startY);
        roi.end   = ImVec2(r.endX, r.endY);
        roi.type  = r.type;
        UI::gROIs.push_back(roi);
    }

    // 工具实例
    UI::g_ToolInstances.clear();
    LogSystem::Add(LOG_INFO, color, "[Apply] 准备恢复 %zu 个工具实例", data.tools.size());
    for (size_t ti = 0; ti < data.tools.size(); ti++)
    {
        const auto& t = data.tools[ti];
        ToolInstance it;
        it.type             = t.type;
        it.enableRotation   = t.enableRotation;
        it.rotationStart    = t.rotationStart;
        it.rotationEnd      = t.rotationEnd;
        it.rotationStep     = t.rotationStep;
        it.maxResults       = t.maxResults;
        it.matchThreshold   = t.matchThreshold;
        it.maxImageDim      = t.maxImageDim;
        it.nmsThreshold     = t.nmsThreshold;
        it.searchMode       = t.searchMode;
        it.tplGray          = t.tplGray;
        it.tplBinary        = t.tplBinary;
        it.tplBinThresh     = t.tplBinThresh;
        it.tplEdge          = t.tplEdge;
        it.tplEdgeLow       = t.tplEdgeLow;
        it.tplEdgeHigh      = t.tplEdgeHigh;
        it.imgUseGray       = t.imgUseGray;
        it.imgEnableThreshold = t.imgEnableThreshold;
        it.imgThreshold     = t.imgThreshold;
        it.cannyLow         = t.cannyLow;
        it.cannyHigh        = t.cannyHigh;
        it.edgeUseGray      = t.edgeUseGray;
        it.dbgUseGray       = t.dbgUseGray;
        it.dbgEnableBlur    = t.dbgEnableBlur;
        it.dbgBlurSize      = t.dbgBlurSize;
        it.dbgEnableThresh  = t.dbgEnableThresh;
        it.dbgThreshold     = t.dbgThreshold;
        it.dbgEnableCanny   = t.dbgEnableCanny;
        it.dbgCannyLow      = t.dbgCannyLow;
        it.dbgCannyHigh     = t.dbgCannyHigh;

        // 搜索ROI
        for (const auto& r : t.searchROIs)
        {
            ROI roi;
            roi.start = ImVec2(r.startX, r.startY);
            roi.end   = ImVec2(r.endX, r.endY);
            roi.type  = r.type;
            it.searchROIs.push_back(roi);
        }

        // 加载模板图像
        if (!t.templateFile.empty() && !g_LastRecipePath.empty())
        {
            size_t slash = g_LastRecipePath.find_last_of("\\/");
            std::string tplPath = (slash != std::string::npos)
                ? g_LastRecipePath.substr(0, slash + 1) + t.templateFile
                : t.templateFile;
            it.templateImg = cv::imread(tplPath, cv::IMREAD_COLOR);
            if (!it.templateImg.empty())
                LogSystem::Add(LOG_INFO, "工具模板已加载: %s (%dx%d)", tplPath.c_str(),
                    it.templateImg.cols, it.templateImg.rows);
        }

        UI::g_ToolInstances.push_back(it);
    }

    LogSystem::Add(LOG_INFO, color, "[Apply] 配方已应用: %s (工具: %zu)", data.name.c_str(), data.tools.size());
}

} // namespace RecipeManager
