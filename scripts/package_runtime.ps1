param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$Output = "",
    [string]$Executable = "",
    [switch]$IncludeCudaProvider
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo "$Platform\$Configuration"
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repo "dist\IMgui_Opencv-$Configuration-$Platform"
}

if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $buildDir "Windows_imgui.exe"
}
elseif (-not [System.IO.Path]::IsPathRooted($Executable)) {
    $Executable = Join-Path $repo $Executable
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Build output not found: $Executable"
}
$bootstrapProject = Join-Path $repo "Bootstrap\Bootstrap.vcxproj"
$bootstrapExecutable = Join-Path $buildDir "bootstrap\Windows_imgui.exe"
$msbuildCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe")
)
$msbuild = $msbuildCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild not found; cannot build the static startup diagnostic launcher."
}
& $msbuild $bootstrapProject /m:1 "/p:Configuration=$Configuration" "/p:Platform=$Platform" /nologo /v:minimal
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $bootstrapExecutable -PathType Leaf)) {
    throw "Static startup diagnostic launcher build failed."
}

if (Test-Path $Output) {
    Remove-Item -LiteralPath $Output -Recurse -Force
}
New-Item -ItemType Directory -Path $Output | Out-Null

Copy-Item -LiteralPath $bootstrapExecutable -Destination (Join-Path $Output "Windows_imgui.exe")
Copy-Item -LiteralPath $Executable -Destination (Join-Path $Output "Windows_imgui_core.exe")
$cameraDiagnostics = Join-Path $repo "Test\$Platform\$Configuration\RegressionTests.exe"
if (Test-Path -LiteralPath $cameraDiagnostics -PathType Leaf) {
    Copy-Item -LiteralPath $cameraDiagnostics -Destination (Join-Path $Output "CameraDiagnostics.exe")
}
else {
    throw "Camera diagnostics executable not found: $cameraDiagnostics"
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Start_Diagnostics.cmd") `
    -Destination (Join-Path $Output "Start_Diagnostics.cmd") -Force
$requiredRuntimeDlls = @(
    "DirectML.dll",
    "ncnn.dll",
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll",
    "opencv_videoio_ffmpeg500_64.dll",
    "opencv_videoio_msmf500_64.dll",
    "opencv_world500.dll"
)
$vcRuntimeDlls = @(
    "concrt140.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_atomic_wait.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "vcomp140.dll"
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

# Package the exact MSVC runtime beside the executable. Clean Windows/LTSC
# installations often do not have the toolset-matching VC runtime installed.
$vcRedistRoots = @()
if (-not [string]::IsNullOrWhiteSpace($env:VCToolsRedistDir) -and
    (Test-Path -LiteralPath $env:VCToolsRedistDir -PathType Container)) {
    $vcRedistRoots += [System.IO.Path]::GetFullPath($env:VCToolsRedistDir)
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $installationPath = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
    if (-not [string]::IsNullOrWhiteSpace($installationPath)) {
        $installedRedist = Join-Path $installationPath "VC\Redist\MSVC"
        if (Test-Path -LiteralPath $installedRedist -PathType Container) {
            $vcRedistRoots += $installedRedist
        }
    }
}
$vcRedistRoots = @($vcRedistRoots | Select-Object -Unique)

foreach ($dll in $vcRuntimeDlls) {
    $source = $null
    foreach ($root in $vcRedistRoots) {
        $candidate = Get-ChildItem -LiteralPath $root -Recurse -File -Filter $dll |
            Where-Object {
                $_.FullName -match '[\\/]x64[\\/]' -and
                $_.FullName -notmatch '[\\/]onecore[\\/]'
            } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            $source = $candidate.FullName
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($source)) {
        $source = Join-Path $buildDir $dll
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        $source = Join-Path (Join-Path $repo "redist") $dll
    }
    if ([string]::IsNullOrWhiteSpace($source) -or
        -not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required MSVC x64 runtime dependency not found: $dll"
    }
    Copy-Item -LiteralPath $source -Destination $Output -Force
}

# When allowed by the Hikrobot deployment license, an installed MVS runtime can
# be carried beside the executable. The application also works without these
# files and reports a clear SDK diagnostic only when the MVS backend is selected.
$mvsRuntimeRoots = @()
foreach ($variableName in @("MVCAM_COMMON_RUNENV", "MVS_RUNTIME")) {
    $value = [Environment]::GetEnvironmentVariable($variableName)
    if (-not [string]::IsNullOrWhiteSpace($value)) {
        $mvsRuntimeRoots += $value
    }
}
$mvsRuntimeRoots += @(
    (Join-Path ${env:ProgramFiles(x86)} "Common Files\MVS\Runtime\Win64_x64"),
    (Join-Path $env:ProgramFiles "Common Files\MVS\Runtime\Win64_x64"),
    (Join-Path ${env:ProgramFiles(x86)} "MVS\Runtime\Win64_x64"),
    (Join-Path $env:ProgramFiles "MVS\Runtime\Win64_x64")
)
$mvsRuntimeRoot = $mvsRuntimeRoots |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ "MvCameraControl.dll") -PathType Leaf } |
    Select-Object -First 1
