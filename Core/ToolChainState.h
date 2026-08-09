#pragma once

#include <vector>
#include <cstdint>
#include <string>

#include "ToolInstance.h"

struct TaskGroupDefinition
{
    std::uint64_t id = 0;
    std::string name;
    bool enabled = true;
    std::string imagePath;
    std::string imageFolderPath;
    int imageFolderIndex = -1;
    int imageFolderCount = 0;
    int cameraIndex = -1;
    // 兼容旧配方；cameraIndex >= 0 时保持为 true。
    bool cameraPreferred = false;
};

// =====================================================
// ToolChainState — 工具链全局状态管理
// 管理工具实例列表、当前激活工具、YOLO 实时检测状态
// =====================================================
namespace ToolChainState
{
    // ---- 工具列表 ----
    std::vector<ToolInstance>& Tools();              // 可写引用
    const std::vector<ToolInstance>& ReadOnlyTools(); // 只读引用
    std::size_t Count();
    bool Empty();
    ToolInstance* At(int index);
    const ToolInstance* AtReadOnly(int index);
    int AddTool(ToolInstance tool);
    std::uint64_t EnsureToolId(ToolInstance& tool);
    void EnsureToolIds();
    int IndexOfToolId(std::uint64_t toolId);
    ToolInstance* FindToolById(std::uint64_t toolId);
    const ToolInstance* FindToolByIdReadOnly(std::uint64_t toolId);

    // Core-owned tool-chain edits keep index remapping and dependency cleanup consistent.
    int FirstMovableIndex();
    bool MoveTool(int from, int to);
    bool MoveToolWithinTaskGroup(int toolIndex, int direction);
    bool RemoveTool(int index);

    // ---- 当前激活工具 ----
    int ActiveIndex();               // 只读
    void SetActiveIndex(int index);  // 设置激活索引

    // ---- YOLO 实时检测状态 ----
    bool YoloLiveDetect();                  // 只读
    void SetYoloLiveDetect(bool enabled);   // 设置开关
    int YoloLiveInstanceIndex();
    void SetYoloLiveInstanceIndex(int index);
    float YoloLastTimeMs();
    void SetYoloLastTimeMs(float ms);
    float YoloLiveFrameMs();
    void SetYoloLiveFrameMs(float ms);
    float McfLastTimeMs();
    void SetMcfLastTimeMs(float ms);
    int McfLastCount();
    void SetMcfLastCount(int count);

    // ---- 工具链操作 ----
    void ClearTools();               // 释放工具实现并清空工具链及实时状态
    void MoveOriginalToolToFront();  // 将"原图"工具移到工具链最前面
    bool DuplicateTool(int index, int* duplicatedIndex = nullptr);
    bool CopyToolToClipboard(int index);
    bool HasToolClipboard();
    bool PasteToolAfter(int index, int* pastedIndex = nullptr);
    const std::vector<TaskGroupDefinition>& ReadOnlyTaskGroups();
    int CreateTaskGroup(const std::string& preferredName = {});
    void ReplaceTaskGroups(std::vector<TaskGroupDefinition> groups);
    int TaskGroupIndexByName(const std::string& name);
    bool RenameTaskGroup(int index, const std::string& name);
    bool MoveTaskGroup(int from, int to);
    bool RemoveTaskGroup(int index);
    bool SetTaskGroupEnabled(int index, bool enabled);
    bool SetTaskGroupCameraPreferred(int index, bool preferred);
    bool SetTaskGroupCameraIndex(int index, int cameraIndex);
    bool SetTaskGroupImagePath(int index, const std::string& imagePath);
    bool SetTaskGroupImageFolder(int index, const std::string& folderPath,
        const std::string& previewImagePath, int imageCount);
    bool SetTaskGroupFolderImagePosition(int index, const std::string& imagePath,
        int imageIndex, int imageCount);
    bool AssignToolToTaskGroup(int toolIndex, int groupIndex);
    bool AssignToolToTaskGroupByName(int toolIndex, const std::string& groupName);
    std::string NextTaskGroupName();
    constexpr std::size_t MaximumTaskGroups() { return 16; }
    void SetAllEnabled(bool enabled);
    void SetGroupEnabled(const std::string& groupName, bool enabled);
    void SetAllResultLabelsVisible(bool visible);
    void SetGroupResultLabelsVisible(const std::string& groupName, bool visible);
    void SetAllStopOnFailure(bool stopOnFailure);
    void SetGroupStopOnFailure(const std::string& groupName, bool stopOnFailure);
}
