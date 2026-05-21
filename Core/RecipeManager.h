#pragma once
#include <string>
#include <vector>

// ===================== 配方数据结构 =====================
struct RecipeThreshold
{
    bool  useGray        = false;
    int   thresholdValue = 128;
    bool  binaryInv      = false;
    int   blurSize       = 1;
    int   cannyLow       = 50;
    int   cannyHigh      = 150;
    float brightness     = 0.0f;
    float contrast       = 1.0f;
    int   processMode    = 0;

    // PipelineState
    bool  pipeBlur       = false;
    bool  pipeThreshold  = false;
    bool  pipeCanny      = false;
    int   pipeBlurSize   = 5;
    int   pipeThresholdVal = 128;
    int   pipeCannyLow   = 50;
    int   pipeCannyHigh  = 150;
};

struct RecipeTemplateMatch
{
    int   method        = 5;
    int   searchMode    = 0;
    int   maxResults    = 10;
    int   maxImageDim   = 1000;
    float matchThreshold = 0.75f;

    bool  enableRotation = false;
    int   rotationStart  = -5;
    int   rotationEnd    = 5;
    int   rotationStep   = 5;
};

struct RecipeROI
{
    float startX, startY;
    float endX, endY;
    int   type = 0;
};

// ===================== 工具实例序列化数据 =====================
struct RecipeToolInstance
{
    int type = 0;                          // 0=边缘检测 1=模板匹配 2=Blob 3=阈值调试
    std::string templateFile;              // 模板图像文件名（与配方同名 _tplN.png）

    // ---- 匹配参数 ----
    bool  enableRotation = false;
    int   rotationStart  = -45;
    int   rotationEnd    = 45;
    int   rotationStep   = 1;
    int   maxResults     = 5;
    float matchThreshold = 0.7f;
    int   maxImageDim    = 1000;
    float nmsThreshold   = 0.3f;
    int   searchMode     = 0;

    // ---- 模板预处理 ----
    bool tplGray      = false;
    bool tplBinary    = false;
    int  tplBinThresh = 128;
    bool tplEdge      = false;
    int  tplEdgeLow   = 50;
    int  tplEdgeHigh  = 150;

    // ---- 图像预处理（模板匹配用） ----
    bool imgUseGray         = false;
    bool imgEnableThreshold = false;
    int  imgThreshold       = 128;

    // ---- 边缘检测参数 ----
    int  cannyLow  = 50;
    int  cannyHigh = 150;
    bool edgeUseGray = false;

    // ---- 阈值调试参数 ----
    bool dbgUseGray      = false;
    bool dbgEnableBlur   = false;
    int  dbgBlurSize     = 5;
    bool dbgEnableThresh = false;
    int  dbgThreshold    = 128;
    bool dbgEnableCanny  = false;
    int  dbgCannyLow     = 50;
    int  dbgCannyHigh    = 150;

    // ---- 搜索ROI ----
    std::vector<RecipeROI> searchROIs;
};

struct RecipeData
{
    std::string name;
    std::string imagePath;
    std::string templateImage;  // 模板图像文件名（与配方同名 .png，兼容旧版）

    RecipeThreshold      threshold;
    RecipeTemplateMatch  tmMatch;
    std::vector<RecipeROI> rois;

    // 工具实例列表（新增）
    std::vector<RecipeToolInstance> tools;
};

// ===================== 配方管理器 =====================
namespace RecipeManager
{
    // 保存配方到文件（JSON 格式，扩展名 .recipe）
    bool Save(const char* filepath, const RecipeData& data);

    // 从文件加载配方
    bool Load(const char* filepath, RecipeData& data);

    // 列出 dir/recipes/ 目录下所有配方文件
    std::vector<std::string> List(const char* exeDir = nullptr);

    // 将当前参数收集到 RecipeData
    RecipeData Capture(const char* name);

    // 将 RecipeData 中的参数应用到当前运行环境
    void Apply(const RecipeData& data);
}
