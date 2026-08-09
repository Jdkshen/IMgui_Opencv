#include "ToolsWindow.h"
#include "DockSpaceHost.h"
#include "../Core/ThemeManager.h"
#include "../Renderer/FontManager.h"
#include "../Renderer/PreviewTextureCache.h"
#include "../Algorithm/ThresholdTool.h"
#include "../include/imgui/imgui.h"
#include "../include/imgui/imgui_internal.h"
#include <windows.h>
#include "ImageViewer.h"
#include "GeometryDrawEditor.h"
#include "ROIManager.h"
#include "Tools/BasicToolPanels.h"
#include "../Core/VideoCapture.h"
#include "../Core/VisionContext.h"
#include "../Core/ToolExecutor.h"
#include "../Core/ToolController.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolTypes.h"
#include "../Core/ToolResultCapabilities.h"
#include "../Core/ResultROIResolver.h"
#include "../Core/ToolChainPreflight.h"
#include "../Core/ToolChainValidator.h"
#include "../Core/ToolAssetService.h"
#include "../Core/ToolROIService.h"
#include "../Core/HardwareRuntimeService.h"
#include "../Core/HardwareSettingsService.h"
#include "../Core/ImageState.h"
#include "../Core/ImageImportService.h"
#include "../Core/ROIState.h"
#include "../Core/CalibrationFitter.h"
#include "../Core/OpenFileDialog.h"
#include "../Core/TemplateState.h"
#include "../Core/RealtimeDetectionState.h"
#include "../Core/RecipeAutosaveService.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/YOLODetector.h"
#include "../Algorithm/OpenCVYoloDetector.h"
#include "../Algorithm/ContourDetector.h"
#include "../Algorithm/ShapeMatcher.h"
#include "../Algorithm/LineDetector.h"
#include "../Algorithm/MorphologyTool.h"
#include "../Algorithm/ColorAnalyzer.h"
#include "../Algorithm/MultiColorFinder.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

static int s_MeasurementROIDrawOwner = -1;
static bool s_MeasurementROIModifying = false;
static std::vector<ROI> s_MeasurementROIPendingBackup;

static void BeginMeasurementROIDrawSequence(int mode)
{
    switch (std::clamp(mode, 0, 7))
    {
    case 0: UI::BeginROIDrawSequence({ROI_TYPE_POINT, ROI_TYPE_POINT}); break;
    case 1: UI::BeginROIDrawSequence({ROI_TYPE_RECT}); break;
    case 2: UI::BeginROIDrawSequence({ROI_TYPE_LINE, ROI_TYPE_LINE}); break;
    case 3: UI::BeginROIDrawSequence({ROI_TYPE_CIRCLE}); break;
    case 4: UI::BeginROIDrawSequence({ROI_TYPE_RECT}); break;
    case 5: UI::BeginROIDrawSequence({ROI_TYPE_RECT}); break;
    case 6: UI::BeginROIDrawSequence({ROI_TYPE_POINT, ROI_TYPE_LINE}); break;
    case 7: UI::BeginROIDrawSequence({ROI_TYPE_LINE, ROI_TYPE_LINE}); break;
    default: UI::CancelROIDrawSequence(); break;
    }
}

static const char* ROITypeDisplayName(int type)
{
    switch (type)
    {
    case ROI_TYPE_RECT: return "矩形";
    case ROI_TYPE_POINT: return "点";
    case ROI_TYPE_LINE: return "线段";
    case ROI_TYPE_CIRCLE: return "圆";
    case ROI_TYPE_POLYGON: return "多边形";
    default: return "ROI";
    }
}

static bool SyncMeasurementRuntimeROIs(ToolInstance& tool)
{
    return ToolROIService::SyncMeasurementROIs(tool);
}

static void RemoveMeasurementRuntimeROIs(ToolInstance& tool)
{
    ToolROIService::RemoveMeasurementROIs(tool);
}

static std::string QuoteCommandArg(const std::string& value)
{
    std::string quoted = "\"";
    for (char ch : value)
        quoted += (ch == '"') ? "\\\"" : std::string(1, ch);
    quoted += "\"";
    return quoted;
}

static std::string GetExecutableDir()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    return path;
}

static bool FileExists(const std::string& path)
{
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::string ResolveOpenCV5HelperPath()
{
    const std::string exeDir = GetExecutableDir();
    const std::string localPath = exeDir + "opencv5_helper\\opencv5_yolo_helper.exe";
    if (FileExists(localPath))
        return localPath;

    std::string releaseDir = exeDir;
    const std::string debugPart = "\\Debug\\";
    size_t pos = releaseDir.rfind(debugPart);
    if (pos != std::string::npos)
    {
        releaseDir.replace(pos, debugPart.size(), "\\Release\\");
        const std::string releasePath = releaseDir + "opencv5_helper\\opencv5_yolo_helper.exe";
        if (FileExists(releasePath))
            return releasePath;
    }

    return localPath;
}

static void LogProcessOutput(const char* prefix, const std::string& output)
{
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            LogSystem::Add(LOG_INFO, "%s%s", prefix, line.c_str());
    }
}

static bool SaveRawImageForOpenCV5Helper(const std::string& path, int& width, int& height, int& channels)
{
    if (ImageState::Current().empty())
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 helper: 请先加载图片或打开摄像头");
        return false;
    }
    if (ImageState::Current().depth() != CV_8U || (ImageState::Current().channels() != 1 && ImageState::Current().channels() != 3 && ImageState::Current().channels() != 4))
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 helper: 当前图像格式不支持 depth=%d channels=%d", ImageState::Current().depth(), ImageState::Current().channels());
        return false;
    }

    cv::Mat src = ImageState::Current().isContinuous() ? ImageState::Current() : ImageState::Current().clone();
    width = src.cols;
    height = src.rows;
    channels = src.channels();

    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        LogSystem::Add(LOG_ERROR, "OpenCV5 helper: 临时图像写入失败 %s", path.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(src.data), (std::streamsize)(src.total() * src.elemSize()));
    if (!out)
    {
        LogSystem::Add(LOG_ERROR, "OpenCV5 helper: 临时图像写入不完整 %s", path.c_str());
        return false;
    }
    return true;
}

static bool CaptureRawImageForOpenCV5Pipe(std::vector<unsigned char>& bytes, int& width, int& height, int& channels)
{
    bytes.clear();
    if (ImageState::Current().empty())
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 server: 请先加载图片或打开摄像头");
        return false;
    }
    if (ImageState::Current().depth() != CV_8U || (ImageState::Current().channels() != 1 && ImageState::Current().channels() != 3 && ImageState::Current().channels() != 4))
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 server: 当前图像格式不支持 depth=%d channels=%d", ImageState::Current().depth(), ImageState::Current().channels());
        return false;
    }

    cv::Mat src = ImageState::Current().isContinuous() ? ImageState::Current() : ImageState::Current().clone();
    width = src.cols;
    height = src.rows;
    channels = src.channels();

    const size_t byteCount = src.total() * src.elemSize();
    bytes.resize(byteCount);
    memcpy(bytes.data(), src.data, byteCount);
    return true;
}

static std::string MakeOpenCV5RawImagePath()
{
    char tempDir[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, tempDir) == 0)
        return GetExecutableDir() + "opencv5_current_frame.raw";

    char filePath[MAX_PATH] = {};
    if (GetTempFileNameA(tempDir, "ocv5", 0, filePath) == 0)
        return std::string(tempDir) + "opencv5_current_frame.raw";
    return filePath;
}

struct OpenCV5HelperServer
{
    HANDLE process = nullptr;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    std::string modelPath;
    std::string engine;
};

static OpenCV5HelperServer g_OpenCV5Server;

static void CloseOpenCV5Server()
{
    if (g_OpenCV5Server.stdinWrite)
    {
        DWORD written = 0;
        const char* quit = "QUIT\n";
        WriteFile(g_OpenCV5Server.stdinWrite, quit, (DWORD)strlen(quit), &written, nullptr);
        CloseHandle(g_OpenCV5Server.stdinWrite);
    }
    if (g_OpenCV5Server.stdoutRead)
        CloseHandle(g_OpenCV5Server.stdoutRead);
    if (g_OpenCV5Server.process)
    {
        WaitForSingleObject(g_OpenCV5Server.process, 300);
        CloseHandle(g_OpenCV5Server.process);
    }
    g_OpenCV5Server = {};
}

static bool ReadOpenCV5ServerLine(std::string& line)
{
    line.clear();
    char ch = 0;
    DWORD read = 0;
    while (ReadFile(g_OpenCV5Server.stdoutRead, &ch, 1, &read, nullptr) && read == 1)
    {
        if (ch == '\n')
            return true;
        if (ch != '\r')
            line.push_back(ch);
    }
    return !line.empty();
}

static bool EnsureOpenCV5Server(const std::string& modelPath, const char* engine)
{
    if (g_OpenCV5Server.process && g_OpenCV5Server.modelPath == modelPath && g_OpenCV5Server.engine == engine)
        return true;

    CloseOpenCV5Server();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0) || !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stdinRead, &stdinWrite, &sa, 0) || !SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0))
    {
        LogSystem::Add(LOG_ERROR, "OpenCV5 server: 创建管道失败");
        if (stdoutRead) CloseHandle(stdoutRead);
        if (stdoutWrite) CloseHandle(stdoutWrite);
        if (stdinRead) CloseHandle(stdinRead);
        if (stdinWrite) CloseHandle(stdinWrite);
        return false;
    }

    const std::string helperPath = ResolveOpenCV5HelperPath();
    std::string commandLine = QuoteCommandArg(helperPath) + " " + QuoteCommandArg(modelPath) + " --server " + engine + " 320";
    std::vector<char> cmd(commandLine.begin(), commandLine.end());
    cmd.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead;
    si.hStdOutput = stdoutWrite;
    si.hStdError = stdoutWrite;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        GetExecutableDir().c_str(), &si, &pi);

    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);
    if (!ok)
    {
        LogSystem::Add(LOG_ERROR, "OpenCV5 server: 启动失败 err=%lu", GetLastError());
        CloseHandle(stdoutRead);
        CloseHandle(stdinWrite);
        return false;
    }
    CloseHandle(pi.hThread);

    g_OpenCV5Server.process = pi.hProcess;
    g_OpenCV5Server.stdinWrite = stdinWrite;
    g_OpenCV5Server.stdoutRead = stdoutRead;
    g_OpenCV5Server.modelPath = modelPath;
    g_OpenCV5Server.engine = engine;

    auto t0 = std::chrono::steady_clock::now();
    std::string line;
    while (ReadOpenCV5ServerLine(line))
    {
        LogSystem::Add(LOG_INFO, "OpenCV5 server: %s", line.c_str());
        if (line == "READY")
        {
            auto t1 = std::chrono::steady_clock::now();
            LogSystem::Add(LOG_INFO, "OpenCV5 server: ready_ms=%.3f",
                std::chrono::duration<double, std::milli>(t1 - t0).count());
            return true;
        }
    }

    LogSystem::Add(LOG_ERROR, "OpenCV5 server: 启动后未返回 READY");
    CloseOpenCV5Server();
    return false;
}

static int RunOpenCV5HelperServer(const std::string& modelPath, const char* engine, int repeat, float confThreshold, float nmsThreshold)
{
    if (modelPath.empty())
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 server: 请先选择 YOLO ONNX 模型");
        return -1;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<unsigned char> imageBytes;
    if (!CaptureRawImageForOpenCV5Pipe(imageBytes, width, height, channels))
        return -1;

    repeat = (std::max)(1, repeat);
    auto total0 = std::chrono::steady_clock::now();
    if (!EnsureOpenCV5Server(modelPath, engine))
        return -1;

    std::string command = "RUNB " + std::to_string(width) + " " + std::to_string(height) + " " +
        std::to_string(channels) + " " + std::to_string(confThreshold) + " " + std::to_string(nmsThreshold) +
        " " + std::to_string(repeat) + " " + std::to_string(imageBytes.size()) + "\n";

    DWORD written = 0;
    if (!WriteFile(g_OpenCV5Server.stdinWrite, command.c_str(), (DWORD)command.size(), &written, nullptr))
    {
        LogSystem::Add(LOG_ERROR, "OpenCV5 server: 发送命令失败 err=%lu", GetLastError());
        CloseOpenCV5Server();
        return -1;
    }
    size_t offset = 0;
    while (offset < imageBytes.size())
    {
        DWORD chunkWritten = 0;
        const DWORD chunk = (DWORD)(std::min<size_t>)(imageBytes.size() - offset, 1 << 20);
        if (!WriteFile(g_OpenCV5Server.stdinWrite, imageBytes.data() + offset, chunk, &chunkWritten, nullptr) || chunkWritten == 0)
        {
            LogSystem::Add(LOG_ERROR, "OpenCV5 server: 发送图像失败 err=%lu", GetLastError());
            CloseOpenCV5Server();
            return -1;
        }
        offset += chunkWritten;
    }

    LogSystem::Add(LOG_INFO, "OpenCV5 server: engine=%s repeat=%d run(pipe), image=%dx%dx%d bytes=%zu", engine, repeat, width, height, channels, imageBytes.size());
    std::string line;
    while (ReadOpenCV5ServerLine(line))
    {
        if (line == "END")
            break;
        LogSystem::Add(LOG_INFO, "OpenCV5 server: %s", line.c_str());
    }
    auto total1 = std::chrono::steady_clock::now();
    LogSystem::Add(LOG_INFO, "OpenCV5 server: engine=%s repeat=%d wall_ms=%.3f", engine, repeat,
        std::chrono::duration<double, std::milli>(total1 - total0).count());
    return 0;
}

static int RunOpenCV5HelperBenchmark(const std::string& modelPath, const char* engine, int repeat, float confThreshold, float nmsThreshold)
{
    const std::string helperPath = ResolveOpenCV5HelperPath();
    if (modelPath.empty())
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 helper: 请先选择 YOLO ONNX 模型");
        return -1;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    const std::string rawPath = MakeOpenCV5RawImagePath();
    if (!SaveRawImageForOpenCV5Helper(rawPath, width, height, channels))
        return -1;

    repeat = (std::max)(1, repeat);
    const std::string innerCommand = QuoteCommandArg(helperPath) + " " + QuoteCommandArg(modelPath) +
        " " + std::to_string(repeat) + " " + engine + " 320 --raw-bgr " + QuoteCommandArg(rawPath) + " " +
        std::to_string(width) + " " + std::to_string(height) + " " + std::to_string(channels) + " " +
        std::to_string(confThreshold) + " " + std::to_string(nmsThreshold);
    const std::string command = "\"" + innerCommand + " 2>&1\"";

    LogSystem::Add(LOG_INFO, "OpenCV5 helper: engine=%s repeat=%d start, image=%dx%dx%d", engine, repeat, width, height, channels);
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe)
    {
        LogSystem::Add(LOG_ERROR, "OpenCV5 helper: 启动失败 %s", helperPath.c_str());
        return -1;
    }

    std::array<char, 512> buffer{};
    std::string output;
    while (fgets(buffer.data(), (int)buffer.size(), pipe))
        output += buffer.data();

    int code = _pclose(pipe);
    LogProcessOutput("OpenCV5 helper: ", output);
    LogSystem::Add(code == 0 ? LOG_INFO : LOG_WARN, "OpenCV5 helper: engine=%s repeat=%d exit=%d", engine, repeat, code);
    DeleteFileA(rawPath.c_str());
    return code;
}

namespace UI
{
    const std::vector<ToolMeta> g_ToolRegistry = {
        {12, "原图", ToolCategory::Base, "▣", "恢复本轮输入原图"},
        {3, "阈值调试", ToolCategory::Base, "◐", "灰度或颜色阈值分割"},
        {8, "形态学", ToolCategory::Base, "▦", "腐蚀、膨胀与开闭运算"},
        {0, "边缘检测", ToolCategory::Base, "◧", "提取灰度图像边缘"},

        {1, "模板匹配", ToolCategory::Detection, "□", "在搜索区域定位模板"},
        {4, "YOLO检测", ToolCategory::Detection, "◎", "ONNX 通用目标检测"},
        {6, "形状匹配", ToolCategory::Detection, "△", "按轮廓形状定位目标"},
        {13, "文字识别", ToolCategory::Detection, "T", "识别图片中的文字"},
        {14, "二维码/条码识别", ToolCategory::Detection, "▣", "识别二维码及常用条码"},

        {2, "Blob分析", ToolCategory::Geometry, "●", "提取连通区域及面积"},
        {5, "轮廓分析", ToolCategory::Geometry, "◇", "分析轮廓、凸包与形状"},
        {7, "直线检测", ToolCategory::Geometry, "▬", "检测直线与线段"},
        {17, "几何绘制", ToolCategory::Geometry, "G", "绘制辅助几何标记"},

        {9, "颜色分析", ToolCategory::Analysis, "◆", "统计颜色范围与占比"},
        {10, "多点找色", ToolCategory::Analysis, "◉", "按多个参考颜色点定位"},
        {16, "图像差分", ToolCategory::Analysis, "Δ", "比较参考图与当前图"},
        {15, "工业测量", ToolCategory::Analysis, "M", "距离、角度、直径与公差"},

        {11, "YOLO OpenCV 5.0", ToolCategory::Experimental, "✦", "OpenCV DNN 实验后端"},
    };

    static std::unordered_map<int, ToolUIFn> g_ToolUIMap;

    void MoveOriginalToolToFront()
    {
        ToolChainState::MoveOriginalToolToFront();
    }

    namespace
    {
        std::uint64_t s_selectedTaskGroupId = 0;
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
        bool s_showWorkflowWindow = false;
        bool s_requestWorkflowWindowFocus = false;
        char s_toolCatalogFilter[96]{};

        std::string AsciiLower(std::string value)
        {
            for (char& ch : value)
            {
                const unsigned char byte = static_cast<unsigned char>(ch);
                if (byte < 0x80)
                    ch = static_cast<char>(std::tolower(byte));
            }
            return value;
        }

        bool ToolMatchesCatalogFilter(const ToolMeta& meta)
        {
            if (s_toolCatalogFilter[0] == '\0')
                return true;
            const std::string needle = AsciiLower(s_toolCatalogFilter);
            const std::string haystack = AsciiLower(
                std::string(meta.name) + " " + meta.description);
            return haystack.find(needle) != std::string::npos;
        }

        void SelectTaskGroupInTools(std::uint64_t groupId, const std::string& groupName)
        {
            const bool selectionChanged = s_selectedTaskGroupId != groupId;
            s_selectedTaskGroupId = groupId;
            s_taskGroupToolFilter = groupId == 0 ? std::string{} : groupName;
            s_taskGroupToolFilterUngrouped = groupId == 0;
            if (selectionChanged)
                ToolController::RequestStepReset();
            if (selectionChanged && groupId != 0)
            {
                const int groupIndex = ToolChainState::TaskGroupIndexByName(groupName);
                if (groupIndex >= 0)
                {
                    const std::string& imagePath =
                        ToolChainState::ReadOnlyTaskGroups()[groupIndex].imagePath;
                    if (!imagePath.empty())
                    {
                        const ImageImportResult result =
                            ImageImportService::ImportSingleImage(imagePath);
                        if (!result.success)
                        {
                            LogSystem::Add(LOG_ERROR,
                                "任务图片加载失败 [%s]: %s",
                                groupName.c_str(), result.message.c_str());
                        }
                    }
                }
            }
            s_requestToolsWindowFocus = true;
            g_ShowTools = true;
            ToolChainState::SetActiveIndex(-1);
        }

