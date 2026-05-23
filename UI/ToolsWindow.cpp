#include "../Windows_imgui.h"
#include "ToolsWindow.h"
#include "ImageViewer.h"
#include "ROIManager.h"
#include "../Log/LogSystem.h"
#include "../Algorithm/ThresholdTool.h"
#include "../Algorithm/TemplateMatch.h"

extern cv::Mat g_FrozenTemplate;  // 定义在 TemplateMatch.cpp

namespace UI
{

int g_ActiveToolIndex = -1;
std::vector<ToolInstance> g_ToolInstances;

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

    // ---- 全部执行 逐帧状态机（批量/单步共用） ----
    static int  g_BatchRunIndex   = -1;   // 批量执行当前索引
    static int  g_StepRunIndex    = -1;   // 单步执行索引（-1=空闲）
    static int  g_StepCursor      = 0;    // 单步进度（持久，不随帧重置）
    static bool g_LoopMode        = false; // 循环执行模式
    static int  g_SwitchDelay     = 0;     // 切换图片/循环等待帧数
    static float g_StepTime       = 0.0f;  // 上一步耗时
    static auto g_BatchStartTime  = std::chrono::high_resolution_clock::now();
    static float g_BatchTotalTime = 0.0f;
    static cv::Mat g_BatchOriginalImage;  // 备份原始图，每实例恢复
    static bool g_BatchImageDirty  = false; // 图片被修改标记（避免无效clone）

    // 每帧执行一个工具实例（渲染之前执行，确保本帧能看到高亮）
    int execIdx = (g_BatchRunIndex >= 0) ? g_BatchRunIndex : g_StepRunIndex;

    // 切换图片/循环前的等待延迟（让用户看清结果）
    if (execIdx < 0 && g_SwitchDelay > 0)
    {
        g_SwitchDelay--;
        // 第一段延迟：显示结果停留
        if (g_SwitchDelay <= 0 && g_BatchRunIndex != -2)
        {
            bool hasNext = (!gImageList.empty() && gCurrentImageIndex >= 0 &&
                gCurrentImageIndex < (int)gImageList.size() - 1);
            if (hasNext) {
                NavigateNextImage();
                FitImageToWindow();
                g_SwitchDelay = 10;      // 第二段延迟：等图片加载
                g_BatchRunIndex = -2;
            } else if (g_LoopMode) {
                g_BatchRunIndex = 0;
                g_BatchStartTime = std::chrono::high_resolution_clock::now();
                g_BatchOriginalImage = gImage.clone();
            }
        }
        // 第二段延迟：图片加载完后启动批量
        else if (g_BatchRunIndex == -2 && g_SwitchDelay <= 0 && !gImage.empty())
        {
            g_BatchRunIndex = 0;
            g_BatchStartTime = std::chrono::high_resolution_clock::now();
            g_BatchOriginalImage = gImage.clone();
        }
        execIdx = -2;
    }

