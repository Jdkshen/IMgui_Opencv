#include "../Windows_imgui.h"
#include "../Core/RecipeManager.h"

// =====================================================
// 全局状态变量定义（extern 声明在 DockSpaceHost.h）
// =====================================================
bool   show_demo_window = false;
bool   g_ShowLog       = true;
bool   g_ShowSidebar   = true;
bool   g_ShowStats     = true;
bool   g_ShowOpenCV    = true;
bool   g_ShowTools     = true;
ImVec4 color           = ImVec4(0.2f, 0.8f, 1.0f, 1.0f);

namespace UI
{

void DrawDockSpaceHost()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();

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

    ImGui::Begin("DockSpaceHost", nullptr, host_flags);

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("文件"))
        {
            if (ImGui::Selectable("新建          "))
                LogSystem::Add(LOG_INFO, "点击新建");

            if (ImGui::Selectable("打开"))
                LogSystem::Add(LOG_INFO, "点击打开");

            if (ImGui::Selectable("保存"))
                LogSystem::Add(LOG_INFO, "点击保存");

            ImGui::Separator();

            if (ImGui::Selectable("退出"))
                exit(0);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("视图"))
        {
            if (ImGui::Selectable("日志窗口       ", g_ShowLog))
                g_ShowLog = !g_ShowLog;
            if (ImGui::Selectable("侧边栏窗口", g_ShowSidebar))
                g_ShowSidebar = !g_ShowSidebar;
            if (ImGui::Selectable("性能窗口", g_ShowStats))
                g_ShowStats = !g_ShowStats;
            if (ImGui::Selectable("图像预览", g_ShowOpenCV))
                g_ShowOpenCV = !g_ShowOpenCV;
            if (ImGui::Selectable("功能窗口", g_ShowTools))
                g_ShowTools = !g_ShowTools;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("工具"))
        {
            ImGui::MenuItem("OpenCV 预览");
            ImGui::MenuItem("检测工具");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("帮助"))
        {
            if (ImGui::MenuItem("ImGui Demo"))
                show_demo_window = true;
            if (ImGui::MenuItem("关于")) {}
            ImGui::EndMenu();
        }

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

        if (ImGui::BeginMenu("配方"))
        {
            static char recipeName[64] = "默认配方";
            ImGui::InputText("名称", recipeName, sizeof(recipeName));

            if (ImGui::MenuItem("保存当前配方"))
            {
                char exeDir[MAX_PATH];
                GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
                std::string dir(exeDir);
                dir = dir.substr(0, dir.find_last_of("\\/") + 1);
                CreateDirectoryA((dir + "recipes").c_str(), nullptr);
                std::string path = dir + "recipes\\" + recipeName + ".recipe";
                RecipeData data = RecipeManager::Capture(recipeName);
                RecipeManager::Save(path.c_str(), data);
            }

            if (ImGui::MenuItem("加载配方"))
            {
                char exeDir[MAX_PATH];
                GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
                std::string dir(exeDir);
                dir = dir.substr(0, dir.find_last_of("\\/") + 1);
                std::string path = dir + "recipes\\" + recipeName + ".recipe";
                RecipeData data;
                if (RecipeManager::Load(path.c_str(), data))
                    RecipeManager::Apply(data);
            }

            ImGui::Separator();
            ImGui::TextDisabled("已有配方:");

            char exeDir[MAX_PATH];
            GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
            std::string dir(exeDir);
            dir = dir.substr(0, dir.find_last_of("\\/") + 1);
            auto recipes = RecipeManager::List(dir.c_str());
            for (const auto& r : recipes)
            {
                if (ImGui::Selectable(r.c_str()))
                {
                    strncpy_s(recipeName, r.c_str(), sizeof(recipeName) - 1);
                    std::string path = dir + "recipes\\" + r + ".recipe";
                    RecipeData data;
                    if (RecipeManager::Load(path.c_str(), data))
                        RecipeManager::Apply(data);
                }
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0),
        ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
    ImGui::PopStyleVar(3);
}

} // namespace UI
