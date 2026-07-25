#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <string>
#include <windows.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "FontManager.h"
#include "../include/imgui/imgui_internal.h"
#include "../Log/LogSystem.h"

static ImFont *g_DefaultFont = nullptr;
static ImFontAtlasRectId g_ToolIconRects[12] = {};

namespace FontManager
{

    static bool FileExists(const char *path)
    {
        FILE *f = fopen(path, "rb");
        if (f)
        {
            fclose(f);
            return true;
        }
        return false;
    }

    // Build glyph ranges: Chinese + icon symbols
    static const ImWchar *BuildIconGlyphRanges()
    {
        static ImVector<ImWchar> ranges;
        if (!ranges.empty())
            return ranges.Data;

        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());

        // Icon blocks: Geometric Shapes, Dingbats, Misc Symbols, Misc Technical
        static const ImWchar iconRanges[] = {
            0x25A0,
            0x25FF, // Geometric Shapes
            0x2700,
            0x27BF, // Dingbats
            0x2600,
            0x26FF, // Miscellaneous Symbols
            0x2300,
            0x23FF, // Miscellaneous Technical
            0,
        };
        builder.AddRanges(iconRanges);
        builder.BuildRanges(&ranges);
        return ranges.Data;
    }

    // Get directory of the running exe
    static std::string GetExeDir()
    {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string dir(path);
        size_t pos = dir.find_last_of("\\/");
        return dir.substr(0, pos + 1);
    }

    static const char* ToolIconFileName(int type)
    {
        switch (type)
        {
        case 0:  return "tool_edge.png";
        case 1:  return "tool_template.png";
        case 2:  return "tool_blob.png";
        case 3:  return "tool_threshold.png";
        case 4:  return "tool_yolo.png";
        case 5:  return "tool_contour.png";
        case 6:  return "tool_shape.png";
        case 7:  return "tool_line.png";
        case 8:  return "tool_morphology.png";
        case 9:  return "tool_color.png";
        case 10: return "tool_multicolor.png";
        case 11: return "tool_opencv5.png";
        default: return "";
        }
    }

    static std::string ResolveToolIconPath(const char* fileName)
    {
        std::string exeDir = GetExeDir();
        std::string path = exeDir + "assets\\icons\\" + fileName;
        if (FileExists(path.c_str()))
            return path;

        path = exeDir + "..\\..\\assets\\icons\\" + fileName;
        if (FileExists(path.c_str()))
            return path;

        return "";
    }

    static void RegisterToolIconRects(ImFontAtlas* atlas)
    {
        for (int i = 0; i < IM_ARRAYSIZE(g_ToolIconRects); ++i)
            g_ToolIconRects[i] = ImFontAtlasRectId_Invalid;

        for (int type = 0; type < IM_ARRAYSIZE(g_ToolIconRects); ++type)
        {
            if (ToolIconFileName(type)[0] == '\0')
                continue;
            g_ToolIconRects[type] = atlas->AddCustomRect(24, 24);
        }
    }

    static bool LoadIconRGBA(int type, cv::Mat& rgba)
    {
        const std::string path = ResolveToolIconPath(ToolIconFileName(type));
        if (path.empty())
            return false;

        cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (img.empty())
            return false;

        if (img.cols != 24 || img.rows != 24)
            cv::resize(img, img, cv::Size(24, 24), 0.0, 0.0, cv::INTER_AREA);

        if (img.channels() == 4)
            cv::cvtColor(img, rgba, cv::COLOR_BGRA2RGBA);
        else if (img.channels() == 3)
            cv::cvtColor(img, rgba, cv::COLOR_BGR2RGBA);
        else if (img.channels() == 1)
            cv::cvtColor(img, rgba, cv::COLOR_GRAY2RGBA);
        else
            return false;

        return !rgba.empty() && rgba.isContinuous();
    }

    static void PaintToolIconRects(ImFontAtlas* atlas)
    {
        unsigned char* pixels = nullptr;
        int atlasW = 0, atlasH = 0, bpp = 0;
        atlas->GetTexDataAsRGBA32(&pixels, &atlasW, &atlasH, &bpp);
        if (!pixels || bpp != 4)
            return;

        int loaded = 0;
        for (int type = 0; type < IM_ARRAYSIZE(g_ToolIconRects); ++type)
        {
            if (g_ToolIconRects[type] == ImFontAtlasRectId_Invalid)
                continue;

            ImFontAtlasRect rect;
            if (!atlas->GetCustomRect(g_ToolIconRects[type], &rect))
                continue;

            cv::Mat rgba;
            if (!LoadIconRGBA(type, rgba))
                continue;

            for (int y = 0; y < 24; ++y)
            {
                unsigned char* dst = pixels + ((rect.y + y) * atlasW + rect.x) * 4;
                const unsigned char* src = rgba.ptr<unsigned char>(y);
                memcpy(dst, src, 24 * 4);
            }
            loaded++;
        }

        if (loaded > 0)
            LogSystem::Add(LOG_INFO, "Tool PNG icons merged: %d", loaded);
    }

    ImFont *InitFonts(float)
    {
        ImGuiIO &io = ImGui::GetIO();

        if (g_DefaultFont)
            return g_DefaultFont;

        ImFont *font = nullptr;

        constexpr float kBaseFontSize = 14.0f;

        // Prefer Microsoft YaHei UI for a cleaner Chinese application UI.
        const char *preferredFont = "C:/Windows/Fonts/msyh.ttc";
        if (FileExists(preferredFont))
        {
            font = io.Fonts->AddFontFromFileTTF(
                preferredFont, kBaseFontSize,
                nullptr, BuildIconGlyphRanges());
            LogSystem::Add(LOG_INFO, "Microsoft YaHei UI loaded");
        }

        // Packaged fallback keeps Chinese glyphs available on minimal systems.
        if (!font)
        {
            std::string localFont = GetExeDir() + "simsun.ttc";
            if (FileExists(localFont.c_str()))
            {
                font = io.Fonts->AddFontFromFileTTF(
                    localFont.c_str(), kBaseFontSize,
                    nullptr, BuildIconGlyphRanges());
                LogSystem::Add(LOG_INFO, "simsun.ttc fallback loaded");
            }
        }

        // Fallback default
        if (!font)
        {
            LogSystem::Add(LOG_ERROR, "font load failed, using default");
            font = io.Fonts->AddFontDefault();
        }

        io.FontDefault = font;

        // Merge Material Icons（优先）
        {
            std::string miPath = GetExeDir() + "MaterialIcons-Regular.ttf";
            if (FileExists(miPath.c_str())) {
                ImFontConfig miCfg;
                miCfg.MergeMode = true;
                miCfg.PixelSnapH = true;
                static const ImWchar iconRanges[] = { 0xE000, 0xF8FF, 0 };
                io.Fonts->AddFontFromFileTTF(miPath.c_str(), 18.0f, &miCfg, iconRanges);
                LogSystem::Add(LOG_INFO, "MaterialIcons-Regular.ttf merged");
            }
        }

        // Merge Segoe UI Emoji（备选）
        {
            const char* emojiPath = "C:/Windows/Fonts/seguiemj.ttf";
            if (FileExists(emojiPath)) {
                ImFontConfig emojiCfg;
                emojiCfg.MergeMode = true;
                ImFontGlyphRangesBuilder builder;
                builder.AddText("📦🎯📐🎨🧪📊⚙️▶🔬🛠️📁💾");
                ImVector<ImWchar> emojiRanges;
                builder.BuildRanges(&emojiRanges);
                io.Fonts->AddFontFromFileTTF(emojiPath, 16.0f, &emojiCfg, emojiRanges.Data);
                LogSystem::Add(LOG_INFO, "Segoe UI Emoji merged");
            }
        }

        RegisterToolIconRects(io.Fonts);
        io.Fonts->Build();
        PaintToolIconRects(io.Fonts);
        g_DefaultFont = font;
        return font;
    }

    ImFont *GetDefaultFont() { return g_DefaultFont; }

    bool GetToolIconRect(int toolType, ImFontAtlasRect* outRect)
    {
        if (!outRect || toolType < 0 || toolType >= IM_ARRAYSIZE(g_ToolIconRects))
            return false;
        if (g_ToolIconRects[toolType] == ImFontAtlasRectId_Invalid)
            return false;
        return ImGui::GetIO().Fonts->GetCustomRect(g_ToolIconRects[toolType], outRect);
    }

}
