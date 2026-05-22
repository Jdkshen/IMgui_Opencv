#include "../imgui/imgui.h"       // 必须在 TemplateMatch.h 之前，确保 ImVec2 已定义
#include "TemplateMatch.h"
#include "../Core/DX12Context.h"
#include "../Log/LogSystem.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <mutex>

// 模板匹配参数引用（来自 UI 命名空间）
using UI::gROIs;
using UI::gSelectedROI;
using UI::gActiveHandle;
using UI::PrintROIToLog;
using UI::ClearROIState;
using UI::ImageToScreenPos;
using UI::ScreenToImagePos;
using UI::NormalizeROI;

// =========================
// 全局变量定义
// =========================
std::vector<ROI> gMatchROIs;             // 模板匹配结果
std::vector<double> gMatchScores;        // 对应匹配分数
std::vector<float>  gMatchAngles;        // 对应匹配角度（度）
bool g_ShowTemplateMatch = false;        // 调试窗口显示标志
bool g_PendingMatch = false;             // 待执行匹配标志（下一帧执行）

// =========================
// 模板匹配参数（extern，可被配方系统读写）
// =========================
float  g_TMMatchThreshold  = 0.75f;   // 匹配分数阈值
float  g_NmsThreshold    = 0.30f;   // NMS 重叠阈值
int    g_TMMaxImageDim     = 1000;    // 匹配前自动缩放到此尺寸以内（越小越快）
float  g_TMLastMatchTime   = 0.0f;    // 上次匹配耗时 (ms)
double g_TMLastBestScore   = 0.0;     // 上次最佳分数
int    g_TMSearchMode      = 0;       // 0=全图 1=区域（只在ROI内搜索）
int    g_TMMaxResults      = 10;      // 最多显示匹配结果数
bool   g_TMEnableRotation  = false;   // 启用旋转模板匹配
int    g_TMRotationStart   = -5;      // 起始角度（度）
int    g_TMRotationEnd     = 5;       // 结束角度（度）
int    g_TMRotationStep    = 5;       // 步长（度，越大越快）

// 预览冻结（独立存储，不依赖 ROI）
static int    g_FrozenTplIdx    = -1;
static ImVec2 g_FrozenTplStart, g_FrozenTplEnd;
cv::Mat g_FrozenTemplate;           // 模板图像数据（独立拷贝）
static int    g_FrozenImgW      = 0;       // 抓取时的图片宽度
static int    g_FrozenImgH      = 0;       // 抓取时的图片高度
bool   g_ShowPreview     = false;
static bool   g_ShowTplEditor   = false;   // 模板编辑弹窗

// 模板独立处理参数（不影响源图）
bool   g_TplGray         = false;   // 模板灰度
bool   g_TplBinary       = false;   // 模板二值化
int    g_TplBinThresh    = 128;     // 模板二值化阈值
bool   g_TplEdge         = false;   // 模板边缘检测
int    g_TplEdgeLow      = 50;      // Canny 低阈值
int    g_TplEdgeHigh     = 150;     // Canny 高阈值

// 预处理参数复用 ThresholdTool 的 gUseGray 和 gPipe
extern bool gUseGray;                     // ThresholdTool.cpp
extern PipelineState gPipe;                // ThresholdTool.cpp

extern cv::Mat gImage;                    // 当前处理图像（ThresholdTool.cpp）
extern ImVec4 color;                      // 日志颜色（DockSpaceHost.cpp）
extern D3D12_GPU_DESCRIPTOR_HANDLE gSrvGpuHandle;  // GPU纹理句柄（DX12Context.cpp）
extern int gImageWidth;                   // 图片宽度（DX12Context.cpp）
extern int gImageHeight;                  // 图片高度（DX12Context.cpp）

// =========================
// 模板匹配命名空间实现
// =========================
namespace TemplateMatch
{
    struct Match { cv::Point pt; double score; float angle; };

