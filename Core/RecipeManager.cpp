#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RecipeManager.h"
#include "FrameSourceState.h"
#include "FrameNavigation.h"
#include "ROIState.h"
#include "TemplateState.h"
#include "ToolChainState.h"
#include "ToolController.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/MultiColorFinder.h"

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

nlohmann::json RecipeToolInstance::ToJson() const
{
    return toolJson_.is_object() ? toolJson_ : nlohmann::json::object();
}

void RecipeToolInstance::LoadToolJson(const nlohmann::json& json)
{
    toolJson_ = json.is_object() ? json : nlohmann::json::object();
    ToolInstance compatibility;
    compatibility.LoadRecipeJson(toolJson_);
    if (!compatibility.useSearchROI && !compatibility.searchROIs.empty())
    {
        compatibility.useSearchROI = compatibility.yoloUseROI ||
            compatibility.lineUseROI || compatibility.mcfUseROI ||
            compatibility.colorUseROI || !compatibility.lineSaveROIs.empty();
        toolJson_["useSearchROI"] = compatibility.useSearchROI;
    }
}

void RecipeToolInstance::CaptureFrom(const ToolInstance& source)
{
    toolJson_ = source.ToRecipeJson();
    templateImage = source.type == 6 ? source.shpTplImage.clone() : source.templateImg.clone();
    if (templateImage.empty())
        templateImage = source.type == 6 ? source.templateImg.clone() : source.shpTplImage.clone();
    differenceReferenceImage = source.differenceReferenceImage.clone();
    multiColorReferenceImage = source.mcfRefImage.clone();
}

