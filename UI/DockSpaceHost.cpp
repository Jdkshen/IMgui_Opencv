#include "../Core/OpenFileDialog.h"
#include "DockSpaceHost.h"
#include "ToolsWindow.h"
#include "../Core/ThemeManager.h"
#include "../Core/VisionContext.h"
#include "../Core/RecipeManager.h"
#include "../Core/TemplateState.h"
#include "../Core/FrameSourceState.h"
#include "../Core/ImageState.h"
#include "../Core/ResultExporter.h"
#include "../Core/ToolChainState.h"
#include "../Core/ToolController.h"
#include "../Log/LogSystem.h"
#include "../include/imgui/imgui_internal.h"
#include <shellapi.h>

// =========================
// UTF-8 ↔ 宽字符转换工具
// =========================
static std::wstring ToWide(const char *u8)
{
    if (!u8 || !*u8)
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, u8, -1, nullptr, 0);
    if (len <= 1)
        return {};
    std::wstring w(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, &w[0], len - 1);
    return w;
}

static std::string ToNarrow(const std::wstring &w)
{
    if (w.empty())
        return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

// 获取 exe 所在目录下的 recipes\ 子目录（宽字符）
static std::wstring GetRecipesDirW()
{
    static std::wstring cached;
    if (!cached.empty())
        return cached;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    dir += L"recipes\\";
    cached = dir;
    return cached;
}

// 获取 exe 所在目录（不含 recipes\，给 RecipeManager::List 用）
static std::string GetExeDir()
{
    static std::string cached;
    if (!cached.empty())
        return cached;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dirW(exe);
    dirW = dirW.substr(0, dirW.find_last_of(L"\\/") + 1);
    std::string dir = ToNarrow(dirW);
    cached = dir;
    return cached;
}

static std::string GetRecipesDirA()
{
    return GetExeDir() + "recipes\\";
}

// 全局状态变量定义
bool show_demo_window = false;
bool g_ShowLog = true;
bool g_ShowSidebar = true;
bool g_ShowStats = true;
bool g_ShowOpenCV = true;
bool g_ShowTools = true;

namespace UI
{
    static char g_CurrentRecipeName[64] = "默认配方";
    static bool g_ResetDockLayout = false;
    static bool g_OpenRenameRecipePopup = false;
    static char g_RenameRecipeName[64] = "";
    static bool g_AutoRunAfterRecipeLoad = false;
    static bool g_PendingAutoRunAfterRecipeLoad = false;
    static std::string g_LastExportPath;

    bool SaveCurrentRecipe()
    {
        std::wstring dir = GetRecipesDirW();
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring path = dir + ToWide(g_CurrentRecipeName) + L".recipe";
        RecipeData data = RecipeManager::Capture(g_CurrentRecipeName);
        return RecipeManager::Save(ToNarrow(path).c_str(), data);
    }

    static std::string ExportPrefix(const char* kind)
    {
        std::string prefix = kind && *kind ? kind : "export";
        if (g_CurrentRecipeName[0] != '\0')
            prefix += std::string("_") + g_CurrentRecipeName;
        return prefix;
    }

    static ResultExporter::ExportSnapshot CaptureExportSnapshot()
    {
        ResultExporter::ExportSnapshot snapshot;
        snapshot.recipeName = g_CurrentRecipeName;
        snapshot.imagePath = FrameSourceState::Current().sourcePath;
        snapshot.imageWidth = ImageState::Width();
        snapshot.imageHeight = ImageState::Height();
        snapshot.imageVersion = ImageState::Version();
        snapshot.totalTimeMs = ToolController::GetTotalTimeMs();
        snapshot.tools = ToolChainState::ReadOnlyTools();
        snapshot.results = gContext.unifiedResults;
        snapshot.toolTimesMs.reserve(snapshot.tools.size());
        for (int i = 0; i < static_cast<int>(snapshot.tools.size()); ++i)
            snapshot.toolTimesMs.push_back(ToolController::GetToolTimeMs(i));
        return snapshot;
    }

    static void ExportCurrentResults()
    {
        auto snapshot = CaptureExportSnapshot();
        std::string path = ResultExporter::BuildDefaultOutputPath(ExportPrefix("results").c_str(), "json");
        if (ResultExporter::ExportResultsJson(path.c_str(), snapshot))
        {
            g_LastExportPath = path;
            LogSystem::Add(LOG_INFO, "结果已导出: %s", path.c_str());
        }
        else
            LogSystem::Add(LOG_ERROR, "结果导出失败: %s", path.c_str());
    }

    static void ExportCurrentRunReport()
    {
        auto snapshot = CaptureExportSnapshot();
        if (!ImageState::Current().empty())
        {
            std::string imagePath = ResultExporter::BuildDefaultOutputPath(ExportPrefix("result_image").c_str(), "png");
            if (ResultExporter::ExportImageSnapshot(imagePath.c_str(), ImageState::Current()))
                snapshot.resultImagePath = imagePath;
            else
                LogSystem::Add(LOG_WARN, "结果图像导出失败: %s", imagePath.c_str());
        }

        std::string path = ResultExporter::BuildDefaultOutputPath(ExportPrefix("run_report").c_str(), "md");
        if (ResultExporter::ExportRunReportMarkdown(path.c_str(), snapshot))
        {
            g_LastExportPath = path;
            LogSystem::Add(LOG_INFO, "运行报告已导出: %s", path.c_str());
        }
        else
            LogSystem::Add(LOG_ERROR, "运行报告导出失败: %s", path.c_str());
    }

    static void OpenReportsFolder()
    {
        std::wstring dir = ToWide(ResultExporter::ReportsDirectory().c_str());
        if (dir.empty())
            return;
        CreateDirectoryW(dir.c_str(), nullptr);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    static void RequestAutoRunAfterRecipeLoad(const RecipeData& data)
    {
        if (!g_AutoRunAfterRecipeLoad)
            return;

        if (data.tools.empty())
        {
            LogSystem::Add(LOG_WARN, "配方加载后自动执行已跳过：没有工具");
            return;
        }

        if (data.imagePath.empty() && ImageState::Current().empty())
        {
            LogSystem::Add(LOG_WARN, "配方加载后自动执行已跳过：没有图片");
            return;
        }

        g_PendingAutoRunAfterRecipeLoad = true;
        LogSystem::Add(LOG_INFO, "配方加载后自动执行：等待图片就绪");
    }

    static void ApplyLoadedRecipe(const RecipeData& data)
    {
        RecipeManager::Apply(data);
        RequestAutoRunAfterRecipeLoad(data);
    }

    static void ProcessPendingAutoRunAfterRecipeLoad()
    {
        if (!g_PendingAutoRunAfterRecipeLoad)
            return;
        if (ToolController::GetMode() != ToolController::Mode::Idle)
            return;
        if (ImageState::Current().empty())
            return;

        g_PendingAutoRunAfterRecipeLoad = false;
        ToolController::RequestRunAll(false);
        LogSystem::Add(LOG_INFO, "配方加载后自动执行：已开始");
    }

    static bool DeleteRecipeFilesByName(const std::string& name)
    {
        std::wstring dirW = GetRecipesDirW();
        std::wstring nameW = ToWide(name.c_str());
        std::string dirA = GetRecipesDirA();
        std::string nameA = name;
        if (nameW.empty() && nameA.empty())
            return false;

        bool deleted = false;
        if (!nameW.empty())
        {
            deleted = DeleteFileW((dirW + nameW + L".recipe").c_str()) != FALSE || deleted;
            DeleteFileW((dirW + nameW + L".png").c_str());
        }
        if (!nameA.empty())
        {
            deleted = DeleteFileA((dirA + nameA + ".recipe").c_str()) != FALSE || deleted;
            DeleteFileA((dirA + nameA + ".png").c_str());
        }

        if (!nameW.empty())
        {
            WIN32_FIND_DATAW findData{};
            std::wstring pattern = dirW + nameW + L"_tpl*.png";
            HANDLE hFind = FindFirstFileW(pattern.c_str(), &findData);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                        DeleteFileW((dirW + findData.cFileName).c_str());
                } while (FindNextFileW(hFind, &findData));
                FindClose(hFind);
            }
        }

        if (!nameA.empty())
        {
            WIN32_FIND_DATAA findData{};
            std::string pattern = dirA + nameA + "_tpl*.png";
            HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                        DeleteFileA((dirA + findData.cFileName).c_str());
                } while (FindNextFileA(hFind, &findData));
                FindClose(hFind);
            }
        }

        return deleted;
    }

    static bool DeleteCurrentRecipe()
    {
        bool deleted = DeleteRecipeFilesByName(g_CurrentRecipeName);
        if (deleted)
            LogSystem::Add(LOG_INFO, "Recipe deleted: %s", g_CurrentRecipeName);
        else
            LogSystem::Add(LOG_WARN, "Recipe delete failed or not found: %s", g_CurrentRecipeName);
        return deleted;
    }

    static void ClearRecipeRuntimeState(const char* reason)
    {
        ToolController::Reset();
TemplateState::ClearResults();
        ToolChainState::SetActiveIndex(-1);
        ToolChainState::SetYoloLiveDetect(false);
        ToolChainState::SetYoloLiveInstanceIndex(-1);

        RecipeData empty;
        RecipeManager::Apply(empty);
        LogSystem::Add(LOG_INFO, "%s", reason);
    }

    static bool RecipeNameExists(const std::vector<std::string>& recipes, const std::string& name)
    {
        for (const auto& recipe : recipes)
        {
            if (recipe == name)
                return true;
        }
        return false;
    }

    static void SetCurrentRecipeName(const std::string& name)
    {
        strncpy_s(g_CurrentRecipeName, sizeof(g_CurrentRecipeName), name.c_str(), _TRUNCATE);
    }

    static void RequestRenameCurrentRecipe()
    {
        strncpy_s(g_RenameRecipeName, sizeof(g_RenameRecipeName), g_CurrentRecipeName, _TRUNCATE);
        g_OpenRenameRecipePopup = true;
    }

    static bool HasInvalidRecipeNameChars(const std::string& name)
    {
        return name.find_first_of("\\/:*?\"<>|") != std::string::npos;
    }

    static bool RenameCurrentRecipe(const std::string& newName)
    {
        std::string oldName = g_CurrentRecipeName;
        if (newName.empty())
        {
            LogSystem::Add(LOG_WARN, "Recipe rename ignored: empty name");
            return false;
        }
        if (HasInvalidRecipeNameChars(newName))
        {
            LogSystem::Add(LOG_WARN, "Recipe rename ignored: invalid name: %s", newName.c_str());
            return false;
        }
        if (newName == oldName)
            return true;

        SetCurrentRecipeName(newName);
        if (!SaveCurrentRecipe())
        {
            SetCurrentRecipeName(oldName);
            LogSystem::Add(LOG_WARN, "Recipe rename failed: %s -> %s", oldName.c_str(), newName.c_str());
            return false;
        }

        DeleteRecipeFilesByName(oldName);
            LogSystem::Add(LOG_INFO, "Recipe renamed: %s -> %s", oldName.c_str(), g_CurrentRecipeName);
        return true;
    }

    static void DrawRenameRecipePopup()
    {
        if (g_OpenRenameRecipePopup)
        {
            ImGui::OpenPopup("重命名当前配方");
            g_OpenRenameRecipePopup = false;
        }

        if (ImGui::BeginPopupModal("重命名当前配方", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("配方名称");
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("##RecipeName", g_RenameRecipeName, IM_ARRAYSIZE(g_RenameRecipeName));

            ImGui::Spacing();
            if (ImGui::Button("确定", ImVec2(86, 0)))
            {
                if (RenameCurrentRecipe(g_RenameRecipeName))
                    ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(86, 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    static std::string RecipeNameFromPath(const std::string& path)
    {
        size_t nameStart = path.find_last_of("\\/");
        nameStart = (nameStart == std::string::npos) ? 0 : nameStart + 1;
        size_t nameEnd = path.find_last_of('.');
        if (nameEnd == std::string::npos || nameEnd < nameStart)
            nameEnd = path.size();
        return path.substr(nameStart, nameEnd - nameStart);
    }

    static void OpenRecipeFromDialog()
    {
        const wchar_t* filter =
            L"配方文件 (*.recipe)\0*.recipe\0"
            L"所有文件 (*.*)\0*.*\0";
        std::string path = OpenFileDialogWithFilter(filter, L"打开配方");
        if (path.empty())
            return;

        RecipeData data;
        if (!RecipeManager::Load(path.c_str(), data))
        {
            LogSystem::Add(LOG_WARN, "Recipe open failed: %s", path.c_str());
            return;
        }

        std::string recipeName = data.name.empty() ? RecipeNameFromPath(path) : data.name;
        if (!recipeName.empty())
            SetCurrentRecipeName(recipeName);
        ApplyLoadedRecipe(data);
            LogSystem::Add(LOG_INFO, "Recipe opened: %s", path.c_str());
    }

    static void NewCurrentRecipe()
    {
        auto recipes = RecipeManager::List(GetExeDir().c_str());
        const std::string baseName = "新建配方";
        std::string name = baseName;
        int suffix = 1;
        while (RecipeNameExists(recipes, name))
            name = baseName + std::to_string(suffix++);

        SetCurrentRecipeName(name);
        ClearRecipeRuntimeState("Recipe UI cleared for new recipe");

        if (SaveCurrentRecipe())
            LogSystem::Add(LOG_INFO, "New recipe created: %s", g_CurrentRecipeName);
    }

    static float ClampFloat(float v, float minV, float maxV)
    {
        return v < minV ? minV : (v > maxV ? maxV : v);
    }

    void DrawAppMainMenuItems()
    {
        ProcessPendingAutoRunAfterRecipeLoad();

        if (ImGui::BeginMenu("文件(F)"))
        {
            if (ImGui::MenuItem("新建配方"))
                NewCurrentRecipe();

            if (ImGui::MenuItem("打开配方..."))
                OpenRecipeFromDialog();

            if (ImGui::MenuItem("重命名当前配方..."))
                RequestRenameCurrentRecipe();

            if (ImGui::MenuItem("保存当前配方"))
                SaveCurrentRecipe();

            ImGui::Separator();

            if (ImGui::MenuItem("加载当前配方"))
            {
                std::wstring path = GetRecipesDirW() + ToWide(g_CurrentRecipeName) + L".recipe";
                RecipeData data;
                if (RecipeManager::Load(ToNarrow(path).c_str(), data))
                    ApplyLoadedRecipe(data);
            }

            if (ImGui::MenuItem("删除当前配方"))
            {
                if (DeleteCurrentRecipe())
                    ClearRecipeRuntimeState("Recipe UI cleared after delete");
            }

            ImGui::Separator();
            ImGui::TextDisabled("已有配方:");
            auto recipes = RecipeManager::List(GetExeDir().c_str());
            for (int i = 0; i < (int)recipes.size(); i++)
            {
                const auto &r = recipes[i];
                ImGui::PushID(i);
                bool selectedRecipe = (r == g_CurrentRecipeName);
                if (ImGui::Selectable(r.c_str(), selectedRecipe))
                {
                    SetCurrentRecipeName(r);
                    std::wstring path = GetRecipesDirW() + ToWide(r.c_str()) + L".recipe";
                    RecipeData data;
                    if (RecipeManager::Load(ToNarrow(path).c_str(), data))
                        ApplyLoadedRecipe(data);
                }
                ImGui::PopID();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("退出"))
                exit(0);

            ImGui::EndMenu();
        }

        DrawRenameRecipePopup();

        if (ImGui::BeginMenu("编辑(E)"))
        {
            if (ImGui::MenuItem("重置处理状态"))
                ClearRecipeRuntimeState("Runtime state cleared from Edit menu");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("选择(S)"))
        {
            ImGui::TextDisabled("图片/ROI 选择在图像预览窗口操作");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("查看(V)"))
        {
            if (ImGui::Selectable("日志窗口       ", g_ShowLog))
                g_ShowLog = !g_ShowLog;
            if (ImGui::Selectable("侧边栏控制", g_ShowSidebar))
                g_ShowSidebar = !g_ShowSidebar;
            if (ImGui::Selectable("性能窗口", g_ShowStats))
                g_ShowStats = !g_ShowStats;
            if (ImGui::Selectable("图像预览", g_ShowOpenCV))
                g_ShowOpenCV = !g_ShowOpenCV;
            if (ImGui::Selectable("功能窗口", g_ShowTools))
                g_ShowTools = !g_ShowTools;
            ImGui::Separator();

            if (ImGui::BeginMenu("主题"))
            {
                for (int i = 0; i < 2; i++)
                {
                    bool selected = (g_CurrentTheme == i);
                    if (ImGui::Selectable(g_ThemeNames[i], &selected))
                        ApplyTheme(i);
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("重置布局"))
                g_ResetDockLayout = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("转到(G)"))
        {
            if (ImGui::MenuItem("图像预览"))
                g_ShowOpenCV = true;
            if (ImGui::MenuItem("工具链"))
                g_ShowTools = true;
            if (ImGui::MenuItem("日志窗口"))
                g_ShowLog = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("运行(R)"))
        {
            ImGui::MenuItem("配方加载后自动执行", nullptr, &g_AutoRunAfterRecipeLoad);
            ImGui::Separator();
            if (ImGui::MenuItem("全部执行"))
                ToolController::RequestRunAll(false);
            if (ImGui::MenuItem("单步执行"))
                ToolController::RequestStepNext();
            if (ImGui::MenuItem("重置单步"))
                ToolController::RequestStepReset();
            ImGui::Separator();
            if (ImGui::MenuItem("导出结果(JSON)"))
                ExportCurrentResults();
            if (ImGui::MenuItem("导出运行报告(MD)"))
                ExportCurrentRunReport();
            if (ImGui::MenuItem("打开 reports 目录"))
                OpenReportsFolder();
            if (!g_LastExportPath.empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("上次导出:");
                ImGui::TextWrapped("%s", g_LastExportPath.c_str());
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("终端(T)"))
        {
            if (ImGui::MenuItem("显示日志窗口", nullptr, g_ShowLog))
                g_ShowLog = !g_ShowLog;
            if (ImGui::MenuItem("清空日志"))
                LogSystem::Clear();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("帮助(H)"))
        {
            if (ImGui::MenuItem("ImGui Demo"))
                show_demo_window = true;
            if (ImGui::MenuItem("关于"))
            {
            }
            ImGui::EndMenu();
        }
    }

    void DrawDockSpaceHost()
    {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags host_flags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImGui::GetStyleColorVec4(ImGuiCol_TitleBgActive));

        ImGui::Begin("DockSpaceHost", nullptr, host_flags);

        if (ImGui::BeginMenuBar())
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
            DrawAppMainMenuItems();
            ImGui::EndMenuBar();
        }

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

        // 首帧：用 DockBuilder 预设布局（需删除 imgui.ini 才能重置）
        static bool first_dock = true;
        if (first_dock || g_ResetDockLayout)
        {
            first_dock = false;
            g_ResetDockLayout = false;

            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImVec2 dockSize = viewport->Size;
            ImGui::DockBuilderSetNodeSize(dockspace_id, dockSize);

            ImGuiID top, main, left, right, bottom;

            float leftPixels = ClampFloat(dockSize.x * 0.14f, 220.0f, 270.0f);
            float rightPixels = ClampFloat(dockSize.x * 0.24f, 380.0f, 480.0f);
            float bottomPixels = ClampFloat(dockSize.y * 0.21f, 165.0f, 230.0f);

            // 底部日志/统计先横贯全宽，再在上方切左/中/右。
            // 这样全屏时不会出现左右栏一直空到底的竖条。
            float bottomRatio = ClampFloat(bottomPixels / dockSize.y, 0.16f, 0.27f);
            ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, bottomRatio, &bottom, &top);

            float leftRatio = ClampFloat(leftPixels / dockSize.x, 0.10f, 0.20f);
            ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, leftRatio, &left, &main);

            float remainingAfterLeft = dockSize.x - leftPixels;
            float rightRatio = ClampFloat(rightPixels / remainingAfterLeft, 0.22f, 0.34f);
            ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, rightRatio, &right, &main);

            ImGui::DockBuilderDockWindow("功能窗口", right );
            ImGui::DockBuilderDockWindow("图像预览", main);
            ImGui::DockBuilderDockWindow("侧边栏",   left);
            ImGui::DockBuilderDockWindow("日志窗口", bottom);
            ImGui::DockBuilderDockWindow("性能统计", bottom);
            // 每个停靠节点的标签栏在单窗口时自动隐藏（节省空间），图钉仍可用
            ImGuiDockNodeFlags autoHideFlags = ImGuiDockNodeFlags_AutoHideTabBar;
            if (auto n = ImGui::DockBuilderGetNode(left))   n->LocalFlags |= autoHideFlags;
            if (auto n = ImGui::DockBuilderGetNode(main))   n->LocalFlags |= autoHideFlags;
            if (auto n = ImGui::DockBuilderGetNode(right))  n->LocalFlags |= autoHideFlags;
            if (auto n = ImGui::DockBuilderGetNode(bottom)) n->LocalFlags |= autoHideFlags;
            ImGui::DockBuilderFinish(dockspace_id);
        }

        ImGui::DockSpace(dockspace_id);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(6);
    }

} // namespace UI