$mvsPackagedDlls = @()
if ($mvsRuntimeRoot) {
    Get-ChildItem -LiteralPath $mvsRuntimeRoot -File -Filter "*.dll" | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Output -Force
        $mvsPackagedDlls += $_.Name
    }
    Write-Host "Hikrobot MVS x64 runtime included from: $mvsRuntimeRoot"
}
else {
    Write-Host "Hikrobot MVS x64 runtime not found; target computers must install MVS Runtime."
}

# Package the complete Huaray/iRAYPLE IMV runtime directory. MVSDKmd.dll loads
# several companion DLL/CTI/data files at runtime, so copying only MVSDKmd.dll
# results in a silent LoadLibrary failure on a clean target computer.
$huarayRuntimeRoots = @()
foreach ($variableName in @("IMV_RUNTIME", "MV_VIEWER_HOME", "MVSDK_PATH")) {
    $value = [Environment]::GetEnvironmentVariable($variableName)
    if (-not [string]::IsNullOrWhiteSpace($value)) {
        $huarayRuntimeRoots += $value
        $huarayRuntimeRoots += (Join-Path $value "Runtime\x64")
    }
}
$huarayRuntimeRoots += @(
    "F:\MV Viewer\Runtime\x64",
    (Join-Path $env:ProgramFiles "MV Viewer\Runtime\x64"),
    (Join-Path ${env:ProgramFiles(x86)} "MV Viewer\Runtime\x64"),
    (Join-Path $env:ProgramFiles "iRAYPLE\MV Viewer\Runtime\x64"),
    (Join-Path ${env:ProgramFiles(x86)} "iRAYPLE\MV Viewer\Runtime\x64")
)
$huarayRuntimeRoot = $huarayRuntimeRoots |
    Where-Object {
        (Test-Path -LiteralPath $_ -PathType Container) -and
        (Test-Path -LiteralPath (Join-Path $_ "MVSDKmd.dll") -PathType Leaf)
    } |
    Select-Object -Unique |
    Select-Object -First 1
$huarayPackagedFiles = @()
$huarayVcRuntimeDlls = @()
if ($huarayRuntimeRoot) {
    Get-ChildItem -LiteralPath $huarayRuntimeRoot -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Output -Force
        $huarayPackagedFiles += $_.Name
    }
    Write-Host "Huaray iRAYPLE IMV x64 runtime included from: $huarayRuntimeRoot"

    # MVSDKmd.dll was built with Visual C++ 2013 and will not load on a clean
    # Windows 10 installation unless the matching x64 CRT is also deployed.
    foreach ($dll in @("msvcp120.dll", "msvcr120.dll")) {
        $candidates = @(
            (Join-Path $huarayRuntimeRoot $dll),
            (Join-Path $env:SystemRoot "System32\$dll"),
            (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio 12.0\VC\redist\x64\Microsoft.VC120.CRT\$dll")
        )
        $source = $candidates |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
        if (-not $source) {
            throw "Huaray IMV dependency not found: $dll (install Visual C++ 2013 x64 Redistributable)"
        }
        Copy-Item -LiteralPath $source -Destination $Output -Force
        $huarayVcRuntimeDlls += $dll
    }
}
else {
    Write-Host "Huaray iRAYPLE IMV x64 runtime not found; target computers must install MV Viewer Runtime."
}