        std::string TrimTaskGroupName(std::string value)
        {
            const std::size_t first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return {};
            const std::size_t last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        int TaskGroupIndexById(std::uint64_t id)
        {
            const auto& groups = ToolChainState::ReadOnlyTaskGroups();
            for (int index = 0; index < static_cast<int>(groups.size()); ++index)
            {
                if (groups[index].id == id)
                    return index;
            }
            return -1;
        }

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

        void DrawTaskGroupListWindow()
        {
            if (!s_showTaskGroupListWindow)
                return;

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float defaultHeight = (std::clamp)(viewport->WorkSize.y * 0.76f, 500.0f, 820.0f);
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 18.0f,
                viewport->WorkPos.y + (viewport->WorkSize.y - defaultHeight) * 0.5f),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360.0f, defaultHeight), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 420.0f),
                ImVec2(600.0f, FLT_MAX));
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

            const bool hasUngroupedTools = std::any_of(
                ToolChainState::ReadOnlyTools().begin(),
                ToolChainState::ReadOnlyTools().end(),
                [](const ToolInstance& tool)
                {
                    return tool.groupName.empty();
                });
            const auto& availableGroups = ToolChainState::ReadOnlyTaskGroups();
            if (!hasUngroupedTools && s_selectedTaskGroupId == 0 &&
                !availableGroups.empty())
            {
                SelectTaskGroupInTools(
                    availableGroups.front().id, availableGroups.front().name);
            }

            int selectedGroupIndex = NormalizeSelectedTaskGroupIndex();
            ImGui::SeparatorText("任务列表");
            const float settingsHeight = selectedGroupIndex >= 0 ? 342.0f : 58.0f;
            const float listHeight = (std::max)(160.0f,
                ImGui::GetContentRegionAvail().y - settingsHeight);
            if (ImGui::BeginChild("##task_group_list", ImVec2(0.0f, listHeight),
                ImGuiChildFlags_Borders))
            {
                if (hasUngroupedTools)
                {
                    const bool ungroupedSelected = s_selectedTaskGroupId == 0;
                    if (ImGui::Selectable("未分组", ungroupedSelected, 0,
                        ImVec2(0.0f, 34.0f)))
                    {
                        SelectTaskGroupInTools(0, {});
                    }
                    DrawTaskGroupDropTarget(-1);
                }

                const auto& groups = ToolChainState::ReadOnlyTaskGroups();
                for (int index = 0; index < static_cast<int>(groups.size()); ++index)
                {
                    const TaskGroupDefinition& group = groups[index];
                    ImGui::PushID(static_cast<int>(group.id));
                    int toolCount = 0;
                    for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
                    {
                        if (tool.groupName == group.name)
                            ++toolCount;
                    }
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
                    if (ImGui::Selectable(rowLabel, s_selectedTaskGroupId == group.id,
                        0, ImVec2(0.0f, 34.0f)))
                    {
                        SelectTaskGroupInTools(group.id, group.name);
                    }
                    DrawTaskGroupDropTarget(index);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            selectedGroupIndex = NormalizeSelectedTaskGroupIndex();
            if (selectedGroupIndex >= 0)
            {
                const TaskGroupDefinition& selectedGroup =
                    ToolChainState::ReadOnlyTaskGroups()[selectedGroupIndex];
                if (s_renameTaskGroupId != selectedGroup.id)
                {
                    std::snprintf(s_taskGroupNameBuffer, sizeof(s_taskGroupNameBuffer),
                        "%s", selectedGroup.name.c_str());
                    s_renameTaskGroupId = selectedGroup.id;
                    s_taskGroupError.clear();
                }

                ImGui::SeparatorText("任务设置");
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
                if (!s_taskGroupError.empty())
                {
                    ImGui::TextColored(ImVec4(0.92f, 0.34f, 0.20f, 1.0f),
                        "%s", s_taskGroupError.c_str());
                }

                bool enabled = selectedGroup.enabled;
                if (ImGui::Checkbox("启用该任务", &enabled) &&
                    ToolChainState::SetTaskGroupEnabled(selectedGroupIndex, enabled))
                {
                    CommitTaskGroupChange();
                }
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
                            char label[24];
                            std::snprintf(label, sizeof(label), "相机 %02d", index + 1);
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
                if (cameraIndex >= 0)
                {
                    const HardwareRuntimeSnapshot hardware =
                        HardwareRuntimeService::Snapshot();
                    const bool cameraConnected = hardware.cameraState ==
                            DeviceConnectionState::Connected &&
                        hardware.cameraSlotIndex == cameraIndex;
                    ImGui::PushStyleColor(ImGuiCol_Text, cameraConnected
                        ? ImVec4(0.24f, 0.86f, 0.48f, 1.0f)
                        : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("%s", cameraConnected
                        ? "绑定相机已连接：相机 → 任务图片 → 公共图片"
                        : "执行时自动连接绑定相机；失败则使用任务图片或公共图片");
                    ImGui::PopStyleColor();
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("输入优先级：任务图片 → 公共图片");
                    ImGui::PopStyleColor();
                }
                const std::size_t imageSlash = selectedGroup.imagePath.find_last_of("\\/");
                const char* imageName = selectedGroup.imagePath.empty()
                    ? "未设置（使用当前公共图片）"
                    : selectedGroup.imagePath.c_str() +
                        (imageSlash == std::string::npos ? 0 : imageSlash + 1);
                if (!selectedGroup.imageFolderPath.empty())
                {
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
                    ImGui::TextDisabled("任务图片: %s", imageName);
                    if (!selectedGroup.imagePath.empty())
                        ImGui::SetItemTooltip("%s", selectedGroup.imagePath.c_str());
                }
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
                ImGui::BeginDisabled(selectedGroupIndex <= 0);
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
                if (ImGui::Button("下移"))
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
                    s_pendingTaskGroupDelete = selectedGroupIndex;
            }
            else
            {
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

        void DrawWorkflowGraphCanvas(const char* childId,
            const std::vector<int>& visibleToolIndices, float childHeight)
        {
            constexpr float nodeWidth = 170.0f;
            constexpr float nodeHeight = 78.0f;
            constexpr float horizontalGap = 18.0f;
            constexpr float verticalGap = 44.0f;
            constexpr float sidePadding = 14.0f;
            constexpr float topPadding = 50.0f;
            constexpr float bottomPadding = 12.0f;

            const float availableWidth = (std::max)(1.0f,
                ImGui::GetContentRegionAvail().x);
            const float usableWidth = (std::max)(nodeWidth,
                availableWidth - sidePadding * 2.0f);
            const int columns = (std::max)(1, static_cast<int>(
                (usableWidth + horizontalGap) / (nodeWidth + horizontalGap)));
            const int rows = (static_cast<int>(visibleToolIndices.size()) +
                columns - 1) / columns;
            const float gridWidth = columns * nodeWidth +
                (columns - 1) * horizontalGap;
            const float canvasWidth = (std::max)(availableWidth,
                sidePadding * 2.0f + gridWidth);
            const float canvasHeight = topPadding + rows * nodeHeight +
                (std::max)(0, rows - 1) * verticalGap + bottomPadding;

            ImGui::BeginChild(childId, ImVec2(0.0f, childHeight),
                ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(canvasWidth, canvasHeight));
            ImDrawList* workflowDraw = ImGui::GetWindowDrawList();
            auto nodePosition = [&](int ordinal)
            {
                const int row = ordinal / columns;
                const int column = ordinal % columns;
                return ImVec2(
                    origin.x + sidePadding + column * (nodeWidth + horizontalGap),
                    origin.y + topPadding + row * (nodeHeight + verticalGap));
            };
            auto visibleOrdinal = [&](int toolIndex)
            {
                const auto found = std::find(visibleToolIndices.begin(),
                    visibleToolIndices.end(), toolIndex);
                return found == visibleToolIndices.end() ? -1 :
                    static_cast<int>(found - visibleToolIndices.begin());
            };
            for (int ordinal = 1; ordinal < static_cast<int>(visibleToolIndices.size()); ++ordinal)
            {
                const ImVec2 previous = nodePosition(ordinal - 1);
                const ImVec2 current = nodePosition(ordinal);
                const bool sameRow = ordinal / columns == (ordinal - 1) / columns;
                if (sameRow)
                {
                    const ImVec2 from(previous.x + nodeWidth,
                        previous.y + nodeHeight * 0.5f);
                    const ImVec2 to(current.x, current.y + nodeHeight * 0.5f);
                    workflowDraw->AddLine(from, to,
                        IM_COL32(105, 125, 140, 180), 2.0f);
                    workflowDraw->AddTriangleFilled(to,
                        ImVec2(to.x - 7.0f, to.y - 4.0f),
                        ImVec2(to.x - 7.0f, to.y + 4.0f),
                        IM_COL32(105, 125, 140, 220));
                }
                else
                {
                    const ImVec2 from(previous.x + nodeWidth * 0.5f,
                        previous.y + nodeHeight);
                    const ImVec2 to(current.x + nodeWidth * 0.5f, current.y);
                    const float middleY = (from.y + to.y) * 0.5f;
                    workflowDraw->AddLine(from, ImVec2(from.x, middleY),
                        IM_COL32(105, 125, 140, 180), 2.0f);
                    workflowDraw->AddLine(ImVec2(from.x, middleY),
                        ImVec2(to.x, middleY), IM_COL32(105, 125, 140, 180), 2.0f);
                    workflowDraw->AddLine(ImVec2(to.x, middleY), to,
                        IM_COL32(105, 125, 140, 180), 2.0f);
                    workflowDraw->AddTriangleFilled(to,
                        ImVec2(to.x - 4.0f, to.y - 7.0f),
                        ImVec2(to.x + 4.0f, to.y - 7.0f),
                        IM_COL32(105, 125, 140, 220));
                }
            }
            const std::vector<ToolChainDependency> workflowDependencies =
                ToolChainValidator::DescribeDependencies(ToolChainState::ReadOnlyTools());
            for (const ToolChainDependency& dependency : workflowDependencies)
            {
                if (!dependency.valid)
                    continue;
                const int sourceOrdinal = visibleOrdinal(dependency.sourceIndex);
                const int consumerOrdinal = visibleOrdinal(dependency.consumerIndex);
                if (sourceOrdinal < 0 || consumerOrdinal < 0)
                    continue;
                const ImVec2 source = nodePosition(sourceOrdinal);
                const ImVec2 consumer = nodePosition(consumerOrdinal);
                const ImU32 color = dependency.kind == ToolDependencyKind::Fixture
                    ? IM_COL32(255, 170, 55, 230) : IM_COL32(170, 100, 255, 230);
                const int sourceRow = sourceOrdinal / columns;
                const int consumerRow = consumerOrdinal / columns;
                if (sourceRow == consumerRow)
                {
                    const ImVec2 from(source.x + nodeWidth * 0.5f, source.y);
                    const ImVec2 to(consumer.x + nodeWidth * 0.5f, consumer.y);
                    const float arch = 22.0f +
                        std::abs(consumerOrdinal - sourceOrdinal) * 7.0f;
                    workflowDraw->AddBezierCubic(from,
                        ImVec2(from.x, from.y - arch),
                        ImVec2(to.x, to.y - arch), to, color, 2.0f);
                }
                else
                {
                    const ImVec2 from(source.x + nodeWidth * 0.5f,
                        source.y + nodeHeight);
                    const ImVec2 to(consumer.x + nodeWidth * 0.5f, consumer.y);
                    const float controlOffset = (std::max)(26.0f,
                        (to.y - from.y) * 0.42f);
                    workflowDraw->AddBezierCubic(from,
                        ImVec2(from.x, from.y + controlOffset),
                        ImVec2(to.x, to.y - controlOffset), to, color, 2.0f);
                }
            }
            for (int ordinal = 0; ordinal < static_cast<int>(visibleToolIndices.size()); ++ordinal)
            {
                const int toolIndex = visibleToolIndices[ordinal];
                const ToolInstance* tool = ToolChainState::AtReadOnly(toolIndex);
                if (!tool)
                    continue;
                const ImVec2 p = nodePosition(ordinal);
                ImU32 fill = tool->enabled ? IM_COL32(35, 68, 78, 245) :
                    IM_COL32(65, 65, 65, 220);
                if (tool->hasLastResult)
                {
                    if (tool->lastResult.status == ToolResultStatus::Pass)
                        fill = IM_COL32(35, 105, 70, 245);
                    else if (tool->lastResult.status == ToolResultStatus::Fail)
                        fill = IM_COL32(125, 55, 45, 245);
                    else if (tool->lastResult.status == ToolResultStatus::Error)
                        fill = IM_COL32(130, 75, 30, 245);
                }
                workflowDraw->AddRectFilled(p,
                    ImVec2(p.x + nodeWidth, p.y + nodeHeight), fill, 7.0f);
                workflowDraw->AddRect(p,
                    ImVec2(p.x + nodeWidth, p.y + nodeHeight),
                    toolIndex == ToolChainState::ActiveIndex()
                        ? IM_COL32(90, 220, 245, 255) : IM_COL32(125, 155, 165, 230),
                    7.0f, 0, toolIndex == ToolChainState::ActiveIndex() ? 3.0f : 1.0f);
                const char* registryName = tool->type == 12 ? "Original" :
                    ToolRegistry::GetName(tool->type);
                const std::string title = std::to_string(toolIndex + 1) + ". " +
                    ToolInstanceTitle(registryName, tool->label);
                const ImVec2 nodeMax(p.x + nodeWidth, p.y + nodeHeight);
                workflowDraw->PushClipRect(ImVec2(p.x + 8.0f, p.y + 5.0f),
                    ImVec2(nodeMax.x - 8.0f, nodeMax.y - 5.0f), true);
                workflowDraw->AddText(ImVec2(p.x + 8.0f, p.y + 8.0f),
                    IM_COL32_WHITE, title.c_str());
                const char* inputName = tool->inputSourceMode == 0 ? "上一原图" :
                    (tool->inputSourceMode == 1 ? "上一处理图" : "原图工具");
                workflowDraw->AddText(ImVec2(p.x + 8.0f, p.y + 32.0f),
                    IM_COL32(190, 215, 220, 255), inputName);
                if (tool->fixture.enabled)
                    workflowDraw->AddText(ImVec2(p.x + 8.0f, p.y + 53.0f),
                        IM_COL32(255, 190, 80, 255), "Fixture");
                else if (tool->resultRoiMode != 0)
                    workflowDraw->AddText(ImVec2(p.x + 8.0f, p.y + 53.0f),
                        IM_COL32(190, 125, 255, 255), "Result ROI");
                workflowDraw->PopClipRect();
                ImGui::SetCursorScreenPos(p);
                ImGui::PushID(toolIndex);
                if (ImGui::InvisibleButton("##workflow_node", ImVec2(nodeWidth, nodeHeight)))
                    ToolChainState::SetActiveIndex(toolIndex);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\n点击后定位到工具卡片", title.c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        void DrawWorkflowWindow()
        {
            if (!s_showWorkflowWindow)
                return;

            if (s_requestWorkflowWindowFocus)
            {
                ImGui::SetNextWindowFocus();
                s_requestWorkflowWindowFocus = false;
            }
            ImGui::SetNextWindowSize(ImVec2(960.0f, 620.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("工具流程图", &s_showWorkflowWindow))
            {
                const std::vector<int> visibleToolIndices =
                    CollectVisibleWorkflowToolIndices();
                const std::string chainTitle = WorkflowChainTitle();
                ImGui::Text("%s · 工具链", chainTitle.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%zu 个", visibleToolIndices.size());
                ImGui::SameLine();
                ImGui::TextDisabled("· 自动换行 · 紫色=结果 ROI · 橙色=Fixture · 点击节点定位");
                ImGui::Separator();
                if (visibleToolIndices.empty())
                    ImGui::TextDisabled("当前筛选没有工具");
                else
                    DrawWorkflowGraphCanvas("##standalone_tool_workflow_graph",
                        visibleToolIndices, 0.0f);
            }
            ImGui::End();
        }

        void DrawTaskGroupManagerWindows()
        {
            DrawTaskGroupListWindow();
            DrawTaskGroupToolsWindow();
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

    void ShowToolsWindow()
    {
        static cv::Mat g_PersistOriginal; // 持久保存原始图
        if (GeometryDrawEditor::ConsumeChanged())
            SaveCurrentRecipe();

        std::vector<std::uint64_t> activeToolIds;
        activeToolIds.reserve(ToolChainState::Count());
        for (const ToolInstance& tool : ToolChainState::ReadOnlyTools())
            activeToolIds.push_back(tool.toolId);
        PreviewTextureCache::Prune(activeToolIds);

        DrawTaskGroupManagerWindows();
        DrawWorkflowWindow();

        if (!g_ShowTools)
            return;

        if (s_requestToolsWindowFocus)
        {
            ImGui::SetNextWindowFocus();
            s_requestToolsWindowFocus = false;
        }

        ImGui::Begin("功能窗口", &g_ShowTools,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const char *kCatNames[] = {
            "输入与预处理", "定位与识别", "区域与几何",
            "分析与测量", "实验工具"
        };
        bool isDark = (g_CurrentTheme == 0);

        auto ToolAccentColor = [](int type) -> ImU32
        {
            switch (type)
            {
            case 12: return IM_COL32(96, 125, 139, 255); // original
            case 0:  return IM_COL32(74, 144, 226, 255);  // edge
            case 3:  return IM_COL32(245, 166, 35, 255);  // threshold
            case 8:  return IM_COL32(126, 211, 33, 255);  // morphology
            case 2:  return IM_COL32(80, 227, 194, 255);  // blob
            case 1:  return IM_COL32(189, 16, 224, 255);  // template
            case 4:  return IM_COL32(255, 82, 82, 255);   // yolo
            case 5:  return IM_COL32(248, 231, 28, 255);  // contour
            case 6:  return IM_COL32(144, 19, 254, 255);  // shape
            case 7:  return IM_COL32(91, 192, 222, 255);  // line
            case 9:  return IM_COL32(255, 112, 67, 255);  // color
            case 10: return IM_COL32(0, 188, 212, 255);   // multi color
            case 11: return IM_COL32(102, 187, 106, 255); // experiment
            case 13: return IM_COL32(67, 160, 255, 255);  // OCR
            case 14: return IM_COL32(38, 198, 218, 255);  // QR code
            case 15: return IM_COL32(255, 193, 7, 255);   // measurement
            case 17: return IM_COL32(0, 172, 193, 255);   // geometry draw
            default: return IM_COL32(120, 140, 160, 255);
            }
        };

        auto DrawToolIcon = [](ImDrawList *drawList, int type, ImVec2 p, float size, ImU32 accent)
        {
            const float r = size * 0.5f;
            const ImU32 white = IM_COL32(255, 255, 255, 235);
            const ImU32 stroke = IM_COL32(20, 24, 30, 160);
            ImVec2 center(p.x + r, p.y + r);

            drawList->AddRectFilled(p, ImVec2(p.x + size, p.y + size), accent, 3.0f);

            switch (type)
            {
            case 12:
                drawList->AddRect(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + size - 3, p.y + size - 3), white, 1.5f, 0, 1.4f);
                drawList->AddLine(ImVec2(p.x + 5, p.y + r), ImVec2(p.x + size - 5, p.y + r), white, 1.3f);
                break;
            case 0:
            case 7:
                drawList->AddLine(ImVec2(p.x + 3, p.y + size - 4), ImVec2(p.x + size - 3, p.y + 4), white, 1.8f);
                break;
            case 3:
                drawList->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + size - 2, p.y + r), IM_COL32(255, 255, 255, 210), 2.0f);
                drawList->AddRect(ImVec2(p.x + 2, p.y + r), ImVec2(p.x + size - 2, p.y + size - 2), white, 2.0f, 0, 1.2f);
                break;
            case 1:
            case 5:
                drawList->AddRect(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + size - 3, p.y + size - 3), white, 1.5f, 0, 1.4f);
                break;
            case 4:
            case 10:
                drawList->AddCircle(center, size * 0.31f, white, 18, 1.5f);
                drawList->AddCircleFilled(center, size * 0.10f, white, 12);
                break;
            case 13:
            {
                const ImVec2 glyphSize = ImGui::CalcTextSize("T");
                drawList->AddText(ImVec2(
                    p.x + (size - glyphSize.x) * 0.5f,
                    p.y + (size - glyphSize.y) * 0.5f), white, "T");
                drawList->AddLine(ImVec2(p.x + 4, p.y + size - 4), ImVec2(p.x + size - 4, p.y + size - 4), white, 1.2f);
                break;
            }
            case 14:
                drawList->AddRect(ImVec2(p.x + 4, p.y + 4), ImVec2(p.x + size - 4, p.y + size - 4), white, 0.0f, 0, 1.3f);
                drawList->AddRectFilled(ImVec2(p.x + 6, p.y + 6), ImVec2(p.x + 9, p.y + 9), white);
                drawList->AddRectFilled(ImVec2(p.x + size - 9, p.y + 6), ImVec2(p.x + size - 6, p.y + 9), white);
                drawList->AddRectFilled(ImVec2(p.x + 6, p.y + size - 9), ImVec2(p.x + 9, p.y + size - 6), white);
                break;
            case 15:
            {
                const ImVec2 glyphSize = ImGui::CalcTextSize("M");
                drawList->AddText(ImVec2(
                    p.x + (size - glyphSize.x) * 0.5f,
                    p.y + (size - glyphSize.y) * 0.5f), white, "M");
                drawList->AddLine(ImVec2(p.x + 4, p.y + size - 5), ImVec2(p.x + size - 4, p.y + size - 5), white, 1.2f);
                break;
            }
            case 17:
                drawList->AddRect(ImVec2(p.x + 3, p.y + 4), ImVec2(p.x + size - 4, p.y + size - 4), white, 0.0f, 0, 1.3f);
                drawList->AddLine(ImVec2(p.x + 3, p.y + size - 3), ImVec2(p.x + size - 3, p.y + 3), white, 1.3f);
                break;
            case 6:
                drawList->AddTriangleFilled(ImVec2(center.x, p.y + 3), ImVec2(p.x + size - 3, p.y + size - 3), ImVec2(p.x + 3, p.y + size - 3), white);
                break;
            case 8:
                drawList->AddLine(ImVec2(p.x + r, p.y + 3), ImVec2(p.x + r, p.y + size - 3), white, 1.3f);
                drawList->AddLine(ImVec2(p.x + 3, p.y + r), ImVec2(p.x + size - 3, p.y + r), white, 1.3f);
                break;
            case 9:
                drawList->AddCircleFilled(center, size * 0.31f, white, 18);
                break;
            case 11:
                drawList->AddCircle(center, size * 0.34f, white, 18, 1.3f);
                drawList->AddLine(ImVec2(p.x + 4, p.y + size - 3), ImVec2(p.x + size - 4, p.y + size - 3), white, 1.2f);
                break;
            default:
                drawList->AddCircleFilled(center, size * 0.28f, white, 18);
                break;
            }

            drawList->AddRect(p, ImVec2(p.x + size, p.y + size), stroke, 3.0f);
        };

        // ---- 调度器：每帧消费执行队列（替代旧 ExecState 状态机） ----
        ToolController::Tick();

        std::vector<std::string> toolGroups;
        toolGroups.reserve(ToolChainState::ReadOnlyTaskGroups().size());
        for (const TaskGroupDefinition& group : ToolChainState::ReadOnlyTaskGroups())
            toolGroups.push_back(group.name);

        const std::vector<int> visibleToolIndices =
            CollectVisibleWorkflowToolIndices();
        const std::string chainTitle = WorkflowChainTitle();
        ImGui::TextColored(isDark ? ImVec4(0.42f, 0.78f, 0.84f, 1.0f) : ImVec4(0.05f, 0.39f, 0.46f, 1.0f),
            "%s · 工具链", chainTitle.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%zu 个", visibleToolIndices.size());

        const char* workflowButtonLabel = s_showWorkflowWindow
            ? "切换到工具流程图##workflow_graph_window"
            : "打开工具流程图##workflow_graph_window";
        if (ImGui::Button(workflowButtonLabel, ImVec2(-1.0f, 0.0f)))
        {
            s_showWorkflowWindow = true;
            s_requestWorkflowWindowFocus = true;
        }

        const bool hasTools = !ToolChainState::Empty();
        const float topActionGap = ImGui::GetStyle().ItemSpacing.x;
        const float topActionWidth = (std::max)(90.0f,
            (ImGui::GetContentRegionAvail().x - topActionGap) * 0.5f);
        if (ImGui::Button("任务分组管理", ImVec2(topActionWidth, 0.0f)))
        {
            s_showTaskGroupListWindow = true;
            s_showTaskGroupToolsWindow = true;
            s_requestTaskGroupListDock = true;
            s_requestTaskGroupToolsDock = true;
        }
        ImGui::SameLine(0.0f, topActionGap);
        ImGui::BeginDisabled(!hasTools);
        if (ImGui::Button("批量操作", ImVec2(topActionWidth, 0.0f)))
            ImGui::OpenPopup("ToolBatchActions");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("ToolBatchActions"))
        {
            bool changed = false;
            if (ImGui::MenuItem("全部启用"))
            {
                ToolChainState::SetAllEnabled(true);
                changed = true;
            }
            if (ImGui::MenuItem("全部禁用"))
            {
                ToolChainState::SetAllEnabled(false);
                changed = true;
            }
            if (ImGui::MenuItem("全部显示结果标签"))
            {
                ToolChainState::SetAllResultLabelsVisible(true);
                changed = true;
            }
            if (ImGui::MenuItem("全部隐藏结果标签"))
            {
                ToolChainState::SetAllResultLabelsVisible(false);
                changed = true;
            }
            if (ImGui::MenuItem("全部失败后停止"))
            {
                ToolChainState::SetAllStopOnFailure(true);
                changed = true;
            }
            if (ImGui::MenuItem("全部失败后继续"))
            {
                ToolChainState::SetAllStopOnFailure(false);
                changed = true;
            }
            ImGui::Separator();
            for (const std::string& group : toolGroups)
            {
                if (ImGui::BeginMenu(group.c_str()))
                {
                    if (ImGui::MenuItem("启用"))
                    {
                        ToolChainState::SetGroupEnabled(group, true);
                        changed = true;
                    }
                    if (ImGui::MenuItem("禁用"))
                    {
                        ToolChainState::SetGroupEnabled(group, false);
                        changed = true;
                    }
                    if (ImGui::MenuItem("显示结果标签"))
                    {
                        ToolChainState::SetGroupResultLabelsVisible(group, true);
                        changed = true;
                    }
                    if (ImGui::MenuItem("隐藏结果标签"))
                    {
                        ToolChainState::SetGroupResultLabelsVisible(group, false);
                        changed = true;
                    }
                    if (ImGui::MenuItem("失败后停止"))
                    {
                        ToolChainState::SetGroupStopOnFailure(group, true);
                        changed = true;
                    }
                    if (ImGui::MenuItem("失败后继续"))
                    {
                        ToolChainState::SetGroupStopOnFailure(group, false);
                        changed = true;
                    }
                    ImGui::EndMenu();
                }
            }
            if (changed)
            {
                ToolController::OnToolChainChanged();
                SaveCurrentRecipe();
            }
            ImGui::EndPopup();
        }
        if (toolGroups.empty())
        {
            ImGui::TextDisabled("暂无分组");
        }
        else
        {
            ImGui::SetNextItemWidth(-1.0f);
            const char* groupPreview = s_taskGroupToolFilterUngrouped
                ? "未分组"
                : (s_taskGroupToolFilter.empty() ? "全部分组" : s_taskGroupToolFilter.c_str());
            if (ImGui::BeginCombo("##group_filter", groupPreview))
            {
                if (ImGui::Selectable("全部分组",
                    s_taskGroupToolFilter.empty() && !s_taskGroupToolFilterUngrouped))
                {
                    s_taskGroupToolFilter.clear();
                    s_taskGroupToolFilterUngrouped = false;
                    s_selectedTaskGroupId = 0;
                    ToolController::RequestStepReset();
                    ToolChainState::SetActiveIndex(-1);
                }
                if (ImGui::Selectable("未分组", s_taskGroupToolFilterUngrouped))
                {
                    s_taskGroupToolFilter.clear();
                    s_taskGroupToolFilterUngrouped = true;
                    s_selectedTaskGroupId = 0;
                    ToolController::RequestStepReset();
                    ToolChainState::SetActiveIndex(-1);
                }
                for (const std::string& group : toolGroups)
                {
                    if (ImGui::Selectable(group.c_str(),
                        !s_taskGroupToolFilterUngrouped && s_taskGroupToolFilter == group))
                    {
                        const int groupIndex = ToolChainState::TaskGroupIndexByName(group);
                        if (groupIndex >= 0)
                        {
                            SelectTaskGroupInTools(
                                ToolChainState::ReadOnlyTaskGroups()[groupIndex].id,
                                group);
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("筛选工具分组");
        }

        const RecipeAutosaveSnapshot recipeSave = RecipeAutosaveService::Snapshot();
        ImGui::TextDisabled("当前配方: %s", CurrentRecipeName());
        ImGui::SetItemTooltip("%s%s%s%s%s", CurrentRecipePath().c_str(),
            recipeSave.lastSavedAt.empty() ? "" : "\n最后保存: ",
            recipeSave.lastSavedAt.c_str(),
            recipeSave.lastError.empty() ? "" : "\n错误: ",
            recipeSave.lastError.c_str());
        ImGui::SameLine();
        const bool saveFailed = !recipeSave.lastError.empty();
        const bool saveBusy = recipeSave.dirty || recipeSave.pending || recipeSave.saving;
        ImGui::TextColored(saveFailed
            ? ImVec4(0.95f, 0.38f, 0.32f, 1.0f)
            : saveBusy ? ImVec4(0.95f, 0.72f, 0.22f, 1.0f)
            : ImVec4(0.35f, 0.78f, 0.48f, 1.0f),
            "%s", saveFailed ? "保存失败" : saveBusy ? "保存中" : "已保存");

        if (ImGui::Button("+ 添加工具", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddToolPopup");

        // Keep the tool catalogue inside the monitor work area. With every
        // category expanded its natural height is larger than common 768p and
        // 900p industrial displays; constraining it enables ImGui's internal
        // vertical scrolling instead of letting the last tools fall behind the
        // taskbar or the bottom edge of the screen.
        const ImGuiViewport* toolPopupViewport = ImGui::GetWindowViewport();
        const float workTop = toolPopupViewport
            ? toolPopupViewport->WorkPos.y : 0.0f;
        const float workBottom = toolPopupViewport
            ? toolPopupViewport->WorkPos.y + toolPopupViewport->WorkSize.y
            : ImGui::GetIO().DisplaySize.y;
        const float availableAbove = ImGui::GetItemRectMin().y - workTop;
        const float availableBelow = workBottom - ImGui::GetItemRectMax().y;
        const float toolPopupMaxHeight = std::clamp(
            (std::max)(availableAbove, availableBelow) - 12.0f,
            220.0f, 720.0f);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(330.0f, 0.0f), ImVec2(430.0f, toolPopupMaxHeight));
        if (ImGui::BeginPopup("AddToolPopup"))
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##tool_catalog_filter",
                "搜索工具名称或用途...", s_toolCatalogFilter,
                IM_ARRAYSIZE(s_toolCatalogFilter));

            std::array<int, static_cast<int>(ToolCategory::COUNT)> visibleCounts{};
            int visibleToolCount = 0;
            int visibleCategoryCount = 0;
            for (const ToolMeta& meta : g_ToolRegistry)
            {
                if (!ToolMatchesCatalogFilter(meta))
                    continue;
                ++visibleCounts[static_cast<int>(meta.category)];
                ++visibleToolCount;
            }
            for (int count : visibleCounts)
                visibleCategoryCount += count > 0 ? 1 : 0;

            ImGui::TextDisabled("%d 个工具 · %d 个分类",
                visibleToolCount, visibleCategoryCount);
            ImGui::Spacing();

            if (visibleToolCount == 0)
            {
                ImGui::TextDisabled("没有匹配的工具");
            }

            for (int c = 0; c < (int)ToolCategory::COUNT; c++)
            {
                if (visibleCounts[c] == 0)
                    continue;

                const bool filterActive = s_toolCatalogFilter[0] != '\0';
                if (filterActive)
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);

                char categoryLabel[96]{};
                std::snprintf(categoryLabel, sizeof(categoryLabel),
                    "%s  %d 个###tool_category_%d",
                    kCatNames[c], visibleCounts[c], c);
                const ImGuiTreeNodeFlags categoryFlags = c <= static_cast<int>(ToolCategory::Detection)
                    ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
                if (ImGui::CollapsingHeader(categoryLabel, categoryFlags))
                {
                    for (const auto &meta : g_ToolRegistry)
                    {
                        if (meta.category != (ToolCategory)c ||
                            !ToolMatchesCatalogFilter(meta))
                            continue;
                        char itemId[32];
                        snprintf(itemId, sizeof(itemId), "##tool_%d", meta.type);
                        ImVec2 rowPos = ImGui::GetCursorScreenPos();
                        const float rowH = (std::max)(
                            42.0f, ImGui::GetTextLineHeightWithSpacing() * 2.0f);
                        if (ImGui::Selectable(itemId, false, 0, ImVec2(0.0f, rowH)))
                        {
                            ToolInstance tool{};
                            tool.type = meta.type;
                            // 从具体任务视图添加工具时，直接归入该任务；
                            // “全部分组/未分组”视图仍保持新增为未分组。
                            if (!s_taskGroupToolFilterUngrouped &&
                                !s_taskGroupToolFilter.empty() &&
                                ToolChainState::TaskGroupIndexByName(
                                    s_taskGroupToolFilter) >= 0)
                            {
                                tool.groupName = s_taskGroupToolFilter;
                            }
                            const int addedIndex = ToolChainState::AddTool(std::move(tool));
                            const ToolInstance* addedTool = ToolChainState::AtReadOnly(addedIndex);
                            const std::uint64_t addedToolId = addedTool ? addedTool->toolId : 0;
                            MoveOriginalToolToFront();
                            ToolController::OnToolChainChanged();
                            ToolChainState::SetActiveIndex(
                                ToolChainState::IndexOfToolId(addedToolId));
                            SaveCurrentRecipe();
                            s_toolCatalogFilter[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }

                        ImDrawList *drawList = ImGui::GetWindowDrawList();
                        const float iconSize = 20.0f;
                        const float iconX = rowPos.x + ImGui::GetStyle().FramePadding.x;
                        const float iconY = rowPos.y + (rowH - iconSize) * 0.5f;
                        ImFontAtlasRect iconRect;
                        if (FontManager::GetToolIconRect(meta.type, &iconRect))
                        {
                            drawList->AddImageRounded(ImGui::GetIO().Fonts->TexRef,
                                ImVec2(iconX, iconY),
                                ImVec2(iconX + iconSize, iconY + iconSize),
                                iconRect.uv0, iconRect.uv1,
                                IM_COL32_WHITE, 3.0f);
                        }
                        else
                        {
                            DrawToolIcon(drawList, meta.type, ImVec2(iconX, iconY), iconSize, ToolAccentColor(meta.type));
                        }

                        const float textX = iconX + iconSize + 9.0f;
                        drawList->AddText(
                            ImVec2(textX, rowPos.y + 5.0f),
                            ImGui::GetColorU32(ImGuiCol_Text), meta.name);
                        drawList->AddText(
                            ImVec2(textX, rowPos.y + 22.0f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled),
                            meta.description);
                    }
                }
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        auto FileName = [](const std::string &path) -> std::string
        {
            size_t p = path.find_last_of("\\/");
            return (p != std::string::npos) ? path.substr(p + 1) : path;
        };

        auto ToolName = [](int type) -> const char *
        {
            for (const auto &m : g_ToolRegistry)
                if (m.type == type)
                    return m.name;
            return "?";
        };

        auto CaptureToolPersistentState = [](const ToolInstance& tool)
        {
            nlohmann::json state = tool.ToRecipeJson();
            if (tool.type == 10 && tool.toolImpl)
            {
                if (const auto* finder = dynamic_cast<const MultiColorFinder*>(tool.toolImpl.get()))
                {
                    const nlohmann::json finderState = finder->Save();
                    state["mcfPoints"] = finderState.value(
                        "points", nlohmann::json::array());
                }
            }
            return state;
        };

        // ---- UI 辅助：统一视觉风格 ----
        auto SectionHeader = [isDark](const char *label)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, isDark
                ? ImVec4(0.42f, 0.78f, 0.84f, 1.0f)
                : ImVec4(0.05f, 0.39f, 0.46f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Separator, isDark
                ? ImVec4(0.18f, 0.36f, 0.40f, 1.0f)
                : ImVec4(0.48f, 0.67f, 0.70f, 1.0f));
            ImGui::SeparatorText(label);
            ImGui::PopStyleColor(2);
        };
        auto PrimaryButton = [isDark](const char *label) -> bool
        {
            ImGui::PushStyleColor(ImGuiCol_Button, isDark
                ? ImVec4(0.10f, 0.40f, 0.48f, 1.0f)
                : ImVec4(0.12f, 0.49f, 0.57f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isDark
                ? ImVec4(0.13f, 0.50f, 0.59f, 1.0f)
                : ImVec4(0.08f, 0.42f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, isDark
                ? ImVec4(0.08f, 0.33f, 0.40f, 1.0f)
                : ImVec4(0.05f, 0.35f, 0.42f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.98f, 0.99f, 1.0f));
            bool clicked = ImGui::Button(label, ImVec2(-1, 28));
            ImGui::PopStyleColor(4);
            return clicked;
        };
        auto RunToolFromCard = [](int inst) -> bool
        {
            if (ImageState::Current().empty())
            {
                LogSystem::Add(LOG_WARN, "请先加载图片");
                return false;
            }
            ToolController::RequestRun(inst);
            return true;
        };
        auto SecondaryButton = [](const char *label, float w = 0) -> bool
        {
            return ImGui::Button(label, ImVec2(w, 0));
        };
        auto ParamLabel = [](const char* label, float labelW = 0.0f)
        {
            const float rowStartX = ImGui::GetCursorPosX();
            const float rowAvailableWidth = ImGui::GetContentRegionAvail().x;
            const float measuredLabelWidth = ImGui::CalcTextSize(label).x;
            const float minimumLabelWidth = labelW > 0.0f
                ? labelW : ImGui::GetFontSize() * 4.3f;
            const float resolvedLabelWidth = (std::max)(minimumLabelWidth,
                measuredLabelWidth);
            const float innerSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
            const float minimumControlWidth = ImGui::GetFontSize() * 5.5f;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            if (rowAvailableWidth >= resolvedLabelWidth + innerSpacing + minimumControlWidth)
            {
                ImGui::SameLine();
                ImGui::SetCursorPosX(rowStartX + resolvedLabelWidth + innerSpacing);
            }
            ImGui::SetNextItemWidth((std::max)(1.0f,
                ImGui::GetContentRegionAvail().x));
        };

        auto DrawSearchROIControls = [&](ToolInstance& it, int)
        {
            const char* resultPolicies[] = {
                "中心点在 ROI 内", "与 ROI 相交", "完全在 ROI 内", "覆盖率达到阈值"
            };
            int resultPolicy = std::clamp(it.roiResultPolicy, 0, 3);
            ParamLabel("查找 ROI 筛选");
            if (ImGui::Combo("##roi_result_policy", &resultPolicy,
                             resultPolicies, IM_ARRAYSIZE(resultPolicies)))
            {
                it.roiResultPolicy = resultPolicy;
                it.MarkParametersChanged();
            }
            if (it.roiResultPolicy == 3)
            {
                ParamLabel("最小覆盖");
                if (ImGui::SliderFloat("##roi_minimum_coverage",
                                       &it.roiMinimumCoverage, 0.0f, 1.0f, "%.2f"))
                {
                    it.roiMinimumCoverage = std::clamp(
                        it.roiMinimumCoverage, 0.0f, 1.0f);
                    it.MarkParametersChanged();
                }
            }
            SectionHeader("查找区域");
            const bool hasToolROI = !it.searchROIs.empty();
            ImGui::TextDisabled(hasToolROI
                ? "本工具ROI: 已绑定 %zu 个"
                : "本工具ROI: 未绑定（执行整图）", it.searchROIs.size());

            if (ToolROIService::IsSearchROIEditActive(it.toolId))
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "拖拽ROI后确认");
                if (PrimaryButton("确认绑定##search_roi_confirm"))
                {
                    const ToolROIEditResult result = ToolROIService::ConfirmSearchROIEdit(it);
                    if (result.success)
                    {
                        LogSystem::Add(LOG_INFO, "查找区域: 已绑定ROI到当前工具");
                        SaveCurrentRecipe();
                    }
                    else
                    {
                        LogSystem::Add(LOG_WARN, "查找区域: ROI 区域无效");
                    }
                }
                if (SecondaryButton("取消##search_roi_cancel", -1.0f))
                    ToolROIService::CancelSearchROIEdit(it.toolId);
                return;
            }

            if (ImGui::BeginTable("##search_roi_actions", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (SecondaryButton(it.searchROIs.empty()
                    ? "添加ROI##search_roi_add" : "修改ROI##search_roi_edit", -1.0f))
                {
                    ToolROIService::BeginSearchROIEdit(it);
                }
                ImGui::TableNextColumn();
                if (SecondaryButton("清除##search_roi_clear", -1.0f))
                {
                    ToolROIService::ClearSearchROIs(it);
                    LogSystem::Add(LOG_INFO, "查找区域: 已清除当前工具ROI");
                    SaveCurrentRecipe();
                }
                ImGui::EndTable();
            }
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);

        ImVec4 themeCard      = isDark ? ImVec4(0.095f, 0.110f, 0.130f, 1.0f) : ImVec4(0.965f, 0.975f, 0.980f, 1.0f);
        ImVec4 themeCardHover = isDark ? ImVec4(0.130f, 0.160f, 0.180f, 1.0f) : ImVec4(0.895f, 0.925f, 0.935f, 1.0f);
        ImVec4 themeActive    = isDark ? ImVec4(0.12f, 0.34f, 0.39f, 0.72f) : ImVec4(0.66f, 0.83f, 0.86f, 1.0f);
        const int toolsColorStackBase = ImGui::GetCurrentContext()->ColorStack.Size;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, themeCard);

        auto ResetCardColor = [isDark]() {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, isDark
                ? ImVec4(0.095f, 0.110f, 0.130f, 1.0f)
                : ImVec4(0.965f, 0.975f, 0.980f, 1.0f));
        };

        // ---- Card 面板辅助 ----
        int currentCardType = -1;
        int currentCardInst = -1;
        int duplicateToolIndex = -1;
        int pasteToolAfterIndex = -1;

        auto DrawUnifiedToolResult = [isDark, &SectionHeader](const ToolInstance& tool)
        {
            if (!tool.hasLastResult)
                return;

            const ToolResult& result = tool.lastResult;
            const std::uint64_t debugImageBytes = result.timing.debugImageBytes > 0
                ? result.timing.debugImageBytes
                : static_cast<std::uint64_t>(result.debugImage.total() *
                    result.debugImage.elemSize());
            SectionHeader("结果输出");
            const char* statusText = result.skipped ? "跳过" :
                (result.status == ToolResultStatus::Pass ? "通过" :
                    (result.status == ToolResultStatus::Fail ? "不合格" : "异常"));
            const ImVec4 statusColor = result.skipped
                ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                : result.status == ToolResultStatus::Pass
                    ? (isDark ? ImVec4(0.25f, 0.90f, 0.40f, 1.0f)
                              : ImVec4(0.05f, 0.48f, 0.20f, 1.0f))
                    : result.status == ToolResultStatus::Fail
                        ? (isDark ? ImVec4(1.0f, 0.65f, 0.20f, 1.0f)
                                  : ImVec4(0.78f, 0.28f, 0.08f, 1.0f))
                        : (isDark ? ImVec4(1.0f, 0.30f, 0.25f, 1.0f)
                                  : ImVec4(0.78f, 0.16f, 0.12f, 1.0f));
            ImGui::TextColored(statusColor, "状态: %s", statusText);

            std::string channelSummary;
            auto AppendCount = [&channelSummary](const char* name, std::size_t count)
            {
                if (count == 0)
                    return;
                if (!channelSummary.empty())
                    channelSummary += "  ·  ";
                channelSummary += name;
                channelSummary += " ";
                channelSummary += std::to_string(count);
            };
            AppendCount("区域", result.regions.size());
            AppendCount("检测", result.detections.size());
            AppendCount("线段", result.lines.size());
            AppendCount("文本", result.texts.size());
            AppendCount("测量", result.measurements.size());
            if (!result.debugImage.empty() || result.timing.debugImageBytes > 0)
                AppendCount("处理图", 1);
            else if (tool.type == 12 && result.success)
                AppendCount("图像", 1);
            if (channelSummary.empty())
                channelSummary = "无结构化输出";
            ImGui::TextWrapped("%s", channelSummary.c_str());

            const float wallMs = result.timing.wallMs > 0.0f
                ? result.timing.wallMs
                : result.timing.prepareMs + result.timing.executeMs +
                    result.timing.publishMs;
            if (result.skipped)
                ImGui::TextDisabled("耗时: --");
            else
                ImGui::TextDisabled("耗时: 总 %.3f ms｜准备 %.3f｜执行 %.3f｜发布 %.3f",
                    wallMs, result.timing.prepareMs, result.timing.executeMs,
                    result.timing.publishMs);

            if (!result.statusReason.empty())
                ImGui::TextWrapped("原因: %s", result.statusReason.c_str());
            if (!result.message.empty() && result.message != result.statusReason)
                ImGui::TextWrapped("说明: %s", result.message.c_str());

            const bool hasDetails = !result.regions.empty() ||
                !result.detections.empty() || !result.lines.empty() ||
                !result.texts.empty() || !result.measurements.empty() ||
                !result.debugImage.empty();
            if (!hasDetails || !ImGui::CollapsingHeader("输出详情##unified_result_details"))
                return;

            constexpr std::size_t kMaximumDisplayedItems = 12;
            auto DrawMore = [](std::size_t size)
            {
                if (size > kMaximumDisplayedItems)
                    ImGui::TextDisabled("其余 %zu 项已省略", size - kMaximumDisplayedItems);
            };

            for (std::size_t index = 0;
                index < (std::min)(result.detections.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.detections[index];
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("检测 %zu: %s  类别 %d  分数 %.3f  框 [%d,%d,%d,%d]",
                    index + 1, item.label.empty() ? "未分类" : item.label.c_str(),
                    item.classId, item.score, item.box.x, item.box.y,
                    item.box.width, item.box.height);
            }
            DrawMore(result.detections.size());

            for (std::size_t index = 0;
                index < (std::min)(result.regions.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.regions[index];
                const cv::Point2f center = item.center != cv::Point2f()
                    ? item.center
                    : cv::Point2f(item.bbox.x + item.bbox.width * 0.5f,
                        item.bbox.y + item.bbox.height * 0.5f);
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("区域 %zu: %s  中心 (%.1f, %.1f)  框 [%d,%d,%d,%d]  面积 %.1f  分数 %.3f  角度 %.2f°",
                    index + 1, item.label.empty() ? "未命名" : item.label.c_str(),
                    center.x, center.y, item.bbox.x, item.bbox.y,
                    item.bbox.width, item.bbox.height, item.area, item.score,
                    item.angle);
            }
            DrawMore(result.regions.size());

            for (std::size_t index = 0;
                index < (std::min)(result.lines.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.lines[index];
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("线段 %zu: (%d,%d) -> (%d,%d)  长度 %.2f  角度 %.2f°",
                    index + 1, item.p1.x, item.p1.y, item.p2.x, item.p2.y,
                    item.length, item.angle);
            }
            DrawMore(result.lines.size());

            for (std::size_t index = 0;
                index < (std::min)(result.texts.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.texts[index];
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("文本 %zu: %s  置信度 %.3f  框 [%d,%d,%d,%d]",
                    index + 1, item.text.c_str(), item.confidence,
                    item.box.x, item.box.y, item.box.width, item.box.height);
            }
            DrawMore(result.texts.size());

            for (std::size_t index = 0;
                index < (std::min)(result.measurements.size(), kMaximumDisplayedItems); ++index)
            {
                const auto& item = result.measurements[index];
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("测量 %zu: %s = %.6g %s",
                    index + 1, item.name.c_str(), item.value, item.unit.c_str());
            }
            DrawMore(result.measurements.size());

            if (!result.debugImage.empty())
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("处理图: %d x %d  通道 %d  %.2f MB",
                    result.debugImage.cols, result.debugImage.rows,
                    result.debugImage.channels(),
                    debugImageBytes / 1048576.0);
            }
        };

        auto BeginCard = [isDark, &currentCardType, &currentCardInst, &duplicateToolIndex,
            &SecondaryButton, &SectionHeader, &ParamLabel,
            &DrawUnifiedToolResult](const char *title, const char *icon = "")
        {
            ImGui::PushID(currentCardInst * 100 + currentCardType);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 3));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 3));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
            char childId[96];
            snprintf(childId, sizeof(childId), "##tool_card_%d_%d", currentCardInst, currentCardType);
            ImGui::BeginChild(childId, ImVec2(0.0f, 0.0f),
                ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const float parameterRegionWidth = ImGui::GetContentRegionAvail().x;
            const float minimumParameterWidth = ImGui::GetFontSize() * 5.5f;
            const float desiredLabelReserve = ImGui::GetFontSize() * 7.0f;
            const float labelReserve = (std::min)(desiredLabelReserve,
                (std::max)(0.0f, parameterRegionWidth - minimumParameterWidth));
            const float parameterWidth = (std::max)(1.0f,
                parameterRegionWidth - labelReserve -
                    ImGui::GetStyle().ItemSpacing.x);
            ImGui::PushItemWidth(parameterWidth);
            ToolInstance* cardTool = ToolChainState::At(currentCardInst);
            const std::string titleText = cardTool && !cardTool->label.empty()
                ? cardTool->label
                : std::string(title);
            ImGui::TextColored(isDark
                ? ImVec4(0.48f, 0.80f, 0.85f, 1.0f)
                : ImVec4(0.05f, 0.39f, 0.46f, 1.0f),
                "%s%s", icon, titleText.c_str());
            const float titleRightX = ImGui::GetItemRectMax().x -
                ImGui::GetWindowPos().x;
            const float toolMs = ToolController::GetToolTimeMs(currentCardInst);
            if (toolMs > 0.0f)
            {
                char timeText[32];
                snprintf(timeText, sizeof(timeText), "%.3fms", toolMs);
                const float textW = ImGui::CalcTextSize(timeText).x;
                const float rightX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textW;
                const bool fitsBesideTitle = titleRightX +
                    ImGui::GetStyle().ItemSpacing.x <= rightX;
                if (fitsBesideTitle)
                    ImGui::SameLine();
                if (rightX > ImGui::GetCursorPosX())
                    ImGui::SetCursorPosX(rightX);
                ImGui::TextColored(isDark ? ImVec4(0.30f, 0.95f, 0.46f, 1.0f) : ImVec4(0.02f, 0.42f, 0.18f, 1.0f), "%s", timeText);
                if (cardTool && cardTool->hasLastResult && ImGui::IsItemHovered())
                {
                    const ToolResultTiming& timing = cardTool->lastResult.timing;
                    ImGui::SetTooltip(
                        "准备 %.3f ms\n执行 %.3f ms\n发布 %.3f ms\n"
                        "输入 %.2f MB\n调试图 %.2f MB\n结果数据 %.2f KB\n"
                        "后端：预处理 %.3f / 推理 %.3f / 后处理 %.3f ms",
                        timing.prepareMs, timing.executeMs, timing.publishMs,
                        timing.inputBytes / 1048576.0,
                        timing.debugImageBytes / 1048576.0,
                        timing.resultDataBytes / 1024.0,
                        timing.backendPreprocessMs, timing.backendInferenceMs,
                        timing.backendPostprocessMs);
                }
            }
            ImGui::Separator();
            if (cardTool)
            {
                if (cardTool->parametersDirty)
                {
                    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.22f, 1.0f),
                        "参数已修改，重新执行后更新结果");
                }
                SectionHeader("实例设置");
                bool labelEnabled = !cardTool->label.empty();
                bool showResultLabels = cardTool->showResultLabels;
                bool enabled = cardTool->enabled;
                bool collapsed = cardTool->collapsed;
                const float instanceLabelWidth = ImGui::CalcTextSize("显示结果").x +
                    ImGui::GetStyle().ItemInnerSpacing.x;
                if (ImGui::BeginTable("##tool_instance_settings", 2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableSetupColumn("##setting_label",
                        ImGuiTableColumnFlags_WidthFixed, instanceLabelWidth);
                    ImGui::TableSetupColumn("##setting_value",
                        ImGuiTableColumnFlags_WidthStretch);
                    auto NextInstanceSetting = [](const char* label)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%s", label);
                        ImGui::TableSetColumnIndex(1);
                    };

                    NextInstanceSetting("工具标签");
                    if (ImGui::Checkbox("启用##tool_label_enabled", &labelEnabled))
                    {
                        if (labelEnabled && cardTool->label.empty())
                            cardTool->label = title ? title : "";
                        if (!labelEnabled)
                            cardTool->label.clear();
                        MarkCurrentRecipeDirty();
                    }

                    NextInstanceSetting("标签名称");
                    ImGui::BeginDisabled(!labelEnabled);
                    if (labelEnabled && cardTool->label.empty())
                        cardTool->label = title ? title : "";
                    char labelBuf[128];
                    snprintf(labelBuf, sizeof(labelBuf), "%s", cardTool->label.c_str());
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##tool_label", labelBuf, IM_ARRAYSIZE(labelBuf)))
                    {
                        cardTool->label = labelBuf;
                        MarkCurrentRecipeDirty();
                    }
                    ImGui::EndDisabled();

                    NextInstanceSetting("工具状态");
                    if (ImGui::Checkbox("启用##tool_enabled", &enabled))
                    {
                        cardTool->enabled = enabled;
                        MarkCurrentRecipeDirty();
                    }

                    NextInstanceSetting("结果显示");
                    if (ImGui::Checkbox("显示标签##tool_result_labels", &showResultLabels))
                    {
                        cardTool->showResultLabels = showResultLabels;
                        MarkCurrentRecipeDirty();
                    }

                    NextInstanceSetting("任务分组");
                    ImGui::SetNextItemWidth(-1.0f);
                    const char* groupPreview = cardTool->groupName.empty()
                        ? "未分组" : cardTool->groupName.c_str();
                    if (ImGui::BeginCombo("##tool_group_select", groupPreview))
                    {
                        if (ImGui::Selectable("未分组", cardTool->groupName.empty()) &&
                            ToolChainState::AssignToolToTaskGroup(currentCardInst, -1))
                        {
                            CommitTaskGroupChange();
                        }
                        const auto& taskGroups = ToolChainState::ReadOnlyTaskGroups();
                        for (int groupIndex = 0;
                            groupIndex < static_cast<int>(taskGroups.size()); ++groupIndex)
                        {
                            const bool selected =
                                cardTool->groupName == taskGroups[groupIndex].name;
                            if (ImGui::Selectable(taskGroups[groupIndex].name.c_str(), selected) &&
                                ToolChainState::AssignToolToTaskGroup(currentCardInst, groupIndex))
                            {
                                CommitTaskGroupChange();
                            }
                        }
                        ImGui::Separator();
                        ImGui::BeginDisabled(
                            taskGroups.size() >= ToolChainState::MaximumTaskGroups());
                        if (ImGui::Selectable("+ 新建任务并加入"))
                        {
                            const int createdIndex = ToolChainState::CreateTaskGroup();
                            if (createdIndex >= 0 &&
                                ToolChainState::AssignToolToTaskGroup(
                                    currentCardInst, createdIndex))
                            {
                                CommitTaskGroupChange();
                            }
                        }
                        ImGui::EndDisabled();
                        ImGui::EndCombo();
                    }

                    NextInstanceSetting("卡片显示");
                    if (ImGui::Checkbox("折叠##tool_collapsed", &collapsed))
                    {
                        cardTool->collapsed = collapsed;
                        if (collapsed && ToolChainState::ActiveIndex() == currentCardInst)
                            ToolChainState::SetActiveIndex(-1);
                        MarkCurrentRecipeDirty();
                    }

                    if (cardTool->type == 4 || cardTool->type == 11 || cardTool->type == 13)
                    {
                        NextInstanceSetting("模型缺失");
                        if (ImGui::Checkbox("跳过工具##skip_missing_model",
                            &cardTool->skipIfModelMissing))
                        {
                            MarkCurrentRecipeDirty();
                        }
                    }

                    NextInstanceSetting("操作");
                    if (SecondaryButton("复制工具", -1.0f))
                        duplicateToolIndex = currentCardInst;
                    ImGui::EndTable();
                }

                const std::vector<ToolChainDependency> dependencies =
                    ToolChainValidator::DescribeDependencies(ToolChainState::ReadOnlyTools());
                bool hasDependencyDisplay = false;
                for (const ToolChainDependency& dependency : dependencies)
                {
                    if (dependency.consumerIndex == currentCardInst ||
                        dependency.sourceIndex == currentCardInst)
                    {
                        hasDependencyDisplay = true;
                        break;
                    }
                }
                if (hasDependencyDisplay)
                {
                    SectionHeader("依赖关系");
                    for (const ToolChainDependency& dependency : dependencies)
                    {
                        const char* kindName = dependency.kind == ToolDependencyKind::ResultROI
                            ? "结果ROI" : "Fixture";
                        if (dependency.consumerIndex == currentCardInst)
                        {
                            if (dependency.valid)
                            {
                                const ToolInstance& source = *ToolChainState::AtReadOnly(dependency.sourceIndex);
                                const char* sourceName = source.type == 12
                                    ? "原图" : ToolRegistry::GetName(source.type);
                                const std::string name = ToolInstanceTitle(sourceName, source.label);
                                ImGui::TextDisabled("%s <- %d. %s", kindName,
                                    dependency.sourceIndex + 1, name.c_str());
                            }
                            else
                            {
                                ImGui::TextColored(ImVec4(0.92f, 0.34f, 0.20f, 1.0f),
                                    "%s: %s", kindName, dependency.issue.c_str());
                            }
                        }
                        if (dependency.valid && dependency.sourceIndex == currentCardInst)
                        {
                            const ToolInstance& consumer = *ToolChainState::AtReadOnly(dependency.consumerIndex);
                            const char* consumerName = consumer.type == 12
                                ? "原图" : ToolRegistry::GetName(consumer.type);
                            const std::string name = ToolInstanceTitle(consumerName, consumer.label);
                            ImGui::TextDisabled("%s -> %d. %s", kindName,
                                dependency.consumerIndex + 1, name.c_str());
                        }
                    }
                }

                bool resultRoiChanged = false;
                const char* resultRoiModes[] = {
                    "固定/手工 ROI", "上游第 N 个结果", "上游全部结果", "选择两个结果"
                };
                const bool supportsSelectedPair = cardTool->type == 15 &&
                    (cardTool->measureMode == 0 || cardTool->measureMode == 2 ||
                     cardTool->measureMode == 6 || cardTool->measureMode == 7);
                const int resultRoiModeCount = supportsSelectedPair ? 4 : 3;
                const int configuredResultRoiMode = cardTool->resultRoiMode;
                cardTool->resultRoiMode = std::clamp(cardTool->resultRoiMode,
                    0, resultRoiModeCount - 1);
                resultRoiChanged |= cardTool->resultRoiMode != configuredResultRoiMode;
                resultRoiChanged |= ImGui::Combo("输入 ROI", &cardTool->resultRoiMode,
                    resultRoiModes, resultRoiModeCount);
                if (cardTool->resultRoiMode != 0)
                {
                    const bool selectedPair = cardTool->resultRoiMode == 3;
                    auto sourceIsCompatible = [&](int sourceType, bool secondInput)
                    {
                        const ToolResultCapabilities capabilities =
                            ToolCapabilitiesForType(sourceType);
                        if (!capabilities.SupportsSpatialResult())
                            return false;
                        if (!selectedPair || cardTool->type != 15)
                            return true;
                        const bool requiresLine = cardTool->measureMode == 2 ||
                            cardTool->measureMode == 7 ||
                            (cardTool->measureMode == 6 && secondInput);
                        return !requiresLine || capabilities.lines;
                    };
                    auto drawResultSource = [&](const char* label, int& configuredIndex,
                                                std::uint64_t& configuredId,
                                                bool secondInput)
                    {
                        std::string sourcePreview = "未选择";
                        int resolvedIndex = ToolChainState::IndexOfToolId(configuredId);
                        if (resolvedIndex < 0)
                            resolvedIndex = configuredIndex;
                        if (resolvedIndex >= 0 && resolvedIndex < currentCardInst &&
                            resolvedIndex < static_cast<int>(ToolChainState::Count()))
                        {
                            const auto& source = *ToolChainState::AtReadOnly(resolvedIndex);
                            const char* sourceName = source.type == 12 ? "原图" : ToolRegistry::GetName(source.type);
                            sourcePreview = std::to_string(resolvedIndex + 1) + ". " +
                                ToolInstanceTitle(sourceName, source.label) + "  [" +
                                ToolResultKindsLabel(source.type) + "]";
                            if (!sourceIsCompatible(source.type, secondInput))
                                sourcePreview = "不兼容: " + sourcePreview;
                        }
                        if (ImGui::BeginCombo(label, sourcePreview.c_str()))
                        {
                            bool hasCompatibleSource = false;
                            for (int sourceIndex = 0; sourceIndex < currentCardInst; ++sourceIndex)
                            {
                                const auto& source = *ToolChainState::AtReadOnly(sourceIndex);
                                if (!sourceIsCompatible(source.type, secondInput))
                                    continue;
                                hasCompatibleSource = true;
                                const char* sourceName = source.type == 12
                                    ? "原图" : ToolRegistry::GetName(source.type);
                                const std::string option = std::to_string(sourceIndex + 1) + ". " +
                                    ToolInstanceTitle(sourceName, source.label) + "  [" +
                                    ToolResultKindsLabel(source.type) + "]";
                                const bool selected = configuredId != 0
                                    ? configuredId == source.toolId
                                    : configuredIndex == sourceIndex;
                                if (ImGui::Selectable(option.c_str(), selected))
                                {
                                    configuredIndex = sourceIndex;
                                    configuredId = source.toolId;
                                    resultRoiChanged = true;
                                }
                            }
                            if (!hasCompatibleSource)
                                ImGui::TextDisabled("前面没有可输出空间结果的工具");
                            ImGui::EndCombo();
                        }
                    };

                    auto drawResultChoice = [&](const char* label,
                                                int configuredSourceIndex,
                                                std::uint64_t configuredSourceId,
                                                int& configuredResultIndex,
                                                bool secondInput)
                    {
                        int resolvedIndex = ToolChainState::IndexOfToolId(configuredSourceId);
                        if (resolvedIndex < 0)
                            resolvedIndex = configuredSourceIndex;

                        const ToolInstance* source = nullptr;
                        if (resolvedIndex >= 0 && resolvedIndex < currentCardInst &&
                            resolvedIndex < static_cast<int>(ToolChainState::Count()))
                        {
                            source = ToolChainState::AtReadOnly(resolvedIndex);
                        }

                        std::vector<ResultROIChoice> choices;
                        if (source && source->hasLastResult)
                        {
                            ResultROIRequest request;
                            request.mode = ResultROIMode::NthResult;
                            request.category = cardTool->resultRoiCategory;
                            request.classId = cardTool->resultRoiClassId;
                            request.minScore = cardTool->resultRoiMinScore;
                            request.minArea = cardTool->resultRoiMinArea;
                            request.sortMode = cardTool->resultRoiSortMode;
                            request.sortDescending = cardTool->resultRoiSortDescending;
                            request.requireLineResults = selectedPair &&
                                cardTool->type == 15 &&
                                (cardTool->measureMode == 2 ||
                                 cardTool->measureMode == 7 ||
                                 (cardTool->measureMode == 6 && secondInput));
                            choices = ResultROIResolver::ListChoices(
                                source->lastResult, request);
                        }

                        std::string preview;
                        if (!source)
                            preview = "请先选择上游工具";
                        else if (!source->hasLastResult)
                            preview = "等待上游执行";
                        else if (choices.empty())
                            preview = "当前筛选条件下没有结果";
                        else if (configuredResultIndex >= 0 &&
                            configuredResultIndex < static_cast<int>(choices.size()))
                        {
                            preview = choices[configuredResultIndex].label;
                        }
                        else
                            preview = "原选择已超出当前结果范围";

                        if (ImGui::BeginCombo(label, preview.c_str()))
                        {
                            if (choices.empty())
                            {
                                ImGui::TextDisabled("请先执行上游工具，或调整结果筛选条件");
                            }
                            else
                            {
                                for (const ResultROIChoice& choice : choices)
                                {
                                    const bool selected =
                                        configuredResultIndex == choice.resultIndex;
                                    if (ImGui::Selectable(choice.label.c_str(), selected))
                                    {
                                        configuredResultIndex = choice.resultIndex;
                                        resultRoiChanged = true;
                                    }
                                    if (selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                    };

                    drawResultSource(selectedPair ? "上游工具 A" : "上游工具",
                        cardTool->resultRoiSourceTool, cardTool->resultRoiSourceToolId, false);
                    if (cardTool->resultRoiMode == 1 || selectedPair)
                    {
                        drawResultChoice(selectedPair ? "选择结果 A" : "选择结果",
                            cardTool->resultRoiSourceTool,
                            cardTool->resultRoiSourceToolId,
                            cardTool->resultRoiIndex, false);
                    }
                    if (selectedPair)
                    {
                        drawResultSource("上游工具 B",
                            cardTool->resultRoiSecondSourceTool,
                            cardTool->resultRoiSecondSourceToolId, true);
                        drawResultChoice("选择结果 B",
                            cardTool->resultRoiSecondSourceTool,
                            cardTool->resultRoiSecondSourceToolId,
                            cardTool->resultRoiSecondIndex, true);
                    }
                    if (cardTool->resultRoiMode != 0)
                    {
                        char resultCategory[128];
                        snprintf(resultCategory, sizeof(resultCategory), "%s", cardTool->resultRoiCategory.c_str());
                        ParamLabel("结果类别");
                        if (ImGui::InputText("##result_roi_category", resultCategory,
                            IM_ARRAYSIZE(resultCategory)))
                        {
                            cardTool->resultRoiCategory = resultCategory;
                            resultRoiChanged = true;
                        }
                        resultRoiChanged |= ImGui::DragInt("类别ID##result_roi_class", &cardTool->resultRoiClassId,
                            1.0f, -1, 100000);
                        resultRoiChanged |= ImGui::DragFloat("最低分数##result_roi_score", &cardTool->resultRoiMinScore,
                            0.01f, -1.0f, 1.0f, "%.3f");
                        resultRoiChanged |= ImGui::DragFloat("最小面积##result_roi_area", &cardTool->resultRoiMinArea,
                            1.0f, -1.0f, 100000000.0f, "%.1f");
                        const char* resultSortModes[] = {"原始顺序", "分数优先", "面积优先"};
                        cardTool->resultRoiSortMode = std::clamp(cardTool->resultRoiSortMode, 0, 2);
                        resultRoiChanged |= ImGui::Combo("结果排序##result_roi_sort", &cardTool->resultRoiSortMode,
                            resultSortModes, IM_ARRAYSIZE(resultSortModes));
                        resultRoiChanged |= ImGui::Checkbox("降序##result_roi_desc", &cardTool->resultRoiSortDescending);
                        const char* missingPolicies[] = {"结果不存在时跳过", "结果不存在时判定失败"};
                        cardTool->resultRoiMissingPolicy = std::clamp(cardTool->resultRoiMissingPolicy, 0, 1);
                        resultRoiChanged |= ImGui::Combo("缺失处理", &cardTool->resultRoiMissingPolicy,
                            missingPolicies, IM_ARRAYSIZE(missingPolicies));
                        if (cardTool->type == 15 && cardTool->measureMode == 0)
                        {
                            if (selectedPair)
                            {
                                ImGui::TextWrapped("点点距离：结果 A、B 分别转换为中心点；"
                                    "可以选择同一上游的不同序号，也可以选择两个不同上游。");
                            }
                            else
                            {
                                ImGui::TextWrapped("点点距离：区域结果自动取中心点，线段结果保留端点；"
                                    "使用当前排序后的前两个点。至少需要 2 个区域结果或 1 条线段。");
                            }
                        }
                        else if (selectedPair && cardTool->type == 15 &&
                            (cardTool->measureMode == 2 || cardTool->measureMode == 7))
                        {
                            ImGui::TextWrapped("线测量：结果 A、B 必须分别选择可输出线段的上游工具。");
                        }
                        else if (selectedPair && cardTool->type == 15 &&
                            cardTool->measureMode == 6)
                        {
                            ImGui::TextWrapped("点线距离：结果 A 转换为中心点；"
                                "结果 B 必须选择可输出线段的上游工具。");
                        }
                    }
                }
                if (resultRoiChanged)
                    SaveCurrentRecipe();

                bool fixtureChanged = ImGui::Checkbox("启用定位坐标系", &cardTool->fixture.enabled);
                if (cardTool->fixture.enabled)
                {
                    std::string fixturePreview = "未选择";
                    int fixtureSourceIndex = ToolChainState::IndexOfToolId(cardTool->fixture.sourceToolId);
                    if (fixtureSourceIndex < 0)
                        fixtureSourceIndex = cardTool->fixture.sourceToolIndex;
                    if (fixtureSourceIndex >= 0 && fixtureSourceIndex < currentCardInst &&
                        fixtureSourceIndex < static_cast<int>(ToolChainState::Count()))
                    {
                        const auto& source = *ToolChainState::AtReadOnly(fixtureSourceIndex);
                        fixturePreview = std::to_string(fixtureSourceIndex + 1) + ". " +
                            ToolInstanceTitle(ToolRegistry::GetName(source.type), source.label) +
                            "  [" + ToolResultKindsLabel(source.type) + "]";
                        if (!ToolCapabilitiesForType(source.type).SupportsSpatialResult())
                            fixturePreview = "不兼容: " + fixturePreview;
                    }
                    if (ImGui::BeginCombo("定位上游", fixturePreview.c_str()))
                    {
                        bool hasCompatibleSource = false;
                        for (int sourceIndex = 0; sourceIndex < currentCardInst; ++sourceIndex)
                        {
                            const auto& source = *ToolChainState::AtReadOnly(sourceIndex);
                            if (!ToolCapabilitiesForType(source.type).SupportsSpatialResult())
                                continue;
                            hasCompatibleSource = true;
                            const std::string option = std::to_string(sourceIndex + 1) + ". " +
                                ToolInstanceTitle(ToolRegistry::GetName(source.type), source.label) +
                                "  [" + ToolResultKindsLabel(source.type) + "]";
                            const bool selected = cardTool->fixture.sourceToolId != 0
                                ? cardTool->fixture.sourceToolId == source.toolId
                                : cardTool->fixture.sourceToolIndex == sourceIndex;
                            if (ImGui::Selectable(option.c_str(), selected))
                            {
                                cardTool->fixture.sourceToolIndex = sourceIndex;
                                cardTool->fixture.sourceToolId = source.toolId;
                                fixtureChanged = true;
                            }
                        }
                        if (!hasCompatibleSource)
                            ImGui::TextDisabled("前面没有可输出定位结果的工具");
                        ImGui::EndCombo();
                    }
                    const ToolInstance* fixtureSource =
                        ToolChainState::AtReadOnly(fixtureSourceIndex);
                    std::vector<std::pair<int, std::string>> fixtureChoices;
                    if (fixtureSource && fixtureSource->hasLastResult)
                    {
                        const ToolResult& sourceResult = fixtureSource->lastResult;
                        const std::size_t candidateCount = (std::max)({
                            sourceResult.regions.size(), sourceResult.detections.size(),
                            sourceResult.lines.size(), sourceResult.texts.size()});
                        fixtureChoices.reserve(candidateCount);
                        for (int resultIndex = 0;
                            resultIndex < static_cast<int>(candidateCount); ++resultIndex)
                        {
                            FixturePose pose;
                            if (!FixtureTransform::TryExtractPose(
                                sourceResult, resultIndex, pose))
                            {
                                continue;
                            }

                            const char* kind = "结果";
                            std::string itemLabel;
                            float score = 0.0f;
                            if (resultIndex < static_cast<int>(sourceResult.regions.size()))
                            {
                                kind = "区域";
                                itemLabel = sourceResult.regions[resultIndex].label;
                                score = sourceResult.regions[resultIndex].score;
                            }
                            else if (resultIndex < static_cast<int>(sourceResult.detections.size()))
                            {
                                kind = "检测框";
                                itemLabel = sourceResult.detections[resultIndex].label;
                                score = sourceResult.detections[resultIndex].score;
                            }
                            else if (resultIndex < static_cast<int>(sourceResult.lines.size()))
                            {
                                kind = "线段";
                            }
                            else if (resultIndex < static_cast<int>(sourceResult.texts.size()))
                            {
                                kind = "文本";
                                itemLabel = sourceResult.texts[resultIndex].text;
                                score = sourceResult.texts[resultIndex].confidence;
                            }

                            std::ostringstream label;
                            label << resultIndex + 1 << ". " << kind;
                            if (!itemLabel.empty())
                                label << "｜" << itemLabel;
                            label << "｜中心(" << std::fixed << std::setprecision(1)
                                << pose.origin.x << ',' << pose.origin.y << ')';
                            if (pose.angleDegrees != 0.0f)
                                label << "｜角度 " << pose.angleDegrees << "°";
                            if (score > 0.0f)
                                label << "｜分数 " << std::setprecision(3) << score;
                            fixtureChoices.emplace_back(resultIndex, label.str());
                        }
                    }

                    std::string fixtureResultPreview;
                    if (!fixtureSource)
                        fixtureResultPreview = "请先选择定位上游";
                    else if (!fixtureSource->hasLastResult)
                        fixtureResultPreview = "等待上游执行";
                    else if (fixtureChoices.empty())
                        fixtureResultPreview = "上游当前没有可用定位结果";
                    else
                    {
                        const auto selected = std::find_if(
                            fixtureChoices.begin(), fixtureChoices.end(),
                            [cardTool](const auto& choice)
                            {
                                return choice.first == cardTool->fixture.resultIndex;
                            });
                        fixtureResultPreview = selected != fixtureChoices.end()
                            ? selected->second
                            : "原选择已超出当前结果范围";
                    }
                    if (ImGui::BeginCombo("定位结果", fixtureResultPreview.c_str()))
                    {
                        if (fixtureChoices.empty())
                        {
                            ImGui::TextDisabled("请先执行定位上游工具");
                        }
                        else
                        {
                            for (const auto& choice : fixtureChoices)
                            {
                                const bool selected =
                                    cardTool->fixture.resultIndex == choice.first;
                                if (ImGui::Selectable(choice.second.c_str(), selected))
                                {
                                    cardTool->fixture.resultIndex = choice.first;
                                    fixtureChanged = true;
                                }
                                if (selected)
                                    ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (SecondaryButton("从当前定位结果记录参考位姿"))
                    {
                        int sourceIndex = ToolChainState::IndexOfToolId(cardTool->fixture.sourceToolId);
                        if (sourceIndex < 0)
                            sourceIndex = cardTool->fixture.sourceToolIndex;
                        const ToolInstance* sourceTool = ToolChainState::AtReadOnly(sourceIndex);
                        if (sourceTool && sourceTool->hasLastResult)
                        {
                            FixturePose pose;
                            if (FixtureTransform::TryExtractPose(
                                sourceTool->lastResult,
                                cardTool->fixture.resultIndex,
                                pose))
                            {
                                cardTool->fixture.referenceOrigin = pose.origin;
                                cardTool->fixture.referenceAngleDegrees = pose.angleDegrees;
                                fixtureChanged = true;
                            }
                        }
                    }
                    ImGui::TextDisabled("参考: (%.2f, %.2f), %.2f deg",
                        cardTool->fixture.referenceOrigin.x,
                        cardTool->fixture.referenceOrigin.y,
                        cardTool->fixture.referenceAngleDegrees);
                    fixtureChanged |= ImGui::Checkbox("定位缺失时判定失败", &cardTool->fixture.failOnMissing);
                }
                if (fixtureChanged)
                    SaveCurrentRecipe();

                bool judgementChanged = false;
                if (ImGui::BeginTable("##judgement_flags", 2,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableNextColumn();
                    judgementChanged |= ImGui::Checkbox("启用判定", &cardTool->judgement.enabled);
                    ImGui::TableNextColumn();
                    judgementChanged |= ImGui::Checkbox("失败停止", &cardTool->judgement.stopOnFailure);
                    ImGui::EndTable();
                }
                if (cardTool->judgement.enabled)
                {
                    judgementChanged |= ImGui::DragInt("最少结果", &cardTool->judgement.minResultCount, 1.0f, 0, 100000);
                    judgementChanged |= ImGui::DragInt("最多结果", &cardTool->judgement.maxResultCount, 1.0f, -1, 100000);
                    ImGui::TextDisabled("最多结果 -1 表示不限制");

                    judgementChanged |= ImGui::DragFloat("最低分数", &cardTool->judgement.minScore, 0.01f, -1.0f, 1.0f, "%.3f");
                    judgementChanged |= ImGui::DragFloat("最小面积", &cardTool->judgement.minArea, 1.0f, -1.0f, 1000000000.0f, "%.1f");
                    judgementChanged |= ImGui::DragFloat("最大面积", &cardTool->judgement.maxArea, 1.0f, -1.0f, 1000000000.0f, "%.1f");

                    judgementChanged |= ImGui::Checkbox("测量项范围", &cardTool->judgement.measurementRangeEnabled);
                    if (cardTool->judgement.measurementRangeEnabled)
                    {
                        char measurementName[128];
                        snprintf(measurementName, sizeof(measurementName), "%s", cardTool->judgement.measurementName.c_str());
                        ParamLabel("测量项名称");
                        if (ImGui::InputText("##judgement_measurement_name", measurementName,
                            IM_ARRAYSIZE(measurementName)))
                        {
                            cardTool->judgement.measurementName = measurementName;
                            judgementChanged = true;
                        }
                        if (cardTool->hasLastResult && !cardTool->lastResult.measurements.empty())
                        {
                            const char* preview = cardTool->judgement.measurementName.empty()
                                ? "从上次结果选择"
                                : cardTool->judgement.measurementName.c_str();
                            if (ImGui::BeginCombo("可用测量项", preview))
                            {
                                for (const ToolResult::Measurement& measurement : cardTool->lastResult.measurements)
                                {
                                    const bool selected = cardTool->judgement.measurementName == measurement.name;
                                    if (ImGui::Selectable(measurement.name.c_str(), selected))
                                    {
                                        cardTool->judgement.measurementName = measurement.name;
                                        judgementChanged = true;
                                    }
                                    if (selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                        }
                        judgementChanged |= ImGui::DragScalar("测量下限", ImGuiDataType_Double,
                            &cardTool->judgement.minMeasurement, 0.01f, nullptr, nullptr, "%.6f");
                        judgementChanged |= ImGui::DragScalar("测量上限", ImGuiDataType_Double,
                            &cardTool->judgement.maxMeasurement, 0.01f, nullptr, nullptr, "%.6f");
                    }

                    char requiredText[256];
                    snprintf(requiredText, sizeof(requiredText), "%s", cardTool->judgement.requiredText.c_str());
                    ParamLabel("文本条件");
                    if (ImGui::InputText("##judgement_required_text", requiredText,
                        IM_ARRAYSIZE(requiredText)))
                    {
                        cardTool->judgement.requiredText = requiredText;
                        judgementChanged = true;
                    }
                    const char* textModes[] = {"包含", "完全相等"};
                    cardTool->judgement.textMatchMode = std::clamp(cardTool->judgement.textMatchMode, 0, 1);
                    judgementChanged |= ImGui::Combo("文本匹配", &cardTool->judgement.textMatchMode, textModes, IM_ARRAYSIZE(textModes));
                    judgementChanged |= ImGui::Checkbox("区分大小写", &cardTool->judgement.textCaseSensitive);
                    ImGui::TextDisabled("分数/面积为 -1 时不参与判定");
                }
                if (judgementChanged)
                    SaveCurrentRecipe();

                DrawUnifiedToolResult(*cardTool);
                ImGui::Separator();
            }
            return true;
        };
        auto EndCard = []()
        {
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::PopStyleVar(3);
            ImGui::PopID();
            ImGui::Spacing();
        };

        // ---- 工具 UI 函数注册 ----
        g_ToolUIMap.clear();
        ToolPanelContext basicPanelContext;
        basicPanelContext.beginCard = [&](const char* title) { BeginCard(title); };
        basicPanelContext.endCard = EndCard;
        basicPanelContext.sectionHeader = SectionHeader;
        basicPanelContext.primaryButton = PrimaryButton;
        basicPanelContext.secondaryButton = [&](const char* label) { return SecondaryButton(label); };
        basicPanelContext.parameterLabel = ParamLabel;
        basicPanelContext.runTool = RunToolFromCard;
        basicPanelContext.drawSearchROI = DrawSearchROIControls;
        RegisterBasicToolPanels(g_ToolUIMap, basicPanelContext);

        // 0: 边缘检测
        g_ToolUIMap[0] = [&](ToolInstance &it, int inst)
        {
            BeginCard("边缘检测");
            if (SecondaryButton("重置参数"))
            {
                it.cannyLow = 50; it.cannyHigh = 150; it.edgeUseGray = false;
            }
            if (PrimaryButton("执行边缘检测"))
            {
                if (RunToolFromCard(inst))
                    LogSystem::Add(LOG_INFO, "Canny(%d,%d)", it.cannyLow, it.cannyHigh);
            }
            DrawSearchROIControls(it, inst);
            SectionHeader("参数");
            ImGui::SliderInt("Canny低阈值", &it.cannyLow, 0, 255);
            ImGui::SliderInt("Canny高阈值", &it.cannyHigh, 0, 255);
            ImGui::Checkbox("转为灰度", &it.edgeUseGray);
            EndCard();
        };

        // 1: 模板匹配
        g_ToolUIMap[1] = [&](ToolInstance &it, int inst)
        {
            BeginCard("模板匹配");
            if (SecondaryButton("重置参数"))
            {
                ToolAssetService::ClearAsset(it, ToolAssetKind::TemplateMatch);
                MarkCurrentRecipeAssetsDirty();
                it.tplGray = false; it.tplBinary = false; it.tplBinThresh = 128;
                it.tplEdge = false; it.tplEdgeLow = 50; it.tplEdgeHigh = 150;
                it.imgUseGray = false; it.imgEnableThreshold = false; it.imgThreshold = 128;
                it.enableRotation = false; it.rotationStart = -45; it.rotationEnd = 45; it.rotationStep = 1;
                it.maxResults = 5; it.matchThreshold = 0.7f; it.maxImageDim = 1000; it.nmsThreshold = 0.3f; it.searchMode = 0;
TemplateState::ClearResults();
            }

            if (PrimaryButton("执行模板匹配"))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (it.templateImg.empty())
                    LogSystem::Add(LOG_WARN, "模板匹配: 请先抓取模板");
                else
                {
                    RunToolFromCard(inst);
                }
            }

            DrawSearchROIControls(it, inst);
            // ---- 模板 ----
            SectionHeader("模板");

            const int templateCaptureROI = ToolAssetService::ActiveROIIndex(
                it.toolId, ToolAssetKind::TemplateMatch);

            if (templateCaptureROI < 0 && it.templateImg.empty())
            {
                if (SecondaryButton("添加ROI获取模板"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::TemplateMatch);
            }
            else if (templateCaptureROI >= 0)
            {
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "模板ROI已就绪");
                ImGui::TextDisabled("拖拽ROI调整位置后点击确认");
                if (PrimaryButton("确认捕获"))
                {
                    const ToolAssetCaptureResult result = ToolAssetService::ConfirmROICapture(
                        it, ToolAssetKind::TemplateMatch);
                    if (result.success)
                    {
                        LogSystem::Add(LOG_INFO, "模板匹配: 模板已抓取 %dx%d",
                            result.bounds.width, result.bounds.height);
                        MarkCurrentRecipeAssetsDirty();
                    }
                    else
                        LogSystem::Add(LOG_WARN, "模板匹配: ROI区域无效或超出图像范围");
                }
                if (SecondaryButton("取消"))
                    ToolAssetService::CancelROICapture(it.toolId, ToolAssetKind::TemplateMatch);
            }
            else if (!it.templateImg.empty())
            {
                if (SecondaryButton("修改模板ROI"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::TemplateMatch);
                if (SecondaryButton("清除模板"))
                {
                    ToolAssetService::ClearAsset(it, ToolAssetKind::TemplateMatch);
                    MarkCurrentRecipeAssetsDirty();
                }
            }

            if (!it.templateImg.empty())
            {
                // 显示预览开关
                ImGui::Checkbox("显示预览##tm", &it.showTemplatePreview);
                if (it.showTemplatePreview)
                {
                    std::uint64_t signature = PreviewTextureCache::ImageSignature(it.templateImg);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplGray);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplBinary);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplBinThresh);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplEdge);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplEdgeLow);
                    signature = PreviewTextureCache::CombineSignature(signature, it.tplEdgeHigh);
                    if (PreviewTextureCache::NeedsUpdate(it.toolId, PreviewTextureKind::TemplateMatch, signature))
                    {
                        cv::Mat preview = it.templateImg.clone();
                        if (it.tplGray && preview.channels() > 1)
                            cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                        if (it.tplBinary)
                        {
                            if (preview.channels() > 1)
                                cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                            cv::threshold(preview, preview, it.tplBinThresh, 255, cv::THRESH_BINARY);
                        }
                        if (it.tplEdge)
                        {
                            if (preview.channels() > 1)
                                cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                            cv::Canny(preview, preview, it.tplEdgeLow, it.tplEdgeHigh);
                        }
                        PreviewTextureCache::Queue(it.toolId, PreviewTextureKind::TemplateMatch,
                            signature, preview, 80);
                    }

                    const PreviewTextureView preview = PreviewTextureCache::Get(
                        it.toolId, PreviewTextureKind::TemplateMatch);
                    const ImVec2 previewSize(preview.width * 2.0f, preview.height * 2.0f);
                    if (preview.ready)
                        ImGui::Image(preview.textureId, previewSize);
                    else
                        ImGui::Dummy(previewSize);
                    ImGui::SetItemTooltip("模板预览");
                    ImGui::TextDisabled("模板: %dx%d", it.templateImg.cols, it.templateImg.rows);
                }
            }
            else
                ImGui::TextDisabled("未抓取模板");

            // ---- 模板预处理 ----
            SectionHeader("模板预处理");
            if (ImGui::BeginTable("##template_preprocess_flags", 3,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("灰度##tm", &it.tplGray);
                ImGui::TableNextColumn();
                ImGui::Checkbox("二值化##tm", &it.tplBinary);
                ImGui::TableNextColumn();
                ImGui::Checkbox("边缘##tm", &it.tplEdge);
                ImGui::EndTable();
            }
            if (it.tplBinary)
                ImGui::SliderInt("阈值##tm", &it.tplBinThresh, 0, 255);
            if (it.tplEdge)
            {
                ImGui::SliderInt("低##tm", &it.tplEdgeLow, 0, 255);
                ImGui::SliderInt("高##tm", &it.tplEdgeHigh, 0, 255);
            }

            // ---- 图像预处理 ----
            SectionHeader("图像预处理");
            if (ImGui::BeginTable("##image_preprocess_flags", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("转为灰度##tm_i", &it.imgUseGray);
                ImGui::TableNextColumn();
                ImGui::Checkbox("二值化##tm_i", &it.imgEnableThreshold);
                ImGui::EndTable();
            }
            if (it.imgEnableThreshold)
                ImGui::SliderInt("阈值##tm_i", &it.imgThreshold, 0, 255);

            SectionHeader("旋转");
            ImGui::Checkbox("启用旋转", &it.enableRotation);
            if (it.enableRotation)
            {
                if (ImGui::BeginTable("##rotation_range", 3,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                {
                    const char* labels[] = {"起始°", "结束°", "步长°"};
                    int* values[] = {&it.rotationStart, &it.rotationEnd, &it.rotationStep};
                    const int minimums[] = {-45, 0, 1};
                    const int maximums[] = {0, 45, 10};
                    for (int column = 0; column < 3; ++column)
                    {
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", labels[column]);
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::PushID(column);
                        ImGui::SliderInt("##rotation_value", values[column],
                            minimums[column], maximums[column]);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }

            // ---- 匹配参数 ----
            SectionHeader("匹配参数");
            ImGui::SliderInt("最大结果数", &it.maxResults, 1, 100);
            ImGui::SliderFloat("匹配阈值", &it.matchThreshold, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("NMS阈值", &it.nmsThreshold, 0.0f, 1.0f, "%.3f");
            ImGui::Checkbox("亚像素位置/角度##tm", &it.matchSubpixel);
            ImGui::SetItemTooltip("对相关峰进行二次曲线插值，并忽略旋转模板的域外边角");
            ImGui::SliderInt("匹配精度", &it.maxImageDim, 400, 2000);

            // ---- 操作 ----
            if (SecondaryButton("清空结果"))
            {
                it.lastResult = ToolResult{};
                it.hasLastResult = false;
                TemplateState::ClearResults();
            }

            EndCard();
        };

        // 4: YOLO
        g_ToolUIMap[4] = [&](ToolInstance &it, int inst)
        {
            BeginCard("YOLO检测");
            if (SecondaryButton("重置参数"))
            {
                it.yoloConfThreshold = 0.5f; it.yoloNmsThreshold = 0.4f;
                it.yoloUseROI = false; it.yoloUseGPU = false;
            }
            bool isLiveMode = VideoCapture::IsOpen();
            const bool yoloLiveDetect = ToolChainState::YoloLiveDetect();
            const int yoloLiveInstance = ToolChainState::YoloLiveInstanceIndex();
            const char *btnLabel = isLiveMode ? (yoloLiveDetect && yoloLiveInstance == inst ? "停止实时检测" : "开始实时检测") : "执行检测";
            bool isThisActive = (yoloLiveDetect && yoloLiveInstance == inst);
            if (PrimaryButton(btnLabel))
            {
                if (!it.yoloModelPath.empty())
                    YOLODetector::LoadModel(it.yoloModelPath, it.yoloClassesPath, it.yoloUseGPU);
                if (isLiveMode)
                {
                    if (YOLODetector::IsLoaded())
                    {
                        if (isThisActive)
                        {
                            ToolChainState::SetYoloLiveDetect(false);
                            ToolChainState::SetYoloLiveInstanceIndex(-1);
                        }
                        else
                        {
                            ToolChainState::SetYoloLiveDetect(true);
                            ToolChainState::SetYoloLiveInstanceIndex(inst);
                            if (!VideoCapture::IsPlaying())
                                VideoCapture::Play();
                        }
                    }
                    else
                        LogSystem::Add(LOG_WARN, "YOLO: 请先选择模型文件");
                }
                else
                {
                    RunToolFromCard(inst);
                }
            }
            DrawSearchROIControls(it, inst);
            SectionHeader("模型");
            if (ImGui::BeginTable("##yolo_model_files", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                const float pickerLabelWidth = (std::max)(
                    ImGui::CalcTextSize("选择 ONNX 模型").x,
                    ImGui::CalcTextSize("选择类别文件").x);
                const float pickerWidth = pickerLabelWidth +
                    ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::TableSetupColumn("##picker", ImGuiTableColumnFlags_WidthFixed, pickerWidth);
                ImGui::TableSetupColumn("##file", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (SecondaryButton("选择 ONNX 模型"))
                {
                    std::string path = OpenFileDialogWithFilter(L"ONNX模型 (*.onnx)\0*.onnx\0所有文件 (*.*)\0*.*\0", L"选择 YOLO ONNX 模型文件");
                    if (!path.empty())
                        it.yoloModelPath = path;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", it.yoloModelPath.empty() ? "未选择" : FileName(it.yoloModelPath).c_str());
                if (!it.yoloModelPath.empty() && ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", it.yoloModelPath.c_str());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (SecondaryButton("选择类别文件"))
                {
                    std::string path = OpenFileDialogWithFilter(L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0", L"选择类别名称文件");
                    if (!path.empty())
                        it.yoloClassesPath = path;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", it.yoloClassesPath.empty() ? "默认 COCO 80 类" : FileName(it.yoloClassesPath).c_str());
                if (!it.yoloClassesPath.empty() && ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", it.yoloClassesPath.c_str());
                ImGui::EndTable();
            }
            SectionHeader("参数");
            ImGui::SliderFloat("置信度阈值", &it.yoloConfThreshold, 0.1f, 1.0f, "%.2f");
            ImGui::SliderFloat("NMS阈值", &it.yoloNmsThreshold, 0.1f, 1.0f, "%.2f");
            float overlayOffsetX = RealtimeDetectionState::OverlayOffsetX();
            if (ImGui::SliderFloat("滚动补偿(X)", &overlayOffsetX, -100.0f, 100.0f, "%.0fpx"))
                RealtimeDetectionState::SetOverlayOffsetX(overlayOffsetX);
            ImGui::Checkbox("GPU加速(CUDA/DML)", &it.yoloUseGPU);
            SectionHeader("状态");
            ImGui::TextDisabled("实际后端: %s", YOLODetector::GetBackendName());
            if (ToolChainState::YoloLiveDetect() && ToolChainState::YoloLiveFrameMs() > 0)
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "实时: %.3fms", ToolChainState::YoloLiveFrameMs());
            EndCard();
        };

        // 11: YOLO OpenCV 5.0 实验入口
        g_ToolUIMap[11] = [&](ToolInstance &it, int inst)
        {
            BeginCard("YOLO OpenCV 5.0", "[实验] ");
            if (SecondaryButton("重置参数"))
            {
                it.yoloConfThreshold = 0.5f;
                it.yoloNmsThreshold = 0.4f;
                it.yoloUseROI = false;
                it.yoloUseGPU = false;
            }

            if (PrimaryButton("执行内置 OpenCV DNN 测试"))
                RunToolFromCard(inst);

            const bool isOpenCV5Live = ToolChainState::YoloLiveDetect() &&
                ToolChainState::YoloLiveInstanceIndex() == inst;
            const char* liveLabel = isOpenCV5Live ? "停止实时测试##ocv5live" : "开始实时测试##ocv5live";
            if (SecondaryButton(liveLabel))
            {
                if (isOpenCV5Live)
                {
                    ToolChainState::SetYoloLiveDetect(false);
                    ToolChainState::SetYoloLiveInstanceIndex(-1);
                    ToolChainState::SetYoloLiveFrameMs(0.0f);
                }
                else
                {
                    if (it.yoloModelPath.empty())
                    {
                        LogSystem::Add(LOG_WARN, "YOLO OpenCV DNN: 请先选择 ONNX 模型");
                    }
                    else
                    {
                        if (!VideoCapture::IsOpen())
                        {
                            LogSystem::Add(LOG_WARN, "YOLO OpenCV DNN: 请先加载视频或打开摄像头");
                        }
                        else
                        {
                            ToolChainState::SetYoloLiveDetect(true);
                            ToolChainState::SetYoloLiveInstanceIndex(inst);
                            if (!VideoCapture::IsPlaying())
                                VideoCapture::Play();
                        }
                    }
                }
            }
            if (isOpenCV5Live && ToolChainState::YoloLiveFrameMs() > 0)
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "实时: %.3fms/帧", ToolChainState::YoloLiveFrameMs());

            DrawSearchROIControls(it, inst);
            SectionHeader("模型");
            if (ImGui::BeginTable("##ocv5_model_files", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                const float pickerLabelWidth = (std::max)(
                    ImGui::CalcTextSize("选择 ONNX 模型").x,
                    ImGui::CalcTextSize("选择类别文件").x);
                const float pickerWidth = pickerLabelWidth +
                    ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::TableSetupColumn("##picker", ImGuiTableColumnFlags_WidthFixed, pickerWidth);
                ImGui::TableSetupColumn("##file", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (SecondaryButton("选择 ONNX 模型##ocv5"))
                {
                    std::string path = OpenFileDialogWithFilter(L"ONNX模型 (*.onnx)\0*.onnx\0所有文件 (*.*)\0*.*\0", L"选择 YOLO ONNX 模型文件");
                    if (!path.empty())
                        it.yoloModelPath = path;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", it.yoloModelPath.empty() ? "未选择" : FileName(it.yoloModelPath).c_str());
                if (!it.yoloModelPath.empty() && ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", it.yoloModelPath.c_str());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (SecondaryButton("选择类别文件##ocv5"))
                {
                    std::string path = OpenFileDialogWithFilter(L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0", L"选择类别名称文件");
                    if (!path.empty())
                        it.yoloClassesPath = path;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", it.yoloClassesPath.empty() ? "默认 COCO 80 类" : FileName(it.yoloClassesPath).c_str());
                if (!it.yoloClassesPath.empty() && ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", it.yoloClassesPath.c_str());
                ImGui::EndTable();
            }

            SectionHeader("参数");
            ImGui::SliderFloat("置信度阈值##ocv5", &it.yoloConfThreshold, 0.1f, 1.0f, "%.2f");
            ImGui::SliderFloat("NMS阈值##ocv5", &it.yoloNmsThreshold, 0.1f, 1.0f, "%.2f");

            SectionHeader("状态");
            ImGui::TextDisabled("%s", OpenCVYoloDetector::IsLoaded() ? "OpenCV DNN 已加载" : "OpenCV DNN 未加载");

            EndCard();
        };

        // 13: OCR文字识别
        g_ToolUIMap[13] = [&](ToolInstance &it, int inst)
        {
            auto ResetOcrDefaults = [&it]() {
                it.ocrDetParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.param";
                it.ocrDetModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.bin";
                it.ocrRecParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.param";
                it.ocrRecModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.bin";
                it.ocrDictionaryPath = "models\\ppocrv6\\ppocr_keys_v6_tiny.txt";
            };
            if (it.ocrDetParamPath.empty() || it.ocrDetModelPath.empty() ||
                it.ocrRecParamPath.empty() || it.ocrRecModelPath.empty() || it.ocrDictionaryPath.empty())
                ResetOcrDefaults();

            BeginCard("文字识别");
            if (SecondaryButton("重置参数"))
            {
                ResetOcrDefaults();
                it.ocrMinConfidence = 0.30f;
                it.ocrMaxItems = 8;
                it.ocrInputSize = 512;
                it.ocrMaxCandidates = 220;
                it.ocrMinBoxArea = 0;
                it.ocrMinBoxHeight = 0;
                it.ocrRoiPadding = 24;
                it.ocrFastMode = true;
                it.ocrDetectOnly = false;
                it.ocrUseROI = true;
            }
            if (PrimaryButton("执行文字识别"))
            {
                RunToolFromCard(inst);
            }
            DrawSearchROIControls(it, inst);

            SectionHeader("参数");
            ImGui::SliderFloat("置信度##ocr", &it.ocrMinConfidence, 0.01f, 1.0f, "%.2f");
            ImGui::SliderInt("最多文本##ocr", &it.ocrMaxItems, 1, 1000);
            ImGui::TextDisabled("提示: 文本数越大，OCR耗时越高");
            ImGui::SliderInt("最大候选##ocr", &it.ocrMaxCandidates, 1, 2000);
            ImGui::SliderInt("输入尺寸##ocr", &it.ocrInputSize, 320, 1536);
            ImGui::SliderInt("最小框面积##ocr", &it.ocrMinBoxArea, 0, 20000);
            ImGui::SliderInt("最小框高度##ocr", &it.ocrMinBoxHeight, 0, 120);
            ImGui::SliderInt("ROI扩边##ocr", &it.ocrRoiPadding, 0, 256);
            if (ImGui::BeginTable("##ocr_options", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("快速模式##ocr", &it.ocrFastMode);
                ImGui::TableNextColumn();
                ImGui::Checkbox("只检测##ocr", &it.ocrDetectOnly);
                ImGui::TableNextColumn();
                ImGui::Checkbox("使用ROI##ocr", &it.ocrUseROI);
                ImGui::EndTable();
            }
            ImGui::TextDisabled("模型: 默认 PP-OCRv6 tiny");
            ImGui::TextDisabled("状态: NCNN OCR接口已接入，未启用依赖时会提示");
            EndCard();
        };

        // 16: 图像差分
        g_ToolUIMap[16] = [&](ToolInstance &it, int inst)
        {
            BeginCard("图像差分");
            if (PrimaryButton("执行图像差分"))
                RunToolFromCard(inst);
            if (it.differenceReferenceImage.empty())
            {
                ImGui::TextDisabled("尚未设置参考图");
                if (SecondaryButton("从当前图抓取参考图"))
                {
                    if (ToolAssetService::CaptureCurrentImage(
                        it, ToolAssetKind::DifferenceReference).success)
                    {
                        MarkCurrentRecipeAssetsDirty();
                    }
                }
            }
            else
            {
                ImGui::Text("参考图: %dx%d", it.differenceReferenceImage.cols,
                    it.differenceReferenceImage.rows);
                if (SecondaryButton("更新参考图"))
                {
                    if (ToolAssetService::CaptureCurrentImage(
                        it, ToolAssetKind::DifferenceReference).success)
                    {
                        MarkCurrentRecipeAssetsDirty();
                    }
                }
                if (SecondaryButton("清除参考图"))
                {
                    ToolAssetService::ClearAsset(it, ToolAssetKind::DifferenceReference);
                    MarkCurrentRecipeAssetsDirty();
                }
            }
            DrawSearchROIControls(it, inst);
            SectionHeader("参数");
            ImGui::SliderInt("差异阈值", &it.differenceThreshold, 0, 255);
            ImGui::SliderInt("最小差异面积", &it.differenceMinArea, 1, 100000);
            ImGui::SliderInt("预模糊", &it.differenceBlurSize, 0, 15);
            ImGui::SliderInt("形态学核", &it.differenceMorphKernelSize, 1, 15);
            ImGui::SliderInt("形态学迭代", &it.differenceMorphIterations, 1, 10);
            ImGui::Checkbox("反相差分", &it.differenceInvert);
            EndCard();
        };

        // 14: 二维码/条码识别
        g_ToolUIMap[14] = [&](ToolInstance &it, int inst)
        {
            BeginCard("二维码/条码识别");
            if (SecondaryButton("重置参数"))
            {
                it.qrUseROI = true;
                it.qrDetectMulti = true;
                it.qrEnhance = true;
                it.qrMinSize = 24;
                it.showResultLabels = true;
                it.qrEngine = 0;
                it.qrFormatMask = BarcodeFormatAll;
                it.qrFilterDuplicates = true;
            }
            if (PrimaryButton("执行二维码/条码识别"))
                RunToolFromCard(inst);
            DrawSearchROIControls(it, inst);

            SectionHeader("参数");
            const char *engines[] = {"自动(ZXing优先)", "OpenCV", "ZXing-cpp"};
            it.qrEngine = std::clamp(it.qrEngine, 0, 2);
            ImGui::Combo("识别引擎##qr", &it.qrEngine, engines, IM_ARRAYSIZE(engines));
            if (ImGui::BeginTable("##qr_options", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("使用ROI##qr", &it.qrUseROI);
                ImGui::TableNextColumn();
                ImGui::Checkbox("识别多个##qr", &it.qrDetectMulti);
                ImGui::TableNextColumn();
                ImGui::Checkbox("增强识别##qr", &it.qrEnhance);
                ImGui::TableNextColumn();
                ImGui::Checkbox("过滤重复码##qr", &it.qrFilterDuplicates);
                ImGui::EndTable();
            }
            ImGui::SliderInt("最小尺寸##qr", &it.qrMinSize, 8, 512);
            ImGui::TextDisabled("码制过滤");
            auto FormatCheckbox = [&](const char* label, std::uint32_t flag)
            {
                bool selected = (it.qrFormatMask & flag) != 0;
                if (ImGui::Checkbox(label, &selected))
                {
                    if (selected)
                        it.qrFormatMask |= flag;
                    else
                        it.qrFormatMask &= ~flag;
                }
            };
            if (ImGui::BeginTable("##qr_formats", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                FormatCheckbox("QR Code##qrFmt", BarcodeFormatQR);
                ImGui::TableNextColumn();
                FormatCheckbox("Code128##qrFmt", BarcodeFormatCode128);
                ImGui::TableNextColumn();
                FormatCheckbox("EAN##qrFmt", BarcodeFormatEAN);
                ImGui::TableNextColumn();
                FormatCheckbox("Data Matrix##qrFmt", BarcodeFormatDataMatrix);
                ImGui::TableNextColumn();
                FormatCheckbox("PDF417##qrFmt", BarcodeFormatPDF417);
                ImGui::EndTable();
            }
            if (it.qrFormatMask == 0)
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "请至少选择一种码制");
            if (it.qrEngine == 1 && (it.qrFormatMask & ~BarcodeFormatQR) != 0)
                ImGui::TextDisabled("OpenCV 引擎仅支持 QR，其他码制请选自动或 ZXing-cpp");
            ImGui::TextDisabled("解码内容条件在卡片公共“合格判定”中配置");
            ImGui::TextDisabled("文字显示由卡片顶部的“结果标签”统一控制");

            EndCard();
        };

        // 15: 工业测量
        g_ToolUIMap[15] = [&](ToolInstance &it, int inst)
        {
            BeginCard("工业测量");
            SyncMeasurementRuntimeROIs(it);
            auto StartMeasurementROIDrawing = [&](bool preserveExisting = false)
            {
                if (preserveExisting && !it.searchROIs.empty())
                {
                    // 兼容旧配方：旧 ROI 没有 runtimeId 或未恢复到 ROIState 时，
                    // 先按几何位置重新挂回当前 ROI 列表。
                    ToolROIService::RestoreMeasurementROIs(it);
                }
                s_MeasurementROIModifying = preserveExisting;
                s_MeasurementROIPendingBackup = preserveExisting ? it.searchROIs : std::vector<ROI>{};
                s_MeasurementROIDrawOwner = inst;
                if (preserveExisting && !it.searchROIs.empty())
                {
                    UI::CancelROIDrawSequence();
                    UI::gCurrentROIType = it.searchROIs.front().type;
                    ToolROIService::SelectMeasurementROI(it);
                    UI::gDrawingROI = false;
                }
                else
                {
                    RemoveMeasurementRuntimeROIs(it);
                    BeginMeasurementROIDrawSequence(it.measureMode);
                }
            };
            if (SecondaryButton("重置参数"))
            {
                it.measureMode = 0;
                it.measureCaliperCount = 16;
                it.measureSearchLength = 30.0f;
                it.measureProjectionWidth = 5.0f;
                it.measureSmoothingSigma = 1.0f;
                it.measureEdgeThreshold = 12.0f;
                it.measureMinPairDistance = 3.0f;
                it.measureEdgePolarity = 0;
                it.measureSubpixel = true;
                it.measureFitMethod = 1;
                it.measureFitInlierThreshold = 1.5f;
                it.measureMinimumValidCalipers = 3;
                it.measureMinimumConfidence = 0.0f;
                it.measureMmPerPixel = 0.0f;
                it.measureCalibrationPixels = 100.0f;
                it.measureCalibrationMm = 10.0f;
                it.measureCalibration = CalibrationModel{};
                it.measureToleranceEnabled = false;
                it.measureNominal = 0.0f;
                it.measureToleranceMinus = 0.0f;
                it.measureTolerancePlus = 0.0f;
                StartMeasurementROIDrawing();
            }
            if (PrimaryButton("执行测量"))
                RunToolFromCard(inst);

            SectionHeader("测量参数");
            const char* modes[] = {
                "点点距离", "边缘对/宽度卡尺", "线线角度", "圆拟合/直径",
                "边缘点卡尺", "直线拟合", "点线距离", "线线距离"
            };
            it.measureMode = std::clamp(it.measureMode, 0, 7);
            if (ImGui::Combo("测量类型##measure", &it.measureMode, modes, IM_ARRAYSIZE(modes)))
            {
                StartMeasurementROIDrawing();
                SaveCurrentRecipe();
            }

            SectionHeader("测量 ROI");
            if (SecondaryButton("按当前测量类型绘制 ROI"))
                StartMeasurementROIDrawing();

            if (s_MeasurementROIDrawOwner == inst)
            {
                std::vector<ROI> completedROIs;
                if (ConsumeCompletedROIDrawSequence(completedROIs))
                {
                    it.searchROIs = std::move(completedROIs);
                    it.lineSaveROIs = it.searchROIs;
                    it.useSearchROI = true;
                    it.measureRuntimeROIIds.clear();
                    it.measureRuntimeROIIds.reserve(it.searchROIs.size());
                    for (const ROI& roi : it.searchROIs)
                        it.measureRuntimeROIIds.push_back(roi.runtimeId);
                    s_MeasurementROIPendingBackup.clear();
                    s_MeasurementROIModifying = false;
                    s_MeasurementROIDrawOwner = -1;
                    SaveCurrentRecipe();
                }
            }

            if (s_MeasurementROIDrawOwner == inst && s_MeasurementROIModifying)
            {
                ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f),
                    "请在图像中拖动 ROI 控制点或中心位置");
                if (PrimaryButton("完成修改##measurement_roi_apply"))
                {
                    SyncMeasurementRuntimeROIs(it);
                    s_MeasurementROIPendingBackup.clear();
                    s_MeasurementROIModifying = false;
                    s_MeasurementROIDrawOwner = -1;
                    ROIState::SetSelectedIndex(-1);
                    SaveCurrentRecipe();
                }
                if (SecondaryButton("取消修改##measurement_roi_cancel"))
                {
                    ToolROIService::RestoreMeasurementROIBackup(it, s_MeasurementROIPendingBackup);
                    s_MeasurementROIPendingBackup.clear();
                    s_MeasurementROIModifying = false;
                    s_MeasurementROIDrawOwner = -1;
                    ROIState::SetSelectedIndex(-1);
                    SaveCurrentRecipe();
                }
            }
            else if (s_MeasurementROIDrawOwner == inst && IsROIDrawSequenceActive())
            {
                const int step = ROIDrawSequenceStep();
                const int count = ROIDrawSequenceCount();
                ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f),
                    "绘制 %d/%d: %s", step + 1, count, ROITypeDisplayName(gCurrentROIType));
            }
            else if (!it.searchROIs.empty())
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                    "已绑定 %zu 个测量 ROI", it.searchROIs.size());

                // 状态文字单独占一行，避免窄侧栏把修改按钮裁掉。
                if (ImGui::BeginTable("##measurement_roi_actions", 2,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableNextColumn();
                    if (SecondaryButton("修改测量 ROI##measurement_roi_edit", -1.0f))
                        StartMeasurementROIDrawing(true);
                    ImGui::TableNextColumn();
                    if (SecondaryButton("清除##measurement_roi_clear", -1.0f))
                    {
                        RemoveMeasurementRuntimeROIs(it);
                        SaveCurrentRecipe();
                    }
                    ImGui::EndTable();
                }
            }
            else
            {
                ImGui::TextDisabled("未绑定测量 ROI");
            }

            const bool usesCaliper = it.measureMode == 1 || it.measureMode == 3 ||
                it.measureMode == 4 || it.measureMode == 5;
            if (usesCaliper)
            {
                SectionHeader("卡尺与拟合");
                ImGui::SliderInt("卡尺数量##measure", &it.measureCaliperCount, 1, 128);
                ImGui::InputFloat("搜索长度(px)##measure", &it.measureSearchLength, 1.0f, 5.0f, "%.2f");
                ImGui::InputFloat("投影宽度(px)##measure", &it.measureProjectionWidth, 1.0f, 5.0f, "%.2f");
                ImGui::InputFloat("平滑 Sigma##measure", &it.measureSmoothingSigma, 0.1f, 0.5f, "%.2f");
                ImGui::InputFloat("边缘阈值##measure", &it.measureEdgeThreshold, 1.0f, 5.0f, "%.2f");
                const char* polarities[] = {"任意极性", "暗到明", "明到暗"};
                it.measureEdgePolarity = std::clamp(it.measureEdgePolarity, 0, 2);
                ImGui::Combo("边缘极性##measure", &it.measureEdgePolarity,
                    polarities, IM_ARRAYSIZE(polarities));
                ImGui::Checkbox("亚像素插值##measure", &it.measureSubpixel);
                if (it.measureMode == 1)
                    ImGui::InputFloat("最小边缘间距##measure", &it.measureMinPairDistance, 0.5f, 2.0f, "%.2f");
                if (it.measureMode == 3 || it.measureMode == 5)
                {
                    const char* fitMethods[] = {"最小二乘", "RANSAC"};
                    it.measureFitMethod = std::clamp(it.measureFitMethod, 0, 1);
                    ImGui::Combo("拟合方法##measure", &it.measureFitMethod,
                        fitMethods, IM_ARRAYSIZE(fitMethods));
                    ImGui::InputFloat("内点阈值(px)##measure", &it.measureFitInlierThreshold,
                        0.1f, 0.5f, "%.2f");
                }
                ImGui::SliderInt("最少有效卡尺##measure", &it.measureMinimumValidCalipers, 1, 128);
                ImGui::SliderFloat("最低可信度##measure", &it.measureMinimumConfidence, 0.0f, 1.0f, "%.3f");
                it.measureCaliperCount = std::clamp(it.measureCaliperCount, 1, 128);
                it.measureSearchLength = (std::max)(1.0f, it.measureSearchLength);
                it.measureProjectionWidth = (std::max)(1.0f, it.measureProjectionWidth);
                it.measureSmoothingSigma = (std::max)(0.0f, it.measureSmoothingSigma);
                it.measureEdgeThreshold = (std::max)(0.0f, it.measureEdgeThreshold);
                it.measureMinPairDistance = (std::max)(0.0f, it.measureMinPairDistance);
                it.measureFitInlierThreshold = (std::max)(0.1f, it.measureFitInlierThreshold);
                it.measureMinimumValidCalipers = std::clamp(it.measureMinimumValidCalipers, 1, 128);
            }

            SectionHeader("完整标定");
            ImGui::Checkbox("启用世界坐标(mm)##measure", &it.measureCalibration.enabled);
            ImGui::InputFloat("参考像素##measure", &it.measureCalibrationPixels, 1.0f, 10.0f, "%.3f");
            ImGui::InputFloat("实际长度(mm)##measure", &it.measureCalibrationMm, 0.1f, 1.0f, "%.4f");
            if (SecondaryButton("计算 mm/px") && it.measureCalibrationPixels > 0.0f)
            {
                it.measureMmPerPixel = it.measureCalibrationMm / it.measureCalibrationPixels;
                it.measureCalibration.scaleX = it.measureMmPerPixel;
                it.measureCalibration.scaleY = it.measureMmPerPixel;
                it.measureCalibration.enabled = true;
            }
            ImGui::InputDouble("X 比例(mm/px)##measure", &it.measureCalibration.scaleX, 0.0001, 0.001, "%.8f");
            ImGui::InputDouble("Y 比例(mm/px)##measure", &it.measureCalibration.scaleY, 0.0001, 0.001, "%.8f");
            ImGui::InputDouble("像素原点 X##measure", &it.measureCalibration.pixelOrigin.x, 0.1, 1.0, "%.4f");
            ImGui::InputDouble("像素原点 Y##measure", &it.measureCalibration.pixelOrigin.y, 0.1, 1.0, "%.4f");
            ImGui::InputDouble("世界原点 X(mm)##measure", &it.measureCalibration.worldOrigin.x, 0.1, 1.0, "%.4f");
            ImGui::InputDouble("世界原点 Y(mm)##measure", &it.measureCalibration.worldOrigin.y, 0.1, 1.0, "%.4f");

            SectionHeader("多点标定向导");
            ImGui::SeparatorText("棋盘格镜头标定");
            ImGui::InputInt("内角点列数##chessboard", &it.measureChessboardColumns);
            ImGui::InputInt("内角点行数##chessboard", &it.measureChessboardRows);
            ImGui::InputFloat("方格尺寸(mm)##chessboard",
                &it.measureChessboardSquareSize, 0.1f, 1.0f, "%.4f");
            it.measureChessboardColumns = std::clamp(it.measureChessboardColumns, 2, 64);
            it.measureChessboardRows = std::clamp(it.measureChessboardRows, 2, 64);
            it.measureChessboardSquareSize = (std::max)(0.0001f,
                it.measureChessboardSquareSize);
            ImGui::InputFloat("RMS验收上限(px)##chessboard",
                &it.measureCalibrationRmsAcceptance, 0.01f, 0.1f, "%.3f");
            ImGui::InputFloat("最大误差上限(px)##chessboard",
                &it.measureCalibrationMaxAcceptance, 0.01f, 0.1f, "%.3f");
            it.measureCalibrationRmsAcceptance = (std::max)(0.0f,
                it.measureCalibrationRmsAcceptance);
            it.measureCalibrationMaxAcceptance = (std::max)(0.0f,
                it.measureCalibrationMaxAcceptance);
            if (ImGui::BeginTable("##chessboard_capture_actions", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (SecondaryButton("采集当前标定图##chessboard_capture", -1.0f))
                {
                    const cv::Mat& image = ImageState::Current();
                    if (!image.empty())
                        it.measureChessboardImages.push_back(image.clone());
                }
                ImGui::TableNextColumn();
                if (SecondaryButton("清空标定图##chessboard_clear", -1.0f))
                {
                    it.measureChessboardImages.clear();
                    it.measureChessboardErrors.clear();
                    it.measureChessboardSuccessfulImages = 0;
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("已采集 %zu 张；建议 10-20 张不同位置和倾角",
                it.measureChessboardImages.size());
            if (SecondaryButton("执行棋盘格标定##chessboard_fit"))
            {
                const CalibrationFitResult fit = CalibrationFitter::FitChessboard(
                    it.measureChessboardImages,
                    cv::Size(it.measureChessboardColumns, it.measureChessboardRows),
                    it.measureChessboardSquareSize, it.measureCalibration,
                    it.measureCalibrationRmsAcceptance,
                    it.measureCalibrationMaxAcceptance);
                it.measureCalibrationFitMessage = fit.message;
                if (fit.success)
                {
                    it.measureCalibration = fit.model;
                    it.measureCalibrationRmsError = fit.rmsError;
                    it.measureCalibrationMaxError = fit.maxError;
                    it.measureChessboardErrors = fit.residuals;
                    it.measureChessboardSuccessfulImages = static_cast<int>(
                        fit.successfulImageCount);
                    SaveCurrentRecipe();
                }
            }
            if (!it.measureChessboardErrors.empty())
            {
                const bool accepted = it.measureCalibrationRmsError <=
                        it.measureCalibrationRmsAcceptance &&
                    it.measureCalibrationMaxError <=
                        it.measureCalibrationMaxAcceptance;
                ImGui::TextColored(accepted ? ImVec4(0.25f, 0.9f, 0.35f, 1.0f)
                                            : ImVec4(1.0f, 0.3f, 0.25f, 1.0f),
                    "%s  成功 %d/%zu  RMS %.4f px  最大 %.4f px",
                    accepted ? "PASS" : "FAIL", it.measureChessboardSuccessfulImages,
                    it.measureChessboardImages.size(), it.measureCalibrationRmsError,
                    it.measureCalibrationMaxError);
                ImGui::Text("重投影误差（像素）");
                const double maximum = (std::max)(1.0,
                    *std::max_element(it.measureChessboardErrors.begin(),
                                      it.measureChessboardErrors.end()));
                for (std::size_t errorIndex = 0;
                    errorIndex < it.measureChessboardErrors.size(); ++errorIndex)
                {
                    char overlay[64];
                    snprintf(overlay, sizeof(overlay), "图 %zu: %.4f px",
                        errorIndex + 1, it.measureChessboardErrors[errorIndex]);
                    ImGui::ProgressBar(static_cast<float>(
                        it.measureChessboardErrors[errorIndex] / maximum),
                        ImVec2(-1.0f, 0.0f), overlay);
                }
                if (SecondaryButton("导出现场验收报告##chessboard_report"))
                {
                    const std::string path = SaveFileDialogWithFilter(
                        L"标定验收报告 (*.json;*.csv)\0*.json;*.csv\0JSON (*.json)\0*.json\0CSV (*.csv)\0*.csv\0",
                        L"导出标定现场验收报告", L"json");
                    if (!path.empty() && CalibrationFitter::SaveAcceptanceReport(
                        path.c_str(), it.measureCalibration,
                        it.measureChessboardImages.size(),
                        static_cast<std::size_t>(it.measureChessboardSuccessfulImages),
                        it.measureChessboardErrors, it.measureCalibrationRmsError,
                        it.measureCalibrationMaxError,
                        it.measureCalibrationRmsAcceptance,
                        it.measureCalibrationMaxAcceptance))
                        LogSystem::Add(LOG_INFO, "标定验收报告已导出: %s", path.c_str());
                    else if (!path.empty())
                        LogSystem::Add(LOG_ERROR, "标定验收报告导出失败: %s", path.c_str());
                }
            }

            const CalibrationFitResult calibrationEvaluation = CalibrationFitter::Evaluate(
                it.measureCalibration, it.measureCalibrationSamples);
            int removeCalibrationSample = -1;
            const float calibrationTableHeight = (std::min)(190.0f,
                ImGui::GetTextLineHeightWithSpacing() *
                    (static_cast<float>(it.measureCalibrationSamples.size()) + 2.5f));
            const float calibrationTableMinWidth = ImGui::GetFontSize() * 34.0f;
            if (ImGui::BeginTable("##measure_calibration_samples", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, (std::max)(70.0f, calibrationTableHeight)),
                calibrationTableMinWidth))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
                ImGui::TableSetupColumn("像素 X");
                ImGui::TableSetupColumn("像素 Y");
                ImGui::TableSetupColumn("世界 X");
                ImGui::TableSetupColumn("世界 Y");
                ImGui::TableSetupColumn("残差", ImGuiTableColumnFlags_WidthFixed, 58.0f);
                ImGui::TableHeadersRow();
                for (size_t sampleIndex = 0;
                    sampleIndex < it.measureCalibrationSamples.size(); ++sampleIndex)
                {
                    CalibrationSample& sample = it.measureCalibrationSamples[sampleIndex];
                    ImGui::PushID(static_cast<int>(sampleIndex));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::SmallButton("X"))
                        removeCalibrationSample = static_cast<int>(sampleIndex);
                    ImGui::SetItemTooltip("删除标定点 %zu", sampleIndex + 1);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##pixel_x", &sample.pixel.x, 0.1, 1.0, "%.3f");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##pixel_y", &sample.pixel.y, 0.1, 1.0, "%.3f");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##world_x", &sample.world.x, 0.1, 1.0, "%.3f");
                    ImGui::TableSetColumnIndex(4);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##world_y", &sample.world.y, 0.1, 1.0, "%.3f");
                    ImGui::TableSetColumnIndex(5);
                    if (sampleIndex < calibrationEvaluation.residuals.size())
                        ImGui::Text("%.4f", calibrationEvaluation.residuals[sampleIndex]);
                    else
                        ImGui::TextDisabled("-");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (removeCalibrationSample >= 0)
                it.measureCalibrationSamples.erase(it.measureCalibrationSamples.begin() +
                    static_cast<std::ptrdiff_t>(removeCalibrationSample));
            if (SecondaryButton("添加标定点##measure_calibration_add"))
                it.measureCalibrationSamples.push_back({});
            if (ImGui::BeginTable("##measure_calibration_fit_actions", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (SecondaryButton("拟合X/Y比例##measure_calibration_scale", -1.0f))
                {
                    const CalibrationFitResult fit = CalibrationFitter::FitScale(it.measureCalibrationSamples);
                    it.measureCalibrationFitMessage = fit.message;
                    if (fit.success)
                    {
                        it.measureCalibration = fit.model;
                        it.measureCalibrationRmsError = fit.rmsError;
                        it.measureCalibrationMaxError = fit.maxError;
                        SaveCurrentRecipe();
                    }
                }
                ImGui::TableNextColumn();
                if (SecondaryButton("拟合透视##measure_calibration_h", -1.0f))
                {
                    const CalibrationFitResult fit = CalibrationFitter::FitHomography(it.measureCalibrationSamples);
                    it.measureCalibrationFitMessage = fit.message;
                    if (fit.success)
                    {
                        it.measureCalibration = fit.model;
                        it.measureCalibrationRmsError = fit.rmsError;
                        it.measureCalibrationMaxError = fit.maxError;
                        SaveCurrentRecipe();
                    }
                }
                ImGui::EndTable();
            }
            if (!it.measureCalibrationFitMessage.empty())
            {
                ImGui::TextDisabled("%s | RMS %.6f | 最大 %.6f",
                    it.measureCalibrationFitMessage.c_str(),
                    it.measureCalibrationRmsError,
                    it.measureCalibrationMaxError);
            }
            if (ImGui::BeginTable("##measure_calibration_file_actions", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (SecondaryButton("导入标定文件##measure_calibration_import", -1.0f))
                {
                    const std::string path = OpenFileDialogWithFilter(
                        L"标定文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0", L"导入标定文件");
                    CalibrationModel loadedModel;
                    std::vector<CalibrationSample> loadedSamples;
                    if (!path.empty() && CalibrationFitter::LoadDocument(
                        path.c_str(), loadedModel, loadedSamples))
                    {
                        it.measureCalibration = loadedModel;
                        it.measureCalibrationSamples = std::move(loadedSamples);
                        const CalibrationFitResult evaluation = CalibrationFitter::Evaluate(
                            it.measureCalibration, it.measureCalibrationSamples);
                        it.measureCalibrationRmsError = evaluation.rmsError;
                        it.measureCalibrationMaxError = evaluation.maxError;
                        it.measureCalibrationFitMessage = "标定文件已导入";
                        SaveCurrentRecipe();
                        LogSystem::Add(LOG_INFO, "工业测量: 已导入标定文件 %s", path.c_str());
                    }
                    else if (!path.empty())
                    {
                        LogSystem::Add(LOG_ERROR, "工业测量: 标定文件导入失败 %s", path.c_str());
                    }
                }
                ImGui::TableNextColumn();
                if (SecondaryButton("导出标定文件##measure_calibration_export", -1.0f))
                {
                    const std::string path = SaveFileDialogWithFilter(
                        L"标定文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0",
                        L"导出标定文件", L"json");
                    if (!path.empty() && CalibrationFitter::SaveDocument(path.c_str(),
                        it.measureCalibration, it.measureCalibrationSamples))
                    {
                        LogSystem::Add(LOG_INFO, "工业测量: 已导出标定文件 %s", path.c_str());
                    }
                    else if (!path.empty())
                    {
                        LogSystem::Add(LOG_ERROR, "工业测量: 标定文件导出失败 %s", path.c_str());
                    }
                }
                ImGui::EndTable();
            }

            ImGui::Checkbox("启用透视矩阵##measure", &it.measureCalibration.homographyEnabled);
            if (it.measureCalibration.homographyEnabled && ImGui::TreeNode("3x3 像素到世界矩阵##measure"))
            {
                if (ImGui::BeginTable("##measure_homography_matrix", 3,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                {
                    for (int row = 0; row < 3; ++row)
                    {
                        ImGui::TableNextRow();
                        for (int column = 0; column < 3; ++column)
                        {
                            ImGui::TableSetColumnIndex(column);
                            char matrixLabel[16] = {};
                            snprintf(matrixLabel, sizeof(matrixLabel), "H%d%d", row, column);
                            ImGui::TextDisabled("%s", matrixLabel);
                            ImGui::SetNextItemWidth(-1.0f);
                            ImGui::PushID(row * 3 + column);
                            ImGui::InputDouble("##matrix_value",
                                &it.measureCalibration.pixelToWorldHomography(row, column),
                                0.0001, 0.001, "%.8f");
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }

            ImGui::Checkbox("启用镜头畸变校正##measure", &it.measureCalibration.distortionEnabled);
            if (it.measureCalibration.distortionEnabled && ImGui::TreeNode("相机内参与畸变##measure"))
            {
                ImGui::InputDouble("fx##measure", &it.measureCalibration.fx, 1.0, 10.0, "%.6f");
                ImGui::InputDouble("fy##measure", &it.measureCalibration.fy, 1.0, 10.0, "%.6f");
                ImGui::InputDouble("cx##measure", &it.measureCalibration.cx, 1.0, 10.0, "%.6f");
                ImGui::InputDouble("cy##measure", &it.measureCalibration.cy, 1.0, 10.0, "%.6f");
                ImGui::InputDouble("k1##measure", &it.measureCalibration.k1, 0.0001, 0.001, "%.8f");
                ImGui::InputDouble("k2##measure", &it.measureCalibration.k2, 0.0001, 0.001, "%.8f");
                ImGui::InputDouble("p1##measure", &it.measureCalibration.p1, 0.0001, 0.001, "%.8f");
                ImGui::InputDouble("p2##measure", &it.measureCalibration.p2, 0.0001, 0.001, "%.8f");
                ImGui::InputDouble("k3##measure", &it.measureCalibration.k3, 0.0001, 0.001, "%.8f");
                ImGui::TreePop();
            }
            it.measureCalibration.scaleX = (std::max)(1.0e-12, it.measureCalibration.scaleX);
            it.measureCalibration.scaleY = (std::max)(1.0e-12, it.measureCalibration.scaleY);

            SectionHeader("公差");
            ImGui::Checkbox("启用公差##measure", &it.measureToleranceEnabled);
            if (it.measureToleranceEnabled)
            {
                ImGui::InputFloat("标称值##measure", &it.measureNominal, 0.1f, 1.0f, "%.4f");
                ImGui::InputFloat("下偏差##measure", &it.measureToleranceMinus, 0.01f, 0.1f, "%.4f");
                ImGui::InputFloat("上偏差##measure", &it.measureTolerancePlus, 0.01f, 0.1f, "%.4f");
                it.measureToleranceMinus = (std::max)(0.0f, it.measureToleranceMinus);
                it.measureTolerancePlus = (std::max)(0.0f, it.measureTolerancePlus);
            }

            EndCard();
        };

        // 17: 几何绘制
        g_ToolUIMap[17] = [&](ToolInstance &it, int inst)
        {
            BeginCard("几何绘制");
            if (SecondaryButton("重置图形"))
            {
                it.geometryDrawType = static_cast<int>(GeometryPrimitiveType::Line);
                it.geometryItems.clear();
                GeometryDrawEditor::Cancel();
                SaveCurrentRecipe();
            }
            if (PrimaryButton("执行几何绘制"))
                RunToolFromCard(inst);
            SectionHeader("图形编辑");
            if (GeometryDrawEditor::DrawToolPanel(it, inst))
                SaveCurrentRecipe();
            EndCard();
        };

        // 5: 轮廓分析
        g_ToolUIMap[5] = [&](ToolInstance &it, int inst)
        {
            BeginCard("轮廓分析");
            if (SecondaryButton("重置参数"))
            {
                it.cntUseGray = true; it.cntBlurSize = 5; it.cntThreshMode = 0; it.cntThreshValue = 128; it.cntAdaptBlock = 11;
                it.cntInvert = false; it.cntRetrMode = 0; it.cntApproxMethod = 1; it.cntMinArea = 100;
                it.cntMaxContours = 500; it.cntFilterConvex = false; it.cntApproxEps = 0.02f;
                it.cntLineThick = 2; it.showResultLabels = true; it.cntFillContours = false;
                it.cntNormalizeDirection = true; it.cntSubpixelBoundary = true;
                it.cntMatchROI = false; it.cntMatchThresh = 0.1f;
            }
            if (ContourDetector::g_ContourTimeMs > 0)
                ImGui::TextDisabled("上次: %d个 %.3fms", ContourDetector::g_ContourCount, ContourDetector::g_ContourTimeMs);
            if (PrimaryButton("执行轮廓分析"))
            {
                RunToolFromCard(inst);
            }
            DrawSearchROIControls(it, inst);
            SectionHeader("参数");
            if (ImGui::BeginTable("##contour_preprocess_options", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("灰度##c", &it.cntUseGray);
                ImGui::TableNextColumn();
                ImGui::Checkbox("反色##c", &it.cntInvert);
                ImGui::EndTable();
            }
            ImGui::SliderInt("模糊核##c", &it.cntBlurSize, 0, 20);
            const char *tm[] = {"OTSU", "固定", "自适应"};
            ImGui::Combo("二值化##c", &it.cntThreshMode, tm, 3);
            if (it.cntThreshMode == 1)
                ImGui::SliderInt("阈值##c", &it.cntThreshValue, 0, 255);
            else if (it.cntThreshMode == 2)
                ImGui::SliderInt("块大小##c", &it.cntAdaptBlock, 3, 51);
            const char *rm[] = {"EXTERNAL", "LIST", "TREE"}, *am[] = {"NONE", "SIMPLE", "Teh-Chin"};
            ImGui::Combo("检索##c", &it.cntRetrMode, rm, 3);
            ImGui::Combo("近似##c", &it.cntApproxMethod, am, 3);
            ImGui::InputFloat("最小面积##c", &it.cntMinArea, 10, 100, "%.0f");
            ImGui::SliderInt("最多##c", &it.cntMaxContours, 10, 2000);
            ImGui::Checkbox("仅凸包##c", &it.cntFilterConvex);
            ImGui::SliderFloat("精度##c", &it.cntApproxEps, 0.005f, 0.05f, "%.3f");
            ImGui::SliderInt("线宽##c", &it.cntLineThick, 1, 5);
            ImGui::Checkbox("填充##c", &it.cntFillContours);
            SectionHeader("高级");
            if (ImGui::BeginTable("##contour_advanced_options", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("统一轮廓方向##c", &it.cntNormalizeDirection);
                ImGui::TableNextColumn();
                ImGui::Checkbox("亚像素边界##c", &it.cntSubpixelBoundary);
                ImGui::EndTable();
            }
            ImGui::Checkbox("ROI模板匹配##c", &it.cntMatchROI);
            if (it.cntMatchROI)
                ImGui::SliderFloat("匹配阈值##c", &it.cntMatchThresh, 0.01f, 0.5f, "%.3f");
            EndCard();
        };

        // 6: 形状匹配
        g_ToolUIMap[6] = [&](ToolInstance &it, int inst)
        {
            BeginCard("形状匹配");
            if (SecondaryButton("重置参数"))
            {
                ToolAssetService::ClearAsset(it, ToolAssetKind::ShapeTemplate); it.shpBlurSize = 5; it.shpTplRetr = 0;
                MarkCurrentRecipeAssetsDirty();
                it.shpTplMinArea = 30; it.shpMinScore = 0.5f; it.shpShapeScore = 0.1f;
                it.shpLineThick = 2; it.shpMethod = 0; it.showResultLabels = true; it.shpMaxResults = 1;
                it.shpEnableRotation = false; it.shpRotationStart = -45;
                it.shpRotationEnd = 45; it.shpRotationStep = 5;
                it.shpTplGray = false; it.shpTplBinary = false; it.shpTplBinThresh = 128;
                it.shpTplBlur = false; it.shpTplBlurK = 5; it.shpTplInvert = false;
                it.showTemplatePreview = true;
            }
            if (ShapeMatcher::g_MatchTimeMs > 0)
                ImGui::TextDisabled("上次: %d个 %.3fms", ShapeMatcher::g_MatchCount, ShapeMatcher::g_MatchTimeMs);
            if (PrimaryButton("执行形状匹配"))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (it.shpTplImage.empty())
                    LogSystem::Add(LOG_WARN, "形状匹配: 请先冻结模板");
                else
                    RunToolFromCard(inst);
            }

            DrawSearchROIControls(it, inst);
            const int shapeCaptureROI = ToolAssetService::ActiveROIIndex(
                it.toolId, ToolAssetKind::ShapeTemplate);

            SectionHeader("模板");

            // 未设置模板 / 想修改：显示"添加ROI"按钮
            if (shapeCaptureROI < 0 && it.shpTplImage.empty())
            {
                if (SecondaryButton("添加ROI获取模板"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::ShapeTemplate);
            }
            else if (shapeCaptureROI >= 0)
            {
                // ROI 已存在，显示操作按钮
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "模板ROI已就绪");
                ImGui::TextDisabled("拖拽ROI调整位置后点击确认");

                if (PrimaryButton("确认捕获"))
                {
                    const ToolAssetCaptureResult result = ToolAssetService::ConfirmROICapture(
                        it, ToolAssetKind::ShapeTemplate);
                    if (result.success)
                    {
                        LogSystem::Add(LOG_INFO, "形状匹配: 模板已捕获 %dx%d",
                            result.bounds.width, result.bounds.height);
                        MarkCurrentRecipeAssetsDirty();
                    }
                    else
                        LogSystem::Add(LOG_WARN, "形状匹配: ROI 区域无效");
                }
                if (SecondaryButton("取消"))
                    ToolAssetService::CancelROICapture(it.toolId, ToolAssetKind::ShapeTemplate);
            }

            // ---- 第一块：按钮（修改/清除） ----
            if (!it.shpTplImage.empty())
            {
                if (shapeCaptureROI < 0)
                {
                    if (SecondaryButton("修改模板ROI"))
                        ToolAssetService::BeginROICapture(it, ToolAssetKind::ShapeTemplate);
                }
                if (SecondaryButton("清除模板"))
                {
                    ToolAssetService::ClearAsset(it, ToolAssetKind::ShapeTemplate);
                    MarkCurrentRecipeAssetsDirty();
                }
            }

            // ---- 第二块：预览渲染（独立重新检查） ----
            if (!it.shpTplImage.empty())
            {
                ImGui::Checkbox("显示预览##shp", &it.showTemplatePreview);
                if (it.showTemplatePreview)
                {
                    std::uint64_t signature = PreviewTextureCache::ImageSignature(it.shpTplImage);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplGray);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplBlur);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplBlurK);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplBinary);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplBinThresh);
                    signature = PreviewTextureCache::CombineSignature(signature, it.shpTplInvert);
                    if (PreviewTextureCache::NeedsUpdate(it.toolId, PreviewTextureKind::ShapeTemplate, signature))
                    {
                        cv::Mat preview = it.shpTplImage.clone();
                        if (it.shpTplGray && preview.channels() > 1)
                            cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                        if (it.shpTplBlur)
                            cv::GaussianBlur(preview, preview,
                                cv::Size(it.shpTplBlurK | 1, it.shpTplBlurK | 1), 0);
                        if (it.shpTplBinary)
                        {
                            if (preview.channels() > 1)
                                cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                            cv::threshold(preview, preview, it.shpTplBinThresh, 255, cv::THRESH_BINARY);
                        }
                        if (it.shpTplInvert)
                            cv::bitwise_not(preview, preview);
                        PreviewTextureCache::Queue(it.toolId, PreviewTextureKind::ShapeTemplate,
                            signature, preview, 80);
                    }

                    const PreviewTextureView preview = PreviewTextureCache::Get(
                        it.toolId, PreviewTextureKind::ShapeTemplate);
                    const ImVec2 previewSize(preview.width * 2.0f, preview.height * 2.0f);
                    if (preview.ready)
                        ImGui::Image(preview.textureId, previewSize);
                    else
                        ImGui::Dummy(previewSize);
                    ImGui::TextDisabled("模板: %dx%d", it.shpTplImage.cols, it.shpTplImage.rows);
                }
            }
            else if (shapeCaptureROI < 0)
                ImGui::TextDisabled("未设置模板");

            SectionHeader("模板预处理");
            if (ImGui::BeginTable("##shape_preprocess_options", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                ImGui::Checkbox("灰度##shp", &it.shpTplGray);
                ImGui::TableNextColumn();
                ImGui::Checkbox("模糊##shp", &it.shpTplBlur);
                ImGui::TableNextColumn();
                ImGui::Checkbox("二值化##shp", &it.shpTplBinary);
                ImGui::TableNextColumn();
                ImGui::Checkbox("反色##shp", &it.shpTplInvert);
                ImGui::EndTable();
            }
            if (it.shpTplBlur)
                ImGui::SliderInt("模糊核##shp", &it.shpTplBlurK, 1, 15);
            if (it.shpTplBinary)
                ImGui::SliderInt("阈值##shp", &it.shpTplBinThresh, 0, 255);

            SectionHeader("搜索参数");
            ImGui::SliderInt("模糊##shp_s", &it.shpBlurSize, 0, 20);
            ImGui::InputFloat("最小面积##shp", &it.shpTplMinArea, 10, 100, "%.0f");
            const char *retrNames[] = {"EXTERNAL", "LIST", "TREE"};
            ImGui::Combo("轮廓检索##shp", &it.shpTplRetr, retrNames, 3);
            ImGui::SliderFloat("匹配阈值", &it.shpMinScore, 0.1f, 1.0f, "%.3f");
            ImGui::SliderFloat("形状阈值", &it.shpShapeScore, 0.05f, 1.0f, "%.3f");
            const char *methodNames[] = {"Hu矩", "ShapeContext", "Hausdorff"};
            ImGui::Combo("方法##shp", &it.shpMethod, methodNames, 3);
            ImGui::SliderInt("线宽##shp", &it.shpLineThick, 1, 5);
            ImGui::SliderInt("最多##shp", &it.shpMaxResults, 1, 200);
            ImGui::Checkbox("启用形状模型角度搜索##shp", &it.shpEnableRotation);
            if (it.shpEnableRotation)
            {
                ImGui::SliderInt("起始角度##shp", &it.shpRotationStart, -180, 180);
                ImGui::SliderInt("结束角度##shp", &it.shpRotationEnd, -180, 180);
                ImGui::SliderInt("角度步长##shp", &it.shpRotationStep, 1, 30);
            }
            EndCard();
        };

        // 7: 直线检测
        g_ToolUIMap[7] = [&](ToolInstance &it, int inst)
        {
            BeginCard("直线检测");
            if (SecondaryButton("重置参数"))
            {
                it.lineCannyLow = 50; it.lineCannyHigh = 150;
                it.lineMinLength = 100; it.lineMaxGap = 20; it.lineMinAngle = 0; it.lineMaxAngle = 180;
                it.lineThickness = 2; it.lineMaxLines = 1;
                it.showResultLabels = true; it.lineUseROI = false;
            }
            if (LineDetector::g_LineTimeMs > 0)
                ImGui::TextDisabled("上次: %d条 %.3fms", LineDetector::g_LineCount, LineDetector::g_LineTimeMs);
            if (PrimaryButton("执行直线检测"))
            {
                RunToolFromCard(inst);
            }
            DrawSearchROIControls(it, inst);
            SectionHeader("参数");
            ImGui::SliderInt("Canny低##l", &it.lineCannyLow, 0, 255);
            ImGui::SliderInt("Canny高##l", &it.lineCannyHigh, 0, 255);
            ImGui::SliderFloat("最小线长##l", &it.lineMinLength, 10, 500);
            ImGui::SliderFloat("最大间隙##l", &it.lineMaxGap, 5, 100);
            ImGui::SliderFloat("最小角度##l", &it.lineMinAngle, 0, 180);
            ImGui::SliderFloat("最大角度##l", &it.lineMaxAngle, 0, 180);
            ImGui::SliderInt("线宽##l", &it.lineThickness, 1, 5);
            ImGui::SliderInt("最多条数##l", &it.lineMaxLines, 1, 100);
            EndCard();
        };

        // 10: 多点找色
        g_ToolUIMap[10] = [&](ToolInstance &it, int inst)
        {
            BeginCard("多点找色");

            // 兼容旧配方：只补齐本工具 ROI，不再写入全局 ROI。
            static std::unordered_map<std::uint64_t, bool> s_mcfRoiRestored;
            if (it.searchROIs.empty() && it.mcfRoiW > 0 && !s_mcfRoiRestored[it.toolId])
            {
                ROI r; r.type = ROI_TYPE_RECT;
                r.start = ImVec2((float)it.mcfRoiX, (float)it.mcfRoiY);
                r.end   = ImVec2((float)(it.mcfRoiX + it.mcfRoiW), (float)(it.mcfRoiY + it.mcfRoiH));
                it.searchROIs.push_back(r);
                it.lineSaveROIs = it.searchROIs;
                s_mcfRoiRestored[it.toolId] = true;
            }

            if (SecondaryButton("重置参数"))
            {
                ToolAssetService::ClearAsset(it, ToolAssetKind::MultiColorReference);
                MarkCurrentRecipeAssetsDirty();
                it.mcfShowPreview = true;
                it.mcfImgGray = false; it.mcfImgBinary = false; it.mcfImgBinThresh = 128;
                it.mcfUseROI = false; it.mcfMaxResults = 1;
                it.mcfMinDist = 5.0f; it.mcfCrossSize = 10; it.mcfCrossThick = 2;
                if (it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                    if (mf) { mf->points.clear(); mf->refImage.release(); }
                }
            }

            bool hasPoints = false;
            if (it.toolImpl)
            {
                auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                if (mf) hasPoints = !mf->points.empty();
            }
            if (ToolChainState::McfLastCount() > 0)
                ImGui::TextDisabled("上次匹配: %d 个", ToolChainState::McfLastCount());
            if (PrimaryButton("执行多点找色"))
            {
                if (it.mcfRefImage.empty())
                    LogSystem::Add(LOG_WARN, "请先抓取参考图");
                else if (!hasPoints)
                    LogSystem::Add(LOG_WARN, "请在参考图上点击取色（至少1个点）");
                else
                    RunToolFromCard(inst);
            }

            DrawSearchROIControls(it, inst);
            // ---- 参考图（对齐模板匹配的ROI捕获流程） ----
            SectionHeader("参考图");

            const int mcfCaptureROI = ToolAssetService::ActiveROIIndex(
                it.toolId, ToolAssetKind::MultiColorReference);

            // === 阶段1: 没有ROI也没有参考图 ===
            if (mcfCaptureROI < 0 && it.mcfRefImage.empty())
            {
                if (SecondaryButton("添加ROI获取参考图"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::MultiColorReference);
            }
            // === 阶段2: ROI已激活，等待确认 ===
            else if (mcfCaptureROI >= 0)
            {
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "参考图ROI已就绪");
                ImGui::TextDisabled("拖拽ROI调整位置后点击确认");
                if (PrimaryButton("确认捕获"))
                {
                    const ToolAssetCaptureResult result = ToolAssetService::ConfirmROICapture(
                        it, ToolAssetKind::MultiColorReference);
                    if (result.success)
                    {
                        LogSystem::Add(LOG_INFO, "多点找色: 参考图已抓取 %dx%d",
                            result.bounds.width, result.bounds.height);
                        MarkCurrentRecipeAssetsDirty();
                    }
                    else
                        LogSystem::Add(LOG_WARN, "多点找色: ROI区域无效或超出图像范围");
                }
                if (SecondaryButton("取消"))
                    ToolAssetService::CancelROICapture(it.toolId, ToolAssetKind::MultiColorReference);
            }
            // === 阶段3: 参考图已就绪 ===
            else if (!it.mcfRefImage.empty())
            {
                if (SecondaryButton("修改参考图ROI"))
                    ToolAssetService::BeginROICapture(it, ToolAssetKind::MultiColorReference);
                if (SecondaryButton("清除参考图"))
                {
                    ToolAssetService::ClearAsset(it, ToolAssetKind::MultiColorReference);
                    if (it.toolImpl)
                    {
                        auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                        if (mf) { mf->points.clear(); mf->refImage.release(); }
                    }
                    MarkCurrentRecipeAssetsDirty();
                }
            }

            // ---- 参考图预览 + 点击取色（仅当参考图存在） ----
            if (!it.mcfRefImage.empty())
            {
                if (ImGui::BeginTable("##mcf_preview_header", 2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    const char* previewAction = it.mcfShowPreview ? "隐藏预览" : "显示预览";
                    const float previewActionWidth = ImGui::CalcTextSize(previewAction).x +
                        ImGui::GetStyle().FramePadding.x * 2.0f;
                    ImGui::TableSetupColumn("##description", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthFixed,
                        previewActionWidth);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1),
                        "参考图 %dx%d", it.mcfRefImage.cols, it.mcfRefImage.rows);
                    ImGui::TableSetColumnIndex(1);
                    if (SecondaryButton(previewAction, -1.0f))
                        it.mcfShowPreview = !it.mcfShowPreview;
                    ImGui::EndTable();
                }

                if (it.mcfShowPreview)
                {
                auto ReadBgrAt = [](const cv::Mat& mat, int y, int x, uchar& b, uchar& g, uchar& r) -> bool
                {
                    if (mat.empty() || mat.depth() != CV_8U ||
                        (unsigned)x >= (unsigned)mat.cols || (unsigned)y >= (unsigned)mat.rows)
                        return false;
                    const int ch = mat.channels();
                    const uchar* p = mat.ptr<uchar>(y) + (size_t)x * ch;
                    if (ch == 1)
                    {
                        b = g = r = p[0];
                        return true;
                    }
                    if (ch >= 3)
                    {
                        b = p[0];
                        g = p[1];
                        r = p[2];
                        return true;
                    }
                    return false;
                };

                std::uint64_t signature = PreviewTextureCache::ImageSignature(it.mcfRefImage);
                signature = PreviewTextureCache::CombineSignature(signature, it.mcfImgGray);
                signature = PreviewTextureCache::CombineSignature(signature, it.mcfImgBinary);
                signature = PreviewTextureCache::CombineSignature(signature, it.mcfImgBinThresh);
                if (PreviewTextureCache::NeedsUpdate(
                    it.toolId, PreviewTextureKind::MultiColorReference, signature))
                {
                    cv::Mat preview = it.mcfRefImage.clone();
                    if (it.mcfImgGray && preview.channels() > 1)
                        cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                    if (it.mcfImgBinary)
                    {
                        if (preview.channels() > 1)
                            cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
                        cv::threshold(preview, preview, it.mcfImgBinThresh, 255, cv::THRESH_BINARY);
                    }
                    PreviewTextureCache::Queue(it.toolId, PreviewTextureKind::MultiColorReference,
                        signature, preview, 120);
                }

                const PreviewTextureView preview = PreviewTextureCache::Get(
                    it.toolId, PreviewTextureKind::MultiColorReference);
                const ImVec2 base = ImGui::GetCursorScreenPos();
                const ImVec2 previewSize(preview.width * 2.0f, preview.height * 2.0f);
                if (preview.ready)
                    ImGui::Image(preview.textureId, previewSize);
                else
                    ImGui::Dummy(previewSize);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float scaleX = preview.width > 0
                    ? previewSize.x / static_cast<float>(it.mcfRefImage.cols) : 0.0f;
                const float scaleY = preview.height > 0
                    ? previewSize.y / static_cast<float>(it.mcfRefImage.rows) : 0.0f;

                // ---- 在小图上绘制已选颜色点的红色标记 ----
                if (it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                    if (mf && !mf->points.empty())
                    {
                        float markerR = 3.0f;
                        int ax = it.mcfAnchorX, ay = it.mcfAnchorY;
                        for (int pi = 0; pi < (int)mf->points.size(); pi++)
                        {
                            const auto& pt = mf->points[pi];
                            float sx = base.x + (ax + pt.x) * scaleX;
                            float sy = base.y + (ay + pt.y) * scaleY;
                            ImU32 mkCol = (pi == 0) ? IM_COL32(255, 60, 60, 255) : IM_COL32(255, 120, 60, 255);
                            // 红色十字
                            dl->AddLine(ImVec2(sx - markerR, sy), ImVec2(sx + markerR, sy), mkCol, 1.5f);
                            dl->AddLine(ImVec2(sx, sy - markerR), ImVec2(sx, sy + markerR), mkCol, 1.5f);
                            // 编号
                            char num[8]; snprintf(num, sizeof(num), "%d", pi + 1);
                            dl->AddText(ImVec2(sx + 4, sy - 8), IM_COL32(255, 255, 100, 255), num);
                        }
                    }
                }

                // 点击取色
                if (ImGui::IsItemClicked())
                {
                    ImVec2 mouse = ImGui::GetMousePos();
                    int px = scaleX > 0.0f ? static_cast<int>((mouse.x - base.x) / scaleX) : -1;
                    int py = scaleY > 0.0f ? static_cast<int>((mouse.y - base.y) / scaleY) : -1;
                    if (px >= 0 && px < it.mcfRefImage.cols && py >= 0 && py < it.mcfRefImage.rows)
                    {
                        uchar b = 0, g = 0, r = 0;
                        if (!ReadBgrAt(it.mcfRefImage, py, px, b, g, r))
                        {
                            LogSystem::Add(LOG_WARN, "取色失败: 图像格式不支持或坐标越界");
                        }
                        else
                        {
                            if (it.mcfImgGray || it.mcfImgBinary)
                            {
                                uchar gray = cv::saturate_cast<uchar>(
                                    0.114f * b + 0.587f * g + 0.299f * r);
                                if (it.mcfImgBinary)
                                    gray = gray > it.mcfImgBinThresh ? 255 : 0;
                                b = g = r = gray;
                            }
                            ColorPoint cp;
                            cp.b = b; cp.g = g; cp.r = r;
                            cp.tolerance = 10;
                            if (!it.toolImpl) it.toolImpl = ITool::Create(10);
                            auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                            // 首点设为锚点，后续点偏移相对于锚点
                            if (mf && mf->points.empty())
                            {
                                it.mcfAnchorX = px;
                                it.mcfAnchorY = py;
                                cp.x = 0;
                                cp.y = 0;
                            }
                            else
                            {
                                cp.x = px - it.mcfAnchorX;
                                cp.y = py - it.mcfAnchorY;
                            }
                            if (mf) mf->points.push_back(cp);
                            LogSystem::Add(LOG_INFO, "取色: (%d,%d) BGR(%d,%d,%d)", cp.x, cp.y, cp.b, cp.g, cp.r);
                        }
                    }
                }
                ImGui::SetItemTooltip("点击取色 | 首点=锚点(0,0) | 后续点=相对偏移");

                // ---- 颜色点列表（色块展示） ----
                if (it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                    if (mf && !mf->points.empty())
                    {
                        ImGui::Separator();
                        ImGui::Text("已选颜色点 (%d个):", (int)mf->points.size());

                        // 全局容差滑块 — 直接控制所有点的容差 + 实时重新匹配
                        int allTol = mf->points[0].tolerance;
                        ParamLabel("统一容差");
                        if (ImGui::SliderInt("##all_tolerance", &allTol, 0, 128))
                        {
                            for (auto& pt : mf->points)
                                pt.tolerance = allTol;
                            // 实时重新执行匹配
                            if (!ImageState::Current().empty() && !it.mcfRefImage.empty() && !mf->points.empty())
                                ToolController::RequestRun(inst);
                        }

                        int removeIdx = -1;
                        for (int pi = 0; pi < (int)mf->points.size(); pi++)
                        {
                            auto& pt = mf->points[pi];
                            ImGui::PushID(pi);
                            if (ImGui::BeginTable("##color_point_header", 3,
                                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                            {
                                ImGui::TableSetupColumn("##swatch", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight());
                                ImGui::TableSetupColumn("##description", ImGuiTableColumnFlags_WidthStretch);
                                ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight());
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                const ImU32 swatch = IM_COL32(pt.r, pt.g, pt.b, 255);
                                const float swatchSize = ImGui::GetTextLineHeight();
                                const ImVec2 cp = ImGui::GetCursorScreenPos();
                                dl->AddRectFilled(cp, ImVec2(cp.x + swatchSize, cp.y + swatchSize), swatch);
                                dl->AddRect(cp, ImVec2(cp.x + swatchSize, cp.y + swatchSize),
                                    IM_COL32(255, 255, 255, 80));
                                ImGui::Dummy(ImVec2(swatchSize, swatchSize));

                                ImGui::TableSetColumnIndex(1);
                                const std::string pointLabel = pi == 0
                                    ? "锚点"
                                    : cv::format("偏移(%+d,%+d)", pt.x, pt.y);
                                ImGui::Text("%s  BGR(%d,%d,%d)", pointLabel.c_str(),
                                    pt.b, pt.g, pt.r);

                                ImGui::TableSetColumnIndex(2);
                                if (SecondaryButton("X", ImGui::GetFrameHeight()))
                                    removeIdx = pi;
                                ImGui::EndTable();
                            }
                            ParamLabel("容差");
                            if (ImGui::SliderInt("##tol", &pt.tolerance, 0, 128))
                            {
                                // 单个点容差变化也实时重新匹配
                                if (!ImageState::Current().empty() && !it.mcfRefImage.empty() && !mf->points.empty())
                                    ToolController::RequestRun(inst);
                            }
                            if (pi + 1 < static_cast<int>(mf->points.size()))
                                ImGui::Separator();
                            ImGui::PopID();
                        }
                        if (removeIdx >= 0)
                            mf->points.erase(mf->points.begin() + removeIdx);
                    }
                }
                } // mcfShowPreview
            }

            // ---- 图像预处理 ----
            SectionHeader("图像预处理");
            auto UpdatePointColors = [&]() {
                if (!it.mcfRefImage.empty() && it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl.get());
                    if (!mf || mf->points.empty()) return;
                    cv::Mat refProc = it.mcfRefImage.clone();
                    if (it.mcfImgGray && refProc.channels() > 1)
                        cv::cvtColor(refProc, refProc, cv::COLOR_BGR2GRAY);
                    if (it.mcfImgBinary)
                    {
                        if (refProc.channels() > 1) cv::cvtColor(refProc, refProc, cv::COLOR_BGR2GRAY);
                        cv::threshold(refProc, refProc, it.mcfImgBinThresh, 255, cv::THRESH_BINARY);
                    }
                    for (auto& cp : mf->points)
                    {
                        int px = cp.x + it.mcfAnchorX;
                        int py = cp.y + it.mcfAnchorY;
                        if ((unsigned)px < (unsigned)refProc.cols && (unsigned)py < (unsigned)refProc.rows)
                        {
                            const int ch = refProc.channels();
                            const uchar* p = refProc.ptr<uchar>(py) + (size_t)px * ch;
                            if (ch == 1)
                            {
                                cp.b = cp.g = cp.r = p[0];
                            }
                            else if (ch >= 3)
                            {
                                cp.b = p[0];
                                cp.g = p[1];
                                cp.r = p[2];
                            }
                        }
                    }
                }
            };
            if (ImGui::BeginTable("##mcf_preprocess_options", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("转为灰度##mcf", &it.mcfImgGray))
                    { McfApplyPreview(it.mcfImgGray, it.mcfImgBinary, it.mcfImgBinThresh, ImageState::Current()); UpdatePointColors(); }
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("二值化##mcf", &it.mcfImgBinary))
                    { McfApplyPreview(it.mcfImgGray, it.mcfImgBinary, it.mcfImgBinThresh, ImageState::Current()); UpdatePointColors(); }
                ImGui::EndTable();
            }
            if (it.mcfImgBinary && ImGui::SliderInt("阈值##mcf", &it.mcfImgBinThresh, 0, 255))
                { McfApplyPreview(it.mcfImgGray, it.mcfImgBinary, it.mcfImgBinThresh, ImageState::Current()); UpdatePointColors(); }
            ImGui::TextDisabled("预处理同时应用于主图和参考图");

            // ---- 搜索参数 ----
            SectionHeader("搜索");
            ImGui::SliderInt("最大结果数", &it.mcfMaxResults, 1, 200);
            ImGui::SliderFloat("去重距离", &it.mcfMinDist, 0, 50, "%.0fpx");
            ImGui::SliderInt("十字大小", &it.mcfCrossSize, 3, 30);
            ImGui::SliderInt("十字粗细", &it.mcfCrossThick, 1, 5);

            EndCard();
        };

        // ---- 手风琴工具列表（点击展开/收起，底部固定执行区预留空间） ----
        const ImGuiStyle& style = ImGui::GetStyle();
        const float actionButtonH = ImGui::GetFrameHeight() + 4.0f;
        const float bottomModeH = ImGui::GetFrameHeight() + 2.0f;
        const float bottomTimeH = ImGui::GetTextLineHeight();
        const float bottomLoopSettingsH = ToolController::IsLoopEnabled()
            ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
        const float bottomParallelStatusH = ImGui::GetFrameHeightWithSpacing();
        const ToolChainPreflightResult preflight = ToolChainState::Empty()
            ? ToolChainPreflightResult{}
            : ToolChainPreflight::Check(
                  ToolChainState::ReadOnlyTools(), ImageState::HasImage(),
                  ROIState::ReadOnlyItems().size());
        float preflightBlockH = 0.0f;
        if (!ToolChainState::Empty())
        {
            if (preflight.valid())
            {
                preflightBlockH = ImGui::GetTextLineHeightWithSpacing();
            }
            else
            {
                const float wrapWidth = std::max(
                    80.0f, ImGui::GetContentRegionAvail().x - style.ScrollbarSize);
                preflightBlockH = ImGui::GetFrameHeightWithSpacing();

                char summary[128]{};
                std::snprintf(summary, sizeof(summary),
                    "发现 %zu 个问题，执行前请处理：", preflight.issues.size());
                preflightBlockH += ImGui::CalcTextSize(
                    summary, nullptr, false, wrapWidth).y + style.ItemSpacing.y;

                for (const ToolChainPreflightIssue& issue : preflight.issues)
                {
                    const std::string line = issue.toolIndex >= 0
                        ? "工具 " + std::to_string(issue.toolIndex + 1) + "：" + issue.message
                        : "全局：" + issue.message;
                    preflightBlockH += ImGui::CalcTextSize(
                        line.c_str(), nullptr, false, wrapWidth).y + style.ItemSpacing.y;
                }
            }
        }
        const float bottomSeparatorH = style.ItemSpacing.y + 1.0f;
        const float bottomPaddingH = style.WindowPadding.y + 4.0f;
        const float bottomActionRowsH = actionButtonH * 2.0f + style.ItemSpacing.y;
        const float bottomH = ToolChainState::Empty()
            ? 0.0f
            : bottomActionRowsH + bottomLoopSettingsH + bottomParallelStatusH +
              bottomModeH + bottomTimeH + preflightBlockH +
              bottomSeparatorH + style.ItemSpacing.y * 5.0f + bottomPaddingH;
        ImGui::BeginChild("##ToolList", ImVec2(0, -bottomH), false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (visibleToolIndices.empty())
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 start = ImGui::GetCursorScreenPos();
            float cardH = (avail.y > 130.0f) ? 120.0f : avail.y;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.10f, 0.11f, 0.13f, 1.0f) : ImVec4(0.82f, 0.86f, 0.91f, 1.0f));
            ImU32 border = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.24f, 0.27f, 0.32f, 1.0f) : ImVec4(0.62f, 0.67f, 0.74f, 1.0f));
            drawList->AddRectFilled(start, ImVec2(start.x + avail.x, start.y + cardH), bg, 6.0f);
            drawList->AddRect(start, ImVec2(start.x + avail.x, start.y + cardH), border, 6.0f);

            const char* emptyLines[] = {
                "暂无工具",
                "点击上方 [+ 添加工具] 组成处理链。",
                "每个工具默认读取原图工具输出。"
            };
            const float lineH = ImGui::GetTextLineHeight();
            const float lineGap = 4.0f;
            const float blockH = lineH * IM_ARRAYSIZE(emptyLines) +
                lineGap * (IM_ARRAYSIZE(emptyLines) - 1);
            float lineY = start.y + (std::max)(0.0f, (cardH - blockH) * 0.5f);
            for (int line = 0; line < IM_ARRAYSIZE(emptyLines); ++line)
            {
                const float textW = ImGui::CalcTextSize(emptyLines[line]).x;
                const float lineX = start.x + (std::max)(12.0f, (avail.x - textW) * 0.5f);
                ImGui::SetCursorScreenPos(ImVec2(lineX, lineY));
                if (line == 0)
                    ImGui::TextDisabled("%s", emptyLines[line]);
                else
                    ImGui::TextUnformatted(emptyLines[line]);
                lineY += lineH + lineGap;
            }
        }
        else
        {
            const float compactSpacingY = (std::max)(1.0f, style.ItemSpacing.y * 0.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                ImVec2(style.ItemSpacing.x, compactSpacingY));
            int selectedForRemove = -1;
            int moveFrom = -1;
            int moveTo = -1;
            static int s_scrollOpenedToolToTop = -1;
            static int s_lastFollowedExecutionIndex = -1;

            float maxIndexWidth = 0.0f;
            float maxTypeWidth = 0.0f;
            for (int visiblePosition = 0;
                visiblePosition < static_cast<int>(visibleToolIndices.size());
                ++visiblePosition)
            {
                char indexText[32];
                std::snprintf(indexText, sizeof(indexText), "%d", visiblePosition + 1);
                maxIndexWidth = (std::max)(maxIndexWidth,
                    ImGui::CalcTextSize(indexText).x);

                const ToolInstance* visibleTool =
                    ToolChainState::AtReadOnly(visibleToolIndices[visiblePosition]);
                if (!visibleTool)
                    continue;
                char typeText[32];
                std::snprintf(typeText, sizeof(typeText), "#%d", visibleTool->type);
                maxTypeWidth = (std::max)(maxTypeWidth,
                    ImGui::CalcTextSize(typeText).x);
            }

            const float headerControlSize = ImGui::GetFrameHeight();
            const float headerHeight = headerControlSize + compactSpacingY;
            const float headerControlPad = style.FramePadding.x;
            const float headerLeftSlotWidth =
                headerControlSize + headerControlPad * 2.0f;
            const float headerRightSlotWidth = headerLeftSlotWidth;
            const float headerIconSize = ImGui::GetTextLineHeight();
            const float headerIconGap = style.ItemInnerSpacing.x;
            const float headerColumnGap = style.ItemInnerSpacing.x;

            const int stepToolIndex = ToolController::GetStepToolIndex();
            const bool batchExecutionActive = !ToolController::IsRuntimeMode() &&
                ToolController::GetMode() != ToolController::Mode::Idle;
            const int executionFollowIndex = batchExecutionActive
                ? ToolController::GetCurrentIndex()
                : stepToolIndex;
            if (executionFollowIndex < 0)
                s_lastFollowedExecutionIndex = -1;
            const bool executionTargetChanged = executionFollowIndex >= 0 &&
                executionFollowIndex != s_lastFollowedExecutionIndex;

            for (int visiblePosition = 0;
                visiblePosition < static_cast<int>(visibleToolIndices.size());
                ++visiblePosition)
            {
                const int inst = visibleToolIndices[visiblePosition];
                ToolInstance* listToolPtr = ToolChainState::At(inst);
                if (!listToolPtr)
                    continue;
                ToolInstance& listTool = *listToolPtr;
                int type = listTool.type;
                bool expanded = (ToolChainState::ActiveIndex() == inst && !listTool.collapsed);
                const nlohmann::json persistentStateBefore = expanded
                    ? CaptureToolPersistentState(listTool)
                    : nlohmann::json();
                float toolHeaderY = ImGui::GetCursorPosY();
                if (s_scrollOpenedToolToTop == inst && expanded)
                {
                    ImGui::SetScrollY(toolHeaderY);
                    s_scrollOpenedToolToTop = -1;
                }

                const bool batchHl = inst == executionFollowIndex;

                // ---- 卡片头部（始终可见） ----
                char cardId[32];
                snprintf(cardId, sizeof(cardId), "##toolhdr%d", inst);

                const int headerColorStackBase = ImGui::GetCurrentContext()->ColorStack.Size;
                if (expanded)
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, themeActive);
                else if (batchHl)
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, isDark
                        ? ImVec4(0.10f, 0.27f, 0.20f, 1.0f)
                        : ImVec4(0.72f, 0.86f, 0.77f, 1.0f));
                else
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, themeCardHover);

                ImGui::BeginChild(cardId, ImVec2(0.0f, headerHeight), 0,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImVec2 headerMin = ImGui::GetCursorScreenPos();

                const char *name = ToolName(type);
                for (const auto &m : g_ToolRegistry)
                    if (m.type == type) { name = m.name; break; }
                // 工具列表标题直接同步标签输入框；未设置标签时才显示原工具名。
                const std::string displayName = listTool.label.empty()
                    ? std::string(name)
                    : listTool.label;
                const std::string fullDisplayName = ToolInstanceTitle(name, listTool.label);

                char indexLabel[32];
                snprintf(indexLabel, sizeof(indexLabel), "%d", visiblePosition + 1);
                char typeLabel[32];
                snprintf(typeLabel, sizeof(typeLabel), "#%d", type);

                float childW = ImGui::GetContentRegionAvail().x;
                float childH = ImGui::GetWindowHeight();
                const float controlY = (childH - headerControlSize) * 0.5f;

                // 透明点击区避开右侧删除按钮
                ImGui::InvisibleButton(cardId,
                    ImVec2((std::max)(0.0f, childW - headerRightSlotWidth), childH));
                const bool headerHovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    listTool.collapsed = false;
                    ToolChainState::SetActiveIndex(expanded ? -1 : inst);
                    if (!expanded)
                        s_scrollOpenedToolToTop = inst;
                }
                if (ImGui::BeginPopupContextItem(cardId)) {
                    const int firstMovable = ToolChainState::FirstMovableIndex();
                    const bool canMove = inst >= firstMovable;
                    const bool canMoveUp = canMove && visiblePosition > 0 &&
                        visibleToolIndices[visiblePosition - 1] >= firstMovable;
                    const bool canMoveDown = canMove &&
                        visiblePosition + 1 < static_cast<int>(visibleToolIndices.size());
                    if (ImGui::MenuItem("上移", nullptr, false, canMoveUp)) {
                        moveFrom = inst;
                        moveTo = -1;
                    }
                    if (ImGui::MenuItem("下移", nullptr, false, canMoveDown)) {
                        moveFrom = inst;
                        moveTo = 1;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("复制", "Ctrl+C"))
                        ToolChainState::CopyToolToClipboard(inst);
                    if (ImGui::MenuItem("粘贴", "Ctrl+V", false, ToolChainState::HasToolClipboard()))
                        pasteToolAfterIndex = inst;
                    ImGui::Separator();
                    const bool canRemove = inst >= 0 && inst < static_cast<int>(ToolChainState::Count());
                    if (ImGui::MenuItem("删除", nullptr, false, canRemove))
                        selectedForRemove = inst;
                    ImGui::EndPopup();
                }

                ImVec2 nameSize = ImGui::CalcTextSize(displayName.c_str());
                ImVec2 indexSize = ImGui::CalcTextSize(indexLabel);
                ImVec2 typeSize = ImGui::CalcTextSize(typeLabel);

                ImDrawList* headerDraw = ImGui::GetWindowDrawList();
                if (batchHl)
                {
                    const ImU32 stepAccent = ImGui::ColorConvertFloat4ToU32(isDark
                        ? ImVec4(0.22f, 0.92f, 0.48f, 1.0f)
                        : ImVec4(0.04f, 0.58f, 0.24f, 1.0f));
                    headerDraw->AddRectFilled(
                        headerMin,
                        ImVec2(headerMin.x + 3.0f, headerMin.y + childH),
                        stepAccent,
                        2.0f);
                    headerDraw->AddRect(
                        headerMin,
                        ImVec2(headerMin.x + childW, headerMin.y + childH),
                        stepAccent,
                        4.0f,
                        0,
                        1.5f);
                }
                ImU32 controlBorder = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.45f, 0.50f, 0.58f, 1.0f) : ImVec4(0.55f, 0.58f, 0.64f, 1.0f));
                ImU32 controlText = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.70f, 0.76f, 0.84f, 1.0f) : ImVec4(0.28f, 0.30f, 0.34f, 1.0f));

                ImVec2 arrowBoxMin(headerMin.x + headerControlPad, headerMin.y + controlY);
                ImVec2 arrowBoxMax(arrowBoxMin.x + headerControlSize,
                    arrowBoxMin.y + headerControlSize);
                headerDraw->AddRect(arrowBoxMin, arrowBoxMax, controlBorder,
                    style.FrameRounding);
                ImVec2 arrowCenter((arrowBoxMin.x + arrowBoxMax.x) * 0.5f, (arrowBoxMin.y + arrowBoxMax.y) * 0.5f);
                const float triHalf = headerControlSize * 0.20f;
                if (expanded)
                {
                    headerDraw->AddTriangleFilled(
                        ImVec2(arrowCenter.x - triHalf, arrowCenter.y - triHalf * 0.45f),
                        ImVec2(arrowCenter.x + triHalf, arrowCenter.y - triHalf * 0.45f),
                        ImVec2(arrowCenter.x, arrowCenter.y + triHalf * 0.65f),
                        controlText);
                }
                else
                {
                    headerDraw->AddTriangleFilled(
                        ImVec2(arrowCenter.x - triHalf * 0.45f, arrowCenter.y - triHalf),
                        ImVec2(arrowCenter.x - triHalf * 0.45f, arrowCenter.y + triHalf),
                        ImVec2(arrowCenter.x + triHalf * 0.65f, arrowCenter.y),
                        controlText);
                }

                // 展开按钮 | 图标 | 名称 | 序号 | 类型 | 删除按钮。
                // 序号和类型使用全列表的固定列宽，避免 #0/#12 或两位序号造成跳动。
                const float headerGroupX = headerMin.x + headerLeftSlotWidth +
                    style.FramePadding.x;
                const float headerIconY = headerMin.y + (childH - headerIconSize) * 0.5f;
                const float nameX = headerGroupX + headerIconSize + headerIconGap;
                const float metadataRightX = headerMin.x + childW -
                    headerRightSlotWidth - style.FramePadding.x;
                const float minimumNameWidth = ImGui::GetFontSize() * 3.0f;
                const float availableAfterNameStart =
                    (std::max)(0.0f, metadataRightX - nameX);
                const bool showIndexColumn = availableAfterNameStart >=
                    minimumNameWidth + headerColumnGap + maxIndexWidth;
                const bool showTypeColumn = showIndexColumn &&
                    availableAfterNameStart >= minimumNameWidth + headerColumnGap +
                        maxIndexWidth + headerColumnGap + maxTypeWidth;

                float metadataCursorX = metadataRightX;
                float typeColumnX = metadataRightX;
                if (showTypeColumn)
                {
                    typeColumnX = metadataCursorX - maxTypeWidth;
                    metadataCursorX = typeColumnX - headerColumnGap;
                }
                float indexColumnX = metadataCursorX;
                if (showIndexColumn)
                {
                    indexColumnX = metadataCursorX - maxIndexWidth;
                    metadataCursorX = indexColumnX - headerColumnGap;
                }
                const float nameMaxX = (std::max)(nameX, metadataCursorX);
                const float nameAvailableW = (std::max)(0.0f, nameMaxX - nameX);
                const bool headerTitleClipped = nameSize.x > nameAvailableW;
                std::string headerDisplayName = displayName;
                ImFontAtlasRect headerIconRect;
                if (FontManager::GetToolIconRect(type, &headerIconRect))
                {
                    headerDraw->AddImageRounded(ImGui::GetIO().Fonts->TexRef,
                        ImVec2(headerGroupX, headerIconY),
                        ImVec2(headerGroupX + headerIconSize, headerIconY + headerIconSize),
                        headerIconRect.uv0, headerIconRect.uv1,
                        IM_COL32_WHITE, 3.0f);
                }
                else
                {
                    DrawToolIcon(headerDraw, type, ImVec2(headerGroupX, headerIconY), headerIconSize, ToolAccentColor(type));
                }

                ImU32 headerTextColor = ImGui::ColorConvertFloat4ToU32(batchHl
                    ? (isDark ? ImVec4(0.25f, 0.95f, 0.45f, 1) : ImVec4(0.05f, 0.55f, 0.20f, 1))   // 绿色高亮
                    : (isDark ? ImVec4(1, 1, 1, 0.85f) : ImVec4(0.1f, 0.1f, 0.1f, 0.85f)));
                const float rowCenterY = headerMin.y + childH * 0.5f;
                const float nameY = rowCenterY - nameSize.y * 0.5f;
                headerDraw->PushClipRect(ImVec2(nameX, headerMin.y), ImVec2(nameMaxX, headerMin.y + childH), true);
                headerDraw->AddText(ImVec2(nameX, nameY), headerTextColor,
                    headerDisplayName.c_str());
                headerDraw->PopClipRect();
                if (showIndexColumn)
                {
                    const float indexX = indexColumnX + maxIndexWidth - indexSize.x;
                    headerDraw->AddText(ImVec2(indexX, rowCenterY - indexSize.y * 0.5f),
                        headerTextColor, indexLabel);
                }
                if (showTypeColumn)
                {
                    const float typeX = typeColumnX + maxTypeWidth - typeSize.x;
                    headerDraw->AddText(ImVec2(typeX, rowCenterY - typeSize.y * 0.5f),
                        headerTextColor, typeLabel);
                }

                bool removeHovered = false;
                char removeId[32];
                snprintf(removeId, sizeof(removeId), "X##remove_tool_%d", inst);
                ImVec2 removeMin(headerMin.x + childW - headerControlPad -
                    headerControlSize, headerMin.y + controlY);
                ImVec2 removeMax(removeMin.x + headerControlSize,
                    removeMin.y + headerControlSize);
                ImGui::SetCursorScreenPos(removeMin);
                ImGui::InvisibleButton(removeId,
                    ImVec2(headerControlSize, headerControlSize));
                removeHovered = ImGui::IsItemHovered();
                const bool removeClicked = ImGui::IsItemClicked();
                ImU32 removeBg = ImGui::ColorConvertFloat4ToU32(removeHovered
                    ? (isDark ? ImVec4(0.55f, 0.16f, 0.16f, 1.0f) : ImVec4(0.95f, 0.20f, 0.20f, 1.0f))
                    : (isDark ? ImVec4(0.18f, 0.20f, 0.24f, 1.0f) : ImVec4(0.90f, 0.92f, 0.95f, 1.0f)));
                ImU32 removeBorder = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.45f, 0.50f, 0.58f, 1.0f) : ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
                ImU32 removeText = ImGui::ColorConvertFloat4ToU32(removeHovered ? ImVec4(1, 1, 1, 1) : (isDark ? ImVec4(0.76f, 0.80f, 0.86f, 1) : ImVec4(0.36f, 0.38f, 0.42f, 1)));
                headerDraw->AddRectFilled(removeMin, removeMax, removeBg,
                    style.FrameRounding);
                headerDraw->AddRect(removeMin, removeMax, removeBorder,
                    style.FrameRounding);
                ImVec2 xCenter((removeMin.x + removeMax.x) * 0.5f, (removeMin.y + removeMax.y) * 0.5f);
                const float xHalf = headerControlSize * 0.20f;
                const float xThickness = (std::max)(1.3f, headerControlSize * 0.07f);
                headerDraw->AddLine(ImVec2(xCenter.x - xHalf, xCenter.y - xHalf),
                    ImVec2(xCenter.x + xHalf, xCenter.y + xHalf), removeText, xThickness);
                headerDraw->AddLine(ImVec2(xCenter.x + xHalf, xCenter.y - xHalf),
                    ImVec2(xCenter.x - xHalf, xCenter.y + xHalf), removeText, xThickness);
                if (removeClicked)
                    selectedForRemove = inst;
                if (headerHovered && !removeHovered)
                {
                    if (headerTitleClipped)
                        ImGui::SetTooltip("%s", fullDisplayName.c_str());
                }

                ImGui::EndChild();
                if (executionTargetChanged && inst == executionFollowIndex)
                {
                    ImGuiWindow* toolListWindow = ImGui::GetCurrentWindow();
                    const ImVec2 itemMin = ImGui::GetItemRectMin();
                    const ImVec2 itemMax = ImGui::GetItemRectMax();
                    if (itemMin.y < toolListWindow->ClipRect.Min.y)
                        ImGui::SetScrollHereY(0.0f);
                    else if (itemMax.y > toolListWindow->ClipRect.Max.y)
                        ImGui::SetScrollHereY(1.0f);
                    s_lastFollowedExecutionIndex = executionFollowIndex;
                }
                int headerColorStackNow = ImGui::GetCurrentContext()->ColorStack.Size;
                if (headerColorStackNow > headerColorStackBase)
                    ImGui::PopStyleColor(headerColorStackNow - headerColorStackBase);

                // ---- 展开的工具 UI（手风琴内容） ----
                if (expanded)
                {
                    if (type == 12)
                    {
                        ImGui::TextDisabled("执行时恢复本轮原图");
                    }
                    else
                    {
                        const char* inputModes[] = { "上一步原图", "上一步处理图", "原图工具输出" };
                        ParamLabel("输入");
                        char inputId[32];
                        snprintf(inputId, sizeof(inputId), "##input_%d", inst);
                        ImGui::Combo(inputId, &listTool.inputSourceMode, inputModes, IM_ARRAYSIZE(inputModes));
                    }

                    auto uiFn = g_ToolUIMap.find(type);
                    if (uiFn != g_ToolUIMap.end() && uiFn->second)
                    {
                        currentCardType = type;
                        currentCardInst = inst;
                        uiFn->second(listTool, inst);
                        currentCardType = -1;
                        currentCardInst = -1;
                    }

                    const nlohmann::json persistentStateAfter =
                        CaptureToolPersistentState(listTool);
                    if (persistentStateAfter != persistentStateBefore)
                    {
                        listTool.MarkParametersChanged();
                        MarkCurrentRecipeDirty();
                    }
                }
            }

            if (ToolChainState::ActiveIndex() >= 0)
            {
                float trailingSpace = ImGui::GetWindowHeight() - 44.0f;
                if (trailingSpace < 120.0f)
                    trailingSpace = 120.0f;
                ImGui::Dummy(ImVec2(1.0f, trailingSpace));
            }

            ImGui::PopStyleVar();

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput && ToolChainState::ActiveIndex() >= 0)
            {
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
                    ToolChainState::CopyToolToClipboard(ToolChainState::ActiveIndex());
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V) && ToolChainState::HasToolClipboard())
                    pasteToolAfterIndex = ToolChainState::ActiveIndex();
            }

            if (moveFrom >= 0 && moveTo != 0) {
                GeometryDrawEditor::Cancel();
                if (ToolChainState::MoveToolWithinTaskGroup(moveFrom, moveTo)) {
                    ToolController::OnToolChainChanged();
                    SaveCurrentRecipe();
                }
            }

            if (selectedForRemove >= 0) {
                GeometryDrawEditor::Cancel();
                if (ToolChainState::RemoveTool(selectedForRemove)) {
                    ToolController::OnToolChainChanged();
                    SaveCurrentRecipe();
                }
            }

            if (duplicateToolIndex >= 0) {
                GeometryDrawEditor::Cancel();
                int insertedIndex = -1;
                if (ToolChainState::DuplicateTool(duplicateToolIndex, &insertedIndex)) {
                    ToolController::OnToolChainChanged();
                    ToolChainState::SetActiveIndex(insertedIndex);
                    SaveCurrentRecipe();
                }
            }

            if (pasteToolAfterIndex >= 0) {
                GeometryDrawEditor::Cancel();
                int insertedIndex = -1;
                if (ToolChainState::PasteToolAfter(pasteToolAfterIndex, &insertedIndex)) {
                    ToolController::OnToolChainChanged();
                    ToolChainState::SetActiveIndex(insertedIndex);
                    SaveCurrentRecipe();
                }
            }
        }

        ImGui::EndChild();

        // ---- 底部：全部执行 / 单步 / 循环 按钮 ----
        if (!ToolChainState::Empty())
        {
            ImGui::Separator();

            if (!preflight.valid())
            {
                if (ImGui::CollapsingHeader("运行前检查", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::TextColored(ImVec4(0.92f, 0.34f, 0.20f, 1.0f),
                        "发现 %zu 个问题，执行前请处理：", preflight.issues.size());
                    for (const ToolChainPreflightIssue& issue : preflight.issues)
                    {
                        if (issue.toolIndex >= 0)
                            ImGui::TextWrapped("工具 %d：%s", issue.toolIndex + 1, issue.message.c_str());
                        else
                            ImGui::TextWrapped("全局：%s", issue.message.c_str());
                    }
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.20f, 0.70f, 0.34f, 1.0f), "运行前检查通过");
            }

            auto mode = ToolController::GetMode();
            bool running = (mode != ToolController::Mode::Idle);
            const bool loopEnabled = ToolController::IsLoopEnabled();

            auto RunActionButton = [](const char* label, const ImVec2& size, const ImVec4& base, const ImVec4& hover, const ImVec4& active) -> bool
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, base);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.95f));
                bool clicked = ImGui::Button(label, size);
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
                return clicked;
            };
            auto ModeButton = [isDark](const char* label, const ImVec2& size, bool selected, const ImVec4& selectedBase, const ImVec4& selectedHover, const ImVec4& idleBase, const ImVec4& idleHover) -> bool
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, selected ? selectedBase : idleBase);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? selectedHover : idleHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, selectedBase);
                ImGui::PushStyleColor(ImGuiCol_Text, selected
                    ? ImVec4(1.0f, 1.0f, 1.0f, 0.95f)
                    : (isDark ? ImVec4(0.78f, 0.84f, 0.90f, 0.92f) : ImVec4(0.18f, 0.25f, 0.32f, 0.90f)));
                bool clicked = ImGui::Button(label, size);
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
                return clicked;
            };

            const float bottomAvailW = ImGui::GetContentRegionAvail().x;
            const float actionGap = style.ItemSpacing.x;
            const float runW = (std::max)(0.0f, (bottomAvailW - actionGap) * 0.5f);
            const float secondaryW = (std::max)(0.0f,
                (bottomAvailW - actionGap * 2.0f) / 3.0f);

            const ImVec4 runBase = isDark ? ImVec4(0.10f, 0.40f, 0.48f, 1.0f) : ImVec4(0.12f, 0.49f, 0.57f, 1.0f);
            const ImVec4 runHover = isDark ? ImVec4(0.13f, 0.50f, 0.59f, 1.0f) : ImVec4(0.08f, 0.42f, 0.50f, 1.0f);
            const ImVec4 runActive = isDark ? ImVec4(0.08f, 0.33f, 0.40f, 1.0f) : ImVec4(0.05f, 0.35f, 0.42f, 1.0f);
            const ImVec4 subBase = isDark ? ImVec4(0.15f, 0.18f, 0.22f, 1.0f) : ImVec4(0.84f, 0.87f, 0.89f, 1.0f);
            const ImVec4 subHover = isDark ? ImVec4(0.20f, 0.27f, 0.30f, 1.0f) : ImVec4(0.76f, 0.84f, 0.86f, 1.0f);
            const ImVec4 subActive = isDark ? ImVec4(0.12f, 0.23f, 0.27f, 1.0f) : ImVec4(0.65f, 0.78f, 0.81f, 1.0f);
            const ImVec4 loopBase = loopEnabled ? (isDark ? ImVec4(0.12f, 0.42f, 0.25f, 1.0f) : ImVec4(0.28f, 0.62f, 0.38f, 1.0f)) : subBase;
            const ImVec4 loopHover = loopEnabled ? (isDark ? ImVec4(0.16f, 0.52f, 0.31f, 1.0f) : ImVec4(0.22f, 0.54f, 0.32f, 1.0f)) : subHover;
            const ImVec4 loopActive = loopEnabled ? (isDark ? ImVec4(0.09f, 0.34f, 0.20f, 1.0f) : ImVec4(0.18f, 0.47f, 0.27f, 1.0f)) : subActive;
            const bool hasCurrentTask = !s_taskGroupToolFilter.empty() &&
                ToolChainState::TaskGroupIndexByName(s_taskGroupToolFilter) >= 0;

            if (RunActionButton("全部执行", ImVec2(runW, actionButtonH),
                runBase, runHover, runActive))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else
                    ToolController::RequestRunAll(loopEnabled);
            }
            ImGui::SameLine();

            ImGui::BeginDisabled(!hasCurrentTask);
            if (RunActionButton("执行当前任务", ImVec2(runW, actionButtonH),
                runBase, runHover, runActive))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else
                    ToolController::RequestRunTaskGroup(s_taskGroupToolFilter, loopEnabled);
            }
            ImGui::EndDisabled();

            // 单步执行：整条配方与当前任务使用两个明确入口。
            int stepCur = ToolController::GetStepCursor();
            const int allStepTotal = static_cast<int>(ToolChainState::Count());
            const int currentTaskStepTotal = hasCurrentTask
                ? static_cast<int>(std::count_if(
                    ToolChainState::ReadOnlyTools().begin(),
                    ToolChainState::ReadOnlyTools().end(),
                    [](const ToolInstance& tool)
                    {
                        return tool.groupName == s_taskGroupToolFilter;
                    })) : 0;
            const bool allStepping = stepCur > 0 && !ToolController::IsStepTaskGroup();
            const bool currentTaskStepping = stepCur > 0 &&
                ToolController::IsStepTaskGroup() &&
                ToolController::GetStepTaskGroupName() == s_taskGroupToolFilter;

            if (RunActionButton(allStepping ? "全部单步中" : "全部单步",
                ImVec2(secondaryW, actionButtonH),
                allStepping ? runBase : subBase,
                allStepping ? runHover : subHover,
                allStepping ? runActive : subActive))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (allStepping && stepCur >= allStepTotal)
                    ToolController::RequestStepReset();
                else
                    ToolController::RequestStepNext();
            }
            ImGui::SameLine();

            ImGui::BeginDisabled(!hasCurrentTask || currentTaskStepTotal == 0);
            if (RunActionButton(currentTaskStepping ? "任务单步中" : "当前任务单步",
                ImVec2(secondaryW, actionButtonH),
                currentTaskStepping ? runBase : subBase,
                currentTaskStepping ? runHover : subHover,
                currentTaskStepping ? runActive : subActive))
            {
                if (ImageState::Current().empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (currentTaskStepping && stepCur >= currentTaskStepTotal)
                    ToolController::RequestStepReset();
                else
                    ToolController::RequestStepNextTaskGroup(s_taskGroupToolFilter);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();

            // 循环
            if (RunActionButton(loopEnabled ? "循环开" : "循环",
                ImVec2(secondaryW, actionButtonH), loopBase, loopHover, loopActive))
            {
                ToolController::SetLoopEnabled(!loopEnabled);
            }

            if (loopEnabled)
            {
                int loopIntervalMs = ToolController::GetLoopIntervalMs();
                ParamLabel("循环等待(ms)");
                if (ImGui::InputInt("##ToolChainLoopInterval", &loopIntervalMs, 10, 100))
                {
                    ToolController::SetLoopIntervalMs(loopIntervalMs);
                    MarkCurrentRecipeDirty();
                }
                ImGui::SetItemTooltip("每轮完成后的等待时间；0 表示立即继续");
            }

            bool taskParallel = ToolController::IsTaskParallelEnabled();
            const int enabledTaskCount = static_cast<int>(std::count_if(
                ToolChainState::ReadOnlyTaskGroups().begin(),
                ToolChainState::ReadOnlyTaskGroups().end(),
                [](const TaskGroupDefinition& group)
                {
                    return group.enabled;
                }));
            if (ImGui::BeginTable("##task_parallel_status", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                const float parallelToggleWidth = ImGui::CalcTextSize("任务并行").x +
                    ImGui::GetFrameHeight();
                ImGui::TableSetupColumn("##toggle", ImGuiTableColumnFlags_WidthFixed,
                    parallelToggleWidth);
                ImGui::TableSetupColumn("##status", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(running || loopEnabled || enabledTaskCount < 2);
                if (ImGui::Checkbox("任务并行", &taskParallel))
                    ToolController::SetTaskParallelEnabled(taskParallel);
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("最多 %d 个任务同时执行",
                    ToolController::GetTaskParallelLimit());
                ImGui::SetItemTooltip(
                    "仅用于“全部执行”；每个任务内部仍按工具顺序执行。\n"
                    "循环、单步、单任务执行保持原来的顺序模式。");
                ImGui::EndTable();
            }

            bool runtimeMode = ToolController::IsRuntimeMode();
            const float modeGap = 5.0f;
            const float modeW = (bottomAvailW - modeGap) * 0.5f;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(modeGap, ImGui::GetStyle().ItemSpacing.y));
            if (ModeButton("普通模式", ImVec2(modeW, bottomModeH), !runtimeMode, runBase, runHover, subBase, subHover))
                ToolController::SetRuntimeMode(false);
            ImGui::SameLine();
            if (ModeButton("运行模式", ImVec2(modeW, bottomModeH), runtimeMode, runBase, runHover, subBase, subHover))
                ToolController::SetRuntimeMode(true);
            ImGui::PopStyleVar();

            const float stepMs = ToolController::GetLastStepTimeMs();
            const float totalMs = ToolController::GetTotalTimeMs();
            const ImVec4 timeColor = isDark ? ImVec4(0.34f, 0.78f, 0.48f, 1.0f) : ImVec4(0.05f, 0.40f, 0.19f, 1.0f);
            const ImVec4 progressColor = isDark ? ImVec4(0.72f, 0.78f, 0.86f, 1.0f) : ImVec4(0.22f, 0.28f, 0.36f, 1.0f);
            if (running)
            {
                const float elapsedMs = ToolController::GetElapsedTimeMs();
                if (loopEnabled)
                {
                    const int waitRemainingMs = ToolController::GetLoopWaitRemainingMs();
                    if (waitRemainingMs > 0)
                    {
                        ImGui::TextColored(progressColor,
                            "第%llu轮  本轮%.1fms  等待%d/%dms",
                            static_cast<unsigned long long>(ToolController::GetLoopIteration()),
                            totalMs,
                            waitRemainingMs,
                            ToolController::GetLoopIntervalMs());
                    }
                    else
                    {
                        ImGui::TextColored(progressColor,
                            "第%llu轮  工具%d/%d  本轮%.1fms  上步%.1fms",
                            static_cast<unsigned long long>(ToolController::GetLoopIteration()),
                            ToolController::GetRunProgressCurrent(),
                            ToolController::GetRunProgressTotal(),
                            elapsedMs,
                            stepMs);
                    }
                }
                else
                {
                    ImGui::TextColored(progressColor,
                        "运行中 %d/%d | 已用 %.3fms | 上步 %.3fms",
                        ToolController::GetRunProgressCurrent(),
                        ToolController::GetRunProgressTotal(),
                        elapsedMs,
                        stepMs);
                }
            }
            else if (totalMs > 0.0f)
            {
                ImGui::TextColored(timeColor,
                    "%s: 总 %.3fms | 上步 %.3fms",
                    ToolController::WasLastRunTaskGroup()
                        ? "上次当前任务" : "上次全部执行",
                    totalMs,
                    stepMs);
            }
            else
            {
                ImGui::TextDisabled("%zu 个工具", ToolChainState::Count());
            }
        }

        ImGui::PopStyleVar(4);
        int toolsColorStackNow = ImGui::GetCurrentContext()->ColorStack.Size;
        if (toolsColorStackNow > toolsColorStackBase)
            ImGui::PopStyleColor(toolsColorStackNow - toolsColorStackBase);

        ImGui::End();
        UpdateCurrentRecipeAutoSave();
    }

} // namespace UI
