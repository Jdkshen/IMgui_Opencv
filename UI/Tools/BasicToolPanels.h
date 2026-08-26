#pragma once

#include <functional>
#include <unordered_map>

#include "../../Core/ToolInstance.h"

using ToolUIFn = std::function<void(ToolInstance& tool, int instanceIndex)>;

struct ToolPanelContext
{
    std::function<void(const char*)> beginCard;
    std::function<void(const char*, const char*)> beginCardWithIcon;
    std::function<void()> endCard;
    std::function<void(const char*)> sectionHeader;
    std::function<bool(const char*)> primaryButton;
    std::function<bool(const char*)> secondaryButton;
    std::function<bool(const char*, float)> secondaryButtonSized;
    std::function<void(const char*, float)> parameterLabel;
    std::function<bool(int)> runTool;
    std::function<void(ToolInstance&, int)> drawSearchROI;
    std::function<void()> markRecipeAssetsDirty;
    std::function<void()> saveRecipe;
};

void RegisterBasicToolPanels(
    std::unordered_map<int, ToolUIFn>& panels,
    const ToolPanelContext& context);
