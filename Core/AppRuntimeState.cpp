#include "AppRuntimeState.h"

#include <algorithm>

namespace
{
HWND s_windowHandle = nullptr;
float s_dpiScale = 1.0f;
}

namespace AppRuntimeState
{
HWND WindowHandle() { return s_windowHandle; }
void SetWindowHandle(HWND window) { s_windowHandle = window; }

float DpiScale() { return s_dpiScale; }
void SetDpiScale(float scale) { s_dpiScale = (std::max)(0.5f, scale); }
}
