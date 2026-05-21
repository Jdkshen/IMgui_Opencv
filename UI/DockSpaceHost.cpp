#include "../Windows_imgui.h"
#include "../Algorithm/TemplateMatch.h"
#include "ROIManager.h"
#include "ImageViewer.h"
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

// 图像显示状态已移到 ImageViewer.cpp / ROIManager.cpp 中定义

extern cv::Mat g_FrozenTemplate;  // 模板匹配冻结模板（定义在 TemplateMatch.cpp）

namespace UI
{

int g_ActiveToolIndex = -1;
std::vector<ToolInstance> g_ToolInstances;

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

		// =========================
		// ROI 类型切换
		// =========================
		const char* kROITypeNames[] = { "通用(0)", "模板(1)", "识别(2)", "类型3", "类型4" };
		if (ImGui::BeginCombo("##ROIType", kROITypeNames[gCurrentROIType]))
		{
			for (int i = 0; i < ROI_TYPE_COUNT; i++)
			{
				bool isSelected = (gCurrentROIType == i);
				ImU32 col = GetROIColor(i, false);
				ImVec4 col4 = ImGui::ColorConvertU32ToFloat4(col);
				ImGui::PushStyleColor(ImGuiCol_Text, col4);
				if (ImGui::Selectable(kROITypeNames[i], isSelected))
					gCurrentROIType = i;
				ImGui::PopStyleColor();
				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("右键画框时将创建此类型的ROI");

		ImGui::SameLine();
		if (ImGui::SmallButton("清除本类"))
		{
			gROIs.erase(
				std::remove_if(gROIs.begin(), gROIs.end(),
					[](const ROI& r) { return r.type == gCurrentROIType; }),
				gROIs.end());
			gSelectedROI = -1;
			gActiveHandle = HANDLE_NONE;
		}

		ImGui::Spacing();

		// 快捷操作
		if (ImGui::Button("打印ROI信息", ImVec2(-1, 0)))
			PrintROIToLog();
		if (ImGui::Button("清理图片", ImVec2(-1, 0)))
			ClearImage();

		ImGui::Spacing();

		ImGui::Separator();

		// 自定义日志输入
		static char inputBuf[256] = { 0 };
		ImGui::Text("自定义日志输入");
		ImGui::PushItemWidth(-1);
		ImGui::InputText("##log_input", inputBuf, sizeof(inputBuf));
		ImGui::PopItemWidth();
		if (ImGui::Button("发送到日志", ImVec2(-1, 28)))
		{
			if (strlen(inputBuf) > 0)
			{
				LogSystem::Add(LOG_INFO, color, "自定义: %s", inputBuf);
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
		static cv::Mat g_PersistOriginal;  // 持久保存原始图
		if (!g_ShowTools) return;

		ImGui::Begin("功能窗口", &g_ShowTools);

		const char* kToolNames[] = { "边缘检测", "模板匹配", "Blob分析", "阈值调试" };

		// ---- 顶部工具栏（固定，不滚动） ----
		if (ImGui::Button("+ 添加工具"))
			ImGui::OpenPopup("AddToolPopup");
		if (ImGui::BeginPopup("AddToolPopup"))
		{
			for (int i = 0; i < 4; i++)
				if (ImGui::Selectable(kToolNames[i]))
					g_ToolInstances.push_back({i});
			ImGui::EndPopup();
		}

		ImGui::Separator();

		// ---- 全部执行 逐帧状态机 ----
		static int  g_BatchRunIndex   = -1;
		static int  g_StepRunIndex    = -1;   // 单步执行索引
		static float g_StepTime       = 0.0f;  // 上一步耗时
		static auto g_BatchStartTime  = std::chrono::high_resolution_clock::now();
		static float g_BatchTotalTime = 0.0f;
		static cv::Mat g_BatchOriginalImage;  // 备份原始图，每实例恢复

		// 每帧执行一个工具实例（在渲染之前）—— 批量 or 单步共用
		int execIdx = (g_BatchRunIndex >= 0) ? g_BatchRunIndex : g_StepRunIndex;
		if (execIdx >= 0 && execIdx < (int)g_ToolInstances.size())
		{
			auto stepT0 = std::chrono::high_resolution_clock::now();
			// 每个实例开始前恢复原始图
			if (!g_BatchOriginalImage.empty())
				gImage = g_BatchOriginalImage.clone();

			int t = g_ToolInstances[execIdx].type;
			auto& it = g_ToolInstances[execIdx];
			const char* modeLabel = (g_BatchRunIndex >= 0) ? "[全部执行]" : "[单步执行]";
			LogSystem::Add(LOG_INFO, color, "%s %d/%zu: %s",
				modeLabel, execIdx + 1, g_ToolInstances.size(), kToolNames[t]);
			if (t == 0)
			{
				gCannyLow = it.cannyLow; gCannyHigh = it.cannyHigh;
				gUseGray = it.edgeUseGray;
				gPipe.enableCanny = true; gPipe.enableThreshold = false;
				gPipe.cannyLow = it.cannyLow; gPipe.cannyHigh = it.cannyHigh;
				ThresholdTool::ApplyProcess();
			}
			else if (t == 1) {
				g_TMEnableRotation = it.enableRotation;
				g_TMRotationStart  = it.rotationStart;
				g_TMRotationEnd    = it.rotationEnd;
				g_TMRotationStep   = it.rotationStep;
				g_TMMaxResults     = it.maxResults;
				g_TMMatchThreshold = it.matchThreshold;
				g_TMMaxImageDim    = it.maxImageDim;
				g_NmsThreshold     = it.nmsThreshold;
				g_TMSearchMode     = it.searchMode;
				g_TplGray          = it.tplGray;
				g_TplBinary        = it.tplBinary;
				g_TplBinThresh     = it.tplBinThresh;
				g_TplEdge          = it.tplEdge;
				g_TplEdgeLow       = it.tplEdgeLow;
				g_TplEdgeHigh      = it.tplEdgeHigh;

				bool didPreprocess = false;
				if (it.imgUseGray || it.imgEnableThreshold)
				{
					if (it.imgUseGray && gImage.channels() > 1)
					{
						int code = (gImage.channels() == 4) ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY;
						cv::cvtColor(gImage, gImage, code);
					}
					if (it.imgEnableThreshold)
					{
						if (gImage.channels() > 1)
							cv::cvtColor(gImage, gImage, cv::COLOR_BGR2GRAY);
						cv::threshold(gImage, gImage, it.imgThreshold, 255, cv::THRESH_BINARY);
					}
					cv::Mat rgba;
					if (gImage.channels() == 1) cv::cvtColor(gImage, rgba, cv::COLOR_GRAY2RGBA);
					else if (gImage.channels() == 3) cv::cvtColor(gImage, rgba, cv::COLOR_BGR2RGBA);
					else cv::cvtColor(gImage, rgba, cv::COLOR_BGRA2RGBA);
					gPendingUpload = rgba;
					gNeedUpload = true;
					didPreprocess = true;
				}
				gUseGray           = didPreprocess ? false : it.imgUseGray;
				gPipe.enableThreshold = didPreprocess ? false : it.imgEnableThreshold;
				gPipe.threshold    = it.imgThreshold;

				g_FrozenTemplate = it.templateImg;
				if (!it.searchROIs.empty())
					gROIs = it.searchROIs;
				TemplateMatch::Run();
			}
			else if (t == 2) { LogSystem::Add(LOG_INFO, color, "Blob分析: 执行完成"); }
			else if (t == 3) {
				gUseGray = it.dbgUseGray;
				gPipe.enableBlur = it.dbgEnableBlur; gPipe.blurSize = it.dbgBlurSize;
				gPipe.enableThreshold = it.dbgEnableThresh; gPipe.threshold = it.dbgThreshold;
				gPipe.enableCanny = it.dbgEnableCanny;
				gPipe.cannyLow = it.dbgCannyLow; gPipe.cannyHigh = it.dbgCannyHigh;
				ThresholdTool::ApplyProcess();
			}
			// 推进索引
			if (g_BatchRunIndex >= 0)
			{
				g_BatchRunIndex++;
				if (g_BatchRunIndex >= (int)g_ToolInstances.size())
				{
					auto t1 = std::chrono::high_resolution_clock::now();
					g_BatchTotalTime = std::chrono::duration<float, std::milli>(t1 - g_BatchStartTime).count();
					g_BatchRunIndex = -1;
					// 恢复原始图到显示
					if (!g_BatchOriginalImage.empty())
					{
						gImage = g_BatchOriginalImage.clone();
						cv::Mat rgba;
						if (gImage.channels() == 1) cv::cvtColor(gImage, rgba, cv::COLOR_GRAY2RGBA);
						else if (gImage.channels() == 3) cv::cvtColor(gImage, rgba, cv::COLOR_BGR2RGBA);
						else cv::cvtColor(gImage, rgba, cv::COLOR_BGRA2RGBA);
						gPendingUpload = rgba;
						gNeedUpload = true;
					}
					LogSystem::Add(LOG_INFO, color, "[全部执行] 完成，共 %zu 个工具，耗时 %.1fms",
						g_ToolInstances.size(), g_BatchTotalTime);
				}
			}
			// 单步模式不重置索引，由按钮控制推进
			if (g_StepRunIndex >= 0)
			{
				auto stepT1 = std::chrono::high_resolution_clock::now();
				g_StepTime = std::chrono::duration<float, std::milli>(stepT1 - stepT0).count();
			}
		}

		// ---- 可滚动工具列表（底部按钮预留 30px） ----
		float bottomH = g_ToolInstances.empty() ? 0.0f : 30.0f;
		ImGui::BeginChild("##ToolList", ImVec2(0, -bottomH), false);

		if (g_ToolInstances.empty())
		{
			ImGui::TextDisabled("暂无工具，点击上方 [+] 添加");
		}
		else
		{
			ImGuiStorage* storage = ImGui::GetStateStorage();
			int removeIdx = -1;
			int newActive = g_ActiveToolIndex;

			for (int inst = 0; inst < (int)g_ToolInstances.size(); inst++)
			{
				int type = g_ToolInstances[inst].type;
				auto& tpl = g_ToolInstances[inst].templateImg; // 该实例的模板
				char label[64];
				snprintf(label, sizeof(label), "%s %d",
					kToolNames[type], inst + 1);

				// 全部执行/单步执行时高亮当前实例
				bool isBatchActive = (inst == g_BatchRunIndex || inst == g_StepRunIndex);
				if (isBatchActive)
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));

				if (ImGui::CollapsingHeader(label))
				{
					if (g_ActiveToolIndex != inst)
						newActive = inst;

					ImGui::BeginChild("##frame", ImVec2(0, 0), true);
					if (type == 0) // 边缘检测
					{
						auto& it = g_ToolInstances[inst];
						ImGui::SliderInt("Canny低阈值", &it.cannyLow, 0, 255);
						ImGui::SliderInt("Canny高阈值", &it.cannyHigh, 0, 255);
						ImGui::Checkbox("转为灰度", &it.edgeUseGray);
						if (ImGui::Button("执行边缘检测", ImVec2(-1, 28)))
						{
							gCannyLow = it.cannyLow;
							gCannyHigh = it.cannyHigh;
							gUseGray = it.edgeUseGray;
							gPipe.enableCanny = true;
							gPipe.enableThreshold = false;
							gPipe.cannyLow = it.cannyLow;
							gPipe.cannyHigh = it.cannyHigh;
							ThresholdTool::ApplyProcess();
							LogSystem::Add(LOG_INFO, color, "边缘检测: Canny(%d,%d)", it.cannyLow, it.cannyHigh);
						}
					}
					else if (type == 1) // 模板匹配
					{
						auto& it = g_ToolInstances[inst];
						// -- 模板预览 --
						ImGui::Text("模板");
						if (gImage.empty())
						{
							ImGui::TextColored(ImVec4(1,0.5f,0,1), "请先在图像预览中加载图片");
						}
						else
						{
							// 已有模板：显示信息 + 预览按钮
							if (!tpl.empty())
							{
								ImGui::TextColored(ImVec4(0.3f,0.9f,0.3f,1), "当前: %dx%d",
									tpl.cols, tpl.rows);
								if (!g_ShowPreview)
								{ if (ImGui::SmallButton("预览")) g_ShowPreview = true; }
								else { if (ImGui::SmallButton("隐藏")) g_ShowPreview = false; }
								ImGui::SameLine();
							}
							// 有 ROI 时始终显示抓取按钮（可更新模板）
							if (!gROIs.empty())
							{
								if (ImGui::Button(tpl.empty() ? "抓取模板" : "重新抓取", ImVec2(-1, 22)))
								{
									int idx = (gSelectedROI >= 0 && gSelectedROI < (int)gROIs.size()) ? gSelectedROI : 0;
									const ROI& fr = gROIs[idx];
									float fx1 = std::min(fr.start.x, fr.end.x);
									float fy1 = std::min(fr.start.y, fr.end.y);
									float fx2 = std::max(fr.start.x, fr.end.x);
									float fy2 = std::max(fr.start.y, fr.end.y);
									int ftx = std::clamp((int)fx1, 0, gImage.cols-1);
									int fty = std::clamp((int)fy1, 0, gImage.rows-1);
									int ftw = std::clamp((int)(fx2-fx1), 1, gImage.cols-ftx);
									int fth = std::clamp((int)(fy2-fy1), 1, gImage.rows-fty);
									tpl = gImage(cv::Rect(ftx,fty,ftw,fth)).clone();
									gROIs.clear(); gSelectedROI = -1; gActiveHandle = HANDLE_NONE;
									LogSystem::Add(LOG_INFO, color, "模板已抓取: %dx%d", ftw, fth);
								}
							}
							else if (tpl.empty())
								ImGui::TextColored(ImVec4(1,0.5f,0,1), "请在图片上用右键框选ROI");
							// 像素预览（应用模板预处理）
							if (g_ShowPreview && !tpl.empty())
							{
								cv::Mat preview = tpl.clone();
								if (it.tplGray && preview.channels() > 1) cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
								if (it.tplBinary)
								{ if (preview.channels() > 1) cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
								  cv::threshold(preview, preview, it.tplBinThresh, 255, cv::THRESH_BINARY); }
								if (it.tplEdge)
								{ if (preview.channels() > 1) cv::cvtColor(preview, preview, cv::COLOR_BGR2GRAY);
								  cv::Canny(preview, preview, it.tplEdgeLow, it.tplEdgeHigh); }
								int maxPx = 80;
								float rs = maxPx / (float)std::max(preview.cols, preview.rows);
								if (rs > 1.0f) rs = 1.0f;
								int dw = (int)(preview.cols*rs), dh = (int)(preview.rows*rs);
								if (dw<2) dw=2; if (dh<2) dh=2;
								cv::resize(preview, preview, cv::Size(dw,dh), 0, 0, cv::INTER_NEAREST);
								ImDrawList* dl = ImGui::GetWindowDrawList();
								ImVec2 base = ImGui::GetCursorScreenPos(); float step = 2.0f;
								bool isColor = preview.channels()>=3;
								for (int y=0; y<dh; y++) for (int x=0; x<dw; x++)
								{
									ImU32 col;
									if (isColor)
									{ auto& p = preview.at<cv::Vec3b>(y,x); col=IM_COL32(p[2],p[1],p[0],255); }
									else
									{ uchar v = preview.at<uchar>(y,x); col=IM_COL32(v,v,v,255); }
									dl->AddRectFilled(ImVec2(base.x+x*step,base.y+y*step),
										ImVec2(base.x+(x+1)*step,base.y+(y+1)*step), col);
								}
								ImGui::Dummy(ImVec2(dw*step, dh*step));
							}
						}

						// 模板预处理（实例独立参数）
						ImGui::Text("模板预处理");
						ImGui::Checkbox("灰度##tm", &it.tplGray); ImGui::SameLine();
						ImGui::Checkbox("二值化##tm", &it.tplBinary);
						if (it.tplBinary)
							ImGui::SliderInt("模板阈值##tm", &it.tplBinThresh, 0, 255);
						ImGui::Checkbox("边缘##tm", &it.tplEdge);
						if (it.tplEdge)
						{
							ImGui::SliderInt("低阈值##tm", &it.tplEdgeLow, 0, 255);
							ImGui::SliderInt("高阈值##tm", &it.tplEdgeHigh, 0, 255);
						}

						ImGui::Separator();
						ImGui::Text("搜索范围");
						const char* kSearchModes[] = { "全图", "区域(ROI内)" };
						ImGui::Combo("##tmSearch", &it.searchMode, kSearchModes, 2);
						ImGui::SameLine();
						if (ImGui::SmallButton("保存ROI"))
						{
							g_ToolInstances[inst].searchROIs = gROIs;
							LogSystem::Add(LOG_INFO, color, "已保存 %zu 个搜索ROI到实例 %d",
								gROIs.size(), inst + 1);
						}
						if (!g_ToolInstances[inst].searchROIs.empty())
							ImGui::TextDisabled("已保存 %zu 个ROI", g_ToolInstances[inst].searchROIs.size());
						ImGui::Checkbox("启用旋转", &it.enableRotation);
						if (it.enableRotation)
						{
							ImGui::PushItemWidth(70);
							ImGui::SliderInt("起始角°", &it.rotationStart, -180, 0);
							ImGui::SameLine();
							ImGui::SliderInt("结束角°", &it.rotationEnd, 0, 180);
							ImGui::SameLine();
							ImGui::SliderInt("步长°", &it.rotationStep, 1, 10);
							ImGui::PopItemWidth();
						}
						ImGui::Separator();
						ImGui::Text("匹配参数");
						ImGui::SliderInt("最大结果数", &it.maxResults, 1, 100);
						ImGui::SliderFloat("匹配阈值", &it.matchThreshold, 0.0f, 1.0f, "%.3f");
						ImGui::SliderInt("匹配精度", &it.maxImageDim, 400, 2000);
						ImGui::TextDisabled("值越小越快，越大越精确");
						ImGui::SliderFloat("NMS阈值", &it.nmsThreshold, 0.0f, 1.0f, "%.3f");
						if (g_TMLastMatchTime > 0.0f)
							ImGui::TextDisabled("上次耗时: %.1fms", g_TMLastMatchTime);
						ImGui::Separator();
						ImGui::Text("预处理");
						ImGui::Checkbox("转为灰度", &it.imgUseGray);
						ImGui::SameLine();
						ImGui::Checkbox("二值化", &it.imgEnableThreshold);
						if (it.imgEnableThreshold)
							ImGui::SliderInt("阈值", &it.imgThreshold, 0, 255);
						if (ImGui::Button("执行匹配", ImVec2(-1, 28)))
						{
							// 维护持久原图备份（首次或图片更换时更新）
							if (g_PersistOriginal.empty() ||
								g_PersistOriginal.size() != gImage.size())
								g_PersistOriginal = gImage.clone();

							// 同步实例参数到全局变量
							g_TMEnableRotation = it.enableRotation;
							g_TMRotationStart  = it.rotationStart;
							g_TMRotationEnd    = it.rotationEnd;
							g_TMRotationStep   = it.rotationStep;
							g_TMMaxResults     = it.maxResults;
							g_TMMatchThreshold = it.matchThreshold;
							g_TMMaxImageDim    = it.maxImageDim;
							g_NmsThreshold     = it.nmsThreshold;
							g_TMSearchMode     = it.searchMode;
							g_TplGray          = it.tplGray;
							g_TplBinary        = it.tplBinary;
							g_TplBinThresh     = it.tplBinThresh;
							g_TplEdge          = it.tplEdge;
							g_TplEdgeLow       = it.tplEdgeLow;
							g_TplEdgeHigh      = it.tplEdgeHigh;

							// 图像预处理：修改 gImage 供匹配使用
							bool didPreprocess = false;
							if (it.imgUseGray || it.imgEnableThreshold)
							{
								if (it.imgUseGray && gImage.channels() > 1)
								{
									int code = (gImage.channels() == 4) ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY;
									cv::cvtColor(gImage, gImage, code);
								}
								if (it.imgEnableThreshold)
								{
									if (gImage.channels() > 1)
										cv::cvtColor(gImage, gImage, cv::COLOR_BGR2GRAY);
									cv::threshold(gImage, gImage, it.imgThreshold, 255, cv::THRESH_BINARY);
								}
								didPreprocess = true;
								LogSystem::Add(LOG_INFO, color, "[执行] 图像预处理: gray=%d thresh=%d(%d)",
									it.imgUseGray, it.imgEnableThreshold, it.imgThreshold);
							}
							gUseGray           = didPreprocess ? false : it.imgUseGray;
							gPipe.enableThreshold = didPreprocess ? false : it.imgEnableThreshold;
							gPipe.threshold    = it.imgThreshold;

							g_FrozenTemplate = tpl;
							if (!g_ToolInstances[inst].searchROIs.empty())
								gROIs = g_ToolInstances[inst].searchROIs;
							TemplateMatch::Run();

							// 未预处理则恢复持久原图；已预处理则保留处理后的图
							if (!didPreprocess)
								gImage = g_PersistOriginal.clone();
							cv::Mat rgba;
							if (gImage.channels() == 1)
								cv::cvtColor(gImage, rgba, cv::COLOR_GRAY2RGBA);
							else if (gImage.channels() == 3)
								cv::cvtColor(gImage, rgba, cv::COLOR_BGR2RGBA);
							else
								cv::cvtColor(gImage, rgba, cv::COLOR_BGRA2RGBA);
							gPendingUpload = rgba;
							gNeedUpload = true;
						}
						if (!gMatchROIs.empty())
						{
							ImGui::Text("匹配结果: %zu 个", gMatchROIs.size());
							if (ImGui::SmallButton("清空结果"))
								TemplateMatch::Clear();
						}
					}
					else if (type == 2) // Blob分析
					{
						static int blobMinArea = 100;
						static int blobMaxArea = 10000;
						ImGui::SliderInt("最小面积", &blobMinArea, 1, 10000);
						ImGui::SliderInt("最大面积", &blobMaxArea, 100, 100000);
						if (ImGui::Button("执行Blob分析", ImVec2(-1, 28)))
							LogSystem::Add(LOG_INFO, color, "Blob分析: 面积=%d~%d", blobMinArea, blobMaxArea);
					}
					else if (type == 3) // 阈值调试
					{
						auto& it = g_ToolInstances[inst];
						if (ImGui::Button("重置参数"))
						{
							it.dbgUseGray = false; it.dbgEnableBlur = false; it.dbgBlurSize = 5;
							it.dbgEnableThresh = false; it.dbgThreshold = 128;
							it.dbgEnableCanny = false; it.dbgCannyLow = 50; it.dbgCannyHigh = 150;
							gUseGray = it.dbgUseGray;
							gPipe.enableBlur = it.dbgEnableBlur; gPipe.blurSize = it.dbgBlurSize;
							gPipe.enableThreshold = it.dbgEnableThresh; gPipe.threshold = it.dbgThreshold;
							gPipe.enableCanny = it.dbgEnableCanny;
							gPipe.cannyLow = it.dbgCannyLow; gPipe.cannyHigh = it.dbgCannyHigh;
							ThresholdTool::ApplyProcess();
						}
						ImGui::Checkbox("转为灰度", &it.dbgUseGray);
						if (ImGui::IsItemDeactivatedAfterEdit()) { gUseGray = it.dbgUseGray; ThresholdTool::ApplyProcess(); }
						ImGui::Checkbox("高斯模糊", &it.dbgEnableBlur);
						if (it.dbgEnableBlur && ImGui::SliderInt("模糊核", &it.dbgBlurSize, 1, 10))
						{ gPipe.enableBlur = true; gPipe.blurSize = it.dbgBlurSize; ThresholdTool::ApplyProcess(); }
						if (ImGui::Checkbox("二值化", &it.dbgEnableThresh))
						{ gPipe.enableThreshold = it.dbgEnableThresh; gPipe.threshold = it.dbgThreshold; ThresholdTool::ApplyProcess(); }
						if (it.dbgEnableThresh && ImGui::SliderInt("阈值", &it.dbgThreshold, 0, 255))
						{ gPipe.threshold = it.dbgThreshold; ThresholdTool::ApplyProcess(); }
						if (ImGui::Checkbox("Canny边缘", &it.dbgEnableCanny))
						{ gPipe.enableCanny = it.dbgEnableCanny; gPipe.cannyLow = it.dbgCannyLow; gPipe.cannyHigh = it.dbgCannyHigh; ThresholdTool::ApplyProcess(); }
						if (it.dbgEnableCanny)
						{
							if (ImGui::SliderInt("Canny低", &it.dbgCannyLow, 0, 255))
							{ gPipe.cannyLow = it.dbgCannyLow; ThresholdTool::ApplyProcess(); }
							if (ImGui::SliderInt("Canny高", &it.dbgCannyHigh, 0, 255))
							{ gPipe.cannyHigh = it.dbgCannyHigh; ThresholdTool::ApplyProcess(); }
						}
						ImGui::Separator();
						if (ImGui::Button("执行处理", ImVec2(-1, 28)))
						{
							gUseGray = it.dbgUseGray;
							gPipe.enableBlur = it.dbgEnableBlur; gPipe.blurSize = it.dbgBlurSize;
							gPipe.enableThreshold = it.dbgEnableThresh; gPipe.threshold = it.dbgThreshold;
							gPipe.enableCanny = it.dbgEnableCanny;
							gPipe.cannyLow = it.dbgCannyLow; gPipe.cannyHigh = it.dbgCannyHigh;
							ThresholdTool::ApplyProcess();
						}
						if (gTimeTotal > 0.0f) ImGui::TextDisabled("总耗时: %.1fms", gTimeTotal);
					}

					ImGui::Separator();
					if (ImGui::Button("移除", ImVec2(-1, 0)))
						removeIdx = inst;
					ImGui::EndChild();
				}
				if (isBatchActive)
					ImGui::PopStyleColor();
			}

			// 移除实例
			if (removeIdx >= 0)
			{
				g_ToolInstances.erase(g_ToolInstances.begin() + removeIdx);
				if (g_ActiveToolIndex == removeIdx)
					g_ActiveToolIndex = -1;
				else if (g_ActiveToolIndex > removeIdx)
					g_ActiveToolIndex--;
			}

			// 手风琴：关闭所有非活跃的 CollapsingHeader
			if (g_ActiveToolIndex != newActive && g_ActiveToolIndex >= 0
				&& g_ActiveToolIndex < (int)g_ToolInstances.size())
			{
				char oldLabel[64];
				snprintf(oldLabel, sizeof(oldLabel), "%s %d",
					kToolNames[g_ToolInstances[g_ActiveToolIndex].type], g_ActiveToolIndex + 1);
				storage->SetInt(ImGui::GetID(oldLabel), 0);
			}
			for (int i = 0; i < (int)g_ToolInstances.size(); i++)
			{
				if (i != newActive)
				{
					char cl[64];
					snprintf(cl, sizeof(cl), "%s %d", kToolNames[g_ToolInstances[i].type], i + 1);
					storage->SetInt(ImGui::GetID(cl), 0);
				}
			}
			g_ActiveToolIndex = newActive;
		}

		ImGui::EndChild();

		// ---- 底部：全部执行按钮 ----
		if (!g_ToolInstances.empty())
		{
			ImGui::Separator();
			if (ImGui::Button("全部执行"))
			{
				g_BatchOriginalImage = gImage.clone();
				g_BatchStartTime = std::chrono::high_resolution_clock::now();
				g_BatchRunIndex = 0;
				g_BatchTotalTime = 0.0f;
				g_StepRunIndex = -1;  // 取消单步
			}
			ImGui::SameLine();
			bool stepping = (g_StepRunIndex >= 0);
			if (stepping) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
			if (ImGui::Button(stepping ? "单步中..." : "单步执行"))
			{
				g_BatchRunIndex = -1;  // 取消批量
				g_BatchTotalTime = 0.0f;
				if (g_StepRunIndex < 0)
				{
					g_BatchOriginalImage = gImage.clone();
					g_StepRunIndex = 0;
				}
				else
				{
					// 当前正在单步中，点按钮推进到下一个
					g_StepRunIndex++;
					if (g_StepRunIndex >= (int)g_ToolInstances.size())
					{
						g_StepRunIndex = -1;
						if (!g_BatchOriginalImage.empty())
						{
							gImage = g_BatchOriginalImage.clone();
							cv::Mat rgba;
							if (gImage.channels() == 1) cv::cvtColor(gImage, rgba, cv::COLOR_GRAY2RGBA);
							else if (gImage.channels() == 3) cv::cvtColor(gImage, rgba, cv::COLOR_BGR2RGBA);
							else cv::cvtColor(gImage, rgba, cv::COLOR_BGRA2RGBA);
							gPendingUpload = rgba;
							gNeedUpload = true;
						}
					}
				}
			}
			if (stepping) ImGui::PopStyleColor();
			if (g_StepTime > 0.0f)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("单步");
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.3f, 1.0f), "%.1fms", g_StepTime);
			}
			if (g_BatchTotalTime > 0.0f)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("总耗时");
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.3f, 1.0f), "%.1fms", g_BatchTotalTime);
			}
		}

		ImGui::End();
	}

}
