#pragma once

#include "../Core/ToolTypes.h"

#include <string>
#include <vector>

namespace UI
{
    extern const std::vector<ToolMeta> g_ToolRegistry;

    void ShowToolsWindow();
    void MoveOriginalToolToFront();
    bool BindSelectedTaskImagePath(const std::string& imagePath);
}
