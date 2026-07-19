#include "ToolChainState.h"
#include "ToolAssetService.h"
#include "ToolROIService.h"

#include <algorithm>
#include <optional>

// =====================================================
// 内部状态（模块私有）
// =====================================================
namespace ToolChainState
{
namespace
{
    std::vector<ToolInstance> s_tools;       // 工具实例列表
    int s_activeToolIndex = -1;              // 当前激活的工具索引（-1 = 无）
    bool s_yoloLiveDetect = false;           // YOLO 实时检测开关
    int s_yoloLiveInstanceIndex = -1;        // 实时检测使用的工具实例索引
    float s_yoloLastTimeMs = 0.0f;           // 最近一次推理耗时
    float s_yoloLiveFrameMs = 0.0f;          // 实时帧耗时
    float s_mcfLastTimeMs = 0.0f;
    int s_mcfLastCount = 0;
    std::uint64_t s_nextToolId = 1;
    std::optional<ToolInstance> s_toolClipboard;

    std::uint64_t EnsureToolIdInternal(ToolInstance& tool)
    {
        if (tool.toolId == 0)
            tool.toolId = s_nextToolId++;
        else if (tool.toolId >= s_nextToolId)
            s_nextToolId = tool.toolId + 1;
        return tool.toolId;
    }

    ToolInstance CloneToolForInsertion(const ToolInstance& source)
    {
        ToolInstance copy = source;
        copy.toolId = 0;
        copy.toolImpl = nullptr;
        copy.lastResult = ToolResult{};
        copy.hasLastResult = false;
        copy.measureRuntimeROIIds.clear();
        copy.templateImg = source.templateImg.empty() ? cv::Mat() : source.templateImg.clone();
        copy.shpTplImage = source.shpTplImage.empty() ? cv::Mat() : source.shpTplImage.clone();
        copy.mcfRefImage = source.mcfRefImage.empty() ? cv::Mat() : source.mcfRefImage.clone();
        copy.differenceReferenceImage = source.differenceReferenceImage.empty()
            ? cv::Mat() : source.differenceReferenceImage.clone();
        return copy;
    }

