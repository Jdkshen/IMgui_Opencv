#pragma once
#include "../Windows_imgui.h"

// Icon macros (UTF-8 encoded Unicode glyphs)
#define ICON_PLAY "\xE2\x96\xB6"    // U+25B6
#define ICON_STOP "\xE2\x96\xA0"    // U+25A0
#define ICON_ADD "\xE2\x9C\x9A"     // U+271A
#define ICON_REMOVE "\xE2\x9C\x96"  // U+2716
#define ICON_GEAR "\xE2\x9A\x99"    // U+2699
#define ICON_IMAGE "\xE2\x96\xA3"   // U+25A3
#define ICON_DETECT "\xE2\x97\x89"  // U+25C9
#define ICON_MATCH "\xE2\x97\x87"   // U+25C7
#define ICON_BLOB "\xE2\x97\x8F"    // U+25CF
#define ICON_EDGE "\xE2\x97\x8B"    // U+25CB
#define ICON_THRESH "\xE2\x97\x98"  // U+25D8
#define ICON_CONTOUR "\xE2\x97\xA6" // U+25E6
#define ICON_SHAPE "\xE2\x97\x86"   // U+25C6
#define ICON_LINE "\xE2\x94\x80"    // U+2500

namespace FontManager
{
    ImFont *InitFonts(float dpi_scale);
    ImFont *GetDefaultFont();
    bool GetToolIconRect(int toolType, ImFontAtlasRect* outRect);
}