    if (execIdx >= 0 && execIdx < (int)g_ToolInstances.size())
    {
        auto stepT0 = std::chrono::high_resolution_clock::now();
        // 仅在上一工具修改了图片时才恢复（避免无效 clone）
        if (g_BatchImageDirty && !g_BatchOriginalImage.empty())
        {
            g_BatchOriginalImage.copyTo(gImage);  // 复用内存
            g_BatchImageDirty = false;
        }

        int t = g_ToolInstances[execIdx].type;
        auto& it = g_ToolInstances[execIdx];
        const char* modeLabel = (g_BatchRunIndex >= 0) ? "[全部执行]" : "[单步执行]";
        LogSystem::Add(LOG_INFO, color, "%s %d/%zu: %s",
            modeLabel, execIdx + 1, g_ToolInstances.size(), kToolNames[t]);
        if (t == 0) // 边缘检测：同步实例参数 → ApplyProcess
        {
            gCannyLow = it.cannyLow; gCannyHigh = it.cannyHigh;
            gUseGray = it.edgeUseGray;
            gPipe.enableCanny = true; gPipe.enableThreshold = false;
            gPipe.cannyLow = it.cannyLow; gPipe.cannyHigh = it.cannyHigh;
            ThresholdTool::ApplyProcess();
        }
        else if (t == 1) { // 模板匹配：同步全部参数 + 图像预处理 + Run
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

            // 图像预处理：灰度/二值化 gImage（批量模式）
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
            g_BatchImageDirty = didPreprocess;  // 记录图片是否被修改
        }
        else if (t == 2) { LogSystem::Add(LOG_INFO, color, "Blob分析: 执行完成"); } // Blob分析（占位）
        else if (t == 3) { // 阈值调试：同步参数 → ApplyProcess
            gUseGray = it.dbgUseGray;
            gPipe.enableBlur = it.dbgEnableBlur; gPipe.blurSize = it.dbgBlurSize;
            gPipe.enableThreshold = it.dbgEnableThresh; gPipe.threshold = it.dbgThreshold;
            gPipe.enableCanny = it.dbgEnableCanny;
            gPipe.cannyLow = it.dbgCannyLow; gPipe.cannyHigh = it.dbgCannyHigh;
            ThresholdTool::ApplyProcess();
        }
        // 推进索引：批量自动+1，单步由按钮控制
        if (g_BatchRunIndex >= 0)
        {
            g_BatchRunIndex++;
            if (g_BatchRunIndex >= (int)g_ToolInstances.size())
            {
                auto t1 = std::chrono::high_resolution_clock::now();
                g_BatchTotalTime = std::chrono::duration<float, std::milli>(t1 - g_BatchStartTime).count();
                // 多图/循环模式：先停留若干帧让用户看清结果，再切换
                bool hasNextImage = (!gImageList.empty() && gCurrentImageIndex >= 0 &&
                    gCurrentImageIndex < (int)gImageList.size() - 1);
                if (hasNextImage || g_LoopMode)
                {
                    g_BatchRunIndex = -1;       // 暂停执行
                    g_SwitchDelay = 60;          // 等待约1秒（60帧）再切换
                }
                else
                {
                    g_BatchRunIndex = -1;
                }
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
        // 单步模式：执行完当前即停，防止循环
        if (g_StepRunIndex >= 0)
        {
            auto stepT1 = std::chrono::high_resolution_clock::now();
            g_StepTime = std::chrono::duration<float, std::milli>(stepT1 - stepT0).count();
            g_StepRunIndex = -1;  // 标记已执行，等待下次按钮点击
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
            bool isBatchActive = (inst == g_BatchRunIndex || inst == g_StepRunIndex || (g_StepCursor > 0 && inst == g_StepCursor - 1));
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

    // ---- 底部：全部执行 / 单步执行 按钮 ----
    if (!g_ToolInstances.empty())
    {
        ImGui::Separator();
        // 全部执行按钮：一键启动所有工具逐帧执行
        if (ImGui::Button("全部执行"))
        {
            g_BatchOriginalImage = gImage.clone();
            g_BatchImageDirty = false;
            g_BatchStartTime = std::chrono::high_resolution_clock::now();
            g_BatchRunIndex = 0;
            g_BatchTotalTime = 0.0f;
            g_SwitchDelay = 0;       // 清除切换延迟
            g_StepRunIndex = -1;   // 取消单步执行
            g_StepCursor = 0;       // 重置单步进度
        }
        ImGui::SameLine();
        // 单步执行按钮：点一下执行一个，按钮变蓝"单步中..."
        bool stepping = (g_StepCursor > 0 && g_StepCursor <= (int)g_ToolInstances.size());
        if (stepping) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
        if (ImGui::Button(stepping ? "单步中..." : "单步执行"))
        {
            g_BatchRunIndex = -1;
            g_BatchTotalTime = 0.0f;
            if (g_StepCursor >= (int)g_ToolInstances.size())
            {
                // 上一轮已完成，重置且不执行（回到空闲）
                g_StepCursor = 0;
                g_StepRunIndex = -1;
                g_StepTime = 0.0f;
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
            else
            {
                if (g_StepCursor == 0)
                    g_BatchOriginalImage = gImage.clone();  // 新轮：备份原图
                g_StepRunIndex = g_StepCursor;  // 触发执行当前步骤
                g_StepCursor++;
            }
        }
        if (stepping) ImGui::PopStyleColor();
        ImGui::SameLine();
        // 循环按钮：自动重复执行所有工具（先存状态，避免按钮内修改导致 Pop 丢失）
        bool wasLooping = g_LoopMode;
        if (wasLooping) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.3f, 1.0f));
        if (ImGui::Button(wasLooping ? "循环中" : "循环"))
        {
            g_LoopMode = !g_LoopMode;
            if (!g_LoopMode)
            {
                // 关闭循环：停止批量执行，恢复原图
                g_BatchRunIndex = -1;
                g_SwitchDelay = 0;
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
        if (wasLooping) ImGui::PopStyleColor();
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

} // namespace UI