    bool InsertToolClone(int insertIndex, const ToolInstance& source, int* insertedIndex)
    {
        if (insertIndex < 0 || insertIndex > static_cast<int>(s_tools.size()))
            return false;

        ToolInstance copy = CloneToolForInsertion(source);
        if (copy.resultRoiSourceTool >= insertIndex)
            ++copy.resultRoiSourceTool;
        if (copy.fixture.sourceToolIndex >= insertIndex)
            ++copy.fixture.sourceToolIndex;

        s_tools.insert(s_tools.begin() + insertIndex, std::move(copy));
        EnsureToolIdInternal(s_tools[insertIndex]);

        for (int i = 0; i < static_cast<int>(s_tools.size()); ++i)
        {
            if (i == insertIndex)
                continue;
            ToolInstance& tool = s_tools[i];
            if (tool.resultRoiSourceTool >= insertIndex)
                ++tool.resultRoiSourceTool;
            if (tool.fixture.sourceToolIndex >= insertIndex)
                ++tool.fixture.sourceToolIndex;
        }
        if (s_activeToolIndex >= insertIndex)
            ++s_activeToolIndex;
        if (s_yoloLiveInstanceIndex >= insertIndex)
            ++s_yoloLiveInstanceIndex;

        if (insertedIndex)
            *insertedIndex = insertIndex;
        return true;
    }
}

std::vector<ToolInstance>& Tools()
{
    for (ToolInstance& tool : s_tools)
        EnsureToolIdInternal(tool);
    return s_tools;
}

const std::vector<ToolInstance>& ReadOnlyTools()
{
    for (ToolInstance& tool : s_tools)
        EnsureToolIdInternal(tool);
    return s_tools;
}

std::size_t Count()
{
    return s_tools.size();
}

bool Empty()
{
    return s_tools.empty();
}

ToolInstance* At(int index)
{
    if (index < 0 || index >= static_cast<int>(s_tools.size()))
        return nullptr;
    EnsureToolIdInternal(s_tools[index]);
    return &s_tools[index];
}

const ToolInstance* AtReadOnly(int index)
{
    return At(index);
}

int AddTool(ToolInstance tool)
{
    EnsureToolIdInternal(tool);
    s_tools.push_back(std::move(tool));
    return static_cast<int>(s_tools.size()) - 1;
}

std::uint64_t EnsureToolId(ToolInstance& tool)
{
    return EnsureToolIdInternal(tool);
}

void EnsureToolIds()
{
    for (ToolInstance& tool : s_tools)
        EnsureToolIdInternal(tool);
}

int IndexOfToolId(std::uint64_t toolId)
{
    if (toolId == 0)
        return -1;
    EnsureToolIds();
    for (int index = 0; index < static_cast<int>(s_tools.size()); ++index)
    {
        if (s_tools[index].toolId == toolId)
            return index;
    }
    return -1;
}

ToolInstance* FindToolById(std::uint64_t toolId)
{
    const int index = IndexOfToolId(toolId);
    return index >= 0 ? &s_tools[index] : nullptr;
}

const ToolInstance* FindToolByIdReadOnly(std::uint64_t toolId)
{
    const int index = IndexOfToolId(toolId);
    return index >= 0 ? &s_tools[index] : nullptr;
}

int FirstMovableIndex()
{
    return (!s_tools.empty() && s_tools.front().type == 12) ? 1 : 0;
}

bool MoveTool(int from, int to)
{
    const int count = static_cast<int>(s_tools.size());
    const int firstMovable = FirstMovableIndex();
    if (from < firstMovable || from >= count || to < firstMovable || to >= count || from == to)
        return false;

    std::swap(s_tools[from], s_tools[to]);
    const auto remap = [from, to](int value)
    {
        if (value == from)
            return to;
        if (value == to)
            return from;
        return value;
    };
    s_activeToolIndex = remap(s_activeToolIndex);
    s_yoloLiveInstanceIndex = remap(s_yoloLiveInstanceIndex);
    for (ToolInstance& tool : s_tools)
    {
        tool.resultRoiSourceTool = remap(tool.resultRoiSourceTool);
        tool.fixture.sourceToolIndex = remap(tool.fixture.sourceToolIndex);
    }
    return true;
}

bool RemoveTool(int index)
{
    if (index < 0 || index >= static_cast<int>(s_tools.size()))
        return false;

    ToolAssetService::ForgetTool(s_tools[index].toolId);
    ToolROIService::ForgetTool(s_tools[index].toolId);
    delete s_tools[index].toolImpl;
    s_tools[index].toolImpl = nullptr;
    s_tools.erase(s_tools.begin() + index);
    for (ToolInstance& tool : s_tools)
    {
        if (tool.resultRoiSourceTool == index)
        {
            tool.resultRoiSourceTool = -1;
            tool.resultRoiSourceToolId = 0;
        }
        else if (tool.resultRoiSourceTool > index)
            --tool.resultRoiSourceTool;

        if (tool.fixture.sourceToolIndex == index)
        {
            tool.fixture.sourceToolIndex = -1;
            tool.fixture.sourceToolId = 0;
        }
        else if (tool.fixture.sourceToolIndex > index)
            --tool.fixture.sourceToolIndex;
    }

    if (s_activeToolIndex == index)
        s_activeToolIndex = -1;
    else if (s_activeToolIndex > index)
        --s_activeToolIndex;

    if (s_yoloLiveInstanceIndex == index)
    {
        s_yoloLiveDetect = false;
        s_yoloLiveInstanceIndex = -1;
    }
    else if (s_yoloLiveInstanceIndex > index)
        --s_yoloLiveInstanceIndex;
    return true;
}

int ActiveIndex()
{
    return s_activeToolIndex;
}

void SetActiveIndex(int index)
{
    s_activeToolIndex = index;
}

bool YoloLiveDetect()
{
    return s_yoloLiveDetect;
}

void SetYoloLiveDetect(bool enabled)
{
    s_yoloLiveDetect = enabled;
}

int YoloLiveInstanceIndex()
{
    return s_yoloLiveInstanceIndex;
}

void SetYoloLiveInstanceIndex(int index)
{
    s_yoloLiveInstanceIndex = index;
}

float YoloLastTimeMs()
{
    return s_yoloLastTimeMs;
}

void SetYoloLastTimeMs(float ms)
{
    s_yoloLastTimeMs = ms;
}

float YoloLiveFrameMs()
{
    return s_yoloLiveFrameMs;
}

void SetYoloLiveFrameMs(float ms)
{
    s_yoloLiveFrameMs = ms;
}

float McfLastTimeMs()
{
    return s_mcfLastTimeMs;
}

void SetMcfLastTimeMs(float ms)
{
    s_mcfLastTimeMs = ms;
}

int McfLastCount()
{
    return s_mcfLastCount;
}

void SetMcfLastCount(int count)
{
    s_mcfLastCount = count;
}

void ClearTools()
{
    ToolAssetService::ClearSessions();
    ToolROIService::ClearSessions();
    for (ToolInstance& tool : s_tools)
    {
        delete tool.toolImpl;
        tool.toolImpl = nullptr;
    }
    s_tools.clear();
    s_activeToolIndex = -1;
    s_yoloLiveDetect = false;
    s_yoloLiveInstanceIndex = -1;
    s_yoloLastTimeMs = 0.0f;
    s_yoloLiveFrameMs = 0.0f;
    s_mcfLastTimeMs = 0.0f;
    s_mcfLastCount = 0;
}

void MoveOriginalToolToFront()
{
    EnsureToolIds();
    if (s_tools.size() < 2)
        return;

    std::vector<int> order;
    order.reserve(s_tools.size());
    for (int i = 0; i < static_cast<int>(s_tools.size()); ++i)
        if (s_tools[i].type == 12)
            order.push_back(i);
    for (int i = 0; i < static_cast<int>(s_tools.size()); ++i)
        if (s_tools[i].type != 12)
            order.push_back(i);

    bool changed = false;
    for (int i = 0; i < static_cast<int>(order.size()); ++i)
        changed |= order[i] != i;
    if (!changed)
        return;

    std::vector<int> oldToNew(s_tools.size(), -1);
    std::vector<ToolInstance> reordered;
    reordered.reserve(s_tools.size());
    for (int newIndex = 0; newIndex < static_cast<int>(order.size()); ++newIndex)
    {
        oldToNew[order[newIndex]] = newIndex;
        reordered.push_back(std::move(s_tools[order[newIndex]]));
    }

    for (ToolInstance& tool : reordered)
    {
        if (tool.resultRoiSourceTool >= 0 &&
            tool.resultRoiSourceTool < static_cast<int>(oldToNew.size()))
        {
            tool.resultRoiSourceTool = oldToNew[tool.resultRoiSourceTool];
        }
        if (tool.fixture.sourceToolIndex >= 0 &&
            tool.fixture.sourceToolIndex < static_cast<int>(oldToNew.size()))
        {
            tool.fixture.sourceToolIndex = oldToNew[tool.fixture.sourceToolIndex];
        }
    }
    if (s_activeToolIndex >= 0 && s_activeToolIndex < static_cast<int>(oldToNew.size()))
        s_activeToolIndex = oldToNew[s_activeToolIndex];
    if (s_yoloLiveInstanceIndex >= 0 && s_yoloLiveInstanceIndex < static_cast<int>(oldToNew.size()))
        s_yoloLiveInstanceIndex = oldToNew[s_yoloLiveInstanceIndex];
    s_tools = std::move(reordered);
}

bool DuplicateTool(int index, int* duplicatedIndex)
{
    if (index < 0 || index >= static_cast<int>(s_tools.size()))
        return false;

    EnsureToolIds();
    return InsertToolClone(index + 1, s_tools[index], duplicatedIndex);
}

bool CopyToolToClipboard(int index)
{
    if (index < 0 || index >= static_cast<int>(s_tools.size()))
        return false;
    EnsureToolIds();
    s_toolClipboard = CloneToolForInsertion(s_tools[index]);
    return true;
}

bool HasToolClipboard()
{
    return s_toolClipboard.has_value();
}

bool PasteToolAfter(int index, int* pastedIndex)
{
    if (!s_toolClipboard || index < -1 || index >= static_cast<int>(s_tools.size()))
        return false;
    return InsertToolClone(index + 1, *s_toolClipboard, pastedIndex);
}

void SetAllEnabled(bool enabled)
{
    for (ToolInstance& tool : s_tools)
        tool.enabled = enabled;
}

void SetGroupEnabled(const std::string& groupName, bool enabled)
{
    for (ToolInstance& tool : s_tools)
    {
        if (tool.groupName == groupName)
            tool.enabled = enabled;
    }
}

void SetAllResultLabelsVisible(bool visible)
{
    for (ToolInstance& tool : s_tools)
        tool.showResultLabels = visible;
}

void SetGroupResultLabelsVisible(const std::string& groupName, bool visible)
{
    for (ToolInstance& tool : s_tools)
    {
        if (tool.groupName == groupName)
            tool.showResultLabels = visible;
    }
}

void SetAllStopOnFailure(bool stopOnFailure)
{
    for (ToolInstance& tool : s_tools)
        tool.judgement.stopOnFailure = stopOnFailure;
}

void SetGroupStopOnFailure(const std::string& groupName, bool stopOnFailure)
{
    for (ToolInstance& tool : s_tools)
    {
        if (tool.groupName == groupName)
            tool.judgement.stopOnFailure = stopOnFailure;
    }
}
}
