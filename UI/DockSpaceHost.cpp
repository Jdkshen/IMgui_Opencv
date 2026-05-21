#include "../Windows_imgui.h"
#include "../Algorithm/TemplateMatch.h"
#include "ROIManager.h"

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

// 图像显示状态已移到 ImageViewer.cpp / ROIManager.cpp 中定义

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

	void ShowLogWindow()
	{
		if (!g_ShowLog) return;

		ImGui::Begin("日志窗口", &g_ShowLog);

		if (ImGui::Button("清空日志"))
			LogSystem::Clear();

		ImGui::Separator();

		ImGui::BeginChild("滚动区域");

		const auto& logs = LogSystem::GetLogs();

		for (auto& log : logs)
		{
			ImVec4 color;

			if (log.useCustomColor) { color = log.color; }
			else
			{
				switch (log.level)
				{
				case LOG_INFO:  color = ImVec4(0.8f, 0.8f, 0.8f, 1); break;
				case LOG_WARN:  color = ImVec4(1.0f, 0.8f, 0.2f, 1); break;
				case LOG_ERROR: color = ImVec4(1.0f, 0.3f, 0.3f, 1); break;
				}
			}

			char buf[1024];
			snprintf(buf, sizeof(buf), "[%s] %s", log.time.c_str(), log.text.c_str());

			ImGui::PushStyleColor(ImGuiCol_Text, color);

			if (ImGui::Selectable(buf, false, ImGuiSelectableFlags_AllowDoubleClick)) {}

			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("复制"))
					ImGui::SetClipboardText(buf);
				ImGui::EndPopup();
			}

			if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl)
				ImGui::SetClipboardText(buf);

			ImGui::PopStyleColor();
		}

		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);

		ImGui::EndChild();
		ImGui::End();
	}

	void ShowSidebar()
	{
		if (!g_ShowSidebar) return;

		ImGui::Begin("侧边栏", &g_ShowSidebar);

		ImGui::Text("控制面板");
		ImGui::Separator();

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

		if (ImGui::Button("并发日志测试", ImVec2(200, 50)))
		{
			for (int i = 0; i < 10; i++)
			{
				std::thread([i]()
				{
					ImVec4 color = LogSystem::GetThreadColor();
					LogSystem::Add(LOG_INFO, color, "并发线程日志 %d, tid=%zu", i,
						std::hash<std::thread::id>{}(std::this_thread::get_id()));
				}).detach();
			}
		}

		if (ImGui::Button("检查pendingPath", ImVec2(200, 50)))
		{
			if (!pendingPath.empty())
				LogSystem::Add(LOG_INFO, color, "path=[%s], len=%d", pendingPath.c_str(), (int)pendingPath.length());
			else
				LogSystem::Add(LOG_ERROR, color, "pendingPath为空");
		}

		ImGui::PopStyleVar(2);

		static char inputBuf[256] = { 0 };
		ImGui::Separator();
		ImGui::Text("自定义日志输入");

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
		ImGui::PushItemWidth(200);
		ImGui::InputText("##log_input", inputBuf, sizeof(inputBuf));
		ImGui::PopItemWidth();
		ImGui::PopStyleVar();

		if (ImGui::Button("发送到日志", ImVec2(200, 40)))
		{
			if (strlen(inputBuf) > 0)
			{
				ImVec4 blueColor = ImVec4(0.2f, 0.8f, 1.0f, 1.0f);
				LogSystem::Add(LOG_INFO, blueColor, "自定义: %s", inputBuf);
				inputBuf[0] = '\0';
			}
		}

		ImGui::End();
	}

	void ShowStatsWindow()
	{
		if (!g_ShowStats) return;

		ImGui::Begin("性能统计", &g_ShowStats);

		ImGuiIO& io = ImGui::GetIO();

		ImGui::Text("FPS: %.1f", io.Framerate);
		ImGui::Text("帧耗时: %.3f ms", 1000.0f / io.Framerate);

		ImGui::Separator();

		ImGui::Text("渲染: DX12 (Direct3D 12)");
		ImGui::Text("Draw Calls: (可扩展)");
		ImGui::Text("三角形数: (可接渲染统计)");

		ImGui::End();
	}

	void ShowToolsWindow()
	{
		if (!g_ShowTools) return;

		ImGui::Begin("功能窗口", &g_ShowTools);

		ImGui::Text("视觉工具");
		ImGui::Separator();

		if (ImGui::Button("打印ROI信息"))
			PrintROIToLog();

		if (ImGui::Button("清理图片"))
			ClearImage();

		if (ImGui::Button("边缘检测"))
			LogSystem::Add(LOG_INFO, color, "执行边缘检测");

		if (ImGui::Selectable("模板匹配", g_ShowTemplateMatch))
		{
			g_ShowTemplateMatch = !g_ShowTemplateMatch;
			LogSystem::Add(LOG_INFO, color, "模板匹配窗口 state=%d", g_ShowTemplateMatch);
		}

		if (ImGui::Button("Blob分析"))
			LogSystem::Add(LOG_INFO, color, "执行Blob分析");

		if (ImGui::Selectable("阈值调试", g_ShowThresholdWindow))
		{
			g_ShowThresholdWindow = !g_ShowThresholdWindow;
			LogSystem::Add(LOG_INFO, color, "阈值调试窗口 state=%d", g_ShowThresholdWindow);
		}

		ImGui::End();
	}

}
