#include "ITool.h"

#include "BlobTool.h"
#include "ColorAnalyzer.h"
#include "EdgeTool.h"
#include "MorphologyTool.h"
#include "MultiColorFinder.h"
#include "ShapeTools.h"
#include "ThresholdTool.h"
#include "YOLOTool.h"

#include <memory>
#include <unordered_map>

namespace
{
    std::unordered_map<int, ToolFactory>& GetRegistry()
    {
        static std::unordered_map<int, ToolFactory> reg;
        return reg;
    }

    std::unordered_map<int, std::string>& GetNames()
    {
        static std::unordered_map<int, std::string> names;
        return names;
    }
}

namespace ToolRegistry
{
    void Register(int type, ToolFactory factory)
    {
        GetRegistry()[type] = factory;
    }

    std::unique_ptr<ITool> Create(int type)
    {
        auto& reg = GetRegistry();
        auto it = reg.find(type);
        if (it != reg.end())
            return it->second();
        return nullptr;
    }

    void RegisterName(int type, const char* name)
    {
        GetNames()[type] = name;
    }

    const char* GetName(int type)
    {
        auto& names = GetNames();
        auto it = names.find(type);
        return it != names.end() ? it->second.c_str() : "Unknown";
    }
}

std::unique_ptr<ITool> ITool::Create(int type)
{
    return ToolRegistry::Create(type);
}

namespace
{
    struct AutoRegister
    {
        AutoRegister()
        {
            ToolRegistry::Register(0, []() -> std::unique_ptr<ITool> { return std::make_unique<EdgeTool>(); });
            ToolRegistry::RegisterName(0, "Edge");
            ToolRegistry::Register(2, []() -> std::unique_ptr<ITool> { return std::make_unique<BlobTool>(); });
            ToolRegistry::RegisterName(2, "Blob");
            ToolRegistry::Register(3, []() -> std::unique_ptr<ITool> { return std::make_unique<ThresholdITool>(); });
            ToolRegistry::RegisterName(3, "Threshold");
            ToolRegistry::Register(4, []() -> std::unique_ptr<ITool> { return std::make_unique<YOLOTool>(); });
            ToolRegistry::RegisterName(4, "YOLO");
            ToolRegistry::Register(5, []() -> std::unique_ptr<ITool> { return std::make_unique<ContourTool>(); });
            ToolRegistry::RegisterName(5, "Contour");
            ToolRegistry::Register(6, []() -> std::unique_ptr<ITool> { return std::make_unique<ShapeTool>(); });
            ToolRegistry::RegisterName(6, "Shape");
            ToolRegistry::Register(7, []() -> std::unique_ptr<ITool> { return std::make_unique<LineTool>(); });
            ToolRegistry::RegisterName(7, "Line");
            ToolRegistry::Register(8, []() -> std::unique_ptr<ITool> { return std::make_unique<MorphologyITool>(); });
            ToolRegistry::RegisterName(8, "Morphology");
            ToolRegistry::Register(9, []() -> std::unique_ptr<ITool> { return std::make_unique<ColorAnalyzerITool>(); });
            ToolRegistry::RegisterName(9, "ColorAnalyzer");
            ToolRegistry::Register(10, []() -> std::unique_ptr<ITool> { return std::make_unique<MultiColorFinder>(); });
            ToolRegistry::RegisterName(10, "MultiColorFinder");
        }
    };

    static AutoRegister s_AutoReg;
}
