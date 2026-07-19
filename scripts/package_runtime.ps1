param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$Output = "",
    [switch]$IncludeCudaProvider
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo "$Platform\$Configuration"
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repo "dist\IMgui_Opencv-$Configuration-$Platform"
}

if (-not (Test-Path (Join-Path $buildDir "Windows_imgui.exe"))) {
    throw "Build output not found: $buildDir\Windows_imgui.exe"
}

if (Test-Path $Output) {
    Remove-Item -LiteralPath $Output -Recurse -Force
}
New-Item -ItemType Directory -Path $Output | Out-Null

Copy-Item (Join-Path $buildDir "Windows_imgui.exe") $Output
$requiredRuntimeDlls = @(
    "DirectML.dll",
    "ncnn.dll",
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll",
    "opencv_videoio_ffmpeg500_64.dll",
    "opencv_videoio_msmf500_64.dll",
    "opencv_world500.dll"
)
$runtimeDlls = @($requiredRuntimeDlls)
if ($IncludeCudaProvider) {
    $runtimeDlls += "onnxruntime_providers_cuda.dll"
}
foreach ($dll in $runtimeDlls) {
    $source = Join-Path $buildDir $dll
    if (-not (Test-Path -LiteralPath $source)) {
        $source = Join-Path (Join-Path $repo "redist") $dll
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required runtime dependency not found in build output or redist: $dll"
    }
    Copy-Item -LiteralPath $source -Destination $Output -Force
}

foreach ($source in @(
    "assets\fonts",
    "assets\images\qr_tests",
    "docs\recipe_examples",
    "recipes",
    "models\ppocrv6"
)) {
    $path = Join-Path $repo $source
    if (Test-Path $path) {
        $destination = Join-Path $Output $source
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        Copy-Item -Path (Join-Path $path "*") -Destination $destination -Recurse -Force
    }
}

foreach ($source in @(
    "assets\images\ocr_product_sample.jpg",
    "docs\HARDWARE_INTEGRATION.md",
    "docs\RELEASE.md"
)) {
    $path = Join-Path $repo $source
    if (Test-Path -LiteralPath $path) {
        $destination = Join-Path $Output $source
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $path -Destination $destination -Force
    }
}

New-Item -ItemType Directory -Path (Join-Path $Output "models") -Force | Out-Null
foreach ($pattern in @("models\*.onnx", "models\*.txt")) {
    Get-ChildItem (Join-Path $repo $pattern) -File -ErrorAction SilentlyContinue |
        Copy-Item -Destination (Join-Path $Output "models")
}

foreach ($file in @("theme.cfg", "run_settings.json")) {
    $path = Join-Path $repo $file
    if (Test-Path $path) { Copy-Item -LiteralPath $path -Destination $Output -Force }
}

$exampleRecipeDir = Join-Path $Output "docs\recipe_examples"
Get-ChildItem -LiteralPath $exampleRecipeDir -Filter "*.recipe" -File | ForEach-Object {
    $recipeFile = $_
    $recipe = Get-Content -LiteralPath $recipeFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not [string]::IsNullOrWhiteSpace($recipe.imagePath)) {
        $imagePath = [System.IO.Path]::GetFullPath(
            (Join-Path $recipeFile.DirectoryName $recipe.imagePath))
        if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
            throw "Example recipe image is missing: $($recipeFile.Name) -> $($recipe.imagePath)"
        }
    }
    foreach ($tool in @($recipe.tools)) {
        if ([string]::IsNullOrWhiteSpace($tool.templateFile)) { continue }
        $localTemplate = Join-Path $recipeFile.DirectoryName $tool.templateFile
        $sharedTemplate = Join-Path (Join-Path $Output "recipes") $tool.templateFile
        if (-not (Test-Path -LiteralPath $localTemplate -PathType Leaf) -and
            -not (Test-Path -LiteralPath $sharedTemplate -PathType Leaf)) {
            throw "Example recipe template is missing: $($recipeFile.Name) -> $($tool.templateFile)"
        }
    }
}

$zip = "$Output.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $Output "*") -DestinationPath $zip

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
    $requiredEntries = @(
        "Windows_imgui.exe",
        "docs/recipe_examples/case_qr_clean.recipe",
        "docs/recipe_examples/case_pipeline.recipe",
        "docs/recipe_examples/12345_tpl0.png",
        "docs/recipe_examples/12345_tpl1.png",
        "assets/images/qr_tests/qr_test.png",
        "assets/images/ocr_product_sample.jpg",
        "docs/HARDWARE_INTEGRATION.md"
    ) + $requiredRuntimeDlls
    if ($IncludeCudaProvider) {
        $requiredEntries += "onnxruntime_providers_cuda.dll"
    }
    foreach ($entry in $requiredEntries) {
        if ($entries -notcontains $entry) {
            throw "Runtime package verification failed; missing archive entry: $entry"
        }
    }
}
finally {
    $archive.Dispose()
}

Write-Host "Runtime package: $zip"
Write-Host "Runtime package verification: passed"