foreach ($source in @(
    "assets\fonts",
    "assets\icons",
    "assets\images\qr_tests",
    "docs\recipe_examples",
    "recipes",
    "models\ppocrv6",
    "third_party\open62541"
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
    "docs\HIKROBOT_MVS.md",
    "docs\HUARAY_IMV.md",
    "docs\MULTI_CAMERA.md",
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
Get-ChildItem -LiteralPath $exampleRecipeDir -Filter "*.recipe" -File -Recurse | ForEach-Object {
    $recipeFile = $_
    $recipe = Get-Content -LiteralPath $recipeFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not [string]::IsNullOrWhiteSpace($recipe.imagePath)) {
        $imagePath = [System.IO.Path]::GetFullPath(
            (Join-Path $recipeFile.DirectoryName $recipe.imagePath))
        if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
            throw "Example recipe image is missing: $($recipeFile.Name) -> $($recipe.imagePath)"
        }
    }
    foreach ($taskGroup in @($recipe.taskGroups)) {
        if ([string]::IsNullOrWhiteSpace($taskGroup.imagePath)) { continue }
        $taskImagePath = [System.IO.Path]::GetFullPath(
            (Join-Path $recipeFile.DirectoryName $taskGroup.imagePath))
        if (-not (Test-Path -LiteralPath $taskImagePath -PathType Leaf)) {
            throw "Example task image is missing: $($recipeFile.Name) / $($taskGroup.name) -> $($taskGroup.imagePath)"
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
        "Windows_imgui_core.exe",
        "CameraDiagnostics.exe",
        "Start_Diagnostics.cmd",
        "docs/recipe_examples/case_qr_clean.recipe",
        "docs/recipe_examples/case_pipeline.recipe",
        "docs/recipe_examples/12345_tpl0.png",
        "docs/recipe_examples/12345_tpl1.png",
        "docs/recipe_examples/task_series/02_tasks/02_tasks.recipe",
        "docs/recipe_examples/task_series/04_tasks/04_tasks.recipe",
        "docs/recipe_examples/task_series/06_tasks/06_tasks.recipe",
        "docs/recipe_examples/task_series/08_tasks/08_tasks.recipe",
        "docs/recipe_examples/task_series/10_tasks/10_tasks.recipe",
        "docs/recipe_examples/task_series/12_tasks/12_tasks.recipe",
        "docs/recipe_examples/task_series/16_tasks/16_tasks.recipe",
        "docs/recipe_examples/task_series/README.md",
        "third_party/open62541/LICENSE",
        "third_party/open62541/README.md",
        "third_party/open62541/open62541.c",
        "third_party/open62541/open62541.h",
        "assets/images/qr_tests/qr_test.png",
        "assets/images/ocr_product_sample.jpg",
        "models/yolo11n.onnx",
        "models/coco_classes.txt",
        "docs/HARDWARE_INTEGRATION.md",
        "docs/HIKROBOT_MVS.md",
        "docs/MULTI_CAMERA.md",
        "docs/HUARAY_IMV.md"
    ) + $requiredRuntimeDlls + $vcRuntimeDlls + $mvsPackagedDlls +
        $huarayPackagedFiles + $huarayVcRuntimeDlls
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

$zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
$checksumPath = "$zip.sha256"
Set-Content -LiteralPath $checksumPath -Encoding ascii -Value `
    ("{0} *{1}" -f $zipHash, [System.IO.Path]::GetFileName($zip))

# The default release path is canonical. Remove old date/suffix variants only
# after the new archive has passed verification, so dist never accumulates
# several similarly named releases again.
$canonicalOutput = Join-Path $repo "dist\IMgui_Opencv-$Configuration-$Platform"
if ([System.IO.Path]::GetFullPath($Output).TrimEnd('\') -eq
    [System.IO.Path]::GetFullPath($canonicalOutput).TrimEnd('\')) {
    $distRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $canonicalOutput))
    $canonicalLeaf = Split-Path -Leaf $canonicalOutput
    Get-ChildItem -LiteralPath $distRoot -Force |
        Where-Object { $_.Name -like "$canonicalLeaf-*" } |
        ForEach-Object {
            $candidate = [System.IO.Path]::GetFullPath($_.FullName)
            if ([System.IO.Path]::GetDirectoryName($candidate) -ne $distRoot) {
                throw "Refusing to clean release outside dist: $candidate"
            }
            Remove-Item -LiteralPath $candidate -Recurse -Force
        }
}

Write-Host "Runtime package: $zip"
Write-Host "Runtime package SHA256: $zipHash"
Write-Host "Checksum file: $checksumPath"
Write-Host "Runtime package verification: passed"