ToolInstance RecipeToolInstance::CreateToolInstance() const
{
    ToolInstance target;
    target.LoadRecipeJson(toolJson_);
    target.ClearRuntimeState();
    target.templateImg = templateImage.clone();
    if (target.type == 6)
        target.shpTplImage = templateImage.clone();
    target.differenceReferenceImage = differenceReferenceImage.clone();
    target.mcfRefImage = multiColorReferenceImage.clone();
    return target;
}

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

    static std::string RecipeAssetOutputPath(const std::string& recipePath,
                                              const std::string& assetPath)
    {
        if (assetPath.empty())
            return {};
        namespace fs = std::filesystem;
        fs::path asset(Utf8ToWide(assetPath));
        if (asset.is_absolute())
            return WideToUtf8(asset.lexically_normal().wstring());
        const fs::path recipe(Utf8ToWide(recipePath));
        return WideToUtf8((recipe.parent_path() / asset).lexically_normal().wstring());
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
            rois.push_back({{"startX", r.startX}, {"startY", r.startY},
                {"endX", r.endX}, {"endY", r.endY}, {"angle", r.angle}, {"type", r.type}});
        }

        // Tool instances
        json &tools = j["tools"] = json::array();
        for (const auto &t : data.tools)
        {
            json tj = t.ToJson();
            tj["templateFile"] = t.templateFile;
            tj["differenceReferenceFile"] = t.differenceReferenceFile;
            tj["mcfPointsJson"] = t.mcfPointsJson;
            tj["mcfRefImageBase64"] = "";
            if (!t.multiColorReferenceImage.empty())
            {
                std::vector<uchar> encoded;
                if (cv::imencode(".png", t.multiColorReferenceImage, encoded))
                    tj["mcfRefImageBase64"] = Base64Encode(encoded);
            }
            tools.push_back(std::move(tj));
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

        // Save asset payloads captured in RecipeData. Save no longer reads live tool state.
        for (size_t ti = 0; ti < data.tools.size(); ti++)
        {
            const auto &t = data.tools[ti];
            const std::string tplFile = t.templateFile.empty()
                ? data.name + "_tpl" + std::to_string(ti) + ".png"
                : t.templateFile;
            const std::string tplPath = RecipeAssetOutputPath(filepath, tplFile);
            if (t.templateFile.empty())
            {
                DeleteFileW(Utf8ToWide(tplPath).c_str());
            }
            else if (!t.templateImage.empty())
            {
                if (WriteImageFile(tplPath, t.templateImage))
                    LogSystem::Add(LOG_INFO, "Tool template saved: %s", tplPath.c_str());
            }

            const std::string differencePath = RecipeAssetOutputPath(
                filepath, t.differenceReferenceFile);
            if (!differencePath.empty() && !t.differenceReferenceImage.empty() &&
                WriteImageFile(differencePath, t.differenceReferenceImage))
            {
                LogSystem::Add(LOG_INFO, "Difference reference saved: %s", t.differenceReferenceFile.c_str());
            }
        }

        LogSystem::Add(LOG_INFO, "Recipe saved: %s", filepath);
        return true;
    }

    // ===================== Load =====================
    static std::string g_LastRecipePath;

    static std::string ResolveRecipeAssetPath(const std::string& value)
    {
        if (value.empty())
            return {};

        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path requested(Utf8ToWide(value));
        if (requested.is_absolute())
            return WideToUtf8(requested.lexically_normal().wstring());

        std::vector<fs::path> roots;
        if (!g_LastRecipePath.empty())
        {
            fs::path recipeDir = fs::path(Utf8ToWide(g_LastRecipePath)).parent_path();
            for (int depth = 0; !recipeDir.empty() && depth < 8; ++depth)
            {
                roots.push_back(recipeDir);
                fs::path parent = recipeDir.parent_path();
                if (parent == recipeDir)
                    break;
                recipeDir = std::move(parent);
            }
        }
        roots.push_back(fs::current_path(ec));

        for (const fs::path& root : roots)
        {
            if (root.empty())
                continue;
            fs::path candidate = (root / requested).lexically_normal();
            ec.clear();
            if (fs::exists(candidate, ec))
                return WideToUtf8(candidate.wstring());
        }
        return WideToUtf8(requested.lexically_normal().wstring());
    }

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
                roi.angle = r.value("angle", 0.0f);
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
                t.LoadToolJson(tj);
                t.templateFile = tj.value("templateFile", "");
                t.differenceReferenceFile = tj.value("differenceReferenceFile", "");
                t.mcfPointsJson = tj.value("mcfPointsJson", "");

                const std::string mcfImageValue = tj.value("mcfRefImageBase64", "");
                if (!mcfImageValue.empty())
                {
                    std::vector<uchar> buffer = Base64Decode(mcfImageValue);
                    if (buffer.empty())
                        buffer.assign(mcfImageValue.begin(), mcfImageValue.end());
                    t.multiColorReferenceImage = cv::imdecode(buffer, cv::IMREAD_COLOR);
                }

                if (!t.templateFile.empty())
                {
                    const std::string templatePath = ResolveRecipeAssetPath(t.templateFile);
                    t.templateImage = ReadImageFile(templatePath, cv::IMREAD_COLOR);
                    if (!t.templateImage.empty())
                        LogSystem::Add(LOG_INFO, "Tool template loaded: %s (%dx%d)",
                            templatePath.c_str(), t.templateImage.cols, t.templateImage.rows);
                }
                if (!t.differenceReferenceFile.empty())
                {
                    const std::string differencePath = ResolveRecipeAssetPath(
                        t.differenceReferenceFile);
                    t.differenceReferenceImage = ReadImageFile(differencePath, cv::IMREAD_COLOR);
                    if (!t.differenceReferenceImage.empty())
                        LogSystem::Add(LOG_INFO, "Difference reference loaded: %s (%dx%d)",
                            differencePath.c_str(), t.differenceReferenceImage.cols,
                            t.differenceReferenceImage.rows);
                }

                data.tools.push_back(std::move(t));
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

        // ROI
        d.rois.clear();
        for (const auto &roi : ROIState::ReadOnlyItems())
        {
            RecipeROI r;
            r.startX = roi.start.x;
            r.startY = roi.start.y;
            r.endX = roi.end.x;
            r.endY = roi.end.y;
            r.angle = roi.angle;
            r.type = roi.type;
            d.rois.push_back(r);
        }

        d.templateImage = d.name + ".png";

        // 工具实例
        d.tools.clear();
        LogSystem::Add(LOG_INFO, "[Capture] Core tool instances: %zu",
                       ToolChainState::ReadOnlyTools().size());
        for (size_t ti = 0; ti < ToolChainState::ReadOnlyTools().size(); ti++)
        {
            const auto &src = ToolChainState::ReadOnlyTools()[ti];
            RecipeToolInstance t;
            t.CaptureFrom(src);
            if (!src.templateImg.empty() || !src.shpTplImage.empty())
                t.templateFile = d.name + "_tpl" + std::to_string(ti) + ".png";
            if (!src.differenceReferenceImage.empty())
                t.differenceReferenceFile = d.name + "_diff_ref_" + std::to_string(ti) + ".png";

            if (src.type == 10)
            {
                if (src.toolImpl)
                {
                    auto* finder = dynamic_cast<MultiColorFinder*>(src.toolImpl);
                    if (finder)
                        t.mcfPointsJson = finder->Save().dump();
                }
            }

            d.tools.push_back(std::move(t));
        }

        return d;
    }

    // ===================== Apply recipe to current runtime =====================
    void Apply(const RecipeData &data)
    {
        // ROI
        std::vector<ROI> restoredROIs;
        restoredROIs.reserve(data.rois.size());
        for (const auto &r : data.rois)
        {
            ROI roi;
            roi.start = ImVec2(r.startX, r.startY);
            roi.end = ImVec2(r.endX, r.endY);
            roi.angle = r.angle;
            roi.type = r.type;
            restoredROIs.push_back(std::move(roi));
        }
        if (!data.imagePath.empty())
        {
            ROIState::QueueRestoreAfterImageLoad(restoredROIs);
            FrameNavigation::RequestImagePath(ResolveRecipeAssetPath(data.imagePath));
        }
        else
        {
            ROIState::CancelQueuedRestore();
            ROIState::Items() = std::move(restoredROIs);
            ROIState::SetSelectedIndex(-1);
        }

        // 工具实例
        ToolController::OnToolChainChanged();
        ToolChainState::ClearTools();
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
            ToolChainState::AddTool(std::move(threshold));
        }
        for (size_t ti = 0; ti < data.tools.size(); ti++)
        {
            const auto &t = data.tools[ti];
            ToolInstance it = t.CreateToolInstance();

            it.yoloModelPath = ResolveRecipeAssetPath(it.yoloModelPath);
            it.yoloClassesPath = ResolveRecipeAssetPath(it.yoloClassesPath);
            it.ocrDetModelPath = ResolveRecipeAssetPath(it.ocrDetModelPath);
            it.ocrDetParamPath = ResolveRecipeAssetPath(it.ocrDetParamPath);
            it.ocrRecModelPath = ResolveRecipeAssetPath(it.ocrRecModelPath);
            it.ocrRecParamPath = ResolveRecipeAssetPath(it.ocrRecParamPath);
            it.ocrDictionaryPath = ResolveRecipeAssetPath(it.ocrDictionaryPath);

            if (it.type == 10)
            {
                if (!t.mcfPointsJson.empty())
                {
                    const json points = json::parse(t.mcfPointsJson, nullptr, false);
                    if (!points.is_discarded())
                    {
                        it.toolImpl = ITool::Create(10).release();
                        if (auto* finder = dynamic_cast<MultiColorFinder*>(it.toolImpl))
                            finder->Load(points);
                    }
                    else
                    {
                        LogSystem::Add(LOG_WARN, "Multi-color recipe points JSON is invalid");
                    }
                }
            }

            ToolChainState::AddTool(std::move(it));
        }

        ToolChainState::EnsureToolIds();
        for (ToolInstance& tool : ToolChainState::Tools())
        {
            if (tool.resultRoiSourceToolId == 0 &&
                tool.resultRoiSourceTool >= 0 &&
                tool.resultRoiSourceTool < static_cast<int>(ToolChainState::ReadOnlyTools().size()))
            {
                tool.resultRoiSourceToolId =
                    ToolChainState::ReadOnlyTools()[tool.resultRoiSourceTool].toolId;
            }
            if (tool.fixture.sourceToolId == 0 &&
                tool.fixture.sourceToolIndex >= 0 &&
                tool.fixture.sourceToolIndex < static_cast<int>(ToolChainState::ReadOnlyTools().size()))
            {
                tool.fixture.sourceToolId =
                    ToolChainState::ReadOnlyTools()[tool.fixture.sourceToolIndex].toolId;
            }
        }
        ToolChainState::MoveOriginalToolToFront();

        LogSystem::Add(LOG_INFO, "[Apply] recipe applied: %s (tools: %zu)", data.name.c_str(), data.tools.size());
    }

} // namespace RecipeManager


