#pragma once

// =====================================================
// 工具分类 + 注册表元数据
// =====================================================
enum class ToolCategory { Base, Detection, Geometry, Analysis, Experimental, COUNT };

struct ToolMeta
{
    int type;
    const char* name;
    ToolCategory category;
    const char* icon;
    const char* description;
};
