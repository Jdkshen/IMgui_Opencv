#pragma once
#include "../Windows_imgui.h"

// =========================
// 字体管理器命名空间
// 负责加载中文字体并管理字体资源
// =========================
namespace FontManager
{
    // 初始化字体（根据DPI缩放）
    // 优先加载 simhei.ttf（黑体），失败则加载 msyh.ttc（微软雅黑）
    ImFont* InitFonts(float dpi_scale);

    // 获取默认字体
    ImFont* GetDefaultFont();
}