#pragma once
#include <string>
#include <vector>
#include <memory>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include "ToolResult.h"

// 前向声明
struct VisionContext;

// =====================================================
// 工具基类
// =====================================================
class ITool
{
public:
    virtual ~ITool() = default;

    // ---- 标识 ----
    virtual const char* GetName() const = 0;
    virtual int GetType() const = 0;

    // ---- 执行（传入上下文，含图像 + ROI + 视图状态） ----
    virtual ToolResult Execute(VisionContext& ctx) = 0;

    // ---- UI ----
    virtual void DrawUI() = 0;

    // ---- 序列化 ----
    virtual nlohmann::json Save() const = 0;
    virtual void Load(const nlohmann::json& j) = 0;

    // ---- 工厂 ----
    static std::unique_ptr<ITool> Create(int type);
};

// =====================================================
// 工具工厂（注册/创建）
// =====================================================
using ToolFactory = std::unique_ptr<ITool>(*)();

namespace ToolRegistry
{
    void Register(int type, ToolFactory factory);
    void RegisterName(int type, const char* name);
    std::unique_ptr<ITool> Create(int type);
    const char* GetName(int type);
}
