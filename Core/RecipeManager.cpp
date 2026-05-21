#include "RecipeManager.h"
#include "DX12Context.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/TemplateMatch.h"
#include "../UI/ROIManager.h"
#include "../UI/DockSpaceHost.h"

#include <windows.h>
#include <fstream>
#include <sstream>

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

// ===================== 手写 JSON 辅助 =====================
static void WriteBool(std::ofstream& f, const char* key, bool val)
{
    f << "\"" << key << "\": " << (val ? "true" : "false");
}
static void WriteInt(std::ofstream& f, const char* key, int val)
{
    f << "\"" << key << "\": " << val;
}
static void WriteFloat(std::ofstream& f, const char* key, float val)
{
    f << "\"" << key << "\": " << std::fixed << std::setprecision(4) << val;
}
static void WriteString(std::ofstream& f, const char* key, const std::string& val)
{
    f << "\"" << key << "\": \"" << val << "\"";
}

// ===================== 保存 =====================
bool Save(const char* filepath, const RecipeData& data)
{
    std::ofstream f(filepath);
    if (!f)
    {
        LogSystem::Add(LOG_ERROR, "RecipeManager: 无法写入 %s", filepath);
        return false;
    }

    f << "{\n";
    f << "\"name\": \"" << data.name << "\",\n";
    f << "\"imagePath\": \"" << data.imagePath << "\",\n";

    // 阈值
    f << "\"threshold\": {\n";
    WriteBool(f, "useGray",         data.threshold.useGray);         f << ",\n";
    WriteInt (f, "thresholdValue",  data.threshold.thresholdValue);  f << ",\n";
    WriteBool(f, "binaryInv",       data.threshold.binaryInv);       f << ",\n";
    WriteInt (f, "blurSize",        data.threshold.blurSize);        f << ",\n";
    WriteInt (f, "cannyLow",        data.threshold.cannyLow);        f << ",\n";
    WriteInt (f, "cannyHigh",       data.threshold.cannyHigh);       f << ",\n";
    WriteFloat(f, "brightness",     data.threshold.brightness);      f << ",\n";
    WriteFloat(f, "contrast",       data.threshold.contrast);        f << ",\n";
    WriteInt (f, "processMode",     data.threshold.processMode);     f << ",\n";
    WriteBool(f, "pipeBlur",        data.threshold.pipeBlur);        f << ",\n";
    WriteBool(f, "pipeThreshold",   data.threshold.pipeThreshold);   f << ",\n";
    WriteBool(f, "pipeCanny",       data.threshold.pipeCanny);       f << ",\n";
    WriteInt (f, "pipeBlurSize",    data.threshold.pipeBlurSize);    f << ",\n";
    WriteInt (f, "pipeThresholdVal", data.threshold.pipeThresholdVal); f << ",\n";
    WriteInt (f, "pipeCannyLow",    data.threshold.pipeCannyLow);    f << ",\n";
    WriteInt (f, "pipeCannyHigh",   data.threshold.pipeCannyHigh);   f << "\n";
    f << "},\n";

    // 模板匹配
    f << "\"templateMatch\": {\n";
    WriteInt (f, "method",          data.tmMatch.method);            f << ",\n";
    WriteInt (f, "searchMode",      data.tmMatch.searchMode);        f << ",\n";
    WriteInt (f, "maxResults",      data.tmMatch.maxResults);        f << ",\n";
    WriteInt (f, "maxImageDim",     data.tmMatch.maxImageDim);       f << ",\n";
    WriteFloat(f, "matchThreshold", data.tmMatch.matchThreshold);    f << ",\n";
    WriteBool(f, "enableRotation",  data.tmMatch.enableRotation);    f << ",\n";
    WriteInt (f, "rotationStart",   data.tmMatch.rotationStart);     f << ",\n";
    WriteInt (f, "rotationEnd",     data.tmMatch.rotationEnd);       f << ",\n";
    WriteInt (f, "rotationStep",    data.tmMatch.rotationStep);      f << "\n";
    f << "},\n";

    // ROI
    f << "\"rois\": [\n";
    for (size_t i = 0; i < data.rois.size(); i++)
    {
        const auto& r = data.rois[i];
        f << "  {";
        WriteFloat(f, "startX", r.startX); f << ", ";
        WriteFloat(f, "startY", r.startY); f << ", ";
        WriteFloat(f, "endX",   r.endX);   f << ", ";
        WriteFloat(f, "endY",   r.endY);   f << ", ";
        WriteInt  (f, "type",   r.type);   f << "}";
        if (i + 1 < data.rois.size()) f << ",";
        f << "\n";
    }
    f << "],\n";

    // ---- 工具实例 ----
    f << "\"tools\": [\n";
    for (size_t ti = 0; ti < data.tools.size(); ti++)
    {
        const auto& t = data.tools[ti];
        f << "  {\n";
        WriteInt   (f, "type",           t.type);           f << ",\n";
        WriteString(f, "templateFile",   t.templateFile);   f << ",\n";
        WriteBool  (f, "enableRotation", t.enableRotation); f << ",\n";
        WriteInt   (f, "rotationStart",  t.rotationStart);  f << ",\n";
        WriteInt   (f, "rotationEnd",    t.rotationEnd);    f << ",\n";
        WriteInt   (f, "rotationStep",   t.rotationStep);   f << ",\n";
        WriteInt   (f, "maxResults",     t.maxResults);     f << ",\n";
        WriteFloat (f, "matchThreshold", t.matchThreshold); f << ",\n";
        WriteInt   (f, "maxImageDim",    t.maxImageDim);    f << ",\n";
        WriteFloat (f, "nmsThreshold",   t.nmsThreshold);   f << ",\n";
        WriteInt   (f, "searchMode",     t.searchMode);     f << ",\n";
        WriteBool  (f, "tplGray",        t.tplGray);        f << ",\n";
        WriteBool  (f, "tplBinary",      t.tplBinary);      f << ",\n";
        WriteInt   (f, "tplBinThresh",   t.tplBinThresh);   f << ",\n";
        WriteBool  (f, "tplEdge",        t.tplEdge);        f << ",\n";
        WriteInt   (f, "tplEdgeLow",     t.tplEdgeLow);     f << ",\n";
        WriteInt   (f, "tplEdgeHigh",    t.tplEdgeHigh);    f << ",\n";
        WriteBool  (f, "imgUseGray",     t.imgUseGray);     f << ",\n";
        WriteBool  (f, "imgEnableThresh", t.imgEnableThreshold); f << ",\n";
        WriteInt   (f, "imgThreshold",   t.imgThreshold);   f << ",\n";
        WriteInt   (f, "cannyLow",       t.cannyLow);       f << ",\n";
        WriteInt   (f, "cannyHigh",      t.cannyHigh);      f << ",\n";
        WriteBool  (f, "edgeUseGray",    t.edgeUseGray);    f << ",\n";
        WriteBool  (f, "dbgUseGray",     t.dbgUseGray);     f << ",\n";
        WriteBool  (f, "dbgEnableBlur",  t.dbgEnableBlur);  f << ",\n";
        WriteInt   (f, "dbgBlurSize",    t.dbgBlurSize);    f << ",\n";
        WriteBool  (f, "dbgEnableThresh", t.dbgEnableThresh); f << ",\n";
        WriteInt   (f, "dbgThreshold",   t.dbgThreshold);   f << ",\n";
        WriteBool  (f, "dbgEnableCanny", t.dbgEnableCanny); f << ",\n";
        WriteInt   (f, "dbgCannyLow",    t.dbgCannyLow);    f << ",\n";
        WriteInt   (f, "dbgCannyHigh",   t.dbgCannyHigh);   f << ",\n";
        // searchROIs
        f << "\"searchROIs\": [";
        for (size_t ri = 0; ri < t.searchROIs.size(); ri++)
        {
            const auto& r = t.searchROIs[ri];
            f << "{";
            WriteFloat(f, "startX", r.startX); f << ", ";
            WriteFloat(f, "startY", r.startY); f << ", ";
            WriteFloat(f, "endX",   r.endX);   f << ", ";
            WriteFloat(f, "endY",   r.endY);   f << ", ";
            WriteInt  (f, "type",   r.type);   f << "}";
            if (ri + 1 < t.searchROIs.size()) f << ", ";
        }
        f << "]\n";
        f << "  }";
        if (ti + 1 < data.tools.size()) f << ",";
        f << "\n";
    }
    f << "],\n";

    // 模板图像路径（与配方文件同名 .png）
    f << "\"templateImage\": \"" << data.templateImage << "\"\n";
    f << "}\n";

    LogSystem::Add(LOG_INFO, color, "[Save] 已写入 %zu 个工具实例", data.tools.size());

    // 保存模板图像（与配方同目录、同名 .png）
    if (!data.templateImage.empty())
    {
        // 从 recipe 文件路径推导模板图像路径
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

        // 从 g_ToolInstances 找到对应实例的模板数据并保存
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

// ===================== 简易 JSON 解析 =====================
static std::string ReadFile(const char* path)
{
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static std::string StrVal(const std::string& json, const char* key)
{
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return "";
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return "";
    p = json.find('"', p + 1);
    if (p == std::string::npos) return "";
    size_t e = json.find('"', p + 1);
    if (e == std::string::npos) return "";
    return json.substr(p + 1, e - p - 1);
}
static int IntVal(const std::string& json, const char* key, int def = 0)
{
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return def;
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return def;
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n')) p++;
    std::string num;
    bool neg = false;
    if (p < json.size() && json[p] == '-') { neg = true; p++; }
    while (p < json.size() && json[p] >= '0' && json[p] <= '9') { num += json[p]; p++; }
    if (num.empty()) return def;
    int v = std::stoi(num);
    return neg ? -v : v;
}
static float FloatVal(const std::string& json, const char* key, float def = 0.0f)
{
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return def;
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return def;
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n')) p++;
    std::string num;
    bool neg = false;
    if (p < json.size() && json[p] == '-') { neg = true; p++; }
    while (p < json.size() && ((json[p] >= '0' && json[p] <= '9') || json[p] == '.'))
    {
        num += json[p]; p++;
    }
    if (num.empty()) return def;
    float v = std::stof(num);
    return neg ? -v : v;
}
static bool BoolVal(const std::string& json, const char* key, bool def = false)
{
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return def;
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return def;
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n')) p++;
    if (p + 4 <= json.size() && json.substr(p, 4) == "true") return true;
    return false;
}

// ===================== 加载 =====================
static std::string g_LastRecipePath; // 记录最近加载的配方路径（用于加载工具模板）

bool Load(const char* filepath, RecipeData& data)
{
    g_LastRecipePath = filepath;  // 记录路径供 Apply() 加载工具模板
    std::string json = ReadFile(filepath);
    if (json.empty())
    {
        LogSystem::Add(LOG_ERROR, "RecipeManager: 无法打开 %s", filepath);
        return false;
    }

    data.name      = StrVal(json, "name");
    data.imagePath = StrVal(json, "imagePath");
    data.templateImage = StrVal(json, "templateImage");

    // 阈值
    data.threshold.useGray          = BoolVal(json, "useGray");
    data.threshold.thresholdValue   = IntVal(json, "thresholdValue", 128);
    data.threshold.binaryInv        = BoolVal(json, "binaryInv");
    data.threshold.blurSize         = IntVal(json, "blurSize", 1);
    data.threshold.cannyLow         = IntVal(json, "cannyLow", 50);
    data.threshold.cannyHigh        = IntVal(json, "cannyHigh", 150);
    data.threshold.brightness       = FloatVal(json, "brightness");
    data.threshold.contrast         = FloatVal(json, "contrast", 1.0f);
    data.threshold.processMode      = IntVal(json, "processMode");
    data.threshold.pipeBlur         = BoolVal(json, "pipeBlur");
    data.threshold.pipeThreshold    = BoolVal(json, "pipeThreshold");
    data.threshold.pipeCanny        = BoolVal(json, "pipeCanny");
    data.threshold.pipeBlurSize     = IntVal(json, "pipeBlurSize", 5);
    data.threshold.pipeThresholdVal = IntVal(json, "pipeThresholdVal", 128);
    data.threshold.pipeCannyLow     = IntVal(json, "pipeCannyLow", 50);
    data.threshold.pipeCannyHigh    = IntVal(json, "pipeCannyHigh", 150);

    // 模板匹配
    data.tmMatch.method          = IntVal(json, "method", 5);
    data.tmMatch.searchMode      = IntVal(json, "searchMode");
    data.tmMatch.maxResults      = IntVal(json, "maxResults", 10);
    data.tmMatch.maxImageDim     = IntVal(json, "maxImageDim", 1000);
    data.tmMatch.matchThreshold  = FloatVal(json, "matchThreshold", 0.75f);
    data.tmMatch.enableRotation  = BoolVal(json, "enableRotation");
    data.tmMatch.rotationStart   = IntVal(json, "rotationStart", -5);
    data.tmMatch.rotationEnd     = IntVal(json, "rotationEnd", 5);
    data.tmMatch.rotationStep    = IntVal(json, "rotationStep", 5);

    // ROI — 解析数组
    data.rois.clear();
    size_t arrStart = json.find("\"rois\"");
    if (arrStart != std::string::npos)
    {
        size_t roiBracket = json.find('[', arrStart);
        if (roiBracket != std::string::npos)
        {
            // 用深度计数找 rois 数组的结束 ]
            int roiArrDepth = 1;
            size_t roiEnd = roiBracket + 1;
            while (roiEnd < json.size() && roiArrDepth > 0)
            {
                if (json[roiEnd] == '[') roiArrDepth++;
                else if (json[roiEnd] == ']') roiArrDepth--;
                roiEnd++;
            }
            roiEnd--; // 指向 ]

            size_t pos = roiBracket + 1;
            while (pos < roiEnd)
            {
                size_t objStart = json.find('{', pos);
                if (objStart == std::string::npos || objStart >= roiEnd) break;
                size_t objEnd = json.find('}', objStart);
                if (objEnd == std::string::npos) break;

                RecipeROI roi;
                roi.startX = FloatVal(json.substr(objStart, objEnd - objStart + 1), "startX");
                roi.startY = FloatVal(json.substr(objStart, objEnd - objStart + 1), "startY");
                roi.endX   = FloatVal(json.substr(objStart, objEnd - objStart + 1), "endX");
                roi.endY   = FloatVal(json.substr(objStart, objEnd - objStart + 1), "endY");
                roi.type   = IntVal(json.substr(objStart, objEnd - objStart + 1), "type", 0);
                data.rois.push_back(roi);
                pos = objEnd + 1;
            }
        }
    }

    // ---- 工具实例 ----
    data.tools.clear();
    size_t toolsStart = json.find("\"tools\"");
    if (toolsStart != std::string::npos)
    {
        size_t toolsBracket = json.find('[', toolsStart);
        if (toolsBracket != std::string::npos)
        {
            // 用深度计数找 tools 数组的结束 ]
            int arrDepth = 1;
            size_t toolsEnd = toolsBracket + 1;
            while (toolsEnd < json.size() && arrDepth > 0)
            {
                if (json[toolsEnd] == '[') arrDepth++;
                else if (json[toolsEnd] == ']') arrDepth--;
                toolsEnd++;
            }
            toolsEnd--; // 指向 ]

            size_t pos = toolsBracket + 1;
            int toolCount = 0;
            while (pos < toolsEnd)
            {
                size_t objStart = json.find('{', pos);
                if (objStart == std::string::npos || objStart >= toolsEnd) break;
                size_t objEnd = json.find('}', objStart);
                if (objEnd == std::string::npos) break;
                // 找嵌套对象结束（searchROIs数组可能包含 {}）
                int depth = 1;
                size_t p = objStart + 1;
                while (p < json.size() && depth > 0)
                {
                    if (json[p] == '{') depth++;
                    else if (json[p] == '}') depth--;
                    p++;
                }
                objEnd = p - 1;

                std::string toolJson = json.substr(objStart, objEnd - objStart + 1);
                toolCount++;
                LogSystem::Add(LOG_INFO, color, "[Load] 解析工具 #%d, json长度=%zu", toolCount, toolJson.size());

                RecipeToolInstance t;
                t.type             = IntVal(toolJson, "type");
                t.templateFile     = StrVal(toolJson, "templateFile");
                t.enableRotation   = BoolVal(toolJson, "enableRotation");
                t.rotationStart    = IntVal(toolJson, "rotationStart", -45);
                t.rotationEnd      = IntVal(toolJson, "rotationEnd", 45);
                t.rotationStep     = IntVal(toolJson, "rotationStep", 1);
                t.maxResults       = IntVal(toolJson, "maxResults", 5);
                t.matchThreshold   = FloatVal(toolJson, "matchThreshold", 0.7f);
                t.maxImageDim      = IntVal(toolJson, "maxImageDim", 1000);
                t.nmsThreshold     = FloatVal(toolJson, "nmsThreshold", 0.3f);
                t.searchMode       = IntVal(toolJson, "searchMode");
                t.tplGray          = BoolVal(toolJson, "tplGray");
                t.tplBinary        = BoolVal(toolJson, "tplBinary");
                t.tplBinThresh     = IntVal(toolJson, "tplBinThresh", 128);
                t.tplEdge          = BoolVal(toolJson, "tplEdge");
                t.tplEdgeLow       = IntVal(toolJson, "tplEdgeLow", 50);
                t.tplEdgeHigh      = IntVal(toolJson, "tplEdgeHigh", 150);
                t.imgUseGray       = BoolVal(toolJson, "imgUseGray");
                t.imgEnableThreshold = BoolVal(toolJson, "imgEnableThresh");
                t.imgThreshold     = IntVal(toolJson, "imgThreshold", 128);
                t.cannyLow         = IntVal(toolJson, "cannyLow", 50);
                t.cannyHigh        = IntVal(toolJson, "cannyHigh", 150);
                t.edgeUseGray      = BoolVal(toolJson, "edgeUseGray");
                t.dbgUseGray       = BoolVal(toolJson, "dbgUseGray");
                t.dbgEnableBlur    = BoolVal(toolJson, "dbgEnableBlur");
                t.dbgBlurSize      = IntVal(toolJson, "dbgBlurSize", 5);
                t.dbgEnableThresh  = BoolVal(toolJson, "dbgEnableThresh");
                t.dbgThreshold     = IntVal(toolJson, "dbgThreshold", 128);
                t.dbgEnableCanny   = BoolVal(toolJson, "dbgEnableCanny");
                t.dbgCannyLow      = IntVal(toolJson, "dbgCannyLow", 50);
                t.dbgCannyHigh     = IntVal(toolJson, "dbgCannyHigh", 150);

                // 解析 searchROIs 子数组
                size_t srStart = toolJson.find("\"searchROIs\"");
                if (srStart != std::string::npos)
                {
                    srStart = toolJson.find('[', srStart);
                    if (srStart != std::string::npos)
                    {
                        size_t sp = srStart + 1;
                        while (sp < toolJson.size())
                        {
                            size_t soStart = toolJson.find('{', sp);
                            if (soStart == std::string::npos || soStart > toolJson.find(']', srStart)) break;
                            size_t soEnd = toolJson.find('}', soStart);
                            if (soEnd == std::string::npos) break;
                            std::string roiJson = toolJson.substr(soStart, soEnd - soStart + 1);
                            RecipeROI rr;
                            rr.startX = FloatVal(roiJson, "startX");
                            rr.startY = FloatVal(roiJson, "startY");
                            rr.endX   = FloatVal(roiJson, "endX");
                            rr.endY   = FloatVal(roiJson, "endY");
                            rr.type   = IntVal(roiJson, "type", 0);
                            t.searchROIs.push_back(rr);
                            sp = soEnd + 1;
                        }
                    }
                }

                data.tools.push_back(t);
                pos = objEnd + 1;
            }
        }
    }

    if (!data.templateImage.empty())
    {
        // 从 recipe 文件路径推导模板图像路径
        std::string tplPath(filepath);
        size_t slash = tplPath.find_last_of("\\/");
        tplPath = (slash != std::string::npos)
            ? tplPath.substr(0, slash + 1) + data.templateImage
            : data.templateImage;

        if (TemplateMatch::LoadTemplate(tplPath.c_str()))
            LogSystem::Add(LOG_INFO, "模板图像已加载: %s", tplPath.c_str());
    }

    // 加载每个工具实例的模板图像（在 Apply 中恢复 g_ToolInstances 后加载）
    // 这里只记录路径，实际加载在 Apply() 中

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

    // 模板图像路径（与配方同名 .png）
    d.templateImage = d.name + ".png";

    // ---- 工具实例 ----
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
    // 图片路径
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

    // ---- 工具实例 ----
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
