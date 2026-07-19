#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RecipeManager.h"
#include "DX12Context.h"
#include "FrameSourceState.h"
#include "FrameNavigation.h"
#include "ROIState.h"
#include "TemplateState.h"
#include "ToolChainState.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/TemplateMatch.h"
#include "../Algorithm/MultiColorFinder.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

        j["version"] = 2;
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
            tj["showResultLabels"] = t.showResultLabels;
            tj["inputSourceMode"] = t.inputSourceMode;
            tj["resultRoiMode"] = t.resultRoiMode;
            tj["resultRoiSourceTool"] = t.resultRoiSourceTool;
            tj["resultRoiIndex"] = t.resultRoiIndex;
            tj["resultRoiMissingPolicy"] = t.resultRoiMissingPolicy;
            tj["fixture"] = {
                {"enabled", t.fixture.enabled},
                {"sourceToolIndex", t.fixture.sourceToolIndex},
                {"resultIndex", t.fixture.resultIndex},
                {"referenceX", t.fixture.referenceOrigin.x},
                {"referenceY", t.fixture.referenceOrigin.y},
                {"referenceAngle", t.fixture.referenceAngleDegrees},
                {"failOnMissing", t.fixture.failOnMissing}
            };
            tj["judgement"] = {
                {"enabled", t.judgement.enabled},
                {"stopOnFailure", t.judgement.stopOnFailure},
                {"minResultCount", t.judgement.minResultCount},
                {"maxResultCount", t.judgement.maxResultCount},
                {"minScore", t.judgement.minScore},
                {"minArea", t.judgement.minArea},
                {"maxArea", t.judgement.maxArea},
                {"requiredText", t.judgement.requiredText},
                {"textMatchMode", t.judgement.textMatchMode},
                {"textCaseSensitive", t.judgement.textCaseSensitive}
            };
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
            tj["ocrDetModelPath"] = t.ocrDetModelPath;
            tj["ocrDetParamPath"] = t.ocrDetParamPath;
            tj["ocrRecModelPath"] = t.ocrRecModelPath;
            tj["ocrRecParamPath"] = t.ocrRecParamPath;
            tj["ocrDictionaryPath"] = t.ocrDictionaryPath;
            tj["ocrMinConfidence"] = t.ocrMinConfidence;
            tj["ocrMaxItems"] = t.ocrMaxItems;
            tj["ocrInputSize"] = t.ocrInputSize;
            tj["ocrMaxCandidates"] = t.ocrMaxCandidates;
            tj["ocrMinBoxArea"] = t.ocrMinBoxArea;
            tj["ocrMinBoxHeight"] = t.ocrMinBoxHeight;
            tj["ocrRoiPadding"] = t.ocrRoiPadding;
            tj["ocrFastMode"] = t.ocrFastMode;
            tj["ocrDetectOnly"] = t.ocrDetectOnly;
            tj["ocrUseROI"] = t.ocrUseROI;
            tj["qrUseROI"] = t.qrUseROI;
            tj["qrDetectMulti"] = t.qrDetectMulti;
            tj["qrEnhance"] = t.qrEnhance;
            tj["qrMinSize"] = t.qrMinSize;
            tj["qrShowText"] = t.qrShowText;
            tj["qrEngine"] = t.qrEngine;
            tj["qrFormatMask"] = t.qrFormatMask;
            tj["qrFilterDuplicates"] = t.qrFilterDuplicates;
            tj["measureMode"] = t.measureMode;
            tj["measureMmPerPixel"] = t.measureMmPerPixel;
            tj["measureCalibrationPixels"] = t.measureCalibrationPixels;
            tj["measureCalibrationMm"] = t.measureCalibrationMm;
            tj["measureToleranceEnabled"] = t.measureToleranceEnabled;
            tj["measureNominal"] = t.measureNominal;
            tj["measureToleranceMinus"] = t.measureToleranceMinus;
            tj["measureTolerancePlus"] = t.measureTolerancePlus;
            std::vector<double> measureHomography(
                t.measureCalibration.pixelToWorldHomography.val,
                t.measureCalibration.pixelToWorldHomography.val + 9);
            tj["measurement"] = {
                {"mode", t.measureMode}, {"caliperCount", t.measureCaliperCount},
                {"searchLength", t.measureSearchLength}, {"projectionWidth", t.measureProjectionWidth},
                {"smoothingSigma", t.measureSmoothingSigma}, {"edgeThreshold", t.measureEdgeThreshold},
                {"minPairDistance", t.measureMinPairDistance}, {"edgePolarity", t.measureEdgePolarity},
                {"subpixel", t.measureSubpixel}, {"fitMethod", t.measureFitMethod},
                {"fitInlierThreshold", t.measureFitInlierThreshold},
                {"minimumValidCalipers", t.measureMinimumValidCalipers},
                {"minimumConfidence", t.measureMinimumConfidence},
                {"legacyMmPerPixel", t.measureMmPerPixel},
                {"calibrationEnabled", t.measureCalibration.enabled},
                {"scaleX", t.measureCalibration.scaleX}, {"scaleY", t.measureCalibration.scaleY},
                {"pixelOriginX", t.measureCalibration.pixelOrigin.x},
                {"pixelOriginY", t.measureCalibration.pixelOrigin.y},
                {"worldOriginX", t.measureCalibration.worldOrigin.x},
                {"worldOriginY", t.measureCalibration.worldOrigin.y},
                {"homographyEnabled", t.measureCalibration.homographyEnabled},
                {"homography", measureHomography},
                {"distortionEnabled", t.measureCalibration.distortionEnabled},
                {"fx", t.measureCalibration.fx}, {"fy", t.measureCalibration.fy},
                {"cx", t.measureCalibration.cx}, {"cy", t.measureCalibration.cy},
                {"k1", t.measureCalibration.k1}, {"k2", t.measureCalibration.k2},
                {"p1", t.measureCalibration.p1}, {"p2", t.measureCalibration.p2},
                {"k3", t.measureCalibration.k3},
                {"toleranceEnabled", t.measureToleranceEnabled}, {"nominal", t.measureNominal},
                {"toleranceMinus", t.measureToleranceMinus}, {"tolerancePlus", t.measureTolerancePlus}
            };

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

            if (WriteImageFile(tplPath, TemplateState::FrozenTemplate()))
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
        if (version > 2)
            LogSystem::Add(LOG_WARN, "RecipeManager: recipe version %d > supported version 2", version);

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
                t.showResultLabels = tj.value("showResultLabels", true);
                t.inputSourceMode = tj.value("inputSourceMode", 2);
                t.resultRoiMode = tj.value("resultRoiMode", 0);
                t.resultRoiSourceTool = tj.value("resultRoiSourceTool", -1);
                t.resultRoiIndex = tj.value("resultRoiIndex", 0);
                t.resultRoiMissingPolicy = tj.value("resultRoiMissingPolicy", 0);
                if (tj.contains("fixture") && tj["fixture"].is_object())
                {
                    const auto& fixture = tj["fixture"];
                    t.fixture.enabled = fixture.value("enabled", false);
                    t.fixture.sourceToolIndex = fixture.value("sourceToolIndex", -1);
                    t.fixture.resultIndex = fixture.value("resultIndex", 0);
                    t.fixture.referenceOrigin.x = fixture.value("referenceX", 0.0f);
                    t.fixture.referenceOrigin.y = fixture.value("referenceY", 0.0f);
                    t.fixture.referenceAngleDegrees = fixture.value("referenceAngle", 0.0f);
                    t.fixture.failOnMissing = fixture.value("failOnMissing", true);
                }
                if (tj.contains("judgement") && tj["judgement"].is_object())
                {
                    const auto& judgement = tj["judgement"];
                    t.judgement.enabled = judgement.value("enabled", false);
                    t.judgement.stopOnFailure = judgement.value("stopOnFailure", false);
                    t.judgement.minResultCount = judgement.value("minResultCount", 1);
                    t.judgement.maxResultCount = judgement.value("maxResultCount", -1);
                    t.judgement.minScore = judgement.value("minScore", -1.0f);
                    t.judgement.minArea = judgement.value("minArea", -1.0f);
                    t.judgement.maxArea = judgement.value("maxArea", -1.0f);
                    t.judgement.requiredText = judgement.value("requiredText", "");
                    t.judgement.textMatchMode = judgement.value("textMatchMode", 0);
                    t.judgement.textCaseSensitive = judgement.value("textCaseSensitive", false);
                }
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
                t.ocrDetModelPath = tj.value("ocrDetModelPath", "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.bin");
                t.ocrDetParamPath = tj.value("ocrDetParamPath", "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.param");
                t.ocrRecModelPath = tj.value("ocrRecModelPath", "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.bin");
                t.ocrRecParamPath = tj.value("ocrRecParamPath", "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.param");
                t.ocrDictionaryPath = tj.value("ocrDictionaryPath", "models\\ppocrv6\\ppocr_keys_v6_tiny.txt");
                t.ocrMinConfidence = tj.value("ocrMinConfidence", 0.30f);
                t.ocrMaxItems = tj.value("ocrMaxItems", 8);
                t.ocrInputSize = tj.value("ocrInputSize", 512);
                t.ocrMaxCandidates = tj.value("ocrMaxCandidates", 220);
                t.ocrMinBoxArea = tj.value("ocrMinBoxArea", 0);
                t.ocrMinBoxHeight = tj.value("ocrMinBoxHeight", 0);
                t.ocrRoiPadding = tj.value("ocrRoiPadding", 24);
                t.ocrFastMode = tj.value("ocrFastMode", true);
                t.ocrDetectOnly = tj.value("ocrDetectOnly", false);
                t.ocrUseROI = tj.value("ocrUseROI", true);
                t.qrUseROI = tj.value("qrUseROI", true);
                t.qrDetectMulti = tj.value("qrDetectMulti", true);
                t.qrEnhance = tj.value("qrEnhance", true);
                t.qrMinSize = tj.value("qrMinSize", 24);
                t.qrShowText = tj.value("qrShowText", true);
                t.qrEngine = tj.value("qrEngine", 0);
                t.qrFormatMask = tj.value("qrFormatMask", static_cast<std::uint32_t>(BarcodeFormatAll));
                t.qrFilterDuplicates = tj.value("qrFilterDuplicates", true);
                t.measureMode = tj.value("measureMode", 0);
                t.measureMmPerPixel = tj.value("measureMmPerPixel", 0.0f);
                t.measureCalibrationPixels = tj.value("measureCalibrationPixels", 100.0f);
                t.measureCalibrationMm = tj.value("measureCalibrationMm", 10.0f);
                t.measureToleranceEnabled = tj.value("measureToleranceEnabled", false);
                t.measureNominal = tj.value("measureNominal", 0.0f);
                t.measureToleranceMinus = tj.value("measureToleranceMinus", 0.0f);
                t.measureTolerancePlus = tj.value("measureTolerancePlus", 0.0f);
                if (tj.contains("measurement") && tj["measurement"].is_object())
                {
                    const auto& measurement = tj["measurement"];
                    t.measureMode = std::clamp(measurement.value("mode", t.measureMode), 0, 7);
                    t.measureCaliperCount = measurement.value("caliperCount", 16);
                    t.measureSearchLength = measurement.value("searchLength", 30.0f);
                    t.measureProjectionWidth = measurement.value("projectionWidth", 5.0f);
                    t.measureSmoothingSigma = measurement.value("smoothingSigma", 1.0f);
                    t.measureEdgeThreshold = measurement.value("edgeThreshold", 12.0f);
                    t.measureMinPairDistance = measurement.value("minPairDistance", 3.0f);
                    t.measureEdgePolarity = measurement.value("edgePolarity", 0);
                    t.measureSubpixel = measurement.value("subpixel", true);
                    t.measureFitMethod = measurement.value("fitMethod", 1);
                    t.measureFitInlierThreshold = measurement.value("fitInlierThreshold", 1.5f);
                    t.measureMinimumValidCalipers = measurement.value("minimumValidCalipers", 3);
                    t.measureMinimumConfidence = measurement.value("minimumConfidence", 0.0f);
                    t.measureMmPerPixel = measurement.value("legacyMmPerPixel", t.measureMmPerPixel);
                    t.measureCalibration.enabled = measurement.value("calibrationEnabled", false);
                    t.measureCalibration.scaleX = measurement.value("scaleX", 1.0);
                    t.measureCalibration.scaleY = measurement.value("scaleY", 1.0);
                    t.measureCalibration.pixelOrigin.x = measurement.value("pixelOriginX", 0.0);
                    t.measureCalibration.pixelOrigin.y = measurement.value("pixelOriginY", 0.0);
                    t.measureCalibration.worldOrigin.x = measurement.value("worldOriginX", 0.0);
                    t.measureCalibration.worldOrigin.y = measurement.value("worldOriginY", 0.0);
                    t.measureCalibration.homographyEnabled = measurement.value("homographyEnabled", false);
                    const auto homography = measurement.value("homography", std::vector<double>());
                    if (homography.size() == 9)
                        std::copy(homography.begin(), homography.end(),
                            t.measureCalibration.pixelToWorldHomography.val);
                    t.measureCalibration.distortionEnabled = measurement.value("distortionEnabled", false);
                    t.measureCalibration.fx = measurement.value("fx", 1.0);
                    t.measureCalibration.fy = measurement.value("fy", 1.0);
                    t.measureCalibration.cx = measurement.value("cx", 0.0);
                    t.measureCalibration.cy = measurement.value("cy", 0.0);
                    t.measureCalibration.k1 = measurement.value("k1", 0.0);
                    t.measureCalibration.k2 = measurement.value("k2", 0.0);
                    t.measureCalibration.p1 = measurement.value("p1", 0.0);
                    t.measureCalibration.p2 = measurement.value("p2", 0.0);
                    t.measureCalibration.k3 = measurement.value("k3", 0.0);
                    t.measureToleranceEnabled = measurement.value("toleranceEnabled", t.measureToleranceEnabled);
                    t.measureNominal = measurement.value("nominal", t.measureNominal);
                    t.measureToleranceMinus = measurement.value("toleranceMinus", t.measureToleranceMinus);
                    t.measureTolerancePlus = measurement.value("tolerancePlus", t.measureTolerancePlus);
                }

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
                TemplateState::FrozenTemplate() = tpl;
                TemplateState::ShowPreview() = true;
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

        // Legacy top-level fields are derived from Core tool instances for old recipe readers.
        for (const auto& tool : ToolChainState::ReadOnlyTools())
        {
            if (tool.type == 3)
            {
                d.threshold.useGray = tool.dbgUseGray;
                d.threshold.thresholdValue = tool.dbgThreshold;
                d.threshold.cannyLow = tool.dbgCannyLow;
                d.threshold.cannyHigh = tool.dbgCannyHigh;
                d.threshold.pipeBlur = tool.dbgEnableBlur;
                d.threshold.pipeThreshold = tool.dbgEnableThresh;
                d.threshold.pipeCanny = tool.dbgEnableCanny;
                d.threshold.pipeBlurSize = tool.dbgBlurSize;
                d.threshold.pipeThresholdVal = tool.dbgThreshold;
                d.threshold.pipeCannyLow = tool.dbgCannyLow;
                d.threshold.pipeCannyHigh = tool.dbgCannyHigh;
            }
            else if (tool.type == 1)
            {
                d.tmMatch.searchMode = tool.searchMode;
                d.tmMatch.maxResults = tool.maxResults;
                d.tmMatch.maxImageDim = tool.maxImageDim;
                d.tmMatch.matchThreshold = tool.matchThreshold;
                d.tmMatch.enableRotation = tool.enableRotation;
                d.tmMatch.rotationStart = tool.rotationStart;
                d.tmMatch.rotationEnd = tool.rotationEnd;
                d.tmMatch.rotationStep = tool.rotationStep;
            }
        }

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
            t.showResultLabels = src.showResultLabels;
            t.inputSourceMode = src.inputSourceMode;
            t.resultRoiMode = src.resultRoiMode;
            t.resultRoiSourceTool = src.resultRoiSourceTool;
            t.resultRoiIndex = src.resultRoiIndex;
            t.resultRoiMissingPolicy = src.resultRoiMissingPolicy;
            t.fixture = src.fixture;
            t.judgement = src.judgement;
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
            t.ocrDetModelPath = src.ocrDetModelPath;
            t.ocrDetParamPath = src.ocrDetParamPath;
            t.ocrRecModelPath = src.ocrRecModelPath;
            t.ocrRecParamPath = src.ocrRecParamPath;
            t.ocrDictionaryPath = src.ocrDictionaryPath;
            t.ocrMinConfidence = src.ocrMinConfidence;
            t.ocrMaxItems = src.ocrMaxItems;
            t.ocrInputSize = src.ocrInputSize;
            t.ocrMaxCandidates = src.ocrMaxCandidates;
            t.ocrMinBoxArea = src.ocrMinBoxArea;
            t.ocrMinBoxHeight = src.ocrMinBoxHeight;
            t.ocrRoiPadding = src.ocrRoiPadding;
            t.ocrFastMode = src.ocrFastMode;
            t.ocrDetectOnly = src.ocrDetectOnly;
            t.ocrUseROI = src.ocrUseROI;
            t.qrUseROI = src.qrUseROI;
            t.qrDetectMulti = src.qrDetectMulti;
            t.qrEnhance = src.qrEnhance;
            t.qrMinSize = src.qrMinSize;
            t.qrShowText = src.qrShowText;
            t.qrEngine = src.qrEngine;
            t.qrFormatMask = src.qrFormatMask;
            t.qrFilterDuplicates = src.qrFilterDuplicates;
            t.measureMode = src.measureMode;
            t.measureCaliperCount = src.measureCaliperCount;
            t.measureSearchLength = src.measureSearchLength;
            t.measureProjectionWidth = src.measureProjectionWidth;
            t.measureSmoothingSigma = src.measureSmoothingSigma;
            t.measureEdgeThreshold = src.measureEdgeThreshold;
            t.measureMinPairDistance = src.measureMinPairDistance;
            t.measureEdgePolarity = src.measureEdgePolarity;
            t.measureSubpixel = src.measureSubpixel;
            t.measureFitMethod = src.measureFitMethod;
            t.measureFitInlierThreshold = src.measureFitInlierThreshold;
            t.measureMinimumValidCalipers = src.measureMinimumValidCalipers;
            t.measureMinimumConfidence = src.measureMinimumConfidence;
            t.measureMmPerPixel = src.measureMmPerPixel;
            t.measureCalibration = src.measureCalibration;
            t.measureCalibrationPixels = src.measureCalibrationPixels;
            t.measureCalibrationMm = src.measureCalibrationMm;
            t.measureToleranceEnabled = src.measureToleranceEnabled;
            t.measureNominal = src.measureNominal;
            t.measureToleranceMinus = src.measureToleranceMinus;
            t.measureTolerancePlus = src.measureTolerancePlus;

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
            FrameNavigation::RequestImagePath(data.imagePath);

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
        if (data.tools.empty())
        {
            ToolInstance threshold;
            threshold.type = 3;
            threshold.dbgUseGray = data.threshold.useGray;
            threshold.dbgEnableBlur = data.threshold.pipeBlur;
            threshold.dbgBlurSize = data.threshold.pipeBlurSize;
            threshold.dbgEnableThresh = data.threshold.pipeThreshold;
            threshold.dbgThreshold = data.threshold.pipeThresholdVal;
            threshold.dbgEnableCanny = data.threshold.pipeCanny;
            threshold.dbgCannyLow = data.threshold.pipeCannyLow;
            threshold.dbgCannyHigh = data.threshold.pipeCannyHigh;
            ToolChainState::Tools().push_back(std::move(threshold));
        }
        for (size_t ti = 0; ti < data.tools.size(); ti++)
        {
            const auto &t = data.tools[ti];
            ToolInstance it;
            it.type = t.type;
            it.label = t.label;
            it.showResultLabels = t.showResultLabels;
            it.inputSourceMode = t.inputSourceMode;
            it.resultRoiMode = t.resultRoiMode;
            it.resultRoiSourceTool = t.resultRoiSourceTool;
            it.resultRoiIndex = t.resultRoiIndex;
            it.resultRoiMissingPolicy = t.resultRoiMissingPolicy;
            it.fixture = t.fixture;
            it.judgement = t.judgement;
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
            it.ocrDetModelPath = t.ocrDetModelPath;
            it.ocrDetParamPath = t.ocrDetParamPath;
            it.ocrRecModelPath = t.ocrRecModelPath;
            it.ocrRecParamPath = t.ocrRecParamPath;
            it.ocrDictionaryPath = t.ocrDictionaryPath;
            it.ocrMinConfidence = t.ocrMinConfidence;
            it.ocrMaxItems = t.ocrMaxItems;
            it.ocrInputSize = t.ocrInputSize;
            it.ocrMaxCandidates = t.ocrMaxCandidates;
            it.ocrMinBoxArea = t.ocrMinBoxArea;
            it.ocrMinBoxHeight = t.ocrMinBoxHeight;
            it.ocrRoiPadding = t.ocrRoiPadding;
            it.ocrFastMode = t.ocrFastMode;
            it.ocrDetectOnly = t.ocrDetectOnly;
            it.ocrUseROI = t.ocrUseROI;
            it.qrUseROI = t.qrUseROI;
            it.qrDetectMulti = t.qrDetectMulti;
            it.qrEnhance = t.qrEnhance;
            it.qrMinSize = t.qrMinSize;
            it.qrShowText = t.qrShowText;
            it.qrEngine = t.qrEngine;
            it.qrFormatMask = t.qrFormatMask;
            it.qrFilterDuplicates = t.qrFilterDuplicates;
            it.measureMode = t.measureMode;
            it.measureCaliperCount = t.measureCaliperCount;
            it.measureSearchLength = t.measureSearchLength;
            it.measureProjectionWidth = t.measureProjectionWidth;
            it.measureSmoothingSigma = t.measureSmoothingSigma;
            it.measureEdgeThreshold = t.measureEdgeThreshold;
            it.measureMinPairDistance = t.measureMinPairDistance;
            it.measureEdgePolarity = t.measureEdgePolarity;
            it.measureSubpixel = t.measureSubpixel;
            it.measureFitMethod = t.measureFitMethod;
            it.measureFitInlierThreshold = t.measureFitInlierThreshold;
            it.measureMinimumValidCalipers = t.measureMinimumValidCalipers;
            it.measureMinimumConfidence = t.measureMinimumConfidence;
            it.measureMmPerPixel = t.measureMmPerPixel;
            it.measureCalibration = t.measureCalibration;
            it.measureCalibrationPixels = t.measureCalibrationPixels;
            it.measureCalibrationMm = t.measureCalibrationMm;
            it.measureToleranceEnabled = t.measureToleranceEnabled;
            it.measureNominal = t.measureNominal;
            it.measureToleranceMinus = t.measureToleranceMinus;
            it.measureTolerancePlus = t.measureTolerancePlus;

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


