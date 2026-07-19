#pragma once

// 兼容入口：模板匹配算法已由 TemplateMatchingTool + ToolInstance 参数承载。
// 本头文件只保留旧配方/旧调用所需的模板文件操作和结果清理，不暴露算法全局参数。
namespace TemplateMatch
{
    void Clear();
    bool SaveTemplate(const char* filepath);
    bool LoadTemplate(const char* filepath);
}
