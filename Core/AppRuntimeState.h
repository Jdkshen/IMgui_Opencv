#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace AppRuntimeState
{
    HWND WindowHandle();
    void SetWindowHandle(HWND window);

    float DpiScale();
    void SetDpiScale(float scale);
}
