// =============================================================================
// TaskGroupWindow.cpp — task list, assignment window and tool-chain group filter
// =============================================================================

#include "TaskGroupWindow.h"

#include "DockSpaceHost.h"
#include "ToolsWindow.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/HardwareSettingsService.h"
#include "../Core/ImageImportService.h"
#include "../Core/ImageLoadController.h"
#include "../Core/ImageState.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/ResultOverlayState.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolController.h"
#include "../Core/ToolTypes.h"
#include "../Log/LogSystem.h"
#include "../include/imgui/imgui.h"
#include "../include/imgui/imgui_internal.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace UI::TaskGroupWindow
{
    std::uint64_t s_selectedTaskGroupId = 0;
    std::uint64_t s_lastPreviewedBatchSerial = 0;
    std::uint64_t s_renameTaskGroupId = 0;
    char s_taskGroupNameBuffer[96]{};
    int s_pendingTaskGroupDelete = -1;
    std::string s_taskGroupError;

    bool s_showTaskGroupListWindow = true;
    bool s_showTaskGroupToolsWindow = false;
    bool s_requestTaskGroupListDock = true;
    bool s_requestTaskGroupToolsDock = false;

    std::string s_taskGroupToolFilter;
    bool s_taskGroupToolFilterUngrouped = false;
    bool s_requestToolsWindowFocus = false;

        // ---- 在工具视图中选中一个任务组（加载其关联图片、重置分步执行）----
        void SelectTaskGroupInTools(std::uint64_t groupId, const std::string& groupName)
        {
            const bool selectionChanged = s_selectedTaskGroupId != groupId;
            s_selectedTaskGroupId = groupId;
            // groupId==0 表示"未分组"，清空筛选条件
            s_taskGroupToolFilter = groupId == 0 ? std::string{} : groupName;
            s_taskGroupToolFilterUngrouped = groupId == 0;
            ResultOverlayState::SetTaskGroupFilter(s_taskGroupToolFilter);
            if (selectionChanged)
                ToolController::RequestStepReset();      // 切换任务组时重置分步执行
            if (selectionChanged && groupId != 0)
            {
                // 加载任务组关联的图片
                const int groupIndex = ToolChainState::TaskGroupIndexByName(groupName);
                if (groupIndex >= 0)
                {
                    const TaskGroupDefinition& group =
                        ToolChainState::ReadOnlyTaskGroups()[groupIndex];
                    const int cameraIndex = group.cameraIndex >= 0
                        ? group.cameraIndex : (group.cameraPreferred ? 0 : -1);
                    bool cameraSelected = false;
                    if (cameraIndex >= 0)
                    {
                        // A late file-loader callback must not overwrite the
                        // camera frame after the user changes task selection.
                        ImageLoadController::CancelPending();
                        const DeviceOperationResult result =
                            HardwareRuntimeService::SelectCameraSlotForPreview(cameraIndex);
                        cameraSelected = result.success;
                        LogSystem::Add(result.success ? LOG_INFO : LOG_WARN,
                            "任务预览切换 [%s] -> 相机%02d: %s",
                            groupName.c_str(), cameraIndex + 1, result.message.c_str());
                    }
                    if (!cameraSelected && !group.imagePath.empty())
                    {
                        const ImageImportResult result =
                            ImageImportService::ImportSingleImage(group.imagePath);
                        if (!result.success)
                        {
                            LogSystem::Add(LOG_ERROR,
                                "任务图片加载失败 [%s]: %s",
                                groupName.c_str(), result.message.c_str());
                        }
                    }
                }
            }
            s_requestToolsWindowFocus = true;           // 聚焦工具窗口
            g_ShowTools = true;
            ToolChainState::SetActiveIndex(-1);          // 取消工具展开
        }

        // ---- 去除任务组名称首尾空白字符 ----
        std::string TrimTaskGroupName(std::string value)
        {
            const std::size_t first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return {};                              // 全是空白，返回空字符串
            const std::size_t last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        // ---- 根据任务组 ID 查找其在列表中的索引 ----
        int TaskGroupIndexById(std::uint64_t id)
        {
            const auto& groups = ToolChainState::ReadOnlyTaskGroups();
            for (int index = 0; index < static_cast<int>(groups.size()); ++index)
            {
                if (groups[index].id == id)
                    return index;
            }
            return -1;                                  // 未找到
        }

        void RefreshSelectedTaskPreviewAfterRun()
        {
            const std::uint64_t completedSerial =
                ToolController::GetCompletedBatchSerial();
            if (completedSerial == s_lastPreviewedBatchSerial)
                return;
            s_lastPreviewedBatchSerial = completedSerial;

            const int groupIndex = TaskGroupIndexById(s_selectedTaskGroupId);
            if (groupIndex < 0)
            {
                if (!ToolChainState::ReadOnlyTaskGroups().empty())
                {
                    ImageState::PendingUploadRef().release();
                    ImageState::NeedUploadRef() = false;
                }
                return;
            }
            const std::string& groupName =
                ToolChainState::ReadOnlyTaskGroups()[groupIndex].name;
            const cv::Mat resultImage =
                ToolController::GetTaskResultImage(groupName);
            if (resultImage.empty())
            {
                ImageState::PendingUploadRef().release();
                ImageState::NeedUploadRef() = false;
                return;
            }

            // 只替换中央显示图，不改原图和输入版本。下一轮仍由任务的
            // imagePath/cameraIndex 准备输入，避免预览刷新污染任务源。
            ImageState::SetDebugImage(resultImage);
        }

        // ---- 获取指定窗口的停靠 ID（用于窗口停靠布局）----
        ImGuiID WindowDockId(const char* primaryWindow, const char* fallbackWindow = nullptr)
        {
            if (ImGuiWindow* window = ImGui::FindWindowByName(primaryWindow))
            {
                if (window->DockId != 0)
                    return window->DockId;
            }
            if (fallbackWindow)
            {
                if (ImGuiWindow* window = ImGui::FindWindowByName(fallbackWindow))
                    return window->DockId;
            }
            return 0;
        }

        std::string ToolManagerDisplayName(const ToolInstance& tool, int index)
        {
            const char* typeName = "工具";
            for (const ToolMeta& meta : g_ToolRegistry)
            {
                if (meta.type == tool.type)
                {
                    typeName = meta.name;
                    break;
                }
            }
            const std::string title = tool.label.empty() ? std::string(typeName) : tool.label;
            return std::to_string(index + 1) + ". " + title;
        }

        void CommitTaskGroupChange()
        {
            ToolController::OnToolChainChanged();
            MarkCurrentRecipeDirty();
        }

        void DrawTaskGroupDropTarget(int groupIndex)
        {
            if (!ImGui::BeginDragDropTarget())
                return;
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TASK_GROUP_TOOL_ID"))
            {
                if (payload->DataSize == sizeof(std::uint64_t))
                {
                    const std::uint64_t toolId = *static_cast<const std::uint64_t*>(payload->Data);
                    const int toolIndex = ToolChainState::IndexOfToolId(toolId);
                    if (ToolChainState::AssignToolToTaskGroup(toolIndex, groupIndex))
                        CommitTaskGroupChange();
                }
            }
            ImGui::EndDragDropTarget();
        }

        void DrawTaskGroupToolRow(int toolIndex, int selectedGroupIndex)
        {
            ToolInstance* tool = ToolChainState::At(toolIndex);
            if (!tool)
                return;

            ImGui::PushID(static_cast<int>(tool->toolId));
            ImGui::TableNextRow(0, ImGui::GetFrameHeight());
            ImGui::TableNextColumn();
            const std::string displayName = ToolManagerDisplayName(*tool, toolIndex);
            ImGui::Selectable(displayName.c_str(), false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const std::uint64_t toolId = tool->toolId;
                ImGui::SetDragDropPayload("TASK_GROUP_TOOL_ID", &toolId, sizeof(toolId));
                ImGui::Text("移动工具：%s", displayName.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", tool->groupName.empty() ? "未分组" : tool->groupName.c_str());
            ImGui::TableNextColumn();
            const bool alreadyAssigned = selectedGroupIndex >= 0
                ? ToolChainState::TaskGroupIndexByName(tool->groupName) == selectedGroupIndex
                : tool->groupName.empty();
            ImGui::BeginDisabled(alreadyAssigned);
            const char* actionText = selectedGroupIndex >= 0 ? "移入" : "移出";
            if (ImGui::SmallButton(actionText) &&
                ToolChainState::AssignToolToTaskGroup(toolIndex, selectedGroupIndex))
            {
                CommitTaskGroupChange();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        int NormalizeSelectedTaskGroupIndex()
        {
            int selectedGroupIndex = TaskGroupIndexById(s_selectedTaskGroupId);
            if (s_selectedTaskGroupId != 0 && selectedGroupIndex < 0)
            {
                s_selectedTaskGroupId = 0;
                s_renameTaskGroupId = 0;
                s_taskGroupToolFilter.clear();
                s_taskGroupToolFilterUngrouped = true;
                selectedGroupIndex = -1;
            }
            return selectedGroupIndex;
        }

        void DrawTaskGroupDeletePopup()
        {
            if (s_pendingTaskGroupDelete >= 0 && !ImGui::IsPopupOpen("确认删除任务"))
                ImGui::OpenPopup("确认删除任务");
            if (!ImGui::BeginPopupModal("确认删除任务", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
                return;

            int toolCount = 0;
            const auto& taskGroups = ToolChainState::ReadOnlyTaskGroups();
            if (s_pendingTaskGroupDelete < static_cast<int>(taskGroups.size()))
            {
                const std::string& groupName = taskGroups[s_pendingTaskGroupDelete].name;
                toolCount = static_cast<int>(std::count_if(
                    ToolChainState::ReadOnlyTools().begin(),
                    ToolChainState::ReadOnlyTools().end(),
                    [&groupName](const ToolInstance& tool)
                    {
                        return tool.groupName == groupName;
                    }));
            }
            ImGui::Text("将同时删除该任务中的 %d 个工具。", toolCount);
            ImGui::TextColored(ImVec4(0.95f, 0.43f, 0.30f, 1.0f),
                "删除后无法撤销。");
            ImGui::Spacing();
            if (ImGui::Button("删除任务和工具", ImVec2(150.0f, 0.0f)))
            {
                if (ToolChainState::RemoveTaskGroup(s_pendingTaskGroupDelete))
                {
                    SelectTaskGroupInTools(0, {});
                    s_renameTaskGroupId = 0;
                    CommitTaskGroupChange();
                }
                s_pendingTaskGroupDelete = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(110.0f, 0.0f)))
            {
                s_pendingTaskGroupDelete = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- 绘制任务列表窗口（左侧面板）----
        void DrawTaskGroupListWindow()
        {
            if (!s_showTaskGroupListWindow)
                return;

            // 设置窗口初始位置和大小（居中偏左）
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float defaultHeight = (std::clamp)(viewport->WorkSize.y * 0.76f, 500.0f, 820.0f);
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 18.0f,
                viewport->WorkPos.y + (viewport->WorkSize.y - defaultHeight) * 0.5f),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360.0f, defaultHeight), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 420.0f),
                ImVec2(600.0f, FLT_MAX));
            // 请求停靠到侧边栏
            if (s_requestTaskGroupListDock)
            {
                const ImGuiID dockId = WindowDockId("侧边栏");
                if (dockId != 0)
                {
                    ImGui::SetNextWindowDockID(dockId, ImGuiCond_Always);
                    s_requestTaskGroupListDock = false;
                }
            }
            if (!ImGui::Begin("任务列表###task_group_list_window",
                &s_showTaskGroupListWindow, ImGuiWindowFlags_NoCollapse))
            {
                ImGui::End();
                return;
            }

            // "新建任务"按钮（达到上限时禁用）
            const bool canCreate = ToolChainState::ReadOnlyTaskGroups().size() <
                ToolChainState::MaximumTaskGroups();
            ImGui::BeginDisabled(!canCreate);
            if (ImGui::Button("+ 新建任务", ImVec2(112.0f, 0.0f)))
            {
                const int createdIndex = ToolChainState::CreateTaskGroup();
                if (createdIndex >= 0)
                {
                    const TaskGroupDefinition& createdGroup =
                        ToolChainState::ReadOnlyTaskGroups()[createdIndex];
                    SelectTaskGroupInTools(createdGroup.id, createdGroup.name);
                    s_renameTaskGroupId = 0;
                    s_taskGroupError.clear();
                    CommitTaskGroupChange();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("打开工具分配"))
            {
                s_showTaskGroupToolsWindow = true;
                s_requestTaskGroupToolsDock = true;
            }

            // 检查是否有未分组工具（用于显示"未分组"选项）
            const bool hasUngroupedTools = std::any_of(
                ToolChainState::ReadOnlyTools().begin(),
                ToolChainState::ReadOnlyTools().end(),
                [](const ToolInstance& tool)
                {
                    return tool.groupName.empty();
                });
            // 无未分组工具且未选中任何任务组时，自动选中第一个任务组
            const auto& availableGroups = ToolChainState::ReadOnlyTaskGroups();
            if (!hasUngroupedTools && s_selectedTaskGroupId == 0 &&
                !availableGroups.empty())
            {
                SelectTaskGroupInTools(
                    availableGroups.front().id, availableGroups.front().name);
            }

            int selectedGroupIndex = NormalizeSelectedTaskGroupIndex();
            ImGui::SeparatorText("任务列表");
            // 根据是否选中任务组动态调整列表高度
            const float settingsHeight = selectedGroupIndex >= 0 ? 342.0f : 58.0f;
            const float listHeight = (std::max)(160.0f,
                ImGui::GetContentRegionAvail().y - settingsHeight);
            // 任务列表可滚动区域
            if (ImGui::BeginChild("##task_group_list", ImVec2(0.0f, listHeight),
                ImGuiChildFlags_Borders))
            {
                // "未分组" 选项（有未分组工具时显示）
                if (hasUngroupedTools)
                {
                    const bool ungroupedSelected = s_selectedTaskGroupId == 0;
                    if (ImGui::Selectable("未分组", ungroupedSelected, 0,
                        ImVec2(0.0f, 34.0f)))
                    {
                        SelectTaskGroupInTools(0, {});
                    }
                    DrawTaskGroupDropTarget(-1);        // 拖放目标：未分组区域
                }

                // 渲染每个任务组条目
                const auto& groups = ToolChainState::ReadOnlyTaskGroups();
                for (int index = 0; index < static_cast<int>(groups.size()); ++index)
                {
                    const TaskGroupDefinition& group = groups[index];
                    ImGui::PushID(static_cast<int>(group.id));
                    // 统计该任务组下的工具数量
                    int toolCount = 0;
                    for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
                    {
                        if (tool.groupName == group.name)
                            ++toolCount;
                    }

                    ImVec4 taskStatusColor = ImVec4(0.92f, 0.66f, 0.18f, 1.0f);
                    const char* taskStatusText = "未配置";
                    if (!group.enabled)
                    {
                        taskStatusColor = ImVec4(0.48f, 0.53f, 0.58f, 1.0f);
                        taskStatusText = "未启用";
                    }
                    else if (toolCount > 0)
                    {
                        bool hasError = false;
                        bool allPassed = true;
                        bool hasResult = false;
                        for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
                        {
                            if (tool.groupName != group.name)
                                continue;
                            if (!tool.hasLastResult)
                            {
                                allPassed = false;
                                continue;
                            }
                            hasResult = true;
                            if (tool.lastResult.status == ToolResultStatus::Error ||
                                tool.lastResult.status == ToolResultStatus::Fail)
                                hasError = true;
                            if (tool.lastResult.status != ToolResultStatus::Pass)
                                allPassed = false;
                        }
                        if (hasError)
                        {
                            taskStatusColor = ImVec4(0.82f, 0.22f, 0.18f, 1.0f);
                            taskStatusText = "错误";
                        }
                        else if (hasResult && allPassed)
                        {
                            taskStatusColor = ImVec4(0.16f, 0.66f, 0.38f, 1.0f);
                            taskStatusText = "正常";
                        }
                    }
                    // 构建来源标签：相机/文件夹/单张图片
                    std::string sourceTags;
                    const int boundCameraIndex = group.cameraIndex >= 0
                        ? group.cameraIndex : (group.cameraPreferred ? 0 : -1);
                    if (boundCameraIndex >= 0)
                    {
                        char cameraTag[24];
                        std::snprintf(cameraTag, sizeof(cameraTag), "  [相机%02d]",
                            boundCameraIndex + 1);
                        sourceTags += cameraTag;
                    }
                    if (!group.imageFolderPath.empty())
                        sourceTags += "  [文件夹]";
                    else if (!group.imagePath.empty())
                        sourceTags += "  [图]";
                    char rowLabel[192]{};
                    std::snprintf(rowLabel, sizeof(rowLabel), "%s  (%d)%s",
                        group.name.c_str(), toolCount, sourceTags.c_str());
					ImGui::AlignTextToFramePadding();
					ImGui::TextColored(taskStatusColor, "●");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("状态：%s", taskStatusText);
					ImGui::SameLine(0.0f, 5.0f);
                    if (ImGui::Selectable(rowLabel, s_selectedTaskGroupId == group.id,
                        0, ImVec2(0.0f, 34.0f)))
                    {
                        SelectTaskGroupInTools(group.id, group.name);
                    }
                    DrawTaskGroupDropTarget(index);     // 拖放目标：具体任务组
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            // ===== 任务设置面板 =====
            selectedGroupIndex = NormalizeSelectedTaskGroupIndex();
            if (selectedGroupIndex >= 0)
            {
                const TaskGroupDefinition& selectedGroup =
                    ToolChainState::ReadOnlyTaskGroups()[selectedGroupIndex];
                // 同步重命名缓冲区
                if (s_renameTaskGroupId != selectedGroup.id)
                {
                    std::snprintf(s_taskGroupNameBuffer, sizeof(s_taskGroupNameBuffer),
                        "%s", selectedGroup.name.c_str());
                    s_renameTaskGroupId = selectedGroup.id;
                    s_taskGroupError.clear();
                }

                ImGui::SeparatorText("任务设置");
                // 任务名称输入框（回车或失去焦点时提交）
                ImGui::SetNextItemWidth(-1.0f);
                const bool renameSubmitted = ImGui::InputText("##task_group_name",
                    s_taskGroupNameBuffer, IM_ARRAYSIZE(s_taskGroupNameBuffer),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                if (renameSubmitted || ImGui::IsItemDeactivatedAfterEdit())
                {
                    const std::string newName = TrimTaskGroupName(s_taskGroupNameBuffer);
                    if (ToolChainState::RenameTaskGroup(selectedGroupIndex, newName))
                    {
                        s_taskGroupToolFilter = newName;
                        s_taskGroupToolFilterUngrouped = false;
                        s_taskGroupError.clear();
                        CommitTaskGroupChange();
                    }
                    else
                    {
                        s_taskGroupError = "名称不能为空，也不能与其他任务重复";
                    }
                }
                // 显示重命名错误
                if (!s_taskGroupError.empty())
                {
                    ImGui::TextColored(ImVec4(0.92f, 0.34f, 0.20f, 1.0f),
                        "%s", s_taskGroupError.c_str());
                }

                // 启用/禁用任务开关
                bool enabled = selectedGroup.enabled;
                if (ImGui::Checkbox("启用该任务", &enabled) &&
                    ToolChainState::SetTaskGroupEnabled(selectedGroupIndex, enabled))
                {
                    CommitTaskGroupChange();
                }

                // 相机绑定下拉框
                int cameraIndex = selectedGroup.cameraIndex >= 0
                    ? selectedGroup.cameraIndex
                    : (selectedGroup.cameraPreferred ? 0 : -1);
                char cameraBindingLabel[32];
                if (cameraIndex >= 0)
                    std::snprintf(cameraBindingLabel, sizeof(cameraBindingLabel),
                        "相机 %02d", cameraIndex + 1);
                else
                    std::snprintf(cameraBindingLabel, sizeof(cameraBindingLabel), "不绑定相机");
                if (ImGui::BeginTable("##task_camera_binding_row", 2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    const float cameraLabelWidth = ImGui::CalcTextSize("绑定相机").x +
                        ImGui::GetStyle().ItemInnerSpacing.x;
                    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed,
                        cameraLabelWidth);
                    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("绑定相机");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##task_camera_binding", cameraBindingLabel))
                    {
                        if (ImGui::Selectable("不绑定相机", cameraIndex < 0) &&
                            ToolChainState::SetTaskGroupCameraIndex(selectedGroupIndex, -1))
                        {
                            CommitTaskGroupChange();
                        }
                        for (int index = 0; index < static_cast<int>(kHardwareCameraCount); ++index)
                        {
                            const HardwareRuntimeSnapshot hardware =
                                HardwareRuntimeService::Snapshot();
                            const auto found = std::find_if(
                                hardware.cameraSlots.begin(),
                                hardware.cameraSlots.end(),
                                [index](const HardwareCameraSlotSnapshot& item)
                                {
                                    return item.slotIndex == index;
                                });
                            const bool online = found != hardware.cameraSlots.end() &&
                                found->state == DeviceConnectionState::Connected;
                            char label[32];
                            std::snprintf(label, sizeof(label), "相机 %02d%s",
                                index + 1, online ? " · 在线" : "");
                            if (ImGui::Selectable(label, cameraIndex == index) &&
                                ToolChainState::SetTaskGroupCameraIndex(selectedGroupIndex, index))
                            {
                                CommitTaskGroupChange();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::EndTable();
                }
                // 相机绑定状态提示
                if (cameraIndex >= 0)
                {
                    const HardwareRuntimeSnapshot hardware =
                        HardwareRuntimeService::Snapshot();
                    const auto found = std::find_if(
                        hardware.cameraSlots.begin(), hardware.cameraSlots.end(),
                        [cameraIndex](const HardwareCameraSlotSnapshot& item)
                        {
                            return item.slotIndex == cameraIndex;
                        });
                    const bool cameraConnected = found != hardware.cameraSlots.end() &&
                        found->state == DeviceConnectionState::Connected;
                    ImGui::PushStyleColor(ImGuiCol_Text, cameraConnected
                        ? ImVec4(0.24f, 0.86f, 0.48f, 1.0f)    // 绿色：已连接
                        : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));  // 灰色
                    ImGui::TextWrapped("%s", cameraConnected
                        ? "绑定相机已连接：相机 → 任务图片 → 公共图片"
                        : "执行时自动连接绑定相机；失败则使用任务图片或公共图片");
                    ImGui::PopStyleColor();
                }
                else
                {
                    // 无相机绑定：显示输入优先级
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("输入优先级：任务图片 → 公共图片");
                    ImGui::PopStyleColor();
                }
                // 显示任务关联的图片信息
                const std::size_t imageSlash = selectedGroup.imagePath.find_last_of("\\/");
                const char* imageName = selectedGroup.imagePath.empty()
                    ? "未设置（使用当前公共图片）"
                    : selectedGroup.imagePath.c_str() +
                        (imageSlash == std::string::npos ? 0 : imageSlash + 1);
                if (!selectedGroup.imageFolderPath.empty())
                {
                    // 文件夹模式：显示文件夹名和当前图片索引
                    const std::size_t folderSlash =
                        selectedGroup.imageFolderPath.find_last_of("\\/");
                    const char* folderName = selectedGroup.imageFolderPath.c_str() +
                        (folderSlash == std::string::npos ? 0 : folderSlash + 1);
                    ImGui::TextDisabled("图片文件夹: %s", folderName);
                    ImGui::SetItemTooltip("%s", selectedGroup.imageFolderPath.c_str());
                    const int displayIndex = selectedGroup.imageFolderCount > 0
                        ? (std::max)(0, selectedGroup.imageFolderIndex) + 1 : 0;
                    ImGui::TextDisabled("当前图片: %s  (%d/%d)", imageName,
                        displayIndex, selectedGroup.imageFolderCount);
                    if (!selectedGroup.imagePath.empty())
                        ImGui::SetItemTooltip("%s", selectedGroup.imagePath.c_str());
                }
                else
                {
                    // 单图片模式
                    ImGui::TextDisabled("任务图片: %s", imageName);
                    if (!selectedGroup.imagePath.empty())
                        ImGui::SetItemTooltip("%s", selectedGroup.imagePath.c_str());
                }
                // ---- 图片操作按钮行 ----
                if (ImGui::Button("选择单张图片"))
                {
                    const std::string imagePath = OpenFileDialog();
                    if (!imagePath.empty())
                    {
                        const ImageImportResult result =
                            ImageImportService::ImportSingleImage(imagePath);
                        if (result.success && ToolChainState::SetTaskGroupImagePath(
                            selectedGroupIndex, imagePath))
                        {
                            CommitTaskGroupChange();
                        }
                        else if (!result.success)
                        {
                            LogSystem::Add(LOG_ERROR, "%s", result.message.c_str());
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("选择图片文件夹"))
                {
                    const std::string folderPath = OpenFolderDialog();
                    if (!folderPath.empty())
                    {
                        const std::vector<std::string> images =
                            ScanImageFiles(folderPath, true);
                        if (images.empty())
                        {
                            LogSystem::Add(LOG_WARN,
                                "所选文件夹中没有可用图片: %s", folderPath.c_str());
                        }
                        else
                        {
                            const ImageImportResult result =
                                ImageImportService::ImportSingleImage(images.front());
                            if (result.success && ToolChainState::SetTaskGroupImageFolder(
                                selectedGroupIndex, folderPath, images.front(),
                                static_cast<int>(images.size())))
                            {
                                CommitTaskGroupChange();
                            }
                            else if (!result.success)
                            {
                                LogSystem::Add(LOG_ERROR, "%s", result.message.c_str());
                            }
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(selectedGroup.imagePath.empty() &&
                    selectedGroup.imageFolderPath.empty());
                if (ImGui::Button("清除图片") &&
                    ToolChainState::SetTaskGroupImagePath(selectedGroupIndex, {}))
                {
                    CommitTaskGroupChange();
                }
                ImGui::EndDisabled();
                // ---- 任务排序/删除按钮 ----
                ImGui::BeginDisabled(selectedGroupIndex <= 0);   // 第一个任务不可上移
                if (ImGui::Button("上移"))
                {
                    if (ToolChainState::MoveTaskGroup(
                        selectedGroupIndex, selectedGroupIndex - 1))
                    {
                        CommitTaskGroupChange();
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(selectedGroupIndex + 1 >=
                    static_cast<int>(ToolChainState::ReadOnlyTaskGroups().size()));
                if (ImGui::Button("下移"))                       // 最后一个任务不可下移
                {
                    if (ToolChainState::MoveTaskGroup(
                        selectedGroupIndex, selectedGroupIndex + 1))
                    {
                        CommitTaskGroupChange();
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("删除任务"))
                    s_pendingTaskGroupDelete = selectedGroupIndex;  // 触发确认弹窗
            }
            else
            {
                // 未选中任务组时的提示
                ImGui::SeparatorText("未分组");
                ImGui::TextDisabled("把右侧工具拖到任意任务即可分组。");
            }

            DrawTaskGroupDeletePopup();
            ImGui::End();
        }

        void DrawTaskGroupToolsWindow()
        {
            if (!s_showTaskGroupToolsWindow)
                return;

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float defaultWidth = (std::clamp)(viewport->WorkSize.x * 0.44f,
                480.0f, 720.0f);
            const float defaultHeight = (std::clamp)(viewport->WorkSize.y * 0.76f,
                500.0f, 820.0f);
            ImGui::SetNextWindowPos(ImVec2(
                viewport->WorkPos.x + viewport->WorkSize.x - defaultWidth - 18.0f,
                viewport->WorkPos.y + (viewport->WorkSize.y - defaultHeight) * 0.5f),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(defaultWidth, defaultHeight),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(ImVec2(430.0f, 360.0f),
                ImVec2(FLT_MAX, FLT_MAX));
            if (s_requestTaskGroupToolsDock)
            {
                const ImGuiID dockId = WindowDockId("功能窗口");
                if (dockId != 0)
                {
                    ImGui::SetNextWindowDockID(dockId, ImGuiCond_Always);
                    s_requestTaskGroupToolsDock = false;
                }
            }
            if (!ImGui::Begin("任务工具分配###task_group_tools_window",
                &s_showTaskGroupToolsWindow, ImGuiWindowFlags_NoCollapse))
            {
                ImGui::End();
                return;
            }

            if (ImGui::Button("打开任务列表"))
            {
                s_showTaskGroupListWindow = true;
                s_requestTaskGroupListDock = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("拖动工具到左侧任务，或使用“移入/移出”按钮");

            const int selectedGroupIndex = NormalizeSelectedTaskGroupIndex();
            const std::string selectedName = selectedGroupIndex >= 0
                ? ToolChainState::ReadOnlyTaskGroups()[selectedGroupIndex].name
                : std::string{};
            ImGui::SeparatorText(selectedGroupIndex >= 0
                ? selectedName.c_str() : "未分组工具");

            if (ImGui::BeginTable("##task_group_tools", 3,
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("工具", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                ImGui::TableSetupColumn("当前任务", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 58.0f);
                ImGui::TableHeadersRow();
                for (int pass = 0; pass < 2; ++pass)
                {
                    for (int toolIndex = 0;
                        toolIndex < static_cast<int>(ToolChainState::Count()); ++toolIndex)
                    {
                        const ToolInstance* tool = ToolChainState::AtReadOnly(toolIndex);
                        if (!tool)
                            continue;
                        const bool inSelected = selectedGroupIndex >= 0
                            ? tool->groupName == selectedName : tool->groupName.empty();
                        if ((pass == 0) != inSelected)
                            continue;
                        DrawTaskGroupToolRow(toolIndex, selectedGroupIndex);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        void NormalizeWorkflowToolFilter()
        {
            if (s_taskGroupToolFilter.empty())
                return;

            const auto& groups = ToolChainState::ReadOnlyTaskGroups();
            const auto found = std::find_if(groups.begin(), groups.end(),
                [](const TaskGroupDefinition& group)
                {
                    return group.name == s_taskGroupToolFilter;
                });
            if (found == groups.end())
            {
                s_taskGroupToolFilter.clear();
                s_taskGroupToolFilterUngrouped = false;
            }
        }

        std::vector<int> CollectVisibleWorkflowToolIndices()
        {
            NormalizeWorkflowToolFilter();
            std::vector<int> visibleToolIndices;
            visibleToolIndices.reserve(ToolChainState::Count());
            for (int index = 0; index < static_cast<int>(ToolChainState::Count()); ++index)
            {
                const ToolInstance* tool = ToolChainState::AtReadOnly(index);
                if (!tool)
                    continue;
                if ((s_taskGroupToolFilterUngrouped && !tool->groupName.empty()) ||
                    (!s_taskGroupToolFilter.empty() &&
                        tool->groupName != s_taskGroupToolFilter))
                {
                    continue;
                }
                visibleToolIndices.push_back(index);
            }
            return visibleToolIndices;
        }

        std::string WorkflowChainTitle()
        {
            if (!s_taskGroupToolFilter.empty())
                return s_taskGroupToolFilter;
            return s_taskGroupToolFilterUngrouped ? "未分组" : "全部工具";
        }

        void DrawTaskGroupManagerWindows()
        {
            DrawTaskGroupListWindow();
            DrawTaskGroupToolsWindow();
        }

    bool ConsumeToolsWindowFocusRequest()
    {
        return std::exchange(s_requestToolsWindowFocus, false);
    }

    void OpenManagerWindows()
    {
        s_showTaskGroupListWindow = true;
        s_showTaskGroupToolsWindow = true;
        s_requestTaskGroupListDock = true;
        s_requestTaskGroupToolsDock = true;
    }

    void SelectAllGroups()
    {
        s_taskGroupToolFilter.clear();
        s_taskGroupToolFilterUngrouped = false;
        s_selectedTaskGroupId = 0;
        ResultOverlayState::ClearTaskGroupFilter();
        ToolController::RequestStepReset();
        ToolChainState::SetActiveIndex(-1);
    }

    void SelectUngroupedTools()
    {
        s_taskGroupToolFilter.clear();
        s_taskGroupToolFilterUngrouped = true;
        s_selectedTaskGroupId = 0;
        ResultOverlayState::SetTaskGroupFilter({});
        ToolController::RequestStepReset();
        ToolChainState::SetActiveIndex(-1);
    }

    bool IsUngroupedFilter()
    {
        return s_taskGroupToolFilterUngrouped;
    }

    bool IsAllGroupsFilter()
    {
        return s_taskGroupToolFilter.empty() && !s_taskGroupToolFilterUngrouped;
    }

    const std::string& CurrentTaskGroupName()
    {
        return s_taskGroupToolFilter;
    }

    void AssignNewToolToCurrentGroup(ToolInstance& tool)
    {
        if (!s_taskGroupToolFilterUngrouped &&
            !s_taskGroupToolFilter.empty() &&
            ToolChainState::TaskGroupIndexByName(s_taskGroupToolFilter) >= 0)
        {
            tool.groupName = s_taskGroupToolFilter;
        }
    }

    bool BindSelectedTaskImagePath(const std::string& imagePath)
    {
        if (imagePath.empty() || s_selectedTaskGroupId == 0 ||
            s_taskGroupToolFilterUngrouped || s_taskGroupToolFilter.empty())
        {
            return false;
        }
        const int groupIndex = TaskGroupIndexById(s_selectedTaskGroupId);
        if (groupIndex < 0 ||
            ToolChainState::ReadOnlyTaskGroups()[groupIndex].name !=
                s_taskGroupToolFilter)
        {
            return false;
        }
        if (!ToolChainState::SetTaskGroupImagePath(groupIndex, imagePath))
            return false;
        MarkCurrentRecipeDirty();
        return true;
    }
}
