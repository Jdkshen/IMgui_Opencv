# Code Layout and Storage Rules

> Documentation sync: 2026-07-25. This file has been reviewed against `master` after the `codex/p0-p4-release` merge; see `docs/STATUS_2026-07-25.md` for the consolidated change summary.


This repository is organized by ownership. Keep production source code in the
existing module directories instead of adding new source files at the project
root.

## Source Modules

| Directory | Responsibility | Typical contents |
| --- | --- | --- |
| `Core/` | Runtime state, image state, ROI state, tool-chain scheduling, recipe services, result publishing, calibration and fixture transforms | `VisionContext`, `ImageState`, `ToolExecutor`, `ToolController`, `RecipeManager` |
| `Algorithm/` | Image-processing and inspection operators. Algorithms receive `VisionContext` and return `ToolResult` through `ITool` | edge, threshold, Blob, contour, line, shape, template, OCR, QR, caliper and measurement tools |
| `UI/` | Dear ImGui windows, parameter collection, ROI editing and result presentation | `ToolsWindow`, `ImageViewer`, `ROIManager`, `DockSpaceHost` |
| `Renderer/` | Rendering-only helpers | fonts and rendering support |
| `Log/` | Logging implementation | log storage and log window support |
| `Test/` | C++ regression project and test fixtures | recipe round-trip, judgement, ROI and caliper tests |
| `docs/` | Design notes, API notes, build instructions and tracked test recipes | architecture, OpenCV 5.0, ImGui, performance and roadmap documents |
| `include/` | Third-party headers and vendored libraries | OpenCV, ImGui, nlohmann/json and ZXing-cpp headers |
| `assets/` | Small test images and fonts | QR test images and runtime fonts |
| `models/` | Optional local model files | PP-OCRv6 assets and local test models |
| `redist/` | Runtime DLL and LIB dependencies | OpenCV, ONNX Runtime, DirectML, NCNN and ZXing binaries |

## Layer Rules

1. `UI` collects parameters and displays state. It requests execution through
   `Core/ToolController` and does not implement algorithm details.
2. `Core` owns execution context and scheduling. It works with `VisionContext`,
   `ITool`, `ToolInstance` and `ToolResult`.
3. `Algorithm` does not read UI globals such as `gImage`, `gROIs` or ImGui
   drawing state. A tool receives its image and ROI through `VisionContext`.
4. `ToolInstance` contains per-instance parameters, labels, ROI bindings and
   judgement settings. Runtime-only fields such as `lastResult` and
   `measureRuntimeROIIds` are not written to recipes.
5. `Renderer` and `Log` must not become alternate application-state stores.

## Recipe Storage

- Tracked source recipe: `docs/recipe_examples/all_tools_test.recipe`
- Runtime copy for the normal Release executable:
  `x64/Release/recipes/全工具测试.recipe`
- Runtime copy for the verification executable:
  `x64/ReleaseVerify/recipes/全工具测试.recipe`
- Template images used by the test recipe stay beside the runtime recipe.
- Build outputs under `x64/`, `Debug/` and `Release/` are ignored and must not
  be treated as source code.

The all-tools recipe contains tool types `0` through `15`, excluding no
functional tool. Type `12` is the original-image chain node. YOLO entries are
kept in the recipe but require a local model path before inference can pass.

## Adding New Code

- New state or orchestration: `Core/`.
- New inspection operator: `Algorithm/` plus an `ITool` adapter if it is part
  of the tool chain.
- New controls or display: `UI/`.
- New regression coverage: `Test/` and update the test project file.
- New design or operating instructions: `docs/`.
- Do not place generated `.obj`, `.exe`, `.dll`, `.pdb`, recipe runtime copies
  or user settings in the source tree root.

## Build Verification

```powershell
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' Windows_imgui.vcxproj /p:Configuration=Debug /p:Platform=x64 /m:1 /v:minimal
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' Test\RegressionTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /m:1 /v:minimal
.\Test\x64\Debug\RegressionTests.exe --policy-only
.\Test\x64\Debug\RegressionTests.exe --caliper-only
```