    // =========================
    // 模板匹配：用选中的ROI在图像中搜索相似区域
    // =========================
    void Run()
    {
        using clock = std::chrono::high_resolution_clock;
        auto tStart = clock::now();
        OutputDebugStringA("[TM] Run() enter\n");

        // ===== 阶段1：安全校验 =====
        if (gImage.empty() || gImage.data == nullptr || gImage.dims != 2)
        {
            OutputDebugStringA("[TM] FAIL: image invalid\n");
            LogSystem::Add(LOG_WARN, color, "模板匹配: 图片无效 (empty=%d data=%p dims=%d)",
                gImage.empty(), (void*)gImage.data, gImage.dims);
            return;
        }
        if (gROIs.empty() && g_FrozenTemplate.empty())
        {
            LogSystem::Add(LOG_WARN, color, "模板匹配: 请先用右键框选一个ROI作为模板");
            return;
        }
        OutputDebugStringA("[TM] validation OK\n");

        // ===== 阶段2：提取模板 ROI 坐标 =====
        float x1, y1, x2, y2;
        int tplIdx;
        if (!g_FrozenTemplate.empty())
        {
            tplIdx = -1;  // 模板已独立保存，区域搜索不跳过任何ROI
            x1 = 0; y1 = 0; x2 = 0; y2 = 0;  // 不需要从 ROI 提取坐标
        }
        else if (g_FrozenTplIdx >= 0 && g_FrozenTplIdx < (int)gROIs.size())
        {
            tplIdx = g_FrozenTplIdx;
            x1 = std::min(g_FrozenTplStart.x, g_FrozenTplEnd.x);
            y1 = std::min(g_FrozenTplStart.y, g_FrozenTplEnd.y);
            x2 = std::max(g_FrozenTplStart.x, g_FrozenTplEnd.x);
            y2 = std::max(g_FrozenTplStart.y, g_FrozenTplEnd.y);
        }
        else
        {
            tplIdx = (gSelectedROI >= 0 && gSelectedROI < (int)gROIs.size())
                ? gSelectedROI : 0;
            const ROI& r = gROIs[tplIdx];
            x1 = std::min(r.start.x, r.end.x);
            y1 = std::min(r.start.y, r.end.y);
            x2 = std::max(r.start.x, r.end.x);
            y2 = std::max(r.start.y, r.end.y);
        }

        int tx = std::clamp((int)x1, 0, gImage.cols - 1);
        int ty = std::clamp((int)y1, 0, gImage.rows - 1);
        int tw = (int)(x2 - x1);
        int th = (int)(y2 - y1);
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
        if (tx + tw > gImage.cols) tw = gImage.cols - tx;
        if (ty + th > gImage.rows) th = gImage.rows - ty;

        // 模板有效性检查
        if (tw <= 0 || th <= 0 || tx < 0 || ty < 0)
        {
            OutputDebugStringA("[TM] FAIL: rect invalid\n");
            LogSystem::Add(LOG_ERROR, color, "模板匹配: 模板区域无效 (%d,%d %dx%d)", tx, ty, tw, th);
            return;
        }

        char dbg[256];
        snprintf(dbg, sizeof(dbg), "[TM] ROI=(%d,%d %dx%d) img=%dx%d thresh=%.3f\n",
            tx, ty, tw, th, gImage.cols, gImage.rows, g_TMMatchThreshold);
        OutputDebugStringA(dbg);

        // ===== 阶段3：自动降采样（缩放到 g_TMMaxImageDim 以内）=====
        cv::Mat srcImage;
        cv::Mat templ;
        float scale = 1.0f;
        int maxDim = std::max(gImage.cols, gImage.rows);

        if (!g_FrozenTemplate.empty())
        {
            // 使用冻结的独立模板
            OutputDebugStringA("[TM] using frozen template\n");
            if (g_FrozenTemplate.cols < 1 || g_FrozenTemplate.rows < 1)
            {
                g_FrozenTemplate.release();
                return;
            }
            if (maxDim > g_TMMaxImageDim)
            {
                scale = (float)g_TMMaxImageDim / maxDim;
                cv::resize(gImage, srcImage, cv::Size(), scale, scale, cv::INTER_AREA);
                cv::resize(g_FrozenTemplate, templ, cv::Size(), scale, scale, cv::INTER_AREA);
            }
            else
            {
                srcImage = gImage;
                templ = g_FrozenTemplate;
            }
        }
        else if (maxDim > g_TMMaxImageDim)
        {
            OutputDebugStringA("[TM] downscaling...\n");
            scale = (float)g_TMMaxImageDim / maxDim;
            cv::resize(gImage, srcImage, cv::Size(), scale, scale, cv::INTER_AREA);

            int stx = (int)(tx * scale);
            int sty = (int)(ty * scale);
            int stw = (int)(tw * scale);
            int sth = (int)(th * scale);
            if (stx < 0) stx = 0;
            if (sty < 0) sty = 0;
            if (stw < 1) stw = 1;
            if (sth < 1) sth = 1;
            if (stx >= srcImage.cols) stx = srcImage.cols - 1;
            if (sty >= srcImage.rows) sty = srcImage.rows - 1;
            if (stx + stw > srcImage.cols) stw = srcImage.cols - stx;
            if (sty + sth > srcImage.rows) sth = srcImage.rows - sty;
            templ = srcImage(cv::Rect(stx, sty, stw, sth)).clone();

            snprintf(dbg, sizeof(dbg), "[TM] downscale %.1f%% -> %dx%d tpl=%dx%d\n",
                scale * 100, srcImage.cols, srcImage.rows, templ.cols, templ.rows);
            OutputDebugStringA(dbg);

            LogSystem::Add(LOG_INFO, color,
                "模板匹配: 图片已缩放 %.1f%% (%dx%d → %dx%d)",
                scale * 100, gImage.cols, gImage.rows, srcImage.cols, srcImage.rows);
        }
        else
        {
            OutputDebugStringA("[TM] no downscale\n");
            srcImage = gImage;
            // 最后一道安全锁
            if (tx < 0) tx = 0;
            if (ty < 0) ty = 0;
            if (tx >= gImage.cols) tx = gImage.cols - 1;
            if (ty >= gImage.rows) ty = gImage.rows - 1;
            if (tx + tw > gImage.cols) tw = gImage.cols - tx;
            if (ty + th > gImage.rows) th = gImage.rows - ty;
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
            templ = gImage(cv::Rect(tx, ty, tw, th)).clone();
        }

        if (templ.empty() || templ.cols > srcImage.cols || templ.rows > srcImage.rows)
        {
            OutputDebugStringA("[TM] FAIL: templ invalid\n");
            LogSystem::Add(LOG_ERROR, color, "模板匹配: 模板尺寸异常 tpl=%dx%d src=%dx%d",
                templ.cols, templ.rows, srcImage.cols, srcImage.rows);
            return;
        }

        LogSystem::Add(LOG_INFO, color,
            "模板匹配: 模板ROI[%d] (%d,%d %dx%d), 模板大小=%dx%d",
            tplIdx, tx, ty, tw, th, templ.cols, templ.rows);

        // ===== 阶段4：预处理源图（大图）=====
        {
            char pdbg[256];
            snprintf(pdbg, sizeof(pdbg), "[TM] preprocess flags: useGray=%d enableThresh=%d thresh=%d\n",
                gUseGray, gPipe.enableThreshold, gPipe.threshold);
            OutputDebugStringA(pdbg);
        }
        cv::Mat procSrc;
        // 转为灰度（兼容3ch BGR 和 4ch BGRA）
        auto ToGray = [](const cv::Mat& s, cv::Mat& d) {
            cv::cvtColor(s, d, s.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        };
        if (gUseGray && srcImage.channels() > 1)
            ToGray(srcImage, procSrc);
        else if (gPipe.enableThreshold && srcImage.channels() > 1)
            ToGray(srcImage, procSrc);
        else
            procSrc = srcImage.clone();  // 深拷贝，避免后续原地修改影响原图
        if (gPipe.enableThreshold)
            cv::threshold(procSrc, procSrc, gPipe.threshold, 255, cv::THRESH_BINARY);

        // ===== 阶段5：预处理模板（小图，独立参数）=====
        cv::Mat procTpl;
        if (g_TplGray && templ.channels() > 1)
            ToGray(templ, procTpl);
        else if (g_TplBinary && templ.channels() > 1)
            ToGray(templ, procTpl);
        else
            procTpl = templ.clone();  // 深拷贝
        if (g_TplBinary)
            cv::threshold(procTpl, procTpl, g_TplBinThresh, 255, cv::THRESH_BINARY);
        if (g_TplEdge)
        {
            cv::Mat g;
            if (procTpl.channels() > 1) cv::cvtColor(procTpl, g, cv::COLOR_BGR2GRAY);
            else g = procTpl;
            cv::Canny(g, procTpl, g_TplEdgeLow, g_TplEdgeHigh);
        }

        srcImage = procSrc;
        templ = procTpl;

        // ===== 阶段6：通道数对齐（matchTemplate 要求一致）=====
        if (templ.channels() != srcImage.channels())
        {
            cv::Mat tmp;
            if (templ.channels() == 1 && srcImage.channels() > 1)
            {
                cv::cvtColor(srcImage, tmp, cv::COLOR_BGR2GRAY);
                srcImage = tmp;
            }
            else if (srcImage.channels() == 1 && templ.channels() > 1)
            {
                cv::cvtColor(templ, tmp, cv::COLOR_BGR2GRAY);
                templ = tmp;
            }
            else if (srcImage.channels() == 4)
            {
                cv::cvtColor(srcImage, tmp, cv::COLOR_BGRA2BGR);
                srcImage = tmp;
            }
            else if (templ.channels() == 4)
            {
                cv::cvtColor(templ, tmp, cv::COLOR_BGRA2BGR);
                templ = tmp;
            }
        }

        snprintf(dbg, sizeof(dbg), "[TM] preprocess done, src=%dx%d ch=%d tpl=%dx%d ch=%d\n",
            srcImage.cols, srcImage.rows, srcImage.channels(),
            templ.cols, templ.rows, templ.channels());
        OutputDebugStringA(dbg);

        // 清除旧结果
        gMatchROIs.clear();
        gMatchScores.clear();
        gMatchAngles.clear();

        // ===== 阶段7：执行匹配（全图 / 区域ROI）=====
        const double matchThreshold = g_TMMatchThreshold;
        const int maxCandidates = g_TMMaxResults * 10;
        std::vector<Match> candidates;
        auto tMatch0 = clock::now();

        // 预计算旋转模板（避免每个区域重复 warpAffine）
        int a0 = g_TMEnableRotation ? g_TMRotationStart : 0;
        int a1 = g_TMEnableRotation ? g_TMRotationEnd   : 0;
        int aStep = g_TMEnableRotation ? g_TMRotationStep : 1;
        std::vector<std::pair<int, cv::Mat>> rotTemplates;
        for (int ang = a0; ang <= a1; ang += aStep)
        {
            if (ang != 0)
            {
                cv::Mat r;
                cv::Point2f ctr(templ.cols/2.f, templ.rows/2.f);
                cv::Mat M = cv::getRotationMatrix2D(ctr, (double)ang, 1.0);
                cv::warpAffine(templ, r, M, templ.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
                rotTemplates.push_back({ang, r});
            }
            else
                rotTemplates.push_back({0, templ});
        }

        if (g_TMSearchMode == 1 && (int)gROIs.size() > 0
            && (!g_FrozenTemplate.empty() || (int)gROIs.size() > 1))
        {
            // 区域搜索
            OutputDebugStringA("[TM] region search mode\n");
            for (int ri = 0; ri < (int)gROIs.size(); ri++)
            {
                if (tplIdx >= 0 && ri == tplIdx) continue;

                // 先 clamp 到原图边界，再缩放
                float rx1 = std::clamp(std::min(gROIs[ri].start.x, gROIs[ri].end.x), 0.0f, (float)gImage.cols);
                float ry1 = std::clamp(std::min(gROIs[ri].start.y, gROIs[ri].end.y), 0.0f, (float)gImage.rows);
                float rx2 = std::clamp(std::max(gROIs[ri].start.x, gROIs[ri].end.x), 0.0f, (float)gImage.cols);
                float ry2 = std::clamp(std::max(gROIs[ri].start.y, gROIs[ri].end.y), 0.0f, (float)gImage.rows);

                int rix = (int)(rx1 * scale);
                int riy = (int)(ry1 * scale);
                int riw = (int)((rx2 - rx1) * scale);
                int rih = (int)((ry2 - ry1) * scale);
                // 最终边界校正
                if (rix < 0) rix = 0;
                if (riy < 0) riy = 0;
                if (riw < 1) riw = 1;
                if (rih < 1) rih = 1;
                if (rix >= srcImage.cols) { OutputDebugStringA("[TM] skip rix>=cols\n"); continue; }
                if (riy >= srcImage.rows) { OutputDebugStringA("[TM] skip riy>=rows\n"); continue; }
                if (rix + riw > srcImage.cols) riw = srcImage.cols - rix;
                if (riy + rih > srcImage.rows) rih = srcImage.rows - riy;
                if (riw < templ.cols || rih < templ.rows) { OutputDebugStringA("[TM] skip too small\n"); continue; }
                if (riw <= 0 || rih <= 0) { OutputDebugStringA("[TM] skip zero\n"); continue; }

                {
                    char tdbg[128];
                    snprintf(tdbg, sizeof(tdbg), "[TM] region[%d] rect=(%d,%d %dx%d) src=%dx%d\n",
                        ri, rix, riy, riw, rih, srcImage.cols, srcImage.rows);
                    OutputDebugStringA(tdbg);
                }
                cv::Mat region = srcImage(cv::Rect(rix, riy, riw, rih));
                // 用预计算旋转模板匹配
                for (auto& [ang, rotTpl] : rotTemplates)
                {
                    cv::Mat partResult;
                    cv::matchTemplate(region, rotTpl, partResult, 5);
                    for (int r = 0; r < partResult.rows; r++)
                    {
                        const float* row = partResult.ptr<float>(r);
                        for (int c = 0; c < partResult.cols; c++)
                        {
                            float score = row[c];
                            if (score >= matchThreshold)
                                candidates.push_back({ cv::Point(rix + c, riy + r), (double)score, (float)ang });
                        }
                    }
                }
            }
            snprintf(dbg, sizeof(dbg), "[TM] region search done, candidates=%d\n", (int)candidates.size());
            OutputDebugStringA(dbg);
        }
        else
        {
            // 全图搜索
            OutputDebugStringA("[TM] full search...\n");
            for (auto& [ang, rotTpl] : rotTemplates)
            {
                cv::Mat result;
                cv::matchTemplate(srcImage, rotTpl, result, 5);

                if (result.empty()) continue;

                if (!g_TMEnableRotation || ang == 0)
                {
                    double minVal, maxVal;
                    cv::Point minLoc, maxLoc;
                    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
                    g_TMLastBestScore = maxVal;
                }

                int resRows = result.rows, resCols = result.cols;
                std::mutex candMutex;
                const double mThresh = matchThreshold;
                int nThreads = std::clamp((int)std::thread::hardware_concurrency(), 2, 8);
                std::vector<std::thread> threads;
                for (int t = 0; t < nThreads; t++)
                {
                    threads.emplace_back([&, t, ang]()
                    {
                        int r0 = resRows * t / nThreads, r1 = resRows * (t + 1) / nThreads;
                        for (int r = r0; r < r1; r++)
                        {
                            const float* row = result.ptr<float>(r);
                            for (int c = 0; c < resCols; c++)
                            {
                                float score = row[c];
                                if (score >= mThresh)
                                {
                                    std::lock_guard<std::mutex> lk(candMutex);
                                    if ((int)candidates.size() >= maxCandidates) return;
                                    candidates.push_back({ cv::Point(c, r), (double)score, (float)ang });
                                }
                            }
                        }
                    });
                }
                for (auto& th : threads) th.join();
            }
        }
        auto tMatch1 = clock::now();

        snprintf(dbg, sizeof(dbg), "[TM] candidates=%d\n", (int)candidates.size());
        OutputDebugStringA(dbg);

        std::sort(candidates.begin(), candidates.end(),
            [](const Match& a, const Match& b) { return a.score > b.score; });

        LogSystem::Add(LOG_INFO, color,
            "模板匹配: 候选匹配 %d 个 (上限%d)", (int)candidates.size(), maxCandidates);

        // ===== 阶段8：NMS 非极大值抑制去重 =====
        OutputDebugStringA("[TM] NMS...\n");
        auto tNms0 = clock::now();
        int tplW = templ.cols;
        int tplH = templ.rows;
        double diagSq = (double)(tplW * tplW + tplH * tplH);
        double nmsDistSq = diagSq * g_NmsThreshold * g_NmsThreshold;

        std::vector<char> suppressed(candidates.size(), 0);
        for (size_t i = 0; i < candidates.size(); i++)
        {
            if (suppressed[i]) continue;
            int xi = candidates[i].pt.x;
            int yi = candidates[i].pt.y;
            for (size_t j = i + 1; j < candidates.size(); j++)
            {
                if (suppressed[j]) continue;
                int dx = xi - candidates[j].pt.x;
                int dy = yi - candidates[j].pt.y;
                if ((double)(dx * dx + dy * dy) < nmsDistSq)
                    suppressed[j] = 1;
            }
        }
        auto tNms1 = clock::now();

        // ===== 阶段9：收集结果（最多 g_TMMaxResults 个）=====
        float invScale = 1.0f / scale;
        int added = 0;
        for (size_t i = 0; i < candidates.size() && added < g_TMMaxResults; i++)
        {
            if (suppressed[i]) continue;
            const auto& m = candidates[i];
            ROI roi;
            roi.start = ImVec2(m.pt.x * invScale, m.pt.y * invScale);
            roi.end   = ImVec2((m.pt.x + tplW) * invScale, (m.pt.y + tplH) * invScale);
            gMatchROIs.push_back(roi);
            gMatchScores.push_back(m.score);
            gMatchAngles.push_back(m.angle);
            added++;
        }

        auto tEnd = clock::now();

        auto ms = [](auto a, auto b) {
            return std::chrono::duration<float, std::milli>(b - a).count();
        };
        float timeMatch = ms(tMatch0, tMatch1);
        float timeNms   = ms(tNms0, tNms1);
        g_TMLastMatchTime = ms(tStart, tEnd);

        snprintf(dbg, sizeof(dbg), "[TM] DONE: %d results | match=%.1fms NMS=%.1fms total=%.1fms\n",
            (int)gMatchROIs.size(), timeMatch, timeNms, g_TMLastMatchTime);
        OutputDebugStringA(dbg);

        LogSystem::Add(LOG_INFO, color,
            "模板匹配: %d个区域 | 匹配=%.1fms NMS=%.1fms 总=%.1fms",
            (int)gMatchROIs.size(), timeMatch, timeNms, g_TMLastMatchTime);
    }

    // =========================
    // 立即应用预处理到图片显示（复选框/滑块变化时调用）
    // =========================
    void ApplyPreprocessDisplay()
    {
        if (gImage.empty()) return;

        cv::Mat processed;
        if (gUseGray || gPipe.enableThreshold)
        {
            if (gImage.channels() > 1)
                cv::cvtColor(gImage, processed, cv::COLOR_BGR2GRAY);  // 直接转，不 clone 原图
            else
                processed = gImage;
        }
        if (gPipe.enableThreshold)
        {
            if (processed.channels() > 1)
                cv::cvtColor(processed, processed, cv::COLOR_BGR2GRAY);
            cv::threshold(processed, processed, gPipe.threshold, 255, cv::THRESH_BINARY);
        }

        if (!processed.empty())
        {
            if (processed.channels() == 1)
                cv::cvtColor(processed, gPendingUpload, cv::COLOR_GRAY2RGBA);
            else
                cv::cvtColor(processed, gPendingUpload, cv::COLOR_BGR2RGBA);
        }
        else
        {
            if (gImage.channels() == 4)
                cv::cvtColor(gImage, gPendingUpload, cv::COLOR_BGRA2RGBA);
            else if (gImage.channels() == 3)
                cv::cvtColor(gImage, gPendingUpload, cv::COLOR_BGR2RGBA);
            else
                cv::cvtColor(gImage, gPendingUpload, cv::COLOR_GRAY2RGBA);
        }
        gNeedUpload = true;
    }

    // =========================
    // 模板编辑弹窗（点击小图放大，可调处理参数）
    // =========================
    void ShowTemplateEditor()
    {
        if (!g_ShowTplEditor || g_FrozenTemplate.empty()) return;

        ImGui::SetNextWindowSize(ImVec2(420, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("模板编辑", &g_ShowTplEditor))
        {
            // 模板独立处理（不影响源图）
            bool tplChanged = false;
            tplChanged |= ImGui::Checkbox("灰度", &g_TplGray); ImGui::SameLine();
            tplChanged |= ImGui::Checkbox("二值化", &g_TplBinary); ImGui::SameLine();
            tplChanged |= ImGui::Checkbox("边缘", &g_TplEdge);

            if (g_TplBinary)
            {
                ImGui::PushItemWidth(80);
                tplChanged |= ImGui::SliderInt("二值阈值", &g_TplBinThresh, 0, 255);
                ImGui::PopItemWidth(); ImGui::SameLine();
            }
            if (g_TplEdge)
            {
                ImGui::PushItemWidth(70);
                tplChanged |= ImGui::SliderInt("边缘低", &g_TplEdgeLow, 0, 255); ImGui::SameLine();
                tplChanged |= ImGui::SliderInt("边缘高", &g_TplEdgeHigh, 0, 255);
                ImGui::PopItemWidth(); ImGui::SameLine();
            }

            if (tplChanged)
                g_PendingMatch = true;  // 改模板参数立即重新匹配

            // 处理状态文字
            const char* status = "原图";
            if (g_TplEdge) status = "边缘";
            else if (g_TplBinary) status = "二值化";
            else if (g_TplGray) status = "灰度";
            ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "[%s]", status);

            ImGui::SameLine();
            if (ImGui::SmallButton("刷新匹配"))
                g_PendingMatch = true;

            ImGui::Separator();

            // 处理后的模板像素渲染
            {
                cv::Mat tpl = g_FrozenTemplate.clone();
                if (g_TplGray && tpl.channels() > 1)
                    cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY);
                if (g_TplBinary)
                {
                    if (tpl.channels() > 1) cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY);
                    cv::threshold(tpl, tpl, g_TplBinThresh, 255, cv::THRESH_BINARY);
                }
                if (g_TplEdge)
                {
                    if (tpl.channels() > 1) cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY);
                    cv::Canny(tpl, tpl, g_TplEdgeLow, g_TplEdgeHigh);
                }
                // 缩放到适合显示
                int maxPx = 120;
                float rs = maxPx / (float)std::max(tpl.cols, tpl.rows);
                if (rs > 1.0f) rs = 1.0f;
                int dw = (int)(tpl.cols * rs), dh = (int)(tpl.rows * rs);
                if (dw < 2) dw = 2; if (dh < 2) dh = 2;
                cv::Mat small;
                cv::resize(tpl, small, cv::Size(dw, dh), 0, 0, cv::INTER_NEAREST);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 base = ImGui::GetCursorScreenPos();
                float step = 2.5f;
                bool isColor = !g_TplGray && !g_TplBinary && !g_TplEdge && small.channels() >= 3;
                for (int y = 0; y < dh; y++)
                {
                    for (int x = 0; x < dw; x++)
                    {
                        ImU32 col;
                        if (isColor)
                        {
                            auto& px = small.at<cv::Vec3b>(y, x);
                            col = IM_COL32(px[2], px[1], px[0], 255);
                        }
                        else
                        {
                            uchar v = (small.channels() == 1) ? small.at<uchar>(y, x)
                                : (uchar)(small.at<cv::Vec3b>(y, x)[0] * 0.3f + small.at<cv::Vec3b>(y, x)[1] * 0.59f + small.at<cv::Vec3b>(y, x)[2] * 0.11f);
                            col = IM_COL32(v, v, v, 255);
                        }
                        dl->AddRectFilled(ImVec2(base.x + x * step, base.y + y * step),
                            ImVec2(base.x + (x + 1) * step, base.y + (y + 1) * step), col);
                    }
                }
                ImGui::Dummy(ImVec2(dw * step, dh * step));
            }

            ImGui::Text("模板: %dx%d", g_FrozenTemplate.cols, g_FrozenTemplate.rows);
        }
        ImGui::End();
    }

    // =========================
    // 绘制匹配结果（蓝色矩形 + 编号标签）
    // =========================
    void DrawMatches(ImDrawList* drawList)
    {
        if (!drawList) return;

        for (int i = 0; i < (int)gMatchROIs.size() && i < g_TMMaxResults; i++)
        {
            auto& roi = gMatchROIs[i];
            float cx = (roi.start.x + roi.end.x) * 0.5f;
            float cy = (roi.start.y + roi.end.y) * 0.5f;
            float hw = (roi.end.x - roi.start.x) * 0.5f;
            float hh = (roi.end.y - roi.start.y) * 0.5f;
            float ang = (i < (int)gMatchAngles.size()) ? gMatchAngles[i] * 3.14159265f / 180.0f : 0.0f;

            // 四角坐标（绕中心旋转）
            auto Rot = [&](float dx, float dy) -> ImVec2 {
                float rx = dx * cosf(ang) - dy * sinf(ang);
                float ry = dx * sinf(ang) + dy * cosf(ang);
                return ImageToScreenPos(ImVec2(cx + rx, cy + ry));
            };
            ImVec2 c0 = Rot(-hw, -hh), c1 = Rot( hw, -hh), c2 = Rot( hw,  hh), c3 = Rot(-hw,  hh);

            drawList->AddQuad(c0, c1, c2, c3, IM_COL32(0, 128, 255, 255), 2.5f);

            // 中心点（红色圆心 + 白边）
            ImVec2 cc = ImageToScreenPos(ImVec2(cx, cy));
            drawList->AddCircleFilled(cc, 4.0f, IM_COL32(255, 80, 80, 255));
            drawList->AddCircle(cc, 5.0f, IM_COL32(255, 255, 255, 200));

            // 旋转角度指示线
            ImVec2 dir = Rot(0, -hh * 1.5f);
            drawList->AddLine(cc, dir, IM_COL32(255, 255, 0, 200), 2.0f);

            char label[32];
            double score = (i < (int)gMatchScores.size()) ? gMatchScores[i] : 0.0;
            snprintf(label, sizeof(label), "#%d %.2f", i + 1, score);
            drawList->AddText(ImVec2(c0.x + 3, c0.y + 1),
                IM_COL32(0, 128, 255, 255), label);
        }
    }

    // =========================
    // 清空匹配结果
    // =========================
    void Clear()
    {
        gMatchROIs.clear();
        gMatchScores.clear();
        gMatchAngles.clear();
    }

    // =========================
    // 保存模板图像到 PNG
    // =========================
    bool SaveTemplate(const char* filepath)
    {
        if (g_FrozenTemplate.empty()) return false;
        return cv::imwrite(filepath, g_FrozenTemplate);
    }

    // =========================
    // 从 PNG 加载模板图像
    // =========================
    bool LoadTemplate(const char* filepath)
    {
        cv::Mat tpl = cv::imread(filepath, cv::IMREAD_COLOR);
        if (tpl.empty()) return false;
        g_FrozenTemplate = tpl;
        g_FrozenImgW = tpl.cols;
        g_FrozenImgH = tpl.rows;
        g_FrozenTplStart = ImVec2(0, 0);
        g_FrozenTplEnd   = ImVec2((float)tpl.cols, (float)tpl.rows);
        g_ShowPreview = true;
        return true;
    }

    // =========================
    // 调试窗口：模板预览 + 参数调节 + 执行
    // =========================
    void ShowWindow()
    {
        if (!g_ShowTemplateMatch) return;
        // 延迟执行：先渲染"匹配中..."再跑 Run()，避免 UI 假死无反馈
        if (g_PendingMatch)
        {
            g_PendingMatch = false;
            OutputDebugStringA("TEMPLATE_MATCH: Run() begin\n");
            Run();
            OutputDebugStringA("TEMPLATE_MATCH: Run() end\n");
        }
        ImGui::SetNextWindowSize(ImVec2(380, 620), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("模板匹配调试", &g_ShowTemplateMatch))  //,ImGuiWindowFlags_NoDocking  禁止停靠窗口
        {
            // =========================
            // 模板预览区域
            // =========================
            ImGui::Text("模板预览");
            ImGui::Separator();

            if (gImage.empty())
            {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "请先加载图片");
            }
            else if (gROIs.empty() && g_FrozenTemplate.empty())
            {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "请先在图片上用右键框选模板ROI");
            }
            else if (!gROIs.empty())
            {
                if (ImGui::Button("抓取模板", ImVec2(-1, 24)))
                {
                    int idx = (gSelectedROI >= 0 && gSelectedROI < (int)gROIs.size()) ? gSelectedROI : 0;
                    const ROI& fr = gROIs[idx];
                    float fx1 = std::min(fr.start.x, fr.end.x);
                    float fy1 = std::min(fr.start.y, fr.end.y);
                    float fx2 = std::max(fr.start.x, fr.end.x);
                    float fy2 = std::max(fr.start.y, fr.end.y);
                    int ftx = std::clamp((int)fx1, 0, gImage.cols - 1);
                    int fty = std::clamp((int)fy1, 0, gImage.rows - 1);
                    int ftw = std::clamp((int)(fx2 - fx1), 1, gImage.cols - ftx);
                    int fth = std::clamp((int)(fy2 - fy1), 1, gImage.rows - fty);
                    if (ftw < 1) ftw = 1;
                    if (fth < 1) fth = 1;
                    if (ftx + ftw > gImage.cols) ftw = gImage.cols - ftx;
                    if (fty + fth > gImage.rows) fth = gImage.rows - fty;
                    if (ftw <= 0 || fth <= 0) { g_FrozenTemplate.release(); g_ShowPreview = false; }
                    else {
                        g_FrozenTemplate = gImage(cv::Rect(ftx, fty, ftw, fth)).clone();
                        g_FrozenTplStart = ImVec2((float)ftx, (float)fty);
                        g_FrozenTplEnd   = ImVec2((float)(ftx + ftw), (float)(fty + fth));
                        g_FrozenImgW = gImage.cols;
                        g_FrozenImgH = gImage.rows;
                        g_ShowPreview = true;
                        gROIs.clear();         // 抓取完成后自动清理ROI
                        gSelectedROI = -1;
                        gActiveHandle = HANDLE_NONE;
                    }
                }
            }
            else
            {
                // gROIs为空但冻结模板存在：模板已就绪
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1), "模板已就绪，可直接执行匹配");
            }

            // 模板预览（不受图片/ROI限制，模板独立保存）
            if (!g_FrozenTemplate.empty() && !g_ShowPreview)
            {
                if (ImGui::Button("打开模板", ImVec2(-1, 24)))
                    g_ShowPreview = true;
            }

            if (g_ShowPreview && !g_FrozenTemplate.empty())
                {
                    const char* tplStatus = "原图";
                    if (g_TplEdge) tplStatus = "边缘";
                    else if (g_TplBinary) tplStatus = "二值化";
                    else if (g_TplGray) tplStatus = "灰度";
                    ImGui::Text("模板: %dx%d [%s]", g_FrozenTemplate.cols, g_FrozenTemplate.rows, tplStatus);

                    // 直接用像素渲染（不依赖当前图片纹理）
                    cv::Mat tpl = g_FrozenTemplate.clone();
                    if (g_TplGray && tpl.channels() > 1) cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY);
                    if (g_TplBinary) { if (tpl.channels() > 1) cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY); cv::threshold(tpl, tpl, g_TplBinThresh, 255, cv::THRESH_BINARY); }
                    if (g_TplEdge) { if (tpl.channels() > 1) cv::cvtColor(tpl, tpl, cv::COLOR_BGR2GRAY); cv::Canny(tpl, tpl, g_TplEdgeLow, g_TplEdgeHigh); }
                    int maxPx = 100;
                    float rs = maxPx / (float)std::max(tpl.cols, tpl.rows);
                    if (rs > 1.0f) rs = 1.0f;
                    int dw = (int)(tpl.cols * rs), dh = (int)(tpl.rows * rs);
                    if (dw < 2) dw = 2; if (dh < 2) dh = 2;
                    cv::Mat small; cv::resize(tpl, small, cv::Size(dw, dh), 0, 0, cv::INTER_NEAREST);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 base = ImGui::GetCursorScreenPos(); float step = 2.0f;
                    bool isColor = !g_TplGray && !g_TplBinary && !g_TplEdge && small.channels() >= 3;
                    for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++)
                    {
                        ImU32 col;
                        if (isColor)
                        {
                            auto& px = small.at<cv::Vec3b>(y, x);
                            col = IM_COL32(px[2], px[1], px[0], 255);
                        }
                        else
                        {
                            uchar v = (small.channels() == 1) ? small.at<uchar>(y, x)
                                : (uchar)(small.at<cv::Vec3b>(y, x)[0] * 0.3f + small.at<cv::Vec3b>(y, x)[1] * 0.59f + small.at<cv::Vec3b>(y, x)[2] * 0.11f);
                            col = IM_COL32(v, v, v, 255);
                        }
                        dl->AddRectFilled(ImVec2(base.x + x * step, base.y + y * step), ImVec2(base.x + (x + 1) * step, base.y + (y + 1) * step), col);
                    }
                    ImGui::Dummy(ImVec2(dw * step, dh * step));
                    if (ImGui::IsItemClicked())
                        g_ShowTplEditor = true;
                    ImGui::SetItemTooltip("点击放大编辑");

                    if (ImGui::SmallButton("隐藏预览"))
                    {
                        g_ShowPreview = false;
                        // 不释放模板，匹配继续用
                    }
                }

            ImGui::Separator();

            // =========================
            // 预处理选项（复选框/滑块变化立即刷新图片显示）
            // =========================
            ImGui::Text("预处理");
            bool preChanged = false;
            preChanged |= ImGui::Checkbox("转为灰度", &gUseGray);
            ImGui::SetItemTooltip("匹配前将图片转为灰度，提高鲁棒性");

            preChanged |= ImGui::Checkbox("二值化", &gPipe.enableThreshold);
            ImGui::SetItemTooltip("灰度图二值化（黑白），适合模板边缘清晰时");

            if (gPipe.enableThreshold)
            {
                preChanged |= ImGui::SliderInt("二值化阈值", &gPipe.threshold, 0, 255);
                ImGui::SetItemTooltip("像素>阈值为白，否则为黑");
            }

            if (preChanged)
                ApplyPreprocessDisplay();

            ImGui::Separator();

            // =========================
            // 搜索范围
            // =========================
            ImGui::Text("搜索范围");
            const char* kSearchModes[] = { "全图", "区域(ROI内)" };
            ImGui::Combo("##search", &g_TMSearchMode, kSearchModes, 2);

            ImGui::Checkbox("启用旋转", &g_TMEnableRotation);
            ImGui::SetItemTooltip("模板旋转多角度搜索，识别倾斜目标");
            if (g_TMEnableRotation)
            {
                ImGui::Indent();
                ImGui::PushItemWidth(60);
                ImGui::SliderInt("起始角°", &g_TMRotationStart, -45, 0); ImGui::SameLine();
                ImGui::SliderInt("结束角°", &g_TMRotationEnd, 0, 45); ImGui::SameLine();
                ImGui::SliderInt("步长°", &g_TMRotationStep, 1, 10);
                ImGui::PopItemWidth();
                ImGui::Unindent();
            }

            ImGui::Separator();

            // =========================
            // 参数调节
            // =========================
            ImGui::Text("匹配参数");

            ImGui::SliderInt("最大结果数", &g_TMMaxResults, 1, 100);
            ImGui::SetItemTooltip("最多显示多少个匹配框");

            if (ImGui::SliderFloat("匹配阈值", &g_TMMatchThreshold, 0.0f, 1.0f, "%.3f"))
                g_PendingMatch = true;  // 拖滑块实时刷新匹配

            // 上次最佳分数参考
            if (g_TMLastBestScore != 0.0)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1),
                    "  (最佳:%.3f)", g_TMLastBestScore);
            }

            ImGui::SliderFloat("NMS阈值", &g_NmsThreshold, 0.0f, 1.0f, "%.3f");
            ImGui::SetItemTooltip("非极大值抑制重叠阈值，越小去重越强");

            // 速度/精度滑块
            ImGui::SliderInt("匹配精度", &g_TMMaxImageDim, 400, 2000);
            ImGui::SetItemTooltip("值越小越快，越大越精确。原图>此值会缩放后再匹配");

            float estTime = (float)g_TMMaxImageDim * g_TMMaxImageDim / 1000000.0f * 40.0f;
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1),
                "预估耗时 ~%.0fms | 缩放到 %dpx 以内", estTime, g_TMMaxImageDim);

            ImGui::Separator();

            // =========================
            // 执行按钮
            // =========================
            if (g_PendingMatch)
            {
                ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "正在匹配，请稍候...");
            }

            if (ImGui::Button("执行匹配", ImVec2(-1, 36)))
            {
                g_PendingMatch = true;  // 下一帧执行，本帧先显示提示
            }

            ImGui::SameLine();
            if (ImGui::Button("清空结果", ImVec2(-1, 36)))
            {
                Clear();
            }

            ImGui::Separator();

            // =========================
            // 结果统计
            // =========================
            int total = (int)gMatchROIs.size();
            int shown = std::min(total, g_TMMaxResults);
            ImGui::Text("匹配结果: %d 个 | 显示: %d 个", total, shown);

            if (g_TMLastMatchTime > 0.0f)
            {
                ImGui::SameLine();
                ImGui::TextColored(
                    g_TMLastMatchTime < 500 ? ImVec4(0.3f, 0.8f, 0.3f, 1) : ImVec4(1, 0.7f, 0.3f, 1),
                    "(%.0f ms)", g_TMLastMatchTime);
            }

            if (total > g_TMMaxResults)
                ImGui::TextColored(ImVec4(1, 0.7f, 0, 1),
                    "  (超过上限%d, 仅显示前%d个)", g_TMMaxResults, shown);

            // 结果列表（可滚动）
            if (!gMatchROIs.empty())
            {
                ImGui::Separator();
                ImGui::Text("结果列表:");
                ImGui::BeginChild("##matchList", ImVec2(0, 120), true, ImGuiWindowFlags_HorizontalScrollbar);
                for (int i = 0; i < (int)gMatchROIs.size() && i < g_TMMaxResults; i++)
                {
                    auto& roi = gMatchROIs[i];
                    float rx = std::min(roi.start.x, roi.end.x);
                    float ry = std::min(roi.start.y, roi.end.y);
                    float rw = std::abs(roi.end.x - roi.start.x);
                    float rh = std::abs(roi.end.y - roi.start.y);
                    float ang = (i < (int)gMatchAngles.size()) ? gMatchAngles[i] : 0.0f;
                    ImGui::Text("#%d: (%.6f,%.6f) %.6fx%.6f  %.6f  %.6f°",
                        i + 1, rx, ry, rw, rh,
                        (i < (int)gMatchScores.size()) ? gMatchScores[i] : 0.0, ang);
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

} // namespace TemplateMatch
