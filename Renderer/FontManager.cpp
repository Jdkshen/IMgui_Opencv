#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <string>
#include <windows.h>

#include "../Windows_imgui.h"

static ImFont* g_DefaultFont = nullptr;

namespace FontManager
{

    static bool FileExists(const char* path)
    {
        FILE* f = fopen(path, "rb");

        if (f)
        {
            fclose(f);
            return true;
        }

        return false;
    }

    // 获取 exe 所在目录的绝对路径（如 "E:\\...\\x64\\Debug\\"）
    static std::string GetExeDir()
    {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string dir(path);
        size_t pos = dir.find_last_of("\\/");
        return dir.substr(0, pos + 1);
    }


    //=========================
    // 初始化字体
    //=========================
    ImFont* InitFonts(float dpi_scale)
    {
        ImGuiIO& io = ImGui::GetIO();

        // 防止重复初始化
        if (g_DefaultFont)
            return g_DefaultFont;

        ImFont* font = nullptr;

        //----------------------------------
        // 优先本地黑体（和 exe 同目录）
        //----------------------------------
        std::string localFont = GetExeDir() + "simhei.ttf";

        if (FileExists(localFont.c_str()))
        {
            font =
                io.Fonts->AddFontFromFileTTF(
                    localFont.c_str(),
                    12.0f * dpi_scale,
                    nullptr,
                    io.Fonts->GetGlyphRangesChineseFull()
                );

            LogSystem::Add(
                LOG_INFO,
                color,
                "加载 simhei.ttf 成功"
            );
        }

        //----------------------------------
        // 系统微软雅黑
        //----------------------------------

        if (!font)
        {
            const char* path =
                "C:/Windows/Fonts/msyh.ttc";

            if (FileExists(path))
            {
                font =
                    io.Fonts->AddFontFromFileTTF(
                        path,
                        12.0f * dpi_scale,
                        nullptr,
                        io.Fonts->GetGlyphRangesChineseFull()
                    );

                LogSystem::Add(
                    LOG_INFO,
                    color,
                    "加载系统微软雅黑成功"
                );
            }
        }

        //----------------------------------
        // 最后降级默认字体
        //----------------------------------

        if (!font)
        {
            LogSystem::Add(
                LOG_ERROR,
                color,
                "字体加载失败，使用默认字体"
            );

            font = io.Fonts->AddFontDefault();
        }

        io.FontDefault = font;

        io.Fonts->Build();

        g_DefaultFont = font;

        return font;
    }


    //=========================
    // 获取默认字体
    //=========================
    ImFont* GetDefaultFont()
    {
        return g_DefaultFont;
    }

}