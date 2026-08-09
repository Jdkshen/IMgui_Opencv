param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$seriesRoot = Join-Path $RepoRoot "docs\recipe_examples\task_series"

function Write-Utf8File([string]$Path, [string]$Content) {
    $parent = Split-Path -Parent $Path
    if ($parent) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
}

function New-ROI(
    [double]$StartX,
    [double]$StartY,
    [double]$EndX,
    [double]$EndY,
    [int]$Type
) {
    return [ordered]@{
        startX = $StartX
        startY = $StartY
        endX = $EndX
        endY = $EndY
        angle = 0.0
        type = $Type
        locked = $false
        visible = $true
        constrainToImage = $true
        points = @()
    }
}

function New-Tool(
    [int]$Type,
    [int]$ToolId,
    [string]$Label,
    [string]$GroupName,
    [hashtable]$Parameters = @{}
) {
    $tool = [ordered]@{
        type = $Type
        toolId = $ToolId
        enabled = $true
        label = $Label
        groupName = $GroupName
        collapsed = $false
        inputSourceMode = 2
        showResultLabels = $true
        useSearchROI = $false
        searchROIs = @()
        judgement = [ordered]@{
            enabled = $false
            stopOnFailure = $false
        }
    }
    foreach ($key in $Parameters.Keys) {
        $tool[$key] = $Parameters[$key]
    }
    return $tool
}

$imageSources = [ordered]@{
    "零件缺陷.jpg" = "assets\images\defect_0000_orig.jpg"
    "清晰二维码.png" = "assets\images\qr_tests\qr_test.png"
    "复杂二维码.png" = "assets\images\qr_tests\qr_extreme_multi_mixed.png"
    "中文文字.jpg" = "assets\images\ocr_product_sample.jpg"
}

$sharedImageDir = Join-Path $seriesRoot "images"
New-Item -ItemType Directory -Path $sharedImageDir -Force | Out-Null
foreach ($entry in $imageSources.GetEnumerator()) {
    $source = Join-Path $RepoRoot $entry.Value
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "示例源图片不存在: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $sharedImageDir $entry.Key) -Force
}

$catalog = @(
    [ordered]@{
        name = "任务01-原图预览"; image = "零件缺陷.jpg"; tool = "原图";
        purpose = "确认任务图片、尺寸与颜色通道是否正确。"; expected = "显示 1920×1080 零件原图。";
        type = 12; parameters = @{}
    },
    [ordered]@{
        name = "任务02-基础边缘"; image = "零件缺陷.jpg"; tool = "边缘检测";
        purpose = "用 Canny 提取零件和纹理边缘。"; expected = "显示主要轮廓和细节边缘。";
        type = 0; parameters = @{ edgeUseGray = $true; cannyLow = 50; cannyHigh = 150 }
    },
    [ordered]@{
        name = "任务03-灰度阈值"; image = "零件缺陷.jpg"; tool = "阈值调试";
        purpose = "演示灰度、模糊和固定阈值二值化。"; expected = "输出黑白分割图。";
        type = 3; parameters = @{ dbgUseGray = $true; dbgEnableBlur = $true; dbgBlurSize = 5; dbgEnableThresh = $true; dbgThreshold = 128 }
    },
    [ordered]@{
        name = "任务04-清晰二维码"; image = "清晰二维码.png"; tool = "二维码识别";
        purpose = "演示未绑定 ROI 时对整图识别二维码。"; expected = "标出二维码并显示解码文本。";
        type = 14; parameters = @{ qrUseROI = $false; qrDetectMulti = $true; qrEnhance = $true; qrMinSize = 16; qrEngine = 0; qrFormatMask = 31; qrFilterDuplicates = $true }
    },
    [ordered]@{
        name = "任务05-Blob区域"; image = "零件缺陷.jpg"; tool = "Blob分析";
        purpose = "按面积筛选二值连通区域。"; expected = "输出符合面积条件的区域框。";
        type = 2; parameters = @{ blobMinArea = 100.0; blobMaxArea = 500000.0; blobThresholdMode = 0; blobThreshold = 128; blobInvert = $false; blobConnectivity = 8 }
    },
    [ordered]@{
        name = "任务06-轮廓分析"; image = "零件缺陷.jpg"; tool = "轮廓分析";
        purpose = "提取并按面积过滤零件轮廓。"; expected = "叠加显示主要轮廓。";
        type = 5; parameters = @{ cntUseGray = $true; cntBlurSize = 5; cntThreshMode = 0; cntThreshValue = 128; cntMinArea = 100.0; cntMaxContours = 100 }
    },
    [ordered]@{
        name = "任务07-形态学开运算"; image = "零件缺陷.jpg"; tool = "形态学";
        purpose = "用开运算去除小噪点。"; expected = "输出灰度形态学处理图。";
        type = 8; parameters = @{ morphOpType = 2; morphKernelSize = 3; morphKernelShape = 0; morphIterations = 1; morphUseGray = $true }
    },
    [ordered]@{
        name = "任务08-直线检测"; image = "清晰二维码.png"; tool = "直线检测";
        purpose = "检测二维码边框和模块中的长直线。"; expected = "叠加显示最多 20 条直线。";
        type = 7; parameters = @{ lineCannyLow = 40; lineCannyHigh = 130; lineMinLength = 40.0; lineMaxGap = 10.0; lineMinAngle = 0.0; lineMaxAngle = 180.0; lineMaxLines = 20; lineUseROI = $false }
    },
    [ordered]@{
        name = "任务09-颜色统计"; image = "零件缺陷.jpg"; tool = "颜色分析";
        purpose = "统计整图颜色与直方图信息。"; expected = "显示颜色统计和直方图。";
        type = 9; parameters = @{ colorSpace = 0; colorHistBins = 32; colorShowHist = $true; colorUseROI = $false }
    },
    [ordered]@{
        name = "任务10-中文OCR"; image = "中文文字.jpg"; tool = "文字识别";
        purpose = "使用随发布包提供的 PP-OCRv6 tiny 模型识别中文。"; expected = "标出文字区域并显示识别文本；首次执行会稍慢。";
        type = 13; parameters = @{
            ocrDetModelPath = "models/ppocrv6/PP_OCRv6_tiny_det.ncnn.bin"
            ocrDetParamPath = "models/ppocrv6/PP_OCRv6_tiny_det.ncnn.param"
            ocrRecModelPath = "models/ppocrv6/PP_OCRv6_tiny_rec.ncnn.bin"
            ocrRecParamPath = "models/ppocrv6/PP_OCRv6_tiny_rec.ncnn.param"
            ocrDictionaryPath = "models/ppocrv6/ppocr_keys_v6_tiny.txt"
            ocrMinConfidence = 0.25; ocrMaxItems = 30; ocrInputSize = 640
            ocrMaxCandidates = 320; ocrRoiPadding = 16; ocrFastMode = $true
            ocrDetectOnly = $false; ocrUseROI = $false
        }
    },
    [ordered]@{
        name = "任务11-复杂二维码"; image = "复杂二维码.png"; tool = "二维码识别";
        purpose = "验证多码、旋转、透视和干扰场景。"; expected = "识别出可恢复的多个二维码并过滤重复结果。";
        type = 14; parameters = @{ qrUseROI = $false; qrDetectMulti = $true; qrEnhance = $true; qrMinSize = 12; qrEngine = 0; qrFormatMask = 31; qrFilterDuplicates = $true }
    },
    [ordered]@{
        name = "任务12-两点距离"; image = "清晰二维码.png"; tool = "工业测量";
        purpose = "演示工具自带两个点 ROI 的距离测量。"; expected = "显示两点连线和像素距离。";
        type = 15; parameters = @{
            measureMode = 0; useSearchROI = $true
            searchROIs = @((New-ROI 160 150 160 150 1), (New-ROI 470 150 470 150 1))
            measurement = [ordered]@{ mode = 0; calibrationEnabled = $false; toleranceEnabled = $false }
        }
    },
    [ordered]@{
        name = "任务13-线线角度"; image = "清晰二维码.png"; tool = "工业测量";
        purpose = "演示两条线 ROI 的夹角测量。"; expected = "显示两条测量线和夹角。";
        type = 15; parameters = @{
            measureMode = 2; useSearchROI = $true
            searchROIs = @((New-ROI 120 220 510 220 2), (New-ROI 180 110 180 520 2))
            measurement = [ordered]@{ mode = 2; calibrationEnabled = $false; toleranceEnabled = $false }
        }
    },
    [ordered]@{
        name = "任务14-形态学闭运算"; image = "零件缺陷.jpg"; tool = "形态学";
        purpose = "用闭运算连接小间隙、填补小孔。"; expected = "输出闭运算后的灰度图。";
        type = 8; parameters = @{ morphOpType = 3; morphKernelSize = 5; morphKernelShape = 1; morphIterations = 1; morphUseGray = $true }
    },
    [ordered]@{
        name = "任务15-高阈值分割"; image = "零件缺陷.jpg"; tool = "阈值调试";
        purpose = "提高分割阈值，与任务03对照观察前景范围变化。"; expected = "高亮区域少于任务03，保留更亮的像素。";
        type = 3; parameters = @{ dbgUseGray = $true; dbgEnableBlur = $true; dbgBlurSize = 3; dbgEnableThresh = $true; dbgThreshold = 180 }
    },
    [ordered]@{
        name = "任务16-强边缘"; image = "零件缺陷.jpg"; tool = "边缘检测";
        purpose = "提高 Canny 阈值，只保留更明显的边缘。"; expected = "边缘数量少于任务02，主体轮廓更突出。";
        type = 0; parameters = @{ edgeUseGray = $true; cannyLow = 100; cannyHigh = 220 }
    }
)

$counts = @(2, 4, 6, 8, 10, 12, 16)
foreach ($count in $counts) {
    $folderName = "{0:D2}_tasks" -f $count
    $caseDir = Join-Path $seriesRoot $folderName
    New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
    $legacyImageDir = Join-Path $caseDir "images"
    foreach ($entry in $imageSources.GetEnumerator()) {
        $legacyImagePath = Join-Path $legacyImageDir $entry.Key
        if (Test-Path -LiteralPath $legacyImagePath -PathType Leaf) {
            Remove-Item -LiteralPath $legacyImagePath -Force
        }
    }
    if ((Test-Path -LiteralPath $legacyImageDir -PathType Container) -and
        -not (Get-ChildItem -LiteralPath $legacyImageDir -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $legacyImageDir -Force
    }

    $groups = @()
    $tools = @()
    for ($index = 0; $index -lt $count; ++$index) {
        $item = $catalog[$index]
        $groups += [ordered]@{
            name = $item.name
            enabled = $true
            imagePath = "../images/$($item.image)"
            imageFolderPath = ""
            imageFolderIndex = -1
            imageFolderCount = 0
            cameraIndex = -1
            cameraPreferred = $false
        }
        $tools += New-Tool -Type $item.type -ToolId ($count * 1000 + $index + 1) `
            -Label ("{0:D2}-{1}" -f ($index + 1), $item.tool) -GroupName $item.name `
            -Parameters $item.parameters
    }

    $recipe = [ordered]@{
        version = 5
        name = ("{0:D2}任务中文完整案例" -f $count)
        imagePath = "../images/零件缺陷.jpg"
        runtime = [ordered]@{ loopIntervalMs = 300 }
        rois = @()
        taskGroups = $groups
        tools = $tools
    }
    $json = $recipe | ConvertTo-Json -Depth 20
    Write-Utf8File (Join-Path $caseDir ("{0:D2}_tasks.recipe" -f $count)) ($json + "`n")

    $lines = @(
        "# $count 任务中文完整案例",
        "",
        "> 配方格式：version 5。本目录包含配方和中文说明，测试图片统一放在上一级 ``images/``，避免 7 套案例重复占用空间。",
        "",
        "## 快速使用",
        "",
        "1. 启动程序，选择菜单「文件(F) → 打开配方...」。",
        "2. 打开 ``$('{0:D2}_tasks.recipe' -f $count)``。",
        "3. 在右侧选择任务后点「执行当前任务」，或直接点「全部执行」。",
        "4. 查看图像叠加结果和底部日志；OCR 第一次加载模型时会稍慢。",
        "",
        "## 任务明细",
        "",
        "| 序号 | 任务 | 图片 | 工具 | 中文说明 | 预期结果 |",
        "| ---: | --- | --- | --- | --- | --- |"
    )
    for ($index = 0; $index -lt $count; ++$index) {
        $item = $catalog[$index]
        $lines += "| $($index + 1) | $($item.name) | ``../images/$($item.image)`` | $($item.tool) | $($item.purpose) | $($item.expected) |"
    }
    $lines += @(
        "",
        "## 配图",
        "",
        "### 零件缺陷",
        "",
        "![零件缺陷](../images/零件缺陷.jpg)",
        "",
        "### 清晰二维码",
        "",
        "![清晰二维码](../images/清晰二维码.png)",
        "",
        "### 复杂二维码",
        "",
        "![复杂二维码](../images/复杂二维码.png)",
        "",
        "### 中文文字",
        "",
        "![中文文字](../images/中文文字.jpg)",
        "",
        "## ROI 与路径规则",
        "",
        "- 边缘、阈值、Blob、轮廓、形态学、直线、颜色、二维码和 OCR 均未绑定 ROI，因此执行整图。",
        "- 工业测量任务在工具内部明确保存了点或线 ROI，不依赖画布上临时绘制的 ROI。",
        "- 配方只保存相对路径；移动案例时请复制整个 ``task_series`` 目录，不要只复制某个 ``.recipe`` 文件。",
        "- 任务数和工具数均为 $count，每个任务恰好绑定一个演示工具，便于逐项观察。",
        ""
    )
    Write-Utf8File (Join-Path $caseDir "README.md") (($lines -join "`n") + "`n")
}

$indexLines = @(
    "# 多任务中文案例",
    "",
    "> 这里提供 2、4、6、8、10、12、16 任务共 7 套完整案例。每套目录都有 version 5 配方和中文逐项说明，测试图片统一放在本目录的 ``images/``，不重复存储。",
    "",
    "| 任务数 | 配方 | 中文说明 | 适合学习 |",
    "| ---: | --- | --- | --- |",
    "| 2 | [02_tasks.recipe](02_tasks/02_tasks.recipe) | [README](02_tasks/README.md) | 原图、边缘 |",
    "| 4 | [04_tasks.recipe](04_tasks/04_tasks.recipe) | [README](04_tasks/README.md) | 加入阈值、二维码 |",
    "| 6 | [06_tasks.recipe](06_tasks/06_tasks.recipe) | [README](06_tasks/README.md) | 加入 Blob、轮廓 |",
    "| 8 | [08_tasks.recipe](08_tasks/08_tasks.recipe) | [README](08_tasks/README.md) | 加入形态学、直线 |",
    "| 10 | [10_tasks.recipe](10_tasks/10_tasks.recipe) | [README](10_tasks/README.md) | 加入颜色、中文 OCR |",
    "| 12 | [12_tasks.recipe](12_tasks/12_tasks.recipe) | [README](12_tasks/README.md) | 加入复杂二维码、两点测量 |",
    "| 16 | [16_tasks.recipe](16_tasks/16_tasks.recipe) | [README](16_tasks/README.md) | 完整覆盖与参数对照 |",
    "",
    "从 2 任务案例开始逐级加载最容易理解。所有处理工具遵循「未绑定 ROI 就执行整图」；测量任务使用配方内明确绑定的 ROI。移动案例时请整体复制 ``task_series`` 目录。",
    "",
    "## 共用测试图片",
    "",
    "![零件缺陷](images/零件缺陷.jpg)",
    "",
    "![清晰二维码](images/清晰二维码.png)",
    "",
    "![复杂二维码](images/复杂二维码.png)",
    "",
    "![中文文字](images/中文文字.jpg)",
    ""
)
Write-Utf8File (Join-Path $seriesRoot "README.md") (($indexLines -join "`n") + "`n")

Write-Host "Generated task examples under: $seriesRoot"
