#pragma once
#include <string>
#include <vector>

// ===================== 配方数据结构 =====================
struct RecipeThreshold
{
    bool useGray = false;
    int thresholdValue = 128;
    bool binaryInv = false;
    int blurSize = 1;
    int cannyLow = 50;
    int cannyHigh = 150;
    float brightness = 0.0f;
    float contrast = 1.0f;
    int processMode = 0;

    // PipelineState
    bool pipeBlur = false;
    bool pipeThreshold = false;
    bool pipeCanny = false;
    int pipeBlurSize = 5;
    int pipeThresholdVal = 128;
    int pipeCannyLow = 50;
    int pipeCannyHigh = 150;
};

struct RecipeTemplateMatch
{
    int method = 5;
    int searchMode = 0;
    int maxResults = 10;
    int maxImageDim = 1000;
    float matchThreshold = 0.75f;

    bool enableRotation = false;
    int rotationStart = -5;
    int rotationEnd = 5;
    int rotationStep = 5;
};

struct RecipeROI
{
    float startX, startY;
    float endX, endY;
    int type = 0;
};

// ===================== 工具实例序列化数据 =====================
struct RecipeToolInstance
{
    int type = 0;             // 0=边缘检测 1=模板匹配 2=Blob 3=阈值调试
    std::string label;
    int inputSourceMode = 2;  // 0=上一步原图, 1=上一步处理图, 2=原图工具输出
    std::string templateFile; // 模板图像文件名（与配方同名 _tplN.png）
    bool hasTemplateROI = false;
    RecipeROI templateROI{};

    // ---- 匹配参数 ----
    bool enableRotation = false;
    int rotationStart = -45;
    int rotationEnd = 45;
    int rotationStep = 1;
    int maxResults = 5;
    float matchThreshold = 0.7f;
    int maxImageDim = 1000;
    float nmsThreshold = 0.3f;
    int searchMode = 0;

    // ---- 模板预处理 ----
    bool tplGray = false;
    bool tplBinary = false;
    int tplBinThresh = 128;
    bool tplEdge = false;
    int tplEdgeLow = 50;
    int tplEdgeHigh = 150;

    // ---- 图像预处理（模板匹配用） ----
    bool imgUseGray = false;
    bool imgEnableThreshold = false;
    int imgThreshold = 128;

    // ---- 边缘检测参数 ----
    int cannyLow = 50;
    int cannyHigh = 150;
    bool edgeUseGray = false;

    // ---- 阈值调试参数 ----
    bool dbgUseGray = false;
    bool dbgEnableBlur = false;
    int dbgBlurSize = 5;
    bool dbgEnableThresh = false;
    int dbgThreshold = 128;
    bool dbgEnableCanny = false;
    int dbgCannyLow = 50;
    int dbgCannyHigh = 150;

    // ---- Blob分析参数 ----
    int blobMinArea = 100;
    int blobMaxArea = 10000;

    // ---- 搜索ROI ----
    bool useSearchROI = false;
    std::vector<RecipeROI> searchROIs;

    // ---- YOLO 检测参数（type==4）----
    std::string yoloModelPath;   // ONNX 模型路径
    std::string yoloClassesPath; // 类别文件路径
    float yoloConfThreshold = 0.5f;
    float yoloNmsThreshold = 0.4f;
    bool yoloUseROI = false;
    bool yoloUseGPU = false;

    // type==5 轮廓分析
    bool cntUseGray = true;
    int cntBlurSize = 5, cntThreshMode = 0, cntThreshValue = 128, cntAdaptBlock = 11;
    bool cntInvert = false;
    int cntRetrMode = 0, cntApproxMethod = 1;
    float cntMinArea = 100;
    int cntMaxContours = 500;
    bool cntFilterConvex = false;
    float cntApproxEps = 0.02f;
    int cntLineThick = 2;
    bool cntShowLabels = true, cntFillContours = false;
    bool cntMatchROI = false;
    float cntMatchThresh = 0.1f;

    // type==6 形状匹配
    std::string shpTplFile;
    int shpBlurSize = 5, shpTplRetr = 0;
    float shpTplMinArea = 30, shpMinScore = 0.5f, shpShapeScore = 0.1f;
    int shpLineThick = 2, shpMethod = 0;
    bool shpShowLabels = true;
    int shpMaxResults = 1;
    bool shpTplGray = false, shpTplBinary = false;
    int shpTplBinThresh = 128;
    bool shpTplBlur = false;
    int shpTplBlurK = 5;
    bool shpTplInvert = false;

    // type==7 直线检测
    int lineCannyLow = 50, lineCannyHigh = 150;
    float lineMinLength = 100, lineMaxGap = 20, lineMinAngle = 0, lineMaxAngle = 180;
    int lineThickness = 2, lineMaxLines = 1;
    bool lineShowLabels = true, lineUseROI = false;
    std::vector<RecipeROI> lineSaveROIs;

    // type==8 形态学
    int morphOpType = 0, morphKernelSize = 3, morphKernelShape = 0, morphIterations = 1;
    bool morphUseGray = false;

    // type==9 颜色分析
    int colorSpace = 0, colorHistBins = 32;
    bool colorShowHist = true, colorUseROI = false;
    int colorHistHeight = 100;

    // type==10 多点找色
    std::string mcfRefImageBase64; // 参考图 base64
    std::string mcfPointsJson;     // 颜色点 JSON
    int mcfAnchorX = 0, mcfAnchorY = 0;
    bool mcfUseROI = false;
    int mcfMaxResults = 1;
    float mcfMinDist = 5.0f;
    int mcfCrossSize = 10, mcfCrossThick = 2;
    bool mcfImgGray = false, mcfImgBinary = false;
    int  mcfImgBinThresh = 128;
    int mcfRoiX = 0, mcfRoiY = 0, mcfRoiW = 0, mcfRoiH = 0;

    // type==13 OCR文字识别
    std::string ocrDetModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.bin";
    std::string ocrDetParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_det.ncnn.param";
    std::string ocrRecModelPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.bin";
    std::string ocrRecParamPath = "models\\ppocrv6\\PP_OCRv6_tiny_rec.ncnn.param";
    std::string ocrDictionaryPath = "models\\ppocrv6\\ppocr_keys_v6_tiny.txt";
    float ocrMinConfidence = 0.30f;
    int ocrMaxItems = 8;
    int ocrInputSize = 512;
    int ocrMaxCandidates = 220;
    int ocrMinBoxArea = 0;
    int ocrMinBoxHeight = 0;
    int ocrRoiPadding = 24;
    bool ocrFastMode = true;
    bool ocrDetectOnly = false;
    bool ocrUseROI = true;
};

struct RecipeData
{
    std::string name;
    std::string imagePath;
    std::string templateImage; // 模板图像文件名（与配方同名 .png，兼容旧版）

    RecipeThreshold threshold;
    RecipeTemplateMatch tmMatch;
    std::vector<RecipeROI> rois;

    // 工具实例列表（新增）
    std::vector<RecipeToolInstance> tools;
};

// ===================== 配方管理器 =====================
namespace RecipeManager
{
    // 保存配方到文件（JSON 格式，扩展名 .recipe）
    bool Save(const char *filepath, const RecipeData &data);

    // 从文件加载配方
    bool Load(const char *filepath, RecipeData &data);

    // 列出 dir/recipes/ 目录下所有配方文件
    std::vector<std::string> List(const char *exeDir = nullptr);

    // 将当前参数收集到 RecipeData
    RecipeData Capture(const char *name);

    // 将 RecipeData 中的参数应用到当前运行环境
    void Apply(const RecipeData &data);
}
