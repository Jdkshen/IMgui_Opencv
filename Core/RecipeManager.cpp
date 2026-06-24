#include "RecipeManager.h"
#include "DX12Context.h"
#include "FrameSourceState.h"
#include "ROIState.h"
#include "ToolChainState.h"
#include "UIStateBridge.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/TemplateMatch.h"
#include "../Algorithm/MultiColorFinder.h"

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// 外部变量引用
extern std::string pendingPath;
extern bool gUseGray;
extern int gThresholdValue;
extern bool gThresholdBinaryInv;
extern int gBlurSize;
extern int gCannyLow, gCannyHigh;
extern float gBrightness, gContrast;
extern int gProcessMode;
extern PipelineState gPipe;
extern cv::Mat g_FrozenTemplate;
extern bool g_ShowPreview;

namespace RecipeManager
{
    static std::wstring Utf8ToWide(const std::string &text)
    {
        if (text.empty())
            return {};

        int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), (int)text.size(), nullptr, 0);
        if (len <= 0)
            len = MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), nullptr, 0);
        if (len <= 0)
            return {};

        std::wstring wide(len, L'\0');
        UINT cp = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), (int)text.size(), wide.data(), len) > 0
                      ? CP_UTF8
                      : CP_ACP;
        MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, text.c_str(), (int)text.size(), wide.data(), len);
        return wide;
    }

    static std::string WideToUtf8(const std::wstring &text)
    {
        if (text.empty())
            return {};

        int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0)
            return {};

        std::string utf8(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), utf8.data(), len, nullptr, nullptr);
        return utf8;
    }

    static std::string ParentDirOf(const std::string &path)
    {
        size_t slash = path.find_last_of("\\/");
        return slash != std::string::npos ? path.substr(0, slash + 1) : std::string();
    }

    static bool WriteTextUtf8File(const std::string &path, const std::string &content)
    {
        std::ofstream f(Utf8ToWide(path), std::ios::binary);
        if (!f)
            return false;
        f.write(content.data(), (std::streamsize)content.size());
        return f.good();
    }

    static bool ReadTextUtf8File(const std::string &path, std::string &content)
    {
        std::ifstream f(Utf8ToWide(path), std::ios::binary);
        if (!f)
            return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        content = ss.str();
        return true;
    }

    static std::string Base64Encode(const unsigned char *data, size_t size)
    {
        static constexpr char kTable[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((size + 2) / 3) * 4);

        for (size_t i = 0; i < size; i += 3)
        {
            const unsigned int b0 = data[i];
            const unsigned int b1 = (i + 1 < size) ? data[i + 1] : 0;
            const unsigned int b2 = (i + 2 < size) ? data[i + 2] : 0;
            const unsigned int triple = (b0 << 16) | (b1 << 8) | b2;

            out.push_back(kTable[(triple >> 18) & 0x3F]);
            out.push_back(kTable[(triple >> 12) & 0x3F]);
            out.push_back((i + 1 < size) ? kTable[(triple >> 6) & 0x3F] : '=');
            out.push_back((i + 2 < size) ? kTable[triple & 0x3F] : '=');
        }

        return out;
    }

    static std::string Base64Encode(const std::vector<uchar> &data)
    {
        return data.empty() ? std::string() : Base64Encode(data.data(), data.size());
    }

    static bool IsLikelyBase64(std::string_view text)
    {
        if (text.empty() || (text.size() % 4) != 0)
            return false;
        for (char ch : text)
        {
            const bool ok = (ch >= 'A' && ch <= 'Z') ||
                            (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') ||
                            ch == '+' || ch == '/' || ch == '=';
            if (!ok)
                return false;
        }
        return true;
    }

    static std::vector<uchar> Base64Decode(std::string_view text)
    {
        static constexpr unsigned char kInvalid = 0xFF;
        unsigned char table[256];
        std::fill_n(table, 256, kInvalid);
        for (unsigned char c = 'A'; c <= 'Z'; ++c) table[c] = c - 'A';
        for (unsigned char c = 'a'; c <= 'z'; ++c) table[c] = c - 'a' + 26;
        for (unsigned char c = '0'; c <= '9'; ++c) table[c] = c - '0' + 52;
        table[(unsigned char)'+'] = 62;
        table[(unsigned char)'/'] = 63;

        std::vector<uchar> out;
        if (!IsLikelyBase64(text))
            return out;
        out.reserve((text.size() / 4) * 3);

        for (size_t i = 0; i < text.size(); i += 4)
        {
            const unsigned char c0 = (unsigned char)text[i];
            const unsigned char c1 = (unsigned char)text[i + 1];
            const unsigned char c2 = (unsigned char)text[i + 2];
            const unsigned char c3 = (unsigned char)text[i + 3];
            if (table[c0] == kInvalid || table[c1] == kInvalid ||
                (c2 != '=' && table[c2] == kInvalid) ||
                (c3 != '=' && table[c3] == kInvalid))
            {
                out.clear();
                return out;
            }

            const unsigned int triple =
                (table[c0] << 18) |
                (table[c1] << 12) |
                ((c2 == '=') ? 0 : (table[c2] << 6)) |
                ((c3 == '=') ? 0 : table[c3]);

            out.push_back((uchar)((triple >> 16) & 0xFF));
            if (c2 != '=')
                out.push_back((uchar)((triple >> 8) & 0xFF));
            if (c3 != '=')
                out.push_back((uchar)(triple & 0xFF));
        }

        return out;
    }

    static std::string NormalizeBinaryStringForJson(const std::string &value)
    {
        if (value.empty() || IsLikelyBase64(value))
            return value;
        return Base64Encode(reinterpret_cast<const unsigned char *>(value.data()), value.size());
    }

    static bool WriteImageFile(const std::string &path, const cv::Mat &image)
    {
        if (image.empty())
            return false;

        std::string ext = ".png";
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos)
            ext = path.substr(dot);

        std::vector<uchar> buf;
        if (!cv::imencode(ext, image, buf))
            return false;

        std::ofstream f(Utf8ToWide(path), std::ios::binary);
        if (!f)
            return false;
        f.write(reinterpret_cast<const char *>(buf.data()), (std::streamsize)buf.size());
        return f.good();
    }

    static cv::Mat ReadImageFile(const std::string &path, int flags)
    {
        std::ifstream f(Utf8ToWide(path), std::ios::binary);
        if (!f)
            return {};

        std::vector<uchar> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (buf.empty())
            return {};
        return cv::imdecode(buf, flags);
    }

    // ===================== 保存 =====================
    bool Save(const char *filepath, const RecipeData &data)
    {
        json j;

        j["version"] = 1;
        j["name"] = data.name;
        j["imagePath"] = data.imagePath;
        j["templateImage"] = data.templateImage;

        // 阈值参数
        json &th = j["threshold"];
        th["useGray"] = data.threshold.useGray;
        th["thresholdValue"] = data.threshold.thresholdValue;
        th["binaryInv"] = data.threshold.binaryInv;
        th["blurSize"] = data.threshold.blurSize;
        th["cannyLow"] = data.threshold.cannyLow;
        th["cannyHigh"] = data.threshold.cannyHigh;
        th["brightness"] = data.threshold.brightness;
        th["contrast"] = data.threshold.contrast;
        th["processMode"] = data.threshold.processMode;
        th["pipeBlur"] = data.threshold.pipeBlur;
        th["pipeThreshold"] = data.threshold.pipeThreshold;
        th["pipeCanny"] = data.threshold.pipeCanny;
        th["pipeBlurSize"] = data.threshold.pipeBlurSize;
        th["pipeThresholdVal"] = data.threshold.pipeThresholdVal;
        th["pipeCannyLow"] = data.threshold.pipeCannyLow;
        th["pipeCannyHigh"] = data.threshold.pipeCannyHigh;

        // Template match settings
        json &tm = j["templateMatch"];
        tm["method"] = data.tmMatch.method;
        tm["searchMode"] = data.tmMatch.searchMode;
        tm["maxResults"] = data.tmMatch.maxResults;
        tm["maxImageDim"] = data.tmMatch.maxImageDim;
        tm["matchThreshold"] = data.tmMatch.matchThreshold;
        tm["enableRotation"] = data.tmMatch.enableRotation;
        tm["rotationStart"] = data.tmMatch.rotationStart;
        tm["rotationEnd"] = data.tmMatch.rotationEnd;
        tm["rotationStep"] = data.tmMatch.rotationStep;

        // ROI 数组
        json &rois = j["rois"] = json::array();
        for (const auto &r : data.rois)
        {
            rois.push_back({{"startX", r.startX}, {"startY", r.startY}, {"endX", r.endX}, {"endY", r.endY}, {"type", r.type}});
        }

        // Tool instances
        json &tools = j["tools"] = json::array();
        for (const auto &t : data.tools)
        {
            json tj;
            tj["type"] = t.type;
            tj["label"] = t.label;
            tj["inputSourceMode"] = t.inputSourceMode;
            tj["templateFile"] = t.templateFile;
            tj["hasTemplateROI"] = t.hasTemplateROI;
            tj["templateROI"] = {{"startX", t.templateROI.startX}, {"startY", t.templateROI.startY}, {"endX", t.templateROI.endX}, {"endY", t.templateROI.endY}, {"type", t.templateROI.type}};
            tj["enableRotation"] = t.enableRotation;
            tj["rotationStart"] = t.rotationStart;
            tj["rotationEnd"] = t.rotationEnd;
            tj["rotationStep"] = t.rotationStep;
            tj["maxResults"] = t.maxResults;
            tj["matchThreshold"] = t.matchThreshold;
            tj["maxImageDim"] = t.maxImageDim;
            tj["nmsThreshold"] = t.nmsThreshold;
            tj["searchMode"] = t.searchMode;
            tj["tplGray"] = t.tplGray;
            tj["tplBinary"] = t.tplBinary;
            tj["tplBinThresh"] = t.tplBinThresh;
            tj["tplEdge"] = t.tplEdge;
            tj["tplEdgeLow"] = t.tplEdgeLow;
            tj["tplEdgeHigh"] = t.tplEdgeHigh;
            tj["imgUseGray"] = t.imgUseGray;
            tj["imgEnableThreshold"] = t.imgEnableThreshold;
            tj["imgThreshold"] = t.imgThreshold;
            tj["cannyLow"] = t.cannyLow;
            tj["cannyHigh"] = t.cannyHigh;
            tj["edgeUseGray"] = t.edgeUseGray;
            tj["dbgUseGray"] = t.dbgUseGray;
            tj["dbgEnableBlur"] = t.dbgEnableBlur;
            tj["dbgBlurSize"] = t.dbgBlurSize;
            tj["dbgEnableThresh"] = t.dbgEnableThresh;
            tj["dbgThreshold"] = t.dbgThreshold;
            tj["dbgEnableCanny"] = t.dbgEnableCanny;
            tj["dbgCannyLow"] = t.dbgCannyLow;
            tj["dbgCannyHigh"] = t.dbgCannyHigh;
            tj["blobMinArea"] = t.blobMinArea;
            tj["blobMaxArea"] = t.blobMaxArea;
            tj["useSearchROI"] = t.useSearchROI;

            // YOLO 参数
            tj["yoloModelPath"] = t.yoloModelPath;
            tj["yoloClassesPath"] = t.yoloClassesPath;
            tj["yoloConfThreshold"] = t.yoloConfThreshold;
            tj["yoloNmsThreshold"] = t.yoloNmsThreshold;
            tj["yoloUseROI"] = t.yoloUseROI;
            tj["yoloUseGPU"] = t.yoloUseGPU;
            tj["cntUseGray"] = t.cntUseGray;
            tj["cntBlurSize"] = t.cntBlurSize;
            tj["cntThreshMode"] = t.cntThreshMode;
            tj["cntThreshValue"] = t.cntThreshValue;
            tj["cntAdaptBlock"] = t.cntAdaptBlock;
            tj["cntInvert"] = t.cntInvert;
            tj["cntRetrMode"] = t.cntRetrMode;
            tj["cntApproxMethod"] = t.cntApproxMethod;
            tj["cntMinArea"] = t.cntMinArea;
            tj["cntMaxContours"] = t.cntMaxContours;
            tj["cntFilterConvex"] = t.cntFilterConvex;
            tj["cntApproxEps"] = t.cntApproxEps;
            tj["cntLineThick"] = t.cntLineThick;
            tj["cntShowLabels"] = t.cntShowLabels;
            tj["cntFillContours"] = t.cntFillContours;
            tj["cntMatchROI"] = t.cntMatchROI;
            tj["cntMatchThresh"] = t.cntMatchThresh;
            tj["shpBlurSize"] = t.shpBlurSize;
            tj["shpTplRetr"] = t.shpTplRetr;
            tj["shpTplMinArea"] = t.shpTplMinArea;
            tj["shpMinScore"] = t.shpMinScore;
            tj["shpShapeScore"] = t.shpShapeScore;
            tj["shpLineThick"] = t.shpLineThick;
            tj["shpMethod"] = t.shpMethod;
            tj["shpShowLabels"] = t.shpShowLabels;
            tj["shpMaxResults"] = t.shpMaxResults;
            tj["shpTplGray"] = t.shpTplGray;
            tj["shpTplBinary"] = t.shpTplBinary;
            tj["shpTplBinThresh"] = t.shpTplBinThresh;
            tj["shpTplBlur"] = t.shpTplBlur;
            tj["shpTplBlurK"] = t.shpTplBlurK;
            tj["shpTplInvert"] = t.shpTplInvert;
            tj["lineCannyLow"] = t.lineCannyLow;
            tj["lineCannyHigh"] = t.lineCannyHigh;
            tj["lineMinLength"] = t.lineMinLength;
            tj["lineMaxGap"] = t.lineMaxGap;
            tj["lineMinAngle"] = t.lineMinAngle;
            tj["lineMaxAngle"] = t.lineMaxAngle;
            tj["lineThickness"] = t.lineThickness;
            tj["lineMaxLines"] = t.lineMaxLines;
            tj["lineShowLabels"] = t.lineShowLabels;
            tj["lineUseROI"] = t.lineUseROI;
            tj["morphOpType"] = t.morphOpType;
            tj["morphKernelSize"] = t.morphKernelSize;
            tj["morphKernelShape"] = t.morphKernelShape;
            tj["morphIterations"] = t.morphIterations;
            tj["morphUseGray"] = t.morphUseGray;
            tj["colorSpace"] = t.colorSpace;
            tj["colorHistBins"] = t.colorHistBins;
            tj["colorShowHist"] = t.colorShowHist;
            tj["colorUseROI"] = t.colorUseROI;
            tj["colorHistHeight"] = t.colorHistHeight;
            tj["mcfUseROI"] = t.mcfUseROI;
            tj["mcfMaxResults"] = t.mcfMaxResults;
            tj["mcfMinDist"] = t.mcfMinDist;
            tj["mcfCrossSize"] = t.mcfCrossSize;
            tj["mcfCrossThick"] = t.mcfCrossThick;
            tj["mcfAnchorX"] = t.mcfAnchorX;
            tj["mcfAnchorY"] = t.mcfAnchorY;
            tj["mcfImgGray"] = t.mcfImgGray;
            tj["mcfImgBinary"] = t.mcfImgBinary;
            tj["mcfImgBinThresh"] = t.mcfImgBinThresh;
            tj["mcfRoiX"] = t.mcfRoiX;
            tj["mcfRoiY"] = t.mcfRoiY;
            tj["mcfRoiW"] = t.mcfRoiW;
            tj["mcfRoiH"] = t.mcfRoiH;
            tj["mcfRefImageBase64"] = NormalizeBinaryStringForJson(t.mcfRefImageBase64);
            tj["mcfPointsJson"] = t.mcfPointsJson;

            // 搜索 ROI 子数组
            json &srois = tj["searchROIs"] = json::array();
            for (const auto &r : t.searchROIs)
            {
                srois.push_back({{"startX", r.startX}, {"startY", r.startY}, {"endX", r.endX}, {"endY", r.endY}, {"type", r.type}});
            }
            json &lineRois = tj["lineSaveROIs"] = json::array();
            for (const auto &r : t.lineSaveROIs)
            {
                lineRois.push_back({{"startX", r.startX}, {"startY", r.startY}, {"endX", r.endX}, {"endY", r.endY}, {"type", r.type}});
            }
            tools.push_back(tj);
        }

        // 写入文件
        if (!WriteTextUtf8File(filepath, j.dump(2)))
        {
            LogSystem::Add(LOG_ERROR, "RecipeManager: failed to write %s", filepath);
            return false;
        }

        LogSystem::Add(LOG_INFO, "[Save] tools: %zu", data.tools.size());

        // Save the legacy template image next to the recipe.
        if (!data.templateImage.empty())
        {
            std::string tplPath(filepath);
            size_t slash = tplPath.find_last_of("\\/");
            tplPath = (slash != std::string::npos)
                          ? tplPath.substr(0, slash + 1) + data.templateImage
                          : data.templateImage;

            if (WriteImageFile(tplPath, g_FrozenTemplate))
                LogSystem::Add(LOG_INFO, "Template image saved: %s", tplPath.c_str());
        }

        // Save per-tool template images.
        for (size_t ti = 0; ti < data.tools.size(); ti++)
        {
            const auto &t = data.tools[ti];
            std::string tplPath(filepath);
            size_t slash = tplPath.find_last_of("\\/");
            const std::string tplFile = t.templateFile.empty()
                ? data.name + "_tpl" + std::to_string(ti) + ".png"
                : t.templateFile;
            tplPath = (slash != std::string::npos)
                          ? tplPath.substr(0, slash + 1) + tplFile
                          : tplFile;

            if (t.templateFile.empty())
            {
                DeleteFileW(Utf8ToWide(tplPath).c_str());
                continue;
            }

            const auto& tools = ToolChainState::ReadOnlyTools();
            if (ti < tools.size())
            {
                const auto& inst = tools[ti];
                cv::Mat tpl = (inst.type == 6) ? inst.shpTplImage : inst.templateImg;
                if (tpl.empty())
                    tpl = (inst.type == 6) ? inst.templateImg : inst.shpTplImage;
                if (WriteImageFile(tplPath, tpl))
                    LogSystem::Add(LOG_INFO, "Tool template saved: %s", tplPath.c_str());
            }
        }

        LogSystem::Add(LOG_INFO, "Recipe saved: %s", filepath);
        return true;
    }

    // ===================== Load =====================
    static std::string g_LastRecipePath;

    bool Load(const char *filepath, RecipeData &data)
    {
        g_LastRecipePath = filepath;

        std::string text;
        if (!ReadTextUtf8File(filepath, text))
        {
            LogSystem::Add(LOG_ERROR, "RecipeManager: failed to open %s", filepath);
            return false;
        }

        json j;
        try
        {
            j = json::parse(text);
        }
        catch (const json::parse_error &e)
        {
            LogSystem::Add(LOG_ERROR, "RecipeManager: JSON parse failed %s (byte %zu)", e.what(), e.byte);
            return false;
        }

        int version = j.value("version", 0);
        if (version > 1)
            LogSystem::Add(LOG_WARN, "RecipeManager: recipe version %d > supported version 1", version);

        data.name = j.value("name", "");
        data.imagePath = j.value("imagePath", "");
        data.templateImage = j.value("templateImage", "");

        // 阈值参数
        if (j.contains("threshold"))
        {
            auto &th = j["threshold"];
            data.threshold.useGray = th.value("useGray", false);
            data.threshold.thresholdValue = th.value("thresholdValue", 128);
            data.threshold.binaryInv = th.value("binaryInv", false);
            data.threshold.blurSize = th.value("blurSize", 1);
            data.threshold.cannyLow = th.value("cannyLow", 50);
            data.threshold.cannyHigh = th.value("cannyHigh", 150);
            data.threshold.brightness = th.value("brightness", 0.0f);
            data.threshold.contrast = th.value("contrast", 1.0f);
            data.threshold.processMode = th.value("processMode", 0);
            data.threshold.pipeBlur = th.value("pipeBlur", false);
            data.threshold.pipeThreshold = th.value("pipeThreshold", false);
            data.threshold.pipeCanny = th.value("pipeCanny", false);
            data.threshold.pipeBlurSize = th.value("pipeBlurSize", 5);
            data.threshold.pipeThresholdVal = th.value("pipeThresholdVal", 128);
            data.threshold.pipeCannyLow = th.value("pipeCannyLow", 50);
            data.threshold.pipeCannyHigh = th.value("pipeCannyHigh", 150);
        }

        // Template match settings
        if (j.contains("templateMatch"))
        {
            auto &tm = j["templateMatch"];
            data.tmMatch.method = tm.value("method", 5);
            data.tmMatch.searchMode = tm.value("searchMode", 0);
            data.tmMatch.maxResults = tm.value("maxResults", 10);
            data.tmMatch.maxImageDim = tm.value("maxImageDim", 1000);
            data.tmMatch.matchThreshold = tm.value("matchThreshold", 0.75f);
            data.tmMatch.enableRotation = tm.value("enableRotation", false);
            data.tmMatch.rotationStart = tm.value("rotationStart", -5);
            data.tmMatch.rotationEnd = tm.value("rotationEnd", 5);
            data.tmMatch.rotationStep = tm.value("rotationStep", 5);
        }

        // ROI 数组
        data.rois.clear();
        if (j.contains("rois") && j["rois"].is_array())
        {
            for (const auto &r : j["rois"])
            {
                RecipeROI roi;
                roi.startX = r.value("startX", 0.0f);
                roi.startY = r.value("startY", 0.0f);
                roi.endX = r.value("endX", 0.0f);
                roi.endY = r.value("endY", 0.0f);
                roi.type = r.value("type", 0);
                data.rois.push_back(roi);
            }
        }

        // Tool instances
        data.tools.clear();
        if (j.contains("tools") && j["tools"].is_array())
        {
            for (const auto &tj : j["tools"])
            {
                RecipeToolInstance t;
                t.type = tj.value("type", 0);
                t.label = tj.value("label", "");
                t.inputSourceMode = tj.value("inputSourceMode", 2);
                t.templateFile = tj.value("templateFile", "");
                t.hasTemplateROI = tj.value("hasTemplateROI", false);
                if (tj.contains("templateROI") && tj["templateROI"].is_object())
                {
                    const auto &r = tj["templateROI"];
                    t.templateROI.startX = r.value("startX", 0.0f);
                    t.templateROI.startY = r.value("startY", 0.0f);
                    t.templateROI.endX = r.value("endX", 0.0f);
                    t.templateROI.endY = r.value("endY", 0.0f);
                    t.templateROI.type = r.value("type", 0);
                }
                t.enableRotation = tj.value("enableRotation", false);
                t.rotationStart = tj.value("rotationStart", -45);
                t.rotationEnd = tj.value("rotationEnd", 45);
                t.rotationStep = tj.value("rotationStep", 1);
                t.maxResults = tj.value("maxResults", 5);
                t.matchThreshold = tj.value("matchThreshold", 0.7f);
                t.maxImageDim = tj.value("maxImageDim", 1000);
                t.nmsThreshold = tj.value("nmsThreshold", 0.3f);
                t.searchMode = tj.value("searchMode", 0);
                t.tplGray = tj.value("tplGray", false);
                t.tplBinary = tj.value("tplBinary", false);
                t.tplBinThresh = tj.value("tplBinThresh", 128);
                t.tplEdge = tj.value("tplEdge", false);
                t.tplEdgeLow = tj.value("tplEdgeLow", 50);
                t.tplEdgeHigh = tj.value("tplEdgeHigh", 150);
                t.imgUseGray = tj.value("imgUseGray", false);
                t.imgEnableThreshold = tj.value("imgEnableThreshold", false);
                t.imgThreshold = tj.value("imgThreshold", 128);
                t.cannyLow = tj.value("cannyLow", 50);
                t.cannyHigh = tj.value("cannyHigh", 150);
                t.edgeUseGray = tj.value("edgeUseGray", false);
                t.dbgUseGray = tj.value("dbgUseGray", false);
                t.dbgEnableBlur = tj.value("dbgEnableBlur", false);
                t.dbgBlurSize = tj.value("dbgBlurSize", 5);
                t.dbgEnableThresh = tj.value("dbgEnableThresh", false);
                t.dbgThreshold = tj.value("dbgThreshold", 128);
                t.dbgEnableCanny = tj.value("dbgEnableCanny", false);
                t.dbgCannyLow = tj.value("dbgCannyLow", 50);
                t.dbgCannyHigh = tj.value("dbgCannyHigh", 150);
                t.blobMinArea = tj.value("blobMinArea", 100);
                t.blobMaxArea = tj.value("blobMaxArea", 10000);
                t.useSearchROI = tj.value("useSearchROI", false);

                // YOLO 参数
                t.yoloModelPath = tj.value("yoloModelPath", "");
                t.yoloClassesPath = tj.value("yoloClassesPath", "");
                t.yoloConfThreshold = tj.value("yoloConfThreshold", 0.5f);
                t.yoloNmsThreshold = tj.value("yoloNmsThreshold", 0.4f);
                t.yoloUseROI = tj.value("yoloUseROI", false);
                t.yoloUseGPU = tj.value("yoloUseGPU", false);
                t.cntUseGray = tj.value("cntUseGray", true);
                t.cntBlurSize = tj.value("cntBlurSize", 5);
                t.cntThreshMode = tj.value("cntThreshMode", 0);
                t.cntThreshValue = tj.value("cntThreshValue", 128);
                t.cntAdaptBlock = tj.value("cntAdaptBlock", 11);
                t.cntInvert = tj.value("cntInvert", false);
                t.cntRetrMode = tj.value("cntRetrMode", 0);
                t.cntApproxMethod = tj.value("cntApproxMethod", 1);
                t.cntMinArea = tj.value("cntMinArea", 100.0f);
                t.cntMaxContours = tj.value("cntMaxContours", 500);
                t.cntFilterConvex = tj.value("cntFilterConvex", false);
                t.cntApproxEps = tj.value("cntApproxEps", 0.02f);
                t.cntLineThick = tj.value("cntLineThick", 2);
                t.cntShowLabels = tj.value("cntShowLabels", true);
                t.cntFillContours = tj.value("cntFillContours", false);
                t.cntMatchROI = tj.value("cntMatchROI", false);
                t.cntMatchThresh = tj.value("cntMatchThresh", 0.1f);
                t.shpBlurSize = tj.value("shpBlurSize", 5);
                t.shpTplRetr = tj.value("shpTplRetr", 0);
                t.shpTplMinArea = tj.value("shpTplMinArea", 30.0f);
                t.shpMinScore = tj.value("shpMinScore", 0.5f);
                t.shpShapeScore = tj.value("shpShapeScore", 0.1f);
                t.shpLineThick = tj.value("shpLineThick", 2);
                t.shpMethod = tj.value("shpMethod", 0);
                t.shpShowLabels = tj.value("shpShowLabels", true);
                t.shpMaxResults = tj.value("shpMaxResults", 1);
                t.shpTplGray = tj.value("shpTplGray", false);
                t.shpTplBinary = tj.value("shpTplBinary", false);
                t.shpTplBinThresh = tj.value("shpTplBinThresh", 128);
                t.shpTplBlur = tj.value("shpTplBlur", false);
                t.shpTplBlurK = tj.value("shpTplBlurK", 5);
                t.shpTplInvert = tj.value("shpTplInvert", false);
                t.lineCannyLow = tj.value("lineCannyLow", 50);
                t.lineCannyHigh = tj.value("lineCannyHigh", 150);
                t.lineMinLength = tj.value("lineMinLength", 100.0f);
                t.lineMaxGap = tj.value("lineMaxGap", 20.0f);
                t.lineMinAngle = tj.value("lineMinAngle", 0.0f);
                t.lineMaxAngle = tj.value("lineMaxAngle", 180.0f);
                t.lineThickness = tj.value("lineThickness", 2);
                t.lineMaxLines = tj.value("lineMaxLines", 1);
                t.lineShowLabels = tj.value("lineShowLabels", true);
                t.lineUseROI = tj.value("lineUseROI", false);
                t.morphOpType = tj.value("morphOpType", 0);
                t.morphKernelSize = tj.value("morphKernelSize", 3);
                t.morphKernelShape = tj.value("morphKernelShape", 0);
                t.morphIterations = tj.value("morphIterations", 1);
                t.morphUseGray = tj.value("morphUseGray", false);
                t.colorSpace = tj.value("colorSpace", 0);
                t.colorHistBins = tj.value("colorHistBins", 32);
                t.colorShowHist = tj.value("colorShowHist", true);
                t.colorUseROI = tj.value("colorUseROI", false);
                t.colorHistHeight = tj.value("colorHistHeight", 100);
                t.mcfUseROI = tj.value("mcfUseROI", false);
                t.mcfMaxResults = tj.value("mcfMaxResults", 1);
                t.mcfMinDist = tj.value("mcfMinDist", 5.0f);
                t.mcfCrossSize = tj.value("mcfCrossSize", 10);
                t.mcfCrossThick = tj.value("mcfCrossThick", 2);
                t.mcfAnchorX = tj.value("mcfAnchorX", 0);
                t.mcfAnchorY = tj.value("mcfAnchorY", 0);
                t.mcfImgGray = tj.value("mcfImgGray", false);
                t.mcfImgBinary = tj.value("mcfImgBinary", false);
                t.mcfImgBinThresh = tj.value("mcfImgBinThresh", 128);
                t.mcfRoiX = tj.value("mcfRoiX", 0);
                t.mcfRoiY = tj.value("mcfRoiY", 0);
                t.mcfRoiW = tj.value("mcfRoiW", 0);
                t.mcfRoiH = tj.value("mcfRoiH", 0);
                t.mcfRefImageBase64 = tj.value("mcfRefImageBase64", "");
                t.mcfPointsJson = tj.value("mcfPointsJson", "");

                // 搜索 ROI 子数组
                if (tj.contains("searchROIs") && tj["searchROIs"].is_array())
                {
                    for (const auto &r : tj["searchROIs"])
                    {
                        RecipeROI rr;
                        rr.startX = r.value("startX", 0.0f);
                        rr.startY = r.value("startY", 0.0f);
                        rr.endX = r.value("endX", 0.0f);
                        rr.endY = r.value("endY", 0.0f);
                        rr.type = r.value("type", 0);
                        t.searchROIs.push_back(rr);
                    }
                }
                if (tj.contains("lineSaveROIs") && tj["lineSaveROIs"].is_array())
                {
                    for (const auto &r : tj["lineSaveROIs"])
                    {
                        RecipeROI rr;
                        rr.startX = r.value("startX", 0.0f);
                        rr.startY = r.value("startY", 0.0f);
                        rr.endX = r.value("endX", 0.0f);
                        rr.endY = r.value("endY", 0.0f);
                        rr.type = r.value("type", 0);
                        t.lineSaveROIs.push_back(rr);
                    }
                }
                if (!t.useSearchROI && !t.searchROIs.empty())
                {
                    t.useSearchROI = t.yoloUseROI || t.lineUseROI || t.mcfUseROI || t.colorUseROI || !t.lineSaveROIs.empty();
                }
                data.tools.push_back(t);
            }
        }

        // Load template image
        if (!data.templateImage.empty())
        {
            std::string tplPath(filepath);
            size_t slash = tplPath.find_last_of("\\/");
            tplPath = (slash != std::string::npos)
                          ? tplPath.substr(0, slash + 1) + data.templateImage
                          : data.templateImage;

            cv::Mat tpl = ReadImageFile(tplPath, cv::IMREAD_COLOR);
            if (!tpl.empty())
            {
                g_FrozenTemplate = tpl;
                g_ShowPreview = true;
                LogSystem::Add(LOG_INFO, "Template image loaded: %s", tplPath.c_str());
            }
        }

        LogSystem::Add(LOG_INFO, "[Load] tools: %zu", data.tools.size());
        LogSystem::Add(LOG_INFO, "Recipe loaded: %s (ROI: %zu, tools: %zu)", data.name.c_str(), data.rois.size(), data.tools.size());
        return true;
    }

    // ===================== 列出所有配方 =====================
    std::vector<std::string> List(const char *exeDir)
    {
        std::vector<std::string> result;
        std::string dir = exeDir ? std::string(exeDir) + "recipes\\" : "recipes\\";
        std::wstring dirW = Utf8ToWide(dir);
        CreateDirectoryW(dirW.c_str(), nullptr);

        WIN32_FIND_DATAW fd{};
        std::wstring pattern = dirW + L"*.recipe";
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            return result;

        do
        {
            std::wstring nameW = fd.cFileName;
            nameW = nameW.substr(0, nameW.rfind(L'.'));
            std::string name = WideToUtf8(nameW);
            result.push_back(name);
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
        return result;
    }

    // ===================== 从当前环境捕获参数 =====================
    RecipeData Capture(const char *name)
    {
        RecipeData d;
        d.name = name;
        const std::string& currentSourcePath = FrameSourceState::Current().sourcePath;
        if (!currentSourcePath.empty())
            d.imagePath = currentSourcePath;
        else if (!pendingPath.empty())
            d.imagePath = pendingPath;

        // 阈值
        d.threshold.useGray = gUseGray;
        d.threshold.thresholdValue = gThresholdValue;
        d.threshold.binaryInv = gThresholdBinaryInv;
        d.threshold.blurSize = gBlurSize;
        d.threshold.cannyLow = gCannyLow;
        d.threshold.cannyHigh = gCannyHigh;
        d.threshold.brightness = gBrightness;
        d.threshold.contrast = gContrast;
        d.threshold.processMode = gProcessMode;

        d.threshold.pipeBlur = gPipe.enableBlur;
        d.threshold.pipeThreshold = gPipe.enableThreshold;
        d.threshold.pipeCanny = gPipe.enableCanny;
        d.threshold.pipeBlurSize = gPipe.blurSize;
        d.threshold.pipeThresholdVal = gPipe.threshold;
        d.threshold.pipeCannyLow = gPipe.cannyLow;
        d.threshold.pipeCannyHigh = gPipe.cannyHigh;

        // Template match
        d.tmMatch.searchMode = g_TMSearchMode;
        d.tmMatch.maxResults = g_TMMaxResults;
        d.tmMatch.maxImageDim = g_TMMaxImageDim;
        d.tmMatch.matchThreshold = g_TMMatchThreshold;
        d.tmMatch.enableRotation = g_TMEnableRotation;
        d.tmMatch.rotationStart = g_TMRotationStart;
        d.tmMatch.rotationEnd = g_TMRotationEnd;
        d.tmMatch.rotationStep = g_TMRotationStep;

        // ROI
        d.rois.clear();
        for (const auto &roi : ROIState::ReadOnlyItems())
        {
            RecipeROI r;
            r.startX = roi.start.x;
            r.startY = roi.start.y;
            r.endX = roi.end.x;
            r.endY = roi.end.y;
            r.type = roi.type;
            d.rois.push_back(r);
        }

        d.templateImage = d.name + ".png";

        // 工具实例
        d.tools.clear();
        LogSystem::Add(LOG_INFO, "[Capture] g_ToolInstances: %zu",
                       ToolChainState::ReadOnlyTools().size());
        for (size_t ti = 0; ti < ToolChainState::ReadOnlyTools().size(); ti++)
        {
            const auto &src = ToolChainState::ReadOnlyTools()[ti];
            RecipeToolInstance t;
            t.type = src.type;
            t.label = src.label;
            t.inputSourceMode = src.inputSourceMode;
            if (!src.templateImg.empty() || !src.shpTplImage.empty())
                t.templateFile = d.name + "_tpl" + std::to_string(ti) + ".png";
            t.hasTemplateROI = src.hasTemplateROI;
            if (src.hasTemplateROI)
            {
                t.templateROI.startX = src.templateROI.start.x;
                t.templateROI.startY = src.templateROI.start.y;
                t.templateROI.endX = src.templateROI.end.x;
                t.templateROI.endY = src.templateROI.end.y;
                t.templateROI.type = src.templateROI.type;
            }

            t.enableRotation = src.enableRotation;
            t.rotationStart = src.rotationStart;
            t.rotationEnd = src.rotationEnd;
            t.rotationStep = src.rotationStep;
            t.maxResults = src.maxResults;
            t.matchThreshold = src.matchThreshold;
            t.maxImageDim = src.maxImageDim;
            t.nmsThreshold = src.nmsThreshold;
            t.searchMode = src.searchMode;
            t.tplGray = src.tplGray;
            t.tplBinary = src.tplBinary;
            t.tplBinThresh = src.tplBinThresh;
            t.tplEdge = src.tplEdge;
            t.tplEdgeLow = src.tplEdgeLow;
            t.tplEdgeHigh = src.tplEdgeHigh;
            t.imgUseGray = src.imgUseGray;
            t.imgEnableThreshold = src.imgEnableThreshold;
            t.imgThreshold = src.imgThreshold;
            t.cannyLow = src.cannyLow;
            t.cannyHigh = src.cannyHigh;
            t.edgeUseGray = src.edgeUseGray;
            t.dbgUseGray = src.dbgUseGray;
            t.dbgEnableBlur = src.dbgEnableBlur;
            t.dbgBlurSize = src.dbgBlurSize;
            t.dbgEnableThresh = src.dbgEnableThresh;
            t.dbgThreshold = src.dbgThreshold;
            t.dbgEnableCanny = src.dbgEnableCanny;
            t.dbgCannyLow = src.dbgCannyLow;
            t.dbgCannyHigh = src.dbgCannyHigh;
            t.blobMinArea = src.blobMinArea;
            t.blobMaxArea = src.blobMaxArea;
            t.useSearchROI = src.useSearchROI;

            // YOLO 参数
            t.yoloModelPath = src.yoloModelPath;
            t.yoloClassesPath = src.yoloClassesPath;
            t.yoloConfThreshold = src.yoloConfThreshold;
            t.yoloNmsThreshold = src.yoloNmsThreshold;
            t.yoloUseROI = src.yoloUseROI;
            t.yoloUseGPU = src.yoloUseGPU;
            t.cntUseGray = src.cntUseGray;
            t.cntBlurSize = src.cntBlurSize;
            t.cntThreshMode = src.cntThreshMode;
            t.cntThreshValue = src.cntThreshValue;
            t.cntAdaptBlock = src.cntAdaptBlock;
            t.cntInvert = src.cntInvert;
            t.cntRetrMode = src.cntRetrMode;
            t.cntApproxMethod = src.cntApproxMethod;
            t.cntMinArea = src.cntMinArea;
            t.cntMaxContours = src.cntMaxContours;
            t.cntFilterConvex = src.cntFilterConvex;
            t.cntApproxEps = src.cntApproxEps;
            t.cntLineThick = src.cntLineThick;
            t.cntShowLabels = src.cntShowLabels;
            t.cntFillContours = src.cntFillContours;
            t.cntMatchROI = src.cntMatchROI;
            t.cntMatchThresh = src.cntMatchThresh;
            t.shpBlurSize = src.shpBlurSize;
            t.shpTplRetr = src.shpTplRetr;
            t.shpTplMinArea = src.shpTplMinArea;
            t.shpMinScore = src.shpMinScore;
            t.shpShapeScore = src.shpShapeScore;
            t.shpLineThick = src.shpLineThick;
            t.shpMethod = src.shpMethod;
            t.shpShowLabels = src.shpShowLabels;
            t.shpMaxResults = src.shpMaxResults;
            t.shpTplGray = src.shpTplGray;
            t.shpTplBinary = src.shpTplBinary;
            t.shpTplBinThresh = src.shpTplBinThresh;
            t.shpTplBlur = src.shpTplBlur;
            t.shpTplBlurK = src.shpTplBlurK;
            t.shpTplInvert = src.shpTplInvert;
            t.lineCannyLow = src.lineCannyLow;
            t.lineCannyHigh = src.lineCannyHigh;
            t.lineMinLength = src.lineMinLength;
            t.lineMaxGap = src.lineMaxGap;
            t.lineMinAngle = src.lineMinAngle;
            t.lineMaxAngle = src.lineMaxAngle;
            t.lineThickness = src.lineThickness;
            t.lineMaxLines = src.lineMaxLines;
            t.lineShowLabels = src.lineShowLabels;
            t.lineUseROI = src.lineUseROI;
            t.morphOpType = src.morphOpType;
            t.morphKernelSize = src.morphKernelSize;
            t.morphKernelShape = src.morphKernelShape;
            t.morphIterations = src.morphIterations;
            t.morphUseGray = src.morphUseGray;
            t.colorSpace = src.colorSpace;
            t.colorHistBins = src.colorHistBins;
            t.colorShowHist = src.colorShowHist;
            t.colorUseROI = src.colorUseROI;
            t.colorHistHeight = src.colorHistHeight;
            t.mcfRoiX = src.mcfRoiX;
            t.mcfRoiY = src.mcfRoiY;
            t.mcfRoiW = src.mcfRoiW;
            t.mcfRoiH = src.mcfRoiH;

            for (const auto &roi : src.searchROIs)
            {
                RecipeROI r;
                r.startX = roi.start.x;
                r.startY = roi.start.y;
                r.endX = roi.end.x;
                r.endY = roi.end.y;
                r.type = roi.type;
                t.searchROIs.push_back(r);
            }
            for (const auto &roi : src.lineSaveROIs)
            {
                RecipeROI r;
                r.startX = roi.start.x;
                r.startY = roi.start.y;
                r.endX = roi.end.x;
                r.endY = roi.end.y;
                r.type = roi.type;
                t.lineSaveROIs.push_back(r);
            }

            // 多点找色 type==10
            if (src.type == 10)
            {
                t.mcfUseROI      = src.mcfUseROI;
                t.mcfMaxResults  = src.mcfMaxResults;
                t.mcfMinDist     = src.mcfMinDist;
                t.mcfCrossSize   = src.mcfCrossSize;
                t.mcfCrossThick  = src.mcfCrossThick;
                t.mcfAnchorX     = src.mcfAnchorX;
                t.mcfAnchorY     = src.mcfAnchorY;
                t.mcfImgGray     = src.mcfImgGray;
                t.mcfImgBinary   = src.mcfImgBinary;
                t.mcfImgBinThresh= src.mcfImgBinThresh;
                // 参考图 base64
                if (!src.mcfRefImage.empty())
                {
                    std::vector<uchar> buf;
                    if (cv::imencode(".png", src.mcfRefImage, buf))
                        t.mcfRefImageBase64 = Base64Encode(buf);
                }
                // 颜色点 JSON
                if (src.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(src.toolImpl);
                    if (mf) t.mcfPointsJson = mf->Save().dump();
                }
            }

            d.tools.push_back(t);
        }

        return d;
    }

    // ===================== Apply recipe to current runtime =====================
    void Apply(const RecipeData &data)
    {
        if (!data.imagePath.empty())
            pendingPath = data.imagePath;

        // 阈值参数
        gUseGray = data.threshold.useGray;
        gThresholdValue = data.threshold.thresholdValue;
        gThresholdBinaryInv = data.threshold.binaryInv;
        gBlurSize = data.threshold.blurSize;
        gCannyLow = data.threshold.cannyLow;
        gCannyHigh = data.threshold.cannyHigh;
        gBrightness = data.threshold.brightness;
        gContrast = data.threshold.contrast;
        gProcessMode = data.threshold.processMode;

        gPipe.enableBlur = data.threshold.pipeBlur;
        gPipe.enableThreshold = data.threshold.pipeThreshold;
        gPipe.enableCanny = data.threshold.pipeCanny;
        gPipe.blurSize = data.threshold.pipeBlurSize;
        gPipe.threshold = data.threshold.pipeThresholdVal;
        gPipe.cannyLow = data.threshold.pipeCannyLow;
        gPipe.cannyHigh = data.threshold.pipeCannyHigh;

        // Template match
        g_TMSearchMode = data.tmMatch.searchMode;
        g_TMMaxResults = data.tmMatch.maxResults;
        g_TMMaxImageDim = data.tmMatch.maxImageDim;
        g_TMMatchThreshold = data.tmMatch.matchThreshold;
        g_TMEnableRotation = data.tmMatch.enableRotation;
        g_TMRotationStart = data.tmMatch.rotationStart;
        g_TMRotationEnd = data.tmMatch.rotationEnd;
        g_TMRotationStep = data.tmMatch.rotationStep;

        // ROI
        ROIState::Items().clear();
        for (const auto &r : data.rois)
        {
            ROI roi;
            roi.start = ImVec2(r.startX, r.startY);
            roi.end = ImVec2(r.endX, r.endY);
            roi.type = r.type;
            ROIState::Items().push_back(roi);
        }

        // 工具实例
        ToolChainState::Tools().clear();
        LogSystem::Add(LOG_INFO, "[Apply] restoring tools: %zu", data.tools.size());
        for (size_t ti = 0; ti < data.tools.size(); ti++)
        {
            const auto &t = data.tools[ti];
            ToolInstance it;
            it.type = t.type;
            it.label = t.label;
            it.inputSourceMode = t.inputSourceMode;
            it.hasTemplateROI = t.hasTemplateROI;
            if (t.hasTemplateROI)
            {
                it.templateROI.start = ImVec2(t.templateROI.startX, t.templateROI.startY);
                it.templateROI.end = ImVec2(t.templateROI.endX, t.templateROI.endY);
                it.templateROI.type = t.templateROI.type;
            }
            it.enableRotation = t.enableRotation;
            it.rotationStart = t.rotationStart;
            it.rotationEnd = t.rotationEnd;
            it.rotationStep = t.rotationStep;
            it.maxResults = t.maxResults;
            it.matchThreshold = t.matchThreshold;
            it.maxImageDim = t.maxImageDim;
            it.nmsThreshold = t.nmsThreshold;
            it.searchMode = t.searchMode;
            it.tplGray = t.tplGray;
            it.tplBinary = t.tplBinary;
            it.tplBinThresh = t.tplBinThresh;
            it.tplEdge = t.tplEdge;
            it.tplEdgeLow = t.tplEdgeLow;
            it.tplEdgeHigh = t.tplEdgeHigh;
            it.imgUseGray = t.imgUseGray;
            it.imgEnableThreshold = t.imgEnableThreshold;
            it.imgThreshold = t.imgThreshold;
            it.cannyLow = t.cannyLow;
            it.cannyHigh = t.cannyHigh;
            it.edgeUseGray = t.edgeUseGray;
            it.dbgUseGray = t.dbgUseGray;
            it.dbgEnableBlur = t.dbgEnableBlur;
            it.dbgBlurSize = t.dbgBlurSize;
            it.dbgEnableThresh = t.dbgEnableThresh;
            it.dbgThreshold = t.dbgThreshold;
            it.dbgEnableCanny = t.dbgEnableCanny;
            it.dbgCannyLow = t.dbgCannyLow;
            it.dbgCannyHigh = t.dbgCannyHigh;
            it.blobMinArea = t.blobMinArea;
            it.blobMaxArea = t.blobMaxArea;
            it.useSearchROI = t.useSearchROI;

            // YOLO 参数
            it.yoloModelPath = t.yoloModelPath;
            it.yoloClassesPath = t.yoloClassesPath;
            it.yoloConfThreshold = t.yoloConfThreshold;
            it.yoloNmsThreshold = t.yoloNmsThreshold;
            it.yoloUseROI = t.yoloUseROI;
            it.yoloUseGPU = t.yoloUseGPU;
            it.cntUseGray = t.cntUseGray;
            it.cntBlurSize = t.cntBlurSize;
            it.cntThreshMode = t.cntThreshMode;
            it.cntThreshValue = t.cntThreshValue;
            it.cntAdaptBlock = t.cntAdaptBlock;
            it.cntInvert = t.cntInvert;
            it.cntRetrMode = t.cntRetrMode;
            it.cntApproxMethod = t.cntApproxMethod;
            it.cntMinArea = t.cntMinArea;
            it.cntMaxContours = t.cntMaxContours;
            it.cntFilterConvex = t.cntFilterConvex;
            it.cntApproxEps = t.cntApproxEps;
            it.cntLineThick = t.cntLineThick;
            it.cntShowLabels = t.cntShowLabels;
            it.cntFillContours = t.cntFillContours;
            it.cntMatchROI = t.cntMatchROI;
            it.cntMatchThresh = t.cntMatchThresh;
            it.shpBlurSize = t.shpBlurSize;
            it.shpTplRetr = t.shpTplRetr;
            it.shpTplMinArea = t.shpTplMinArea;
            it.shpMinScore = t.shpMinScore;
            it.shpShapeScore = t.shpShapeScore;
            it.shpLineThick = t.shpLineThick;
            it.shpMethod = t.shpMethod;
            it.shpShowLabels = t.shpShowLabels;
            it.shpMaxResults = t.shpMaxResults;
            it.shpTplGray = t.shpTplGray;
            it.shpTplBinary = t.shpTplBinary;
            it.shpTplBinThresh = t.shpTplBinThresh;
            it.shpTplBlur = t.shpTplBlur;
            it.shpTplBlurK = t.shpTplBlurK;
            it.shpTplInvert = t.shpTplInvert;
            it.lineCannyLow = t.lineCannyLow;
            it.lineCannyHigh = t.lineCannyHigh;
            it.lineMinLength = t.lineMinLength;
            it.lineMaxGap = t.lineMaxGap;
            it.lineMinAngle = t.lineMinAngle;
            it.lineMaxAngle = t.lineMaxAngle;
            it.lineThickness = t.lineThickness;
            it.lineMaxLines = t.lineMaxLines;
            it.lineShowLabels = t.lineShowLabels;
            it.lineUseROI = t.lineUseROI;
            it.morphOpType = t.morphOpType;
            it.morphKernelSize = t.morphKernelSize;
            it.morphKernelShape = t.morphKernelShape;
            it.morphIterations = t.morphIterations;
            it.morphUseGray = t.morphUseGray;
            it.colorSpace = t.colorSpace;
            it.colorHistBins = t.colorHistBins;
            it.colorShowHist = t.colorShowHist;
            it.colorUseROI = t.colorUseROI;
            it.colorHistHeight = t.colorHistHeight;
            it.mcfRoiX = t.mcfRoiX;
            it.mcfRoiY = t.mcfRoiY;
            it.mcfRoiW = t.mcfRoiW;
            it.mcfRoiH = t.mcfRoiH;

            // 多点找色 type==10
            if (t.type == 10)
            {
                it.mcfUseROI      = t.mcfUseROI;
                it.mcfMaxResults  = t.mcfMaxResults;
                it.mcfMinDist     = t.mcfMinDist;
                it.mcfCrossSize   = t.mcfCrossSize;
                it.mcfCrossThick  = t.mcfCrossThick;
                it.mcfAnchorX     = t.mcfAnchorX;
                it.mcfAnchorY     = t.mcfAnchorY;
                it.mcfImgGray     = t.mcfImgGray;
                it.mcfImgBinary   = t.mcfImgBinary;
                it.mcfImgBinThresh= t.mcfImgBinThresh;
                // 恢复参考图
                if (!t.mcfRefImageBase64.empty())
                {
                    std::vector<uchar> buf = Base64Decode(t.mcfRefImageBase64);
                    if (buf.empty())
                        buf.assign(t.mcfRefImageBase64.begin(), t.mcfRefImageBase64.end());
                    it.mcfRefImage = cv::imdecode(buf, cv::IMREAD_COLOR);
                }
                // 恢复颜色点
                if (!t.mcfPointsJson.empty())
                {
                    it.toolImpl = ITool::Create(10).release();
                    if (it.toolImpl)
                    {
                        auto j = nlohmann::json::parse(t.mcfPointsJson);
                        auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl);
                        if (mf) mf->Load(j);
                    }
                }
            }

            // 搜索 ROI
            for (const auto &r : t.searchROIs)
            {
                ROI roi;
                roi.start = ImVec2(r.startX, r.startY);
                roi.end = ImVec2(r.endX, r.endY);
                roi.type = r.type;
                it.searchROIs.push_back(roi);
            }
            for (const auto &r : t.lineSaveROIs)
            {
                ROI roi;
                roi.start = ImVec2(r.startX, r.startY);
                roi.end = ImVec2(r.endX, r.endY);
                roi.type = r.type;
                it.lineSaveROIs.push_back(roi);
            }

            // Load template image
            if (!t.templateFile.empty() && !g_LastRecipePath.empty())
            {
                size_t slash = g_LastRecipePath.find_last_of("\\/");
                std::string tplPath = (slash != std::string::npos)
                                          ? g_LastRecipePath.substr(0, slash + 1) + t.templateFile
                                          : t.templateFile;
                it.templateImg = ReadImageFile(tplPath, cv::IMREAD_COLOR);
                if (!it.templateImg.empty())
                {
                    LogSystem::Add(LOG_INFO, "Tool template loaded: %s (%dx%d)", tplPath.c_str(),
                                   it.templateImg.cols, it.templateImg.rows);
                    // 形状匹配使用 shpTplImage（深拷贝）
                    if (it.type == 6)
                    {
                        it.shpTplImage = it.templateImg.clone();
                        LogSystem::Add(LOG_INFO, "Shape template synced: %dx%d", it.shpTplImage.cols, it.shpTplImage.rows);
                    }
                }
            }

            ToolChainState::Tools().push_back(it);
        }

        ToolChainState::MoveOriginalToolToFront();

        LogSystem::Add(LOG_INFO, "[Apply] recipe applied: %s (tools: %zu)", data.name.c_str(), data.tools.size());
    }

} // namespace RecipeManager


