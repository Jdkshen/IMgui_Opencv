#include "../Windows_imgui.h"
#include "ToolsWindow.h"
#include "ImageViewer.h"
#include "ROIManager.h"
#include "../Core/VideoCapture.h"
#include "../Core/VisionContext.h"
#include "../Core/ToolExecutor.h"
#include "../Core/ToolController.h"
#include "../Core/ToolChainState.h"
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
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

extern cv::Mat g_FrozenTemplate; // 定义在 TemplateMatch.cpp
extern bool g_ShowTplEditor;     // 定义在 TemplateMatch.cpp

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
    if (gImage.empty())
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 helper: 请先加载图片或打开摄像头");
        return false;
    }
    if (gImage.depth() != CV_8U || (gImage.channels() != 1 && gImage.channels() != 3 && gImage.channels() != 4))
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 helper: 当前图像格式不支持 depth=%d channels=%d", gImage.depth(), gImage.channels());
        return false;
    }

    cv::Mat src = gImage.isContinuous() ? gImage : gImage.clone();
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
    if (gImage.empty())
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 server: 请先加载图片或打开摄像头");
        return false;
    }
    if (gImage.depth() != CV_8U || (gImage.channels() != 1 && gImage.channels() != 3 && gImage.channels() != 4))
    {
        LogSystem::Add(LOG_WARN, "OpenCV5 server: 当前图像格式不支持 depth=%d channels=%d", gImage.depth(), gImage.channels());
        return false;
    }

    cv::Mat src = gImage.isContinuous() ? gImage : gImage.clone();
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
        {12, "原图",     ToolCategory::Base,     "▣"},
        {0, "边缘检测", ToolCategory::Base,     "◧"},
        {3, "阈值调试", ToolCategory::Base,     "◐"},
        {8, "形态学",   ToolCategory::Base,     "▦"},
        {2, "Blob分析", ToolCategory::Base,     "●"},

        {1, "模板匹配", ToolCategory::Detection, "□"},
        {4, "YOLO检测", ToolCategory::Detection, "◎"},
        {5, "轮廓分析", ToolCategory::Detection, "◇"},
        {6, "形状匹配", ToolCategory::Detection, "△"},

        {7, "直线检测", ToolCategory::Geometry,  "▬"},

        {9, "颜色分析", ToolCategory::Analysis,  "◆"},
        {10, "多点找色", ToolCategory::Detection, "◉"},

        {11, "YOLO OpenCV 5.0", ToolCategory::Experimental, "✦"},
    };

    std::unordered_map<int, ToolUIFn> g_ToolUIMap;

    int& g_ActiveToolIndex = ToolChainState::ActiveIndexRef();
    bool& g_YoloLiveDetect = ToolChainState::YoloLiveDetectRef();
    int& g_YoloLiveInstanceIdx = ToolChainState::YoloLiveInstanceIndexRef(); // 哪个 YOLO 实例触发了实时检测
    float& g_YoloLastTimeMs = ToolChainState::YoloLastTimeMsRef();  // 上次 YOLO 推理耗时
    float& g_YoloLiveFrameMs = ToolChainState::YoloLiveFrameMsRef(); // 实时检测每帧耗时
    std::vector<ToolInstance>& g_ToolInstances = ToolChainState::Tools();

    void MoveOriginalToolToFront()
    {
        ToolChainState::MoveOriginalToolToFront();
    }

    void ShowToolsWindow()
    {
        static cv::Mat g_PersistOriginal; // 持久保存原始图
        if (!g_ShowTools)
            return;

        ImGui::Begin("功能窗口", &g_ShowTools,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const char *kCatNames[] = {"图像基础处理", "检测与识别", "几何分析", "图像分析", "实验功能"};
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

        ImGui::TextColored(isDark ? ImVec4(0.72f, 0.82f, 0.94f, 1.0f) : ImVec4(0.16f, 0.30f, 0.50f, 1.0f),
            "工具链");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu 个", g_ToolInstances.size());

        if (ImGui::Button("+ 添加工具", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddToolPopup");
        if (ImGui::BeginPopup("AddToolPopup"))
        {
            for (int c = 0; c < (int)ToolCategory::COUNT; c++)
            {
                if (ImGui::CollapsingHeader(kCatNames[c], ImGuiTreeNodeFlags_DefaultOpen))
                {
                    for (const auto &meta : g_ToolRegistry)
                    {
                        if (meta.category != (ToolCategory)c)
                            continue;
                        char itemId[32];
                        snprintf(itemId, sizeof(itemId), "##tool_%d", meta.type);
                        ImVec2 rowPos = ImGui::GetCursorScreenPos();
                        const float rowH = ImGui::GetTextLineHeightWithSpacing();
                        if (ImGui::Selectable(itemId, false, 0, ImVec2(0.0f, rowH)))
                        {
                            ToolInstance tool{};
                            tool.type = meta.type;
                            tool.label = meta.name ? meta.name : "";
                            g_ToolInstances.push_back(std::move(tool));
                            MoveOriginalToolToFront();
                            SaveCurrentRecipe();
                        }

                        ImDrawList *drawList = ImGui::GetWindowDrawList();
                        const float iconSize = 16.0f;
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

                        ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
                        drawList->AddText(ImVec2(iconX + iconSize + 8.0f, rowPos.y + (rowH - ImGui::GetFontSize()) * 0.5f), textColor, meta.name);
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

        // ---- UI 辅助：统一视觉风格 ----
        auto SectionHeader = [isDark](const char *label)
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, isDark
                ? ImVec4(0.5f, 0.7f, 1.0f, 1.0f)   // 暗色: 浅蓝
                : ImVec4(0.18f, 0.34f, 0.58f, 1.0f));  // 亮色: 柔和蓝
            ImGui::Text("[%s]", label);
            ImGui::PopStyleColor();
            ImGui::Separator();
        };
        auto PrimaryButton = [](const char *label) -> bool
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.55f, 0.85f, 1.0f));
            bool clicked = ImGui::Button(label, ImVec2(-1, 28));
            ImGui::PopStyleColor(2);
            return clicked;
        };
        auto RunToolFromCard = [](int inst) -> bool
        {
            if (gImage.empty())
            {
                LogSystem::Add(LOG_WARN, "请先加载图片");
                return false;
            }
            ToolController::RequestRun(inst);
            return true;
        };
        auto SecondaryButton = [isDark](const char *label, float w = 0) -> bool
        {
            ImGui::PushStyleColor(ImGuiCol_Button, isDark
                ? ImVec4(0.25f, 0.25f, 0.28f, 1.0f)   // 暗色: 深灰
                : ImVec4(0.52f, 0.66f, 0.82f, 1.0f));  // 亮色: 工业蓝灰
            bool clicked = ImGui::Button(label, ImVec2(w, 0));
            ImGui::PopStyleColor();
            return clicked;
        };
        auto ParamLabel = [](const char* label, float labelW = 56.0f)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(labelW);
            float itemW = ImGui::GetContentRegionAvail().x;
            if (itemW < 120.0f)
                itemW = 120.0f;
            ImGui::SetNextItemWidth(itemW);
        };

        auto RemoveROIByIndex = [&](int& roiIdx)
        {
            if (roiIdx < 0 || roiIdx >= (int)gROIs.size())
            {
                roiIdx = -1;
                return false;
            }

            gROIs.erase(gROIs.begin() + roiIdx);
            if (gSelectedROI == roiIdx) gSelectedROI = -1;
            else if (gSelectedROI > roiIdx) gSelectedROI--;
            roiIdx = -1;
            return true;
        };

        auto DrawSearchROIControls = [&](ToolInstance& it, int inst)
        {
            SectionHeader("查找区域");
            const bool hasToolROI = !it.searchROIs.empty();
            ImGui::TextDisabled(hasToolROI ? "本工具ROI: 已绑定 %zu 个" : "本工具ROI: 未绑定", it.searchROIs.size());

            struct SearchRoiState
            {
                int roiIdx = -1;
                bool hasBackup = false;
                bool backupUseROI = false;
                std::vector<ROI> backupROIs;
            };
            static std::unordered_map<int, SearchRoiState> s_searchRoiStates;
            auto& st = s_searchRoiStates[inst];
            if (st.roiIdx >= 0 && st.roiIdx >= (int)gROIs.size())
                st.roiIdx = -1;

            auto MakeSearchROI = [&it]() {
                if (!it.searchROIs.empty())
                    return it.searchROIs[0];

                ROI r;
                r.type = ROI_TYPE_RECT;
                float cx = gImage.empty() ? 200.0f : gImage.cols * 0.5f;
                float cy = gImage.empty() ? 200.0f : gImage.rows * 0.5f;
                r.start = ImVec2(cx - 80, cy - 60);
                r.end = ImVec2(cx + 80, cy + 60);
                return r;
            };

            auto RemoveEditingROI = [&]() {
                RemoveROIByIndex(st.roiIdx);
            };

            auto ApplySearchROI = [&](const ROI& roi) {
                it.searchROIs.clear();
                it.searchROIs.push_back(roi);
                it.lineSaveROIs = it.searchROIs;
                it.useSearchROI = true;
                it.yoloUseROI = true;
                it.lineUseROI = true;
                it.mcfUseROI = true;
                it.colorUseROI = true;
                cv::Rect r = roi.ToCvRect();
                it.mcfRoiX = r.x;
                it.mcfRoiY = r.y;
                it.mcfRoiW = r.width;
                it.mcfRoiH = r.height;
            };

            auto RestoreSearchROIBackup = [&]() {
                if (!st.hasBackup)
                    return;
                it.searchROIs = st.backupROIs;
                it.lineSaveROIs = st.backupROIs;
                it.useSearchROI = st.backupUseROI;
                it.yoloUseROI = st.backupUseROI;
                it.lineUseROI = st.backupUseROI;
                it.mcfUseROI = st.backupUseROI;
                it.colorUseROI = st.backupUseROI;
                if (!it.searchROIs.empty())
                {
                    cv::Rect r = it.searchROIs[0].ToCvRect();
                    it.mcfRoiX = r.x;
                    it.mcfRoiY = r.y;
                    it.mcfRoiW = r.width;
                    it.mcfRoiH = r.height;
                }
                else
                {
                    it.mcfRoiX = it.mcfRoiY = it.mcfRoiW = it.mcfRoiH = 0;
                }
                st.hasBackup = false;
                st.backupROIs.clear();
            };

            if (st.roiIdx >= 0)
            {
                ApplySearchROI(gROIs[st.roiIdx]);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "拖拽ROI后确认");
                if (PrimaryButton("确认绑定##search_roi_confirm"))
                {
                    ROI roi = gROIs[st.roiIdx];
                    cv::Rect r = roi.ToCvRect();
                    if (r.width > 0 && r.height > 0)
                    {
                        ApplySearchROI(roi);
                        RemoveEditingROI();
                        st.hasBackup = false;
                        st.backupROIs.clear();
                        LogSystem::Add(LOG_INFO, color, "查找区域: 已绑定ROI到当前工具");
                        SaveCurrentRecipe();
                    }
                    else
                    {
                        LogSystem::Add(LOG_WARN, "查找区域: ROI 区域无效");
                    }
                }
                ImGui::SameLine();
                if (SecondaryButton("取消##search_roi_cancel"))
                {
                    RemoveEditingROI();
                    RestoreSearchROIBackup();
                }
                return;
            }

            if (SecondaryButton(it.searchROIs.empty() ? "添加ROI##search_roi_add" : "修改ROI##search_roi_edit"))
            {
                st.hasBackup = true;
                st.backupUseROI = it.useSearchROI;
                st.backupROIs = it.searchROIs;
                gROIs.push_back(MakeSearchROI());
                st.roiIdx = (int)gROIs.size() - 1;
                gSelectedROI = st.roiIdx;
            }
            ImGui::SameLine();
            if (SecondaryButton("清除##search_roi_clear"))
            {
                RemoveEditingROI();
                it.searchROIs.clear();
                it.lineSaveROIs.clear();
                it.useSearchROI = false;
                it.yoloUseROI = false;
                it.lineUseROI = false;
                it.mcfUseROI = false;
                it.colorUseROI = false;
                it.mcfRoiX = it.mcfRoiY = it.mcfRoiW = it.mcfRoiH = 0;
                LogSystem::Add(LOG_INFO, color, "查找区域: 已清除当前工具ROI");
                SaveCurrentRecipe();
            }
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);

        // ---- Material You 主题色 ----
        ImVec4 themeCard      = isDark ? ImVec4(0.13f, 0.14f, 0.17f, 1.0f) : ImVec4(0.78f, 0.83f, 0.89f, 1.0f);
        ImVec4 themeCardHover = isDark ? ImVec4(0.18f, 0.20f, 0.24f, 1.0f) : ImVec4(0.68f, 0.75f, 0.84f, 1.0f);
        ImVec4 themeActive    = isDark ? ImVec4(0.18f, 0.45f, 0.80f, 0.30f) : ImVec4(0.18f, 0.52f, 0.78f, 0.28f);
        const int toolsColorStackBase = ImGui::GetCurrentContext()->ColorStack.Size;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, themeCard);

        auto ResetCardColor = [isDark]() {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, isDark
                ? ImVec4(0.13f, 0.14f, 0.17f, 1.0f)   // 暗色
                : ImVec4(0.78f, 0.83f, 0.89f, 1.0f));  // 亮色
        };

        // ---- Card 面板辅助 ----
        int currentCardType = -1;
        int currentCardInst = -1;
        auto BeginCard = [isDark, &currentCardType, &currentCardInst](const char *title, const char *icon = "")
        {
            ImGui::PushID(currentCardInst * 100 + currentCardType);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            char childId[96];
            snprintf(childId, sizeof(childId), "##tool_card_%d_%d", currentCardInst, currentCardType);
            ImGui::BeginChild(childId, ImVec2(0.0f, 0.0f),
                ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ToolInstance* cardTool = (currentCardInst >= 0 && currentCardInst < (int)g_ToolInstances.size())
                ? &g_ToolInstances[currentCardInst]
                : nullptr;
            const std::string titleText = cardTool ? ToolInstanceTitle(title, cardTool->label) : std::string(title);
            ImGui::TextColored(isDark
                ? ImVec4(0.6f, 0.8f, 1.0f, 1.0f)
                : ImVec4(0.18f, 0.34f, 0.58f, 1.0f),
                "%s%s", icon, titleText.c_str());
            const float toolMs = ToolController::GetToolTimeMs(currentCardInst);
            if (toolMs > 0.0f)
            {
                char timeText[32];
                snprintf(timeText, sizeof(timeText), "%.3fms", toolMs);
                const float textW = ImGui::CalcTextSize(timeText).x;
                const float rightX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textW;
                if (rightX > ImGui::GetCursorPosX())
                {
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(rightX);
                }
                ImGui::TextColored(isDark ? ImVec4(0.30f, 0.95f, 0.46f, 1.0f) : ImVec4(0.02f, 0.42f, 0.18f, 1.0f), "%s", timeText);
            }
            ImGui::Separator();
            if (cardTool)
            {
                static std::unordered_map<int, bool> s_labelEnabled;
                bool labelEnabled = !cardTool->label.empty() || s_labelEnabled[currentCardInst];
                if (ImGui::Checkbox("标签", &labelEnabled))
                {
                    s_labelEnabled[currentCardInst] = labelEnabled;
                    if (labelEnabled && cardTool->label.empty())
                    {
                        cardTool->label = title ? title : "";
                        SaveCurrentRecipe();
                    }
                    if (!labelEnabled)
                    {
                        cardTool->label.clear();
                        SaveCurrentRecipe();
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!labelEnabled);
                if (labelEnabled && cardTool->label.empty())
                    cardTool->label = title ? title : "";
                char labelBuf[128];
                snprintf(labelBuf, sizeof(labelBuf), "%s", cardTool->label.c_str());
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##tool_label", labelBuf, IM_ARRAYSIZE(labelBuf)))
                    cardTool->label = labelBuf;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    SaveCurrentRecipe();
                ImGui::EndDisabled();
                ImGui::Separator();
            }
            return true;
        };
        auto EndCard = []()
        {
            ImGui::EndChild();
            ImGui::PopStyleVar(3);
            ImGui::PopID();
            ImGui::Spacing();
        };

        // ---- 工具 UI 函数注册 ----
        g_ToolUIMap.clear();

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
                    LogSystem::Add(LOG_INFO, color, "Canny(%d,%d)", it.cannyLow, it.cannyHigh);
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
                it.templateImg.release();
                it.hasTemplateROI = false;
                it.templateROI = ROI();
                it.tplGray = false; it.tplBinary = false; it.tplBinThresh = 128;
                it.tplEdge = false; it.tplEdgeLow = 50; it.tplEdgeHigh = 150;
                it.imgUseGray = false; it.imgEnableThreshold = false; it.imgThreshold = 128;
                it.enableRotation = false; it.rotationStart = -45; it.rotationEnd = 45; it.rotationStep = 1;
                it.maxResults = 5; it.matchThreshold = 0.7f; it.maxImageDim = 1000; it.nmsThreshold = 0.3f; it.searchMode = 0;
                g_TMSearchMode = 0; g_TMMaxResults = 5; g_TMMaxImageDim = 1000;
                g_TMMatchThreshold = 0.7f; g_TMEnableRotation = false;
                g_TMRotationStart = -45; g_TMRotationEnd = 45; g_TMRotationStep = 1;
                g_NmsThreshold = 0.3f;
                TemplateMatch::Clear();
            }

            if (PrimaryButton("执行模板匹配"))
            {
                if (gImage.empty())
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

            struct TmState { int roiIdx = -1; };
            static std::unordered_map<int, TmState> s_tmStates;
            auto &tms = s_tmStates[inst];
            if (tms.roiIdx >= 0 && tms.roiIdx >= (int)gROIs.size()) tms.roiIdx = -1;
            auto MakeTemplateROI = [&it]() {
                ROI r; r.type = ROI_TYPE_RECT;
                if (it.hasTemplateROI)
                {
                    r = it.templateROI;
                }
                else
                {
                    float cx = gImage.empty() ? 200.0f : gImage.cols * 0.5f;
                    float cy = gImage.empty() ? 200.0f : gImage.rows * 0.5f;
                    r.start = ImVec2(cx - 60, cy - 60);
                    r.end   = ImVec2(cx + 60, cy + 60);
                }
                return r;
            };

            if (tms.roiIdx < 0 && it.templateImg.empty())
            {
                if (SecondaryButton("添加ROI获取模板"))
                {
                    gROIs.push_back(MakeTemplateROI());
                    tms.roiIdx = (int)gROIs.size() - 1;
                }
            }
            else if (tms.roiIdx >= 0)
            {
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "模板ROI已就绪");
                ImGui::TextDisabled("拖拽ROI调整位置后点击确认");
                if (PrimaryButton("确认捕获"))
                {
                    cv::Rect r = gROIs[tms.roiIdx].ToCvRect();
                    if (r.width > 0 && r.height > 0 && !gImage.empty())
                    {
                        it.templateImg = gImage(r).clone();
                        it.templateROI = gROIs[tms.roiIdx];
                        it.hasTemplateROI = true;
                        g_ShowPreview = true;
                        RemoveROIByIndex(tms.roiIdx);
                        LogSystem::Add(LOG_INFO, color, "模板匹配: 模板已抓取 %dx%d", r.width, r.height);
                        SaveCurrentRecipe();
                    }
                }
                if (SecondaryButton("取消"))
                {
                    RemoveROIByIndex(tms.roiIdx);
                }
            }
            else if (!it.templateImg.empty())
            {
                if (SecondaryButton("修改模板ROI"))
                {
                    gROIs.push_back(MakeTemplateROI());
                    tms.roiIdx = (int)gROIs.size() - 1;
                }
                if (SecondaryButton("清除模板"))
                {
                    it.templateImg.release();
                    it.hasTemplateROI = false;
                    it.templateROI = ROI();
                    SaveCurrentRecipe();
                }
            }

            if (!it.templateImg.empty())
            {
                // 显示预览开关
                ImGui::Checkbox("显示预览##tm", &g_ShowPreview);
                if (g_ShowPreview)
                {
                // 模板预览
                cv::Mat tpl = it.templateImg.clone();
                if (it.tplGray && tpl.channels() > 1) cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY);
                if (it.tplBinary) { if (tpl.channels() > 1) cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY); cv::threshold(tpl, tpl, it.tplBinThresh, 255, cv::THRESH_BINARY); }
                if (it.tplEdge) { if (tpl.channels() > 1) cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY); cv::Canny(tpl, tpl, it.tplEdgeLow, it.tplEdgeHigh); }
                int maxPx = 80; float rs = maxPx / (float)std::max(tpl.cols, tpl.rows);
                if (rs > 1.0f) rs = 1.0f;
                int dw = (int)(tpl.cols * rs), dh = (int)(tpl.rows * rs);
                if (dw < 2) dw = 2; if (dh < 2) dh = 2;
                cv::Mat small; cv::resize(tpl, small, cv::Size(dw, dh), 0, 0, cv::INTER_NEAREST);
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImVec2 base = ImGui::GetCursorScreenPos(); float step = 2.0f;
                bool isColor = !it.tplGray && !it.tplBinary && !it.tplEdge && small.channels() >= 3;
                for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) {
                    ImU32 col;
                    if (isColor) { auto &px = small.at<cv::Vec3b>(y, x); col = IM_COL32(px[2], px[1], px[0], 255); }
                    else { uchar v = (small.channels() == 1) ? small.at<uchar>(y, x) : (uchar)(small.at<cv::Vec3b>(y, x)[0] * 0.3f + small.at<cv::Vec3b>(y, x)[1] * 0.59f + small.at<cv::Vec3b>(y, x)[2] * 0.11f); col = IM_COL32(v, v, v, 255); }
                    dl->AddRectFilled(ImVec2(base.x + x * step, base.y + y * step), ImVec2(base.x + (x + 1) * step, base.y + (y + 1) * step), col);
                }
                ImGui::Dummy(ImVec2(dw * step, dh * step));
                if (ImGui::IsItemClicked())
                {
                    // 同步到全局打开模板编辑器
                    g_FrozenTemplate = it.templateImg;
                    g_TplGray = it.tplGray; g_TplBinary = it.tplBinary; g_TplBinThresh = it.tplBinThresh;
                    g_TplEdge = it.tplEdge; g_TplEdgeLow = it.tplEdgeLow; g_TplEdgeHigh = it.tplEdgeHigh;
                    g_ShowTplEditor = true;
                }
                ImGui::SetItemTooltip("点击放大编辑");
                ImGui::TextDisabled("模板: %dx%d", it.templateImg.cols, it.templateImg.rows);
                } // g_ShowPreview
            }
            else
                ImGui::TextDisabled("未抓取模板");

            // ---- 模板预处理 ----
            SectionHeader("模板预处理");
            ImGui::Checkbox("灰度##tm", &it.tplGray); ImGui::SameLine();
            ImGui::Checkbox("二值化##tm", &it.tplBinary); ImGui::SameLine();
            if (it.tplBinary)
                ImGui::SliderInt("阈值##tm", &it.tplBinThresh, 0, 255);
            ImGui::Checkbox("边缘##tm", &it.tplEdge);
            if (it.tplEdge)
            {
                ImGui::SliderInt("低##tm", &it.tplEdgeLow, 0, 255);
                ImGui::SliderInt("高##tm", &it.tplEdgeHigh, 0, 255);
            }

            // ---- 图像预处理 ----
            SectionHeader("图像预处理");
            ImGui::Checkbox("转为灰度##tm_i", &it.imgUseGray);
            ImGui::SameLine();
            ImGui::Checkbox("二值化##tm_i", &it.imgEnableThreshold);
            if (it.imgEnableThreshold)
                ImGui::SliderInt("阈值##tm_i", &it.imgThreshold, 0, 255);

            SectionHeader("旋转");
            ImGui::Checkbox("启用旋转", &it.enableRotation);
            if (it.enableRotation)
            {
                ImGui::PushItemWidth(60);
                ImGui::SliderInt("起始°", &it.rotationStart, -45, 0); ImGui::SameLine();
                ImGui::SliderInt("结束°", &it.rotationEnd, 0, 45); ImGui::SameLine();
                ImGui::SliderInt("步长°", &it.rotationStep, 1, 10);
                ImGui::PopItemWidth();
            }

            // ---- 匹配参数 ----
            SectionHeader("匹配参数");
            ImGui::SliderInt("最大结果数", &it.maxResults, 1, 100);
            ImGui::SliderFloat("匹配阈值", &it.matchThreshold, 0.0f, 1.0f, "%.3f");
            if (g_TMLastBestScore != 0.0)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(最佳:%.3f)", g_TMLastBestScore);
            }
            ImGui::SliderFloat("NMS阈值", &it.nmsThreshold, 0.0f, 1.0f, "%.3f");
            ImGui::SliderInt("匹配精度", &it.maxImageDim, 400, 2000);

            // ---- 操作 ----
            if (g_TMLastMatchTime > 0)
                ImGui::TextDisabled("上次匹配: %.3fms", g_TMLastMatchTime);
            if (SecondaryButton("清空结果"))
                TemplateMatch::Clear();

            // ---- 结果统计 ----
            int total = (int)gMatchROIs.size();
            int shown = std::min(total, it.maxResults);
            if (total > 0)
            {
                SectionHeader("结果");
                ImGui::Text("匹配结果: %d 个 | 显示: %d 个", total, shown);
                if (g_TMLastMatchTime > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(g_TMLastMatchTime < 500 ? ImVec4(0.3f, 0.8f, 0.3f, 1) : ImVec4(1, 0.7f, 0.3f, 1),
                        "(%.3fms)", g_TMLastMatchTime);
                }
                // 结果列表
                ImGui::BeginChild("##matchList", ImVec2(0, std::min(100.0f, shown * ImGui::GetTextLineHeight() + 10.0f)), true);
                for (int i = 0; i < (int)gMatchROIs.size() && i < it.maxResults; i++)
                {
                    auto &roi = gMatchROIs[i];
                    float rx = std::min(roi.start.x, roi.end.x);
                    float ry = std::min(roi.start.y, roi.end.y);
                    float rw = std::abs(roi.end.x - roi.start.x);
                    float rh = std::abs(roi.end.y - roi.start.y);
                    float ang = (i < (int)gMatchAngles.size()) ? gMatchAngles[i] : 0.0f;
                    ImGui::Text("#%d: (%.0f,%.0f) %.0fx%.0f  %.3f  %.1f°",
                        i + 1, rx, ry, rw, rh,
                        (i < (int)gMatchScores.size()) ? gMatchScores[i] : 0.0, ang);
                }
                ImGui::EndChild();
            }

            // 每帧消费待匹配标志
            if (g_PendingMatch)
            {
                g_PendingMatch = false;
                TemplateMatch::RunAsync();
            }
            TemplateMatch::CheckAsyncResult();

            EndCard();
        };

        // 2: Blob分析
        g_ToolUIMap[2] = [&](ToolInstance &it, int inst)
        {
            BeginCard("Blob分析");
            if (PrimaryButton("执行Blob分析"))
            {
                RunToolFromCard(inst);
            }
            DrawSearchROIControls(it, inst);
            SectionHeader("参数");
            ImGui::SliderInt("最小面积", &it.blobMinArea, 1, 10000);
            ImGui::SliderInt("最大面积", &it.blobMaxArea, 100, 100000);
            EndCard();
        };

        // 3: 阈值调试
        g_ToolUIMap[3] = [&](ToolInstance &it, int inst)
        {
            BeginCard("阈值调试");
            if (SecondaryButton("重置参数"))
            {
                it.dbgUseGray = false;
                it.dbgEnableBlur = false;
                it.dbgBlurSize = 5;
                it.dbgEnableThresh = false;
                it.dbgThreshold = 128;
                it.dbgEnableCanny = false;
                it.dbgCannyLow = 50;
                it.dbgCannyHigh = 150;
                gUseGray = false;
                gPipe = {};
                ThresholdTool::ApplyProcess();
            }
            if (PrimaryButton("执行处理"))
            {
                RunToolFromCard(inst);
            }
            DrawSearchROIControls(it, inst);
            SectionHeader("参数");
            ImGui::Checkbox("转为灰度", &it.dbgUseGray);
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                gUseGray = it.dbgUseGray;
                ThresholdTool::ApplyProcess();
            }
            ImGui::Checkbox("高斯模糊", &it.dbgEnableBlur);
            if (it.dbgEnableBlur && ImGui::SliderInt("模糊核", &it.dbgBlurSize, 1, 10))
            {
                gPipe.enableBlur = true;
                gPipe.blurSize = it.dbgBlurSize;
                ThresholdTool::ApplyProcess();
            }
            if (ImGui::Checkbox("二值化", &it.dbgEnableThresh))
            {
                gPipe.enableThreshold = it.dbgEnableThresh;
                gPipe.threshold = it.dbgThreshold;
                ThresholdTool::ApplyProcess();
            }
            if (it.dbgEnableThresh && ImGui::SliderInt("阈值", &it.dbgThreshold, 0, 255))
            {
                gPipe.threshold = it.dbgThreshold;
                ThresholdTool::ApplyProcess();
            }
            if (ImGui::Checkbox("Canny边缘", &it.dbgEnableCanny))
            {
                gPipe.enableCanny = it.dbgEnableCanny;
                gPipe.cannyLow = it.dbgCannyLow;
                gPipe.cannyHigh = it.dbgCannyHigh;
                ThresholdTool::ApplyProcess();
            }
            if (it.dbgEnableCanny)
            {
                if (ImGui::SliderInt("Canny低", &it.dbgCannyLow, 0, 255))
                {
                    gPipe.cannyLow = it.dbgCannyLow;
                    ThresholdTool::ApplyProcess();
                }
                if (ImGui::SliderInt("Canny高", &it.dbgCannyHigh, 0, 255))
                {
                    gPipe.cannyHigh = it.dbgCannyHigh;
                    ThresholdTool::ApplyProcess();
                }
            }
            if (gTimeTotal > 0)
                ImGui::TextDisabled("总耗时: %.3fms", gTimeTotal);
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
            const char *btnLabel = isLiveMode ? (g_YoloLiveDetect && g_YoloLiveInstanceIdx == inst ? "停止实时检测" : "开始实时检测") : "执行检测";
            bool isThisActive = (g_YoloLiveDetect && g_YoloLiveInstanceIdx == inst);
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
                            g_YoloLiveDetect = false;
                            g_YoloLiveInstanceIdx = -1;
                        }
                        else
                        {
                            g_YoloLiveDetect = true;
                            g_YoloLiveInstanceIdx = inst;
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
            if (SecondaryButton("选择 ONNX 模型"))
            {
                std::string path = OpenFileDialogWithFilter(L"ONNX模型 (*.onnx)\0*.onnx\0所有文件 (*.*)\0*.*\0", L"选择 YOLO ONNX 模型文件");
                if (!path.empty())
                    it.yoloModelPath = path;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", it.yoloModelPath.empty() ? "未选择" : FileName(it.yoloModelPath).c_str());
            if (SecondaryButton("选择类别文件"))
            {
                std::string path = OpenFileDialogWithFilter(L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0", L"选择类别名称文件");
                if (!path.empty())
                    it.yoloClassesPath = path;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", it.yoloClassesPath.empty() ? "默认 COCO 80 类" : FileName(it.yoloClassesPath).c_str());
            SectionHeader("参数");
            ImGui::SliderFloat("置信度阈值", &it.yoloConfThreshold, 0.1f, 1.0f, "%.2f");
            ImGui::SliderFloat("NMS阈值", &it.yoloNmsThreshold, 0.1f, 1.0f, "%.2f");
            ImGui::SliderFloat("滚动补偿(X)", &g_YoloOverlayOffsetX, -100.0f, 100.0f, "%.0fpx");
            ImGui::Checkbox("GPU加速(CUDA/DML)", &it.yoloUseGPU);
            SectionHeader("状态");
            ImGui::TextDisabled("实际后端: %s", YOLODetector::GetBackendName());
            if (g_YoloLastTimeMs > 0)
                ImGui::TextDisabled("上次耗时: %.3fms", g_YoloLastTimeMs);
            if (g_YoloLiveDetect && g_YoloLiveFrameMs > 0)
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "实时: %.3fms", g_YoloLiveFrameMs);
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

            const bool isOpenCV5Live = g_YoloLiveDetect && g_YoloLiveInstanceIdx == inst;
            const char* liveLabel = isOpenCV5Live ? "停止实时测试##ocv5live" : "开始实时测试##ocv5live";
            if (SecondaryButton(liveLabel))
            {
                if (isOpenCV5Live)
                {
                    g_YoloLiveDetect = false;
                    g_YoloLiveInstanceIdx = -1;
                    g_YoloLiveFrameMs = 0.0f;
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
                            g_YoloLiveDetect = true;
                            g_YoloLiveInstanceIdx = inst;
                            if (!VideoCapture::IsPlaying())
                                VideoCapture::Play();
                        }
                    }
                }
            }
            if (isOpenCV5Live && g_YoloLiveFrameMs > 0)
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "实时: %.3fms/帧", g_YoloLiveFrameMs);

            DrawSearchROIControls(it, inst);
            SectionHeader("模型");
            if (SecondaryButton("选择 ONNX 模型##ocv5"))
            {
                std::string path = OpenFileDialogWithFilter(L"ONNX模型 (*.onnx)\0*.onnx\0所有文件 (*.*)\0*.*\0", L"选择 YOLO ONNX 模型文件");
                if (!path.empty())
                    it.yoloModelPath = path;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", it.yoloModelPath.empty() ? "未选择" : FileName(it.yoloModelPath).c_str());

            if (SecondaryButton("选择类别文件##ocv5"))
            {
                std::string path = OpenFileDialogWithFilter(L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0", L"选择类别名称文件");
                if (!path.empty())
                    it.yoloClassesPath = path;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", it.yoloClassesPath.empty() ? "默认 COCO 80 类" : FileName(it.yoloClassesPath).c_str());

            SectionHeader("参数");
            ImGui::SliderFloat("置信度阈值##ocv5", &it.yoloConfThreshold, 0.1f, 1.0f, "%.2f");
            ImGui::SliderFloat("NMS阈值##ocv5", &it.yoloNmsThreshold, 0.1f, 1.0f, "%.2f");

            SectionHeader("状态");
            ImGui::TextDisabled("%s", OpenCVYoloDetector::IsLoaded() ? "OpenCV DNN 已加载" : "OpenCV DNN 未加载");
            if (OpenCVYoloDetector::g_OpenCVYoloTotalMs > 0)
            {
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "总耗时: %.3fms", OpenCVYoloDetector::g_OpenCVYoloTotalMs);
                ImGui::TextDisabled("预处理 %.3f | 推理 %.3f | 后处理 %.3f",
                    OpenCVYoloDetector::g_OpenCVYoloPreMs,
                    OpenCVYoloDetector::g_OpenCVYoloInfMs,
                    OpenCVYoloDetector::g_OpenCVYoloPostMs);
            }

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
                it.cntLineThick = 2; it.cntShowLabels = true; it.cntFillContours = false;
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
            ImGui::Checkbox("灰度##c", &it.cntUseGray);
            ImGui::SameLine();
            ImGui::Checkbox("反色##c", &it.cntInvert);
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
            ImGui::Checkbox("标签##c", &it.cntShowLabels);
            ImGui::SameLine();
            ImGui::Checkbox("填充##c", &it.cntFillContours);
            SectionHeader("高级");
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
                it.shpTplImage.release(); it.templateImg.release(); it.hasTemplateROI = false; it.templateROI = ROI(); it.shpBlurSize = 5; it.shpTplRetr = 0;
                it.shpTplMinArea = 30; it.shpMinScore = 0.5f; it.shpShapeScore = 0.1f;
                it.shpLineThick = 2; it.shpMethod = 0; it.shpShowLabels = true; it.shpMaxResults = 1;
                it.shpTplGray = false; it.shpTplBinary = false; it.shpTplBinThresh = 128;
                it.shpTplBlur = false; it.shpTplBlurK = 5; it.shpTplInvert = false;
            }
            if (ShapeMatcher::g_MatchTimeMs > 0)
                ImGui::TextDisabled("上次: %d个 %.3fms", ShapeMatcher::g_MatchCount, ShapeMatcher::g_MatchTimeMs);
            if (PrimaryButton("执行形状匹配"))
            {
                if (gImage.empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (it.shpTplImage.empty())
                    LogSystem::Add(LOG_WARN, "形状匹配: 请先冻结模板");
                else
                    RunToolFromCard(inst);
            }

            DrawSearchROIControls(it, inst);
            // ---- 模板 ROI 管理（专属此工具实例） ----
            struct ShpState {
                int roiIdx = -1;
                bool captured = false;
            };
            static std::unordered_map<int, ShpState> s_states; // key=inst
            auto &st = s_states[inst];
            auto MakeShapeTemplateROI = [&it]() {
                ROI r; r.type = ROI_TYPE_RECT;
                if (it.hasTemplateROI)
                {
                    r = it.templateROI;
                }
                else
                {
                    float cx = gImage.empty() ? 200.0f : gImage.cols * 0.5f;
                    float cy = gImage.empty() ? 200.0f : gImage.rows * 0.5f;
                    r.start = ImVec2(cx - 60, cy - 60);
                    r.end   = ImVec2(cx + 60, cy + 60);
                }
                return r;
            };

            // 清理无效 ROI 索引
            if (st.roiIdx >= 0 && st.roiIdx >= (int)gROIs.size()) st.roiIdx = -1;

            SectionHeader("模板");

            // 未设置模板 / 想修改：显示"添加ROI"按钮
            if (st.roiIdx < 0 && it.shpTplImage.empty())
            {
                if (SecondaryButton("添加ROI获取模板"))
                {
                    gROIs.push_back(MakeShapeTemplateROI());
                    st.roiIdx = (int)gROIs.size() - 1;
                    st.captured = false;
                }
            }
            else if (st.roiIdx >= 0)
            {
                // ROI 已存在，显示操作按钮
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "模板ROI已就绪");
                ImGui::TextDisabled("拖拽ROI调整位置后点击确认");

                if (PrimaryButton("确认捕获"))
                {
                    cv::Rect r = gROIs[st.roiIdx].ToCvRect();
                    if (r.width > 0 && r.height > 0 && !gImage.empty() &&
                        r.x >= 0 && r.y >= 0 &&
                        r.x + r.width <= gImage.cols && r.y + r.height <= gImage.rows)
                    {
                        it.shpTplImage = gImage(r).clone();
                        it.templateImg = it.shpTplImage.clone();
                        it.templateROI = gROIs[st.roiIdx];
                        it.hasTemplateROI = true;
                        LogSystem::Add(LOG_INFO, color, "形状匹配: 模板已捕获 %dx%d", r.width, r.height);
                        // 隐藏 ROI
                        RemoveROIByIndex(st.roiIdx);
                        st.captured = true;
                        SaveCurrentRecipe();
                    }
                    else
                        LogSystem::Add(LOG_WARN, "形状匹配: ROI 区域无效");
                }
                if (SecondaryButton("取消"))
                {
                    RemoveROIByIndex(st.roiIdx);
                }
            }

            // ---- 第一块：按钮（修改/清除） ----
            if (!it.shpTplImage.empty())
            {
                if (st.roiIdx < 0)
                {
                    if (SecondaryButton("修改模板ROI"))
                    {
                        gROIs.push_back(MakeShapeTemplateROI());
                        st.roiIdx = (int)gROIs.size() - 1;
                        st.captured = false;
                    }
                }
                if (SecondaryButton("清除模板"))
                {
                    it.shpTplImage.release();
                    it.templateImg.release();
                    st.captured = false;
                    it.hasTemplateROI = false;
                    it.templateROI = ROI();
                    SaveCurrentRecipe();
                }
            }

            // ---- 第二块：预览渲染（独立重新检查） ----
            if (!it.shpTplImage.empty())
            {
                static bool shpShowPreview = true;
                ImGui::Checkbox("显示预览##shp", &shpShowPreview);
                if (shpShowPreview)
                {
                // 缩放到适合显示，逐像素渲染（应用预处理）
                cv::Mat tpl = it.shpTplImage.clone();
                if (it.shpTplGray && tpl.channels() > 1)
                    cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY);
                if (it.shpTplBlur)
                    cv::GaussianBlur(tpl, tpl, cv::Size(it.shpTplBlurK | 1, it.shpTplBlurK | 1), 0);
                if (it.shpTplBinary)
                {
                    if (tpl.channels() > 1) cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY);
                    cv::threshold(tpl, tpl, it.shpTplBinThresh, 255, cv::THRESH_BINARY);
                }
                if (it.shpTplInvert)
                    cv::bitwise_not(tpl, tpl);
                int maxPx = 80;
                float rs = maxPx / (float)std::max(tpl.cols, tpl.rows);
                if (rs > 1.0f) rs = 1.0f;
                int dw = (int)(tpl.cols * rs), dh = (int)(tpl.rows * rs);
                if (dw < 2) dw = 2; if (dh < 2) dh = 2;
                cv::Mat small;
                cv::resize(tpl, small, cv::Size(dw, dh), 0, 0, cv::INTER_NEAREST);

                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImVec2 base = ImGui::GetCursorScreenPos();
                float step = 2.0f;
                bool isColor = (small.channels() >= 3);
                for (int y = 0; y < dh; y++)
                    for (int x = 0; x < dw; x++)
                    {
                        ImU32 col;
                        if (isColor) {
                            auto &px = small.at<cv::Vec3b>(y, x);
                            col = IM_COL32(px[2], px[1], px[0], 255);
                        } else {
                            uchar v = small.at<uchar>(y, x);
                            col = IM_COL32(v, v, v, 255);
                        }
                        dl->AddRectFilled(ImVec2(base.x + x * step, base.y + y * step),
                            ImVec2(base.x + (x + 1) * step, base.y + (y + 1) * step), col);
                    }
                ImGui::Dummy(ImVec2(dw * step, dh * step));
                ImGui::TextDisabled("模板: %dx%d", it.shpTplImage.cols, it.shpTplImage.rows);
                } // shpShowPreview
            }
            else if (!st.captured && st.roiIdx < 0)
                ImGui::TextDisabled("未设置模板");

            SectionHeader("模板预处理");
            ImGui::Checkbox("灰度##shp", &it.shpTplGray);
            ImGui::SameLine();
            ImGui::Checkbox("模糊##shp", &it.shpTplBlur);
            if (it.shpTplBlur)
                ImGui::SliderInt("模糊核##shp", &it.shpTplBlurK, 1, 15);
            ImGui::Checkbox("二值化##shp", &it.shpTplBinary);
            if (it.shpTplBinary)
                ImGui::SliderInt("阈值##shp", &it.shpTplBinThresh, 0, 255);
            ImGui::Checkbox("反色##shp", &it.shpTplInvert);

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
            ImGui::Checkbox("标签##shp", &it.shpShowLabels);

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
                it.lineShowLabels = true; it.lineUseROI = false;
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
            ImGui::Checkbox("标签##l", &it.lineShowLabels);
            EndCard();
        };

        // 8: 形态学
        g_ToolUIMap[8] = [&](ToolInstance &it, int inst)
        {
            BeginCard("形态学");
            if (SecondaryButton("重置参数"))
            {
                it.morphOpType = 0; it.morphKernelSize = 3; it.morphKernelShape = 0; it.morphIterations = 1;
                it.morphUseGray = false;
            }
            if (MorphologyTool::g_ProcTimeMs > 0)
                ImGui::TextDisabled("上次: %.3fms", MorphologyTool::g_ProcTimeMs);
            if (PrimaryButton("执行形态学##morph"))
            {
                RunToolFromCard(inst);
            }
            DrawSearchROIControls(it, inst);
            SectionHeader("参数");
            const char *morphNames[] = {"Erode 腐蚀", "Dilate 膨胀", "Open 开运算", "Close 闭运算", "Gradient 梯度", "TopHat 顶帽", "BlackHat 黑帽"};
            ParamLabel("操作");
            ImGui::Combo("##morph_op", &it.morphOpType, morphNames, 7);
            ParamLabel("核大小");
            ImGui::SliderInt("##morph_kernel_size", &it.morphKernelSize, 1, 15);
            const char *ksh[] = {"矩形", "椭圆", "十字"};
            ParamLabel("核形状");
            ImGui::Combo("##morph_kernel_shape", &it.morphKernelShape, ksh, 3);
            ParamLabel("迭代");
            ImGui::SliderInt("##morph_iterations", &it.morphIterations, 1, 10);
            ImGui::Checkbox("灰度##morph", &it.morphUseGray);
            EndCard();
        };

        // 10: 多点找色
        g_ToolUIMap[10] = [&](ToolInstance &it, int inst)
        {
            BeginCard("多点找色");

            // 兼容旧配方：只补齐本工具 ROI，不再写入全局 ROI。
            static std::unordered_map<int, bool> s_mcfRoiRestored;
            if (it.searchROIs.empty() && it.mcfRoiW > 0 && !s_mcfRoiRestored[inst])
            {
                ROI r; r.type = ROI_TYPE_RECT;
                r.start = ImVec2((float)it.mcfRoiX, (float)it.mcfRoiY);
                r.end   = ImVec2((float)(it.mcfRoiX + it.mcfRoiW), (float)(it.mcfRoiY + it.mcfRoiH));
                it.searchROIs.push_back(r);
                it.lineSaveROIs = it.searchROIs;
                s_mcfRoiRestored[inst] = true;
            }

            if (SecondaryButton("重置参数"))
            {
                it.mcfRefImage.release();
                it.mcfAnchorX = 0; it.mcfAnchorY = 0;
                it.mcfImgGray = false; it.mcfImgBinary = false; it.mcfImgBinThresh = 128;
                it.mcfUseROI = false; it.mcfMaxResults = 1;
                it.mcfMinDist = 5.0f; it.mcfCrossSize = 10; it.mcfCrossThick = 2;
                if (it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl);
                    if (mf) { mf->points.clear(); mf->refImage.release(); }
                }
            }

            bool hasPoints = false;
            if (it.toolImpl)
            {
                auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl);
                if (mf) hasPoints = !mf->points.empty();
            }
            if (g_McfLastCount > 0)
                ImGui::TextDisabled("上次匹配: %d 个", g_McfLastCount);
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

            struct McfState { cv::Rect lastRect; int roiIdx = -1; };
            static std::unordered_map<int, McfState> s_mcfStates;
            auto &mfs = s_mcfStates[inst];
            if (mfs.roiIdx >= 0 && mfs.roiIdx >= (int)gROIs.size()) mfs.roiIdx = -1;

            // === 阶段1: 没有ROI也没有参考图 ===
            if (mfs.roiIdx < 0 && it.mcfRefImage.empty())
            {
                if (SecondaryButton("添加ROI获取参考图"))
                {
                    ROI r; r.type = ROI_TYPE_RECT;
                    if (mfs.lastRect.width > 0)
                    {
                        r.start = ImVec2((float)mfs.lastRect.x, (float)mfs.lastRect.y);
                        r.end   = ImVec2((float)(mfs.lastRect.x + mfs.lastRect.width),
                                         (float)(mfs.lastRect.y + mfs.lastRect.height));
                    }
                    else
                    {
                        float cx = gImage.empty() ? 200.0f : gImage.cols * 0.5f;
                        float cy = gImage.empty() ? 200.0f : gImage.rows * 0.5f;
                        r.start = ImVec2(cx - 60, cy - 60);
                        r.end   = ImVec2(cx + 60, cy + 60);
                    }
                    gROIs.push_back(r);
                    mfs.roiIdx = (int)gROIs.size() - 1;
                }
            }
            // === 阶段2: ROI已激活，等待确认 ===
            else if (mfs.roiIdx >= 0)
            {
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "参考图ROI已就绪");
                ImGui::TextDisabled("拖拽ROI调整位置后点击确认");
                if (PrimaryButton("确认捕获"))
                {
                    cv::Rect r = gROIs[mfs.roiIdx].ToCvRect();
                    if (r.width > 0 && r.height > 0 && !gImage.empty())
                    {
                        it.mcfRefImage = gImage(r).clone();
                        it.mcfAnchorX = 0; it.mcfAnchorY = 0;
                        mfs.lastRect = r;
                        RemoveROIByIndex(mfs.roiIdx);
                        LogSystem::Add(LOG_INFO, color, "多点找色: 参考图已抓取 %dx%d", r.width, r.height);
                    }
                }
                if (SecondaryButton("取消"))
                {
                    RemoveROIByIndex(mfs.roiIdx);
                }
            }
            // === 阶段3: 参考图已就绪 ===
            else if (!it.mcfRefImage.empty())
            {
                if (SecondaryButton("修改参考图ROI"))
                {
                    ROI r; r.type = ROI_TYPE_RECT;
                    if (mfs.lastRect.width > 0)
                    {
                        r.start = ImVec2((float)mfs.lastRect.x, (float)mfs.lastRect.y);
                        r.end   = ImVec2((float)(mfs.lastRect.x + mfs.lastRect.width),
                                         (float)(mfs.lastRect.y + mfs.lastRect.height));
                    }
                    else
                    {
                        float cx = gImage.empty() ? 200.0f : gImage.cols * 0.5f;
                        float cy = gImage.empty() ? 200.0f : gImage.rows * 0.5f;
                        r.start = ImVec2(cx - 60, cy - 60);
                        r.end   = ImVec2(cx + 60, cy + 60);
                    }
                    gROIs.push_back(r);
                    mfs.roiIdx = (int)gROIs.size() - 1;
                }
                if (SecondaryButton("清除参考图"))
                {
                    it.mcfRefImage.release();
                    mfs.lastRect = cv::Rect();
                    if (it.toolImpl)
                    {
                        auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl);
                        if (mf) { mf->points.clear(); mf->refImage.release(); }
                    }
                }
            }

            // ---- 参考图预览 + 点击取色（仅当参考图存在） ----
            if (!it.mcfRefImage.empty())
            {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1), "参考图 %dx%d", it.mcfRefImage.cols, it.mcfRefImage.rows);
                ImGui::SameLine();
                static bool s_mcfPreviewOpen = true;
                if (SecondaryButton(s_mcfPreviewOpen ? "隐藏预览" : "显示预览"))
                    s_mcfPreviewOpen = !s_mcfPreviewOpen;

                if (s_mcfPreviewOpen)
                {
                cv::Mat ref = it.mcfRefImage.clone();
                // 应用预处理到参考图预览
                if (it.mcfImgGray && ref.channels() > 1)
                    cv::cvtColor(ref, ref, cv::COLOR_BGR2GRAY);
                if (it.mcfImgBinary)
                {
                    if (ref.channels() > 1) cv::cvtColor(ref, ref, cv::COLOR_BGR2GRAY);
                    cv::threshold(ref, ref, it.mcfImgBinThresh, 255, cv::THRESH_BINARY);
                }
                int maxPx = 120;
                float rs = maxPx / (float)std::max(ref.cols, ref.rows);
                if (rs > 1.0f) rs = 1.0f;
                int dw = (int)(ref.cols * rs), dh = (int)(ref.rows * rs);
                if (dw < 2) dw = 2; if (dh < 2) dh = 2;
                cv::Mat small; cv::resize(ref, small, cv::Size(dw, dh), 0, 0, cv::INTER_NEAREST);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 base = ImGui::GetCursorScreenPos();
                float step = 2.0f;  // 对齐模板匹配的 2px 像素步长
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
                for (int y = 0; y < dh; y++)
                    for (int x = 0; x < dw; x++)
                    {
                        ImU32 col;
                        uchar b = 0, g = 0, r = 0;
                        ReadBgrAt(small, y, x, b, g, r);
                        col = IM_COL32(r, g, b, 255);
                        dl->AddRectFilled(ImVec2(base.x + x * step, base.y + y * step),
                            ImVec2(base.x + (x + 1) * step, base.y + (y + 1) * step), col);
                    }
                ImGui::Dummy(ImVec2(dw * step, dh * step));

                // ---- 在小图上绘制已选颜色点的红色标记 ----
                if (it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl);
                    if (mf && !mf->points.empty())
                    {
                        float markerR = 3.0f;
                        int ax = it.mcfAnchorX, ay = it.mcfAnchorY;
                        for (int pi = 0; pi < (int)mf->points.size(); pi++)
                        {
                            const auto& pt = mf->points[pi];
                            float sx = base.x + (ax + pt.x) * rs * step;
                            float sy = base.y + (ay + pt.y) * rs * step;
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
                    int px = (int)((mouse.x - base.x) / step / rs);
                    int py = (int)((mouse.y - base.y) / step / rs);
                    if (px >= 0 && px < ref.cols && py >= 0 && py < ref.rows)
                    {
                        uchar b = 0, g = 0, r = 0;
                        if (!ReadBgrAt(ref, py, px, b, g, r))
                        {
                            LogSystem::Add(LOG_WARN, "取色失败: 图像格式不支持或坐标越界");
                        }
                        else
                        {
                            ColorPoint cp;
                            cp.b = b; cp.g = g; cp.r = r;
                            cp.tolerance = 10;
                            if (!it.toolImpl) it.toolImpl = ITool::Create(10).release();
                            auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl);
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
                            LogSystem::Add(LOG_INFO, color, "取色: (%d,%d) BGR(%d,%d,%d)", cp.x, cp.y, cp.b, cp.g, cp.r);
                        }
                    }
                }
                ImGui::SetItemTooltip("点击取色 | 首点=锚点(0,0) | 后续点=相对偏移");

                // ---- 颜色点列表（色块展示） ----
                if (it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl);
                    if (mf && !mf->points.empty())
                    {
                        ImGui::Separator();
                        ImGui::Text("已选颜色点 (%d个):", (int)mf->points.size());

                        // 全局容差滑块 — 直接控制所有点的容差 + 实时重新匹配
                        ImGui::SetNextItemWidth(160);
                        int allTol = mf->points[0].tolerance;
                        if (ImGui::SliderInt("统一容差", &allTol, 0, 128))
                        {
                            for (auto& pt : mf->points)
                                pt.tolerance = allTol;
                            // 实时重新执行匹配
                            if (!gImage.empty() && !it.mcfRefImage.empty() && !mf->points.empty())
                                ToolController::RequestRun(inst);
                        }

                        int removeIdx = -1;
                        for (int pi = 0; pi < (int)mf->points.size(); pi++)
                        {
                            auto& pt = mf->points[pi];
                            ImGui::PushID(pi);
                            // 色块 + RGB值
                            ImU32 swatch = IM_COL32(pt.r, pt.g, pt.b, 255);
                            ImVec2 cp = ImGui::GetCursorScreenPos();
                            dl->AddRectFilled(cp, ImVec2(cp.x + 18, cp.y + 18), swatch);
                            dl->AddRect(cp, ImVec2(cp.x + 18, cp.y + 18), IM_COL32(255, 255, 255, 80));
                            ImGui::Dummy(ImVec2(18, 18));
                            ImGui::SameLine();
                            const char* label = (pi == 0) ? "锚点" : cv::format("偏移(%+d,%+d)", pt.x, pt.y).c_str();
                            ImGui::Text("%s BGR(%d,%d,%d) 容差:%d", label, pt.b, pt.g, pt.r, pt.tolerance);
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(80);
                            if (ImGui::SliderInt("##tol", &pt.tolerance, 0, 128))
                            {
                                // 单个点容差变化也实时重新匹配
                                if (!gImage.empty() && !it.mcfRefImage.empty() && !mf->points.empty())
                                    ToolController::RequestRun(inst);
                            }
                            ImGui::SameLine();
                            if (SecondaryButton("X", 20)) removeIdx = pi;
                            ImGui::PopID();
                        }
                        if (removeIdx >= 0)
                            mf->points.erase(mf->points.begin() + removeIdx);
                    }
                }
                } // s_mcfPreviewOpen
            }

            // ---- 图像预处理 ----
            SectionHeader("图像预处理");
            auto UpdatePointColors = [&]() {
                if (!it.mcfRefImage.empty() && it.toolImpl)
                {
                    auto* mf = dynamic_cast<MultiColorFinder*>(it.toolImpl);
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
            if (ImGui::Checkbox("转为灰度##mcf", &it.mcfImgGray))
                { McfApplyPreview(it.mcfImgGray, it.mcfImgBinary, it.mcfImgBinThresh, gImage); UpdatePointColors(); }
            ImGui::SameLine();
            if (ImGui::Checkbox("二值化##mcf", &it.mcfImgBinary))
                { McfApplyPreview(it.mcfImgGray, it.mcfImgBinary, it.mcfImgBinThresh, gImage); UpdatePointColors(); }
            if (it.mcfImgBinary && ImGui::SliderInt("阈值##mcf", &it.mcfImgBinThresh, 0, 255))
                { McfApplyPreview(it.mcfImgGray, it.mcfImgBinary, it.mcfImgBinThresh, gImage); UpdatePointColors(); }
            ImGui::TextDisabled("预处理同时应用于主图和参考图");

            // ---- 搜索参数 ----
            SectionHeader("搜索");
            ImGui::SliderInt("最大结果数", &it.mcfMaxResults, 1, 200);
            ImGui::SliderFloat("去重距离", &it.mcfMinDist, 0, 50, "%.0fpx");
            ImGui::SliderInt("十字大小", &it.mcfCrossSize, 3, 30);
            ImGui::SliderInt("十字粗细", &it.mcfCrossThick, 1, 5);

            // ---- 结果列表（对齐模板匹配） ----
            {
                const auto& results = gContext.unifiedResults;
                for (const auto& r : results)
                {
                    if (r.toolName != "多点找色" || r.regions.empty()) continue;
                    SectionHeader("结果");
                    int total = (int)r.regions.size();
                    ImGui::Text("匹配结果: %d 个", total);
                    ImGui::BeginChild("##mcfResultList", ImVec2(0, std::min(120.0f, total * ImGui::GetTextLineHeight() + 10.0f)), true);
                    for (int i = 0; i < total; i++)
                    {
                        const auto& reg = r.regions[i];
                        bool partial = (reg.score < 1.0f);
                        int ax = it.mcfAnchorX, ay = it.mcfAnchorY;
                        if (partial)
                            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "#%d: 部分 %d/%d (%.0f,%.0f)",
                                i + 1, (int)reg.area, (int)reg.contour.size(),
                                (float)reg.bbox.x + ax, (float)reg.bbox.y + ay);
                        else
                            ImGui::Text("#%d: (%.0f,%.0f)", i + 1,
                                (float)reg.bbox.x + ax, (float)reg.bbox.y + ay);
                    }
                    ImGui::EndChild();
                }
            }

            EndCard();
        };

        // 9: 颜色分析
        g_ToolUIMap[9] = [&](ToolInstance &it, int inst)
        {
            BeginCard("颜色分析");
            if (SecondaryButton("重置参数"))
            {
                it.colorSpace = 0; it.colorHistBins = 32;
                it.colorShowHist = true; it.colorHistHeight = 100;
            }
            if (PrimaryButton("执行颜色分析##color"))
            {
                RunToolFromCard(inst);
            }
            DrawSearchROIControls(it, inst);
            SectionHeader("参数");
            const char *csNames[] = {"BGR", "HSV", "Lab", "YCbCr", "Gray"};
            ImGui::Combo("色域##color", &it.colorSpace, csNames, 5);
            ImGui::SliderInt("直方图Bins##color", &it.colorHistBins, 8, 128);
            ImGui::Checkbox("显示直方图##color", &it.colorShowHist);
            if (it.colorShowHist)
                ImGui::SliderInt("高度##color", &it.colorHistHeight, 50, 300);
            SectionHeader("结果");
            if (ColorAnalyzer::g_AnalyzeTimeMs > 0)
            {
                ImGui::TextDisabled("R:%.1f±%.1f G:%.1f±%.1f B:%.1f±%.1f",
                                    ColorAnalyzer::g_LastResult.meanR, ColorAnalyzer::g_LastResult.stdR,
                                    ColorAnalyzer::g_LastResult.meanG, ColorAnalyzer::g_LastResult.stdG,
                                    ColorAnalyzer::g_LastResult.meanB, ColorAnalyzer::g_LastResult.stdB);
                ImGui::TextDisabled("%.3fms", ColorAnalyzer::g_AnalyzeTimeMs);
            }
            EndCard();
        };

        // ---- 手风琴工具列表（点击展开/收起，底部固定执行区预留空间） ----
        const ImGuiStyle& style = ImGui::GetStyle();
        const float actionButtonH = ImGui::GetFrameHeight() + 4.0f;
        const float bottomModeH = ImGui::GetFrameHeight() + 2.0f;
        const float bottomTimeH = ImGui::GetTextLineHeight();
        const float bottomH = g_ToolInstances.empty()
            ? 0.0f
            : actionButtonH + bottomModeH + bottomTimeH + style.ItemSpacing.y * 5.0f + 4.0f;
        ImGui::BeginChild("##ToolList", ImVec2(0, -bottomH), false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (g_ToolInstances.empty())
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 start = ImGui::GetCursorScreenPos();
            float cardH = (avail.y > 130.0f) ? 120.0f : avail.y;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.10f, 0.11f, 0.13f, 1.0f) : ImVec4(0.82f, 0.86f, 0.91f, 1.0f));
            ImU32 border = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.24f, 0.27f, 0.32f, 1.0f) : ImVec4(0.62f, 0.67f, 0.74f, 1.0f));
            drawList->AddRectFilled(start, ImVec2(start.x + avail.x, start.y + cardH), bg, 6.0f);
            drawList->AddRect(start, ImVec2(start.x + avail.x, start.y + cardH), border, 6.0f);

            ImGui::Dummy(ImVec2(1.0f, 18.0f));
            ImGui::Indent(12.0f);
            ImGui::TextDisabled("暂无工具");
            ImGui::TextWrapped("点击上方 [+ 添加工具] 组成处理链。");
            ImGui::TextWrapped("每个工具默认读取原图工具输出。");
            ImGui::Unindent(12.0f);
        }
        else
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 3));
            int selectedForRemove = -1;
            int moveFrom = -1;
            int moveTo = -1;
            static int s_scrollOpenedToolToTop = -1;

            for (int inst = 0; inst < (int)g_ToolInstances.size(); inst++)
            {
                int type = g_ToolInstances[inst].type;
                bool expanded = (g_ActiveToolIndex == inst);
                float toolHeaderY = ImGui::GetCursorPosY();
                if (s_scrollOpenedToolToTop == inst && expanded)
                {
                    ImGui::SetScrollY(toolHeaderY);
                    s_scrollOpenedToolToTop = -1;
                }

                bool batchHl = (!ToolController::IsRuntimeMode()
                            && ToolController::GetMode() != ToolController::Mode::Idle
                            && inst == ToolController::GetCurrentIndex())
                            || (ToolController::GetStepCursor() > 0 && inst == ToolController::GetStepCursor() - 1);

                // ---- 卡片头部（始终可见） ----
                char cardId[32];
                snprintf(cardId, sizeof(cardId), "##toolhdr%d", inst);

                const int headerColorStackBase = ImGui::GetCurrentContext()->ColorStack.Size;
                if (expanded)
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, themeActive);
                else if (batchHl)
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, isDark
                        ? ImVec4(0.08f, 0.55f, 0.25f, 0.28f)   // 暗色: 翠绿
                        : ImVec4(0.15f, 0.75f, 0.35f, 0.22f));  // 亮色: 翠绿
                else
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, themeCardHover);

                ImGui::BeginChild(cardId, ImVec2(0, 32), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImVec2 headerMin = ImGui::GetCursorScreenPos();

                const char *name = ToolName(type);
                for (const auto &m : g_ToolRegistry)
                    if (m.type == type) { name = m.name; break; }
                const std::string displayName = ToolInstanceTitle(name, g_ToolInstances[inst].label);

                char indexLabel[32];
                snprintf(indexLabel, sizeof(indexLabel), "%d", inst + 1);
                char typeLabel[32];
                snprintf(typeLabel, sizeof(typeLabel), "#%d", type);

                float childW = ImGui::GetWindowWidth();
                float childH = ImGui::GetWindowHeight();
                const float controlSize = 24.0f;
                const float controlPad = 4.0f;
                const float leftSlotW = controlSize + controlPad * 2.0f;
                const float rightSlotW = controlSize + controlPad * 2.0f;
                const float controlY = (childH - controlSize) * 0.5f;

                // 透明点击区避开右侧删除按钮
                ImGui::InvisibleButton(cardId, ImVec2(childW - rightSlotW, childH));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    g_ActiveToolIndex = expanded ? -1 : inst;
                    if (!expanded)
                        s_scrollOpenedToolToTop = inst;
                }
                if (ImGui::BeginPopupContextItem(cardId)) {
                    const int firstMovable = FirstMovableToolIndex(g_ToolInstances);
                    const bool canMove = inst >= firstMovable;
                    const bool canMoveUp = canMove && inst > firstMovable;
                    const bool canMoveDown = canMove && inst + 1 < (int)g_ToolInstances.size();
                    if (ImGui::MenuItem("上移", nullptr, false, canMoveUp)) {
                        moveFrom = inst;
                        moveTo = inst - 1;
                    }
                    if (ImGui::MenuItem("下移", nullptr, false, canMoveDown)) {
                        moveFrom = inst;
                        moveTo = inst + 1;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("删除", nullptr, false, canMove))
                        selectedForRemove = inst;
                    ImGui::EndPopup();
                }

                ImVec2 nameSize = ImGui::CalcTextSize(displayName.c_str());
                ImVec2 indexSize = ImGui::CalcTextSize(indexLabel);
                ImVec2 typeSize = ImGui::CalcTextSize(typeLabel);

                ImDrawList* headerDraw = ImGui::GetWindowDrawList();
                ImU32 controlBorder = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.45f, 0.50f, 0.58f, 1.0f) : ImVec4(0.55f, 0.58f, 0.64f, 1.0f));
                ImU32 controlText = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.70f, 0.76f, 0.84f, 1.0f) : ImVec4(0.28f, 0.30f, 0.34f, 1.0f));

                ImVec2 arrowBoxMin(headerMin.x + controlPad, headerMin.y + controlY);
                ImVec2 arrowBoxMax(arrowBoxMin.x + controlSize, arrowBoxMin.y + controlSize);
                headerDraw->AddRect(arrowBoxMin, arrowBoxMax, controlBorder, 3.0f);
                ImVec2 arrowCenter((arrowBoxMin.x + arrowBoxMax.x) * 0.5f, (arrowBoxMin.y + arrowBoxMax.y) * 0.5f);
                const float triHalf = 4.8f;
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

                // PNG图标 + 名称 + 编号使用固定列，避免长名称挤压右侧控件
                float labelAreaX = leftSlotW;
                float labelAreaW = childW - leftSlotW - rightSlotW;
                const float headerIconSize = 16.0f;
                const float headerIconGap = 7.0f;
                const float columnGap = 8.0f;
                const float headerGroupX = headerMin.x + labelAreaX + 6.0f;
                const float headerIconY = headerMin.y + (childH - headerIconSize) * 0.5f;
                const float headerTextY = headerMin.y + (childH - nameSize.y) * 0.5f;
                const float typeX = headerMin.x + childW - rightSlotW - typeSize.x - 6.0f;
                const float indexX = typeX - columnGap - indexSize.x;
                const float nameX = headerGroupX + headerIconSize + headerIconGap;
                const float nameMaxX = indexX - columnGap;
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
                headerDraw->PushClipRect(ImVec2(nameX, headerMin.y), ImVec2(nameMaxX, headerMin.y + childH), true);
                headerDraw->AddText(ImVec2(nameX, headerTextY), headerTextColor, displayName.c_str());
                headerDraw->PopClipRect();
                headerDraw->AddText(ImVec2(indexX, headerTextY), headerTextColor, indexLabel);
                headerDraw->AddText(ImVec2(typeX, headerTextY), headerTextColor, typeLabel);

                char removeId[32];
                snprintf(removeId, sizeof(removeId), "X##remove_tool_%d", inst);
                ImVec2 removePos(childW - controlPad - controlSize, controlY);
                ImGui::SetCursorPos(removePos);
                ImGui::InvisibleButton(removeId, ImVec2(controlSize, controlSize));
                ImVec2 removeMin(headerMin.x + childW - controlPad - controlSize, headerMin.y + controlY);
                ImVec2 removeMax(removeMin.x + controlSize, removeMin.y + controlSize);
                bool removeHovered = ImGui::IsItemHovered();
                bool removeClicked = ImGui::IsItemClicked();
                ImU32 removeBg = ImGui::ColorConvertFloat4ToU32(removeHovered
                    ? (isDark ? ImVec4(0.55f, 0.16f, 0.16f, 1.0f) : ImVec4(0.95f, 0.20f, 0.20f, 1.0f))
                    : (isDark ? ImVec4(0.18f, 0.20f, 0.24f, 1.0f) : ImVec4(0.90f, 0.92f, 0.95f, 1.0f)));
                ImU32 removeBorder = ImGui::ColorConvertFloat4ToU32(isDark ? ImVec4(0.45f, 0.50f, 0.58f, 1.0f) : ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
                ImU32 removeText = ImGui::ColorConvertFloat4ToU32(removeHovered ? ImVec4(1, 1, 1, 1) : (isDark ? ImVec4(0.76f, 0.80f, 0.86f, 1) : ImVec4(0.36f, 0.38f, 0.42f, 1)));
                headerDraw->AddRectFilled(removeMin, removeMax, removeBg, 3.0f);
                headerDraw->AddRect(removeMin, removeMax, removeBorder, 3.0f);
                ImVec2 xCenter((removeMin.x + removeMax.x) * 0.5f, (removeMin.y + removeMax.y) * 0.5f);
                float xHalf = 4.8f;
                headerDraw->AddLine(ImVec2(xCenter.x - xHalf, xCenter.y - xHalf), ImVec2(xCenter.x + xHalf, xCenter.y + xHalf), removeText, 1.7f);
                headerDraw->AddLine(ImVec2(xCenter.x + xHalf, xCenter.y - xHalf), ImVec2(xCenter.x - xHalf, xCenter.y + xHalf), removeText, 1.7f);
                if (removeClicked)
                    selectedForRemove = inst;

                ImGui::EndChild();
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
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("输入");
                        ImGui::SameLine();
                        float inputComboW = ImGui::GetContentRegionAvail().x;
                        if (inputComboW < 120.0f)
                            inputComboW = 120.0f;
                        ImGui::SetNextItemWidth(inputComboW);
                        char inputId[32];
                        snprintf(inputId, sizeof(inputId), "##input_%d", inst);
                        ImGui::Combo(inputId, &g_ToolInstances[inst].inputSourceMode, inputModes, IM_ARRAYSIZE(inputModes));
                    }

                    auto uiFn = g_ToolUIMap.find(type);
                    if (uiFn != g_ToolUIMap.end() && uiFn->second)
                    {
                        currentCardType = type;
                        currentCardInst = inst;
                        uiFn->second(g_ToolInstances[inst], inst);
                        currentCardType = -1;
                        currentCardInst = -1;
                    }
                }
            }

            if (g_ActiveToolIndex >= 0)
            {
                float trailingSpace = ImGui::GetWindowHeight() - 44.0f;
                if (trailingSpace < 120.0f)
                    trailingSpace = 120.0f;
                ImGui::Dummy(ImVec2(1.0f, trailingSpace));
            }

            ImGui::PopStyleVar();

            if (moveFrom >= 0 && moveTo >= 0) {
                if (MoveToolInstance(g_ToolInstances, moveFrom, moveTo,
                    g_ActiveToolIndex, g_YoloLiveInstanceIdx, g_YoloLiveDetect)) {
                    SaveCurrentRecipe();
                }
            }

            if (selectedForRemove >= 0) {
                if (RemoveToolInstance(g_ToolInstances, selectedForRemove,
                    g_ActiveToolIndex, g_YoloLiveInstanceIdx, g_YoloLiveDetect)) {
                    SaveCurrentRecipe();
                }
            }
        }

        ImGui::EndChild();

        // ---- 底部：全部执行 / 单步 / 循环 按钮 ----
        if (!g_ToolInstances.empty())
        {
            ImGui::Separator();

            static bool s_looping = false;
            auto mode = ToolController::GetMode();
            bool running = (mode != ToolController::Mode::Idle);

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

            float bottomAvailW = ImGui::GetContentRegionAvail().x;
            float actionGap = 5.0f;
            float runW = bottomAvailW * 0.40f;
            float sideW = (bottomAvailW - runW - actionGap * 2.0f) * 0.5f;
            if (runW < 92.0f) runW = 92.0f;
            if (sideW < 66.0f) sideW = 66.0f;

            const ImVec4 runBase = isDark ? ImVec4(0.12f, 0.42f, 0.78f, 1.0f) : ImVec4(0.20f, 0.49f, 0.78f, 1.0f);
            const ImVec4 runHover = isDark ? ImVec4(0.16f, 0.52f, 0.92f, 1.0f) : ImVec4(0.26f, 0.58f, 0.88f, 1.0f);
            const ImVec4 runActive = isDark ? ImVec4(0.08f, 0.32f, 0.62f, 1.0f) : ImVec4(0.14f, 0.38f, 0.66f, 1.0f);
            const ImVec4 subBase = isDark ? ImVec4(0.20f, 0.24f, 0.30f, 1.0f) : ImVec4(0.48f, 0.62f, 0.78f, 1.0f);
            const ImVec4 subHover = isDark ? ImVec4(0.28f, 0.34f, 0.42f, 1.0f) : ImVec4(0.56f, 0.70f, 0.86f, 1.0f);
            const ImVec4 subActive = isDark ? ImVec4(0.15f, 0.19f, 0.25f, 1.0f) : ImVec4(0.38f, 0.52f, 0.70f, 1.0f);
            const ImVec4 loopBase = s_looping ? ImVec4(0.14f, 0.58f, 0.30f, 1.0f) : subBase;
            const ImVec4 loopHover = s_looping ? ImVec4(0.18f, 0.70f, 0.38f, 1.0f) : subHover;
            const ImVec4 loopActive = s_looping ? ImVec4(0.10f, 0.46f, 0.24f, 1.0f) : subActive;

            if (RunActionButton("全部执行", ImVec2(runW, actionButtonH), runBase, runHover, runActive))
            {
                if (gImage.empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else
                    ToolController::RequestRunAll(s_looping);
            }
            ImGui::SameLine();

            // 单步执行
            int stepCur = ToolController::GetStepCursor();
            bool stepping = (stepCur > 0 && stepCur <= (int)g_ToolInstances.size());
            const ImVec4 stepBase = stepping ? runBase : subBase;
            const ImVec4 stepHover = stepping ? runHover : subHover;
            const ImVec4 stepActive = stepping ? runActive : subActive;
            if (RunActionButton(stepping ? "单步中" : "单步", ImVec2(sideW, actionButtonH), stepBase, stepHover, stepActive))
            {
                if (gImage.empty())
                    LogSystem::Add(LOG_WARN, "请先加载图片");
                else if (stepCur >= (int)g_ToolInstances.size())
                    ToolController::RequestStepReset();
                else
                    ToolController::RequestStepNext();
            }
            ImGui::SameLine();

            // 循环
            if (RunActionButton(s_looping ? "循环开" : "循环", ImVec2(sideW, actionButtonH), loopBase, loopHover, loopActive))
            {
                s_looping = !s_looping;
                if (!s_looping)
                    ToolController::Reset();
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
            const ImVec4 timeColor = isDark ? ImVec4(0.30f, 0.95f, 0.46f, 1.0f) : ImVec4(0.02f, 0.42f, 0.18f, 1.0f);
            const ImVec4 progressColor = isDark ? ImVec4(0.72f, 0.78f, 0.86f, 1.0f) : ImVec4(0.22f, 0.28f, 0.36f, 1.0f);
            if (running)
            {
                const float elapsedMs = ToolController::GetElapsedTimeMs();
                ImGui::TextColored(progressColor,
                    "运行中 %d/%zu | 已用 %.3fms | 上步 %.3fms",
                    ToolController::GetCurrentIndex() + 1,
                    g_ToolInstances.size(),
                    elapsedMs,
                    stepMs);
            }
            else if (totalMs > 0.0f)
            {
                ImGui::TextColored(timeColor,
                    "上次全部执行: 总 %.3fms | 上步 %.3fms",
                    totalMs,
                    stepMs);
            }
            else
            {
                ImGui::TextDisabled("%zu 个工具", g_ToolInstances.size());
            }
        }

        ImGui::PopStyleVar(4);
        int toolsColorStackNow = ImGui::GetCurrentContext()->ColorStack.Size;
        if (toolsColorStackNow > toolsColorStackBase)
            ImGui::PopStyleColor(toolsColorStackNow - toolsColorStackBase);

        ImGui::End();
    }

} // namespace UI
