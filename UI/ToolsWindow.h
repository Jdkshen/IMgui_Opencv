#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include "../Core/ToolInstance.h"
#include "../Core/ToolTypes.h"

using ToolUIFn = std::function<void(ToolInstance& it, int inst)>;

extern std::unordered_map<int, ToolUIFn> g_ToolUIMap;

namespace UI
{
    void ShowToolsWindow();
    void MoveOriginalToolToFront();
    bool BindSelectedTaskImagePath(const std::string& imagePath);
}
