# Release and CI

> Documentation sync: 2026-07-25. This file has been reviewed against `master` after the `codex/p0-p4-release` merge; see `docs/STATUS_2026-07-25.md` for the consolidated change summary.


## 2026-07-25 master update

`codex/p0-p4-release` has been merged into the GitHub default branch `master`
and pushed to `origin/master` at merge commit `64ddcba`.

This update brings in the P0-P4 application work: component refactoring, restored
rotated ROI and geometry drawing workflows, run-result review UI, preview texture
caching, frame archiving, recipe autosave, persisted hardware settings, camera
trigger/exposure flow updates, SPC storage, expanded inspection history/statistics,
and tool execution/dependency UI refinements.

### Code modules documented for this update

- Geometry drawing: `Algorithm/GeometryDrawTool.*`, `UI/GeometryDrawEditor.*`, `Core/GeometryPrimitive.*`, and `Core/RotatedROI.*`.
- Result review: `UI/RunResultWindow.*`.
- Tool UI/settings: `UI/Tools/BasicToolPanels.*` and `Core/ToolSettings.h`.
- Execution dependency model: `Core/ToolExecutionGraph.*`.
- Persistence: `Core/RecipeAutosaveService.*` and `Core/HardwareSettingsService.*`.
- Hardware/camera flow: `Core/HardwareRuntimeService.*`, `Core/OpenCvCameraAdapter.*`, and `UI/HardwareWindow.cpp`.
- Traceability/statistics: `Core/FrameArchiveService.*`, `Core/SpcDatabase.*`, and `Core/InspectionHistory.*`.
- Preview performance: `Renderer/PreviewTextureCache.*`.
## Local runtime package

Build the Release x64 application first, then run:

```powershell
pwsh -File .\scripts\package_runtime.ps1
```

The script creates `dist/IMgui_Opencv-Release-x64.zip`. It includes the executable,
runtime DLLs copied by the post-build step, fonts, production recipes, runnable example
recipes, QR/OCR sample images, OCR assets, optional model files, hardware documentation,
and runtime configuration files. Packaging fails when a required runtime DLL or required
example asset is missing and verifies those entries after compression.

The default package excludes the optional 333 MB CUDA provider because the current GPU path uses DirectML. Build a CUDA-provider package explicitly when required:

```powershell
pwsh -File .\scripts\package_runtime.ps1 -IncludeCudaProvider
```

## Continuous integration

`.github/workflows/windows-build.yml` uses the Node 24 action generations, checks out Git LFS runtime dependencies, builds both Visual Studio projects, and runs the complete `RegressionTests.exe` command. Missing optional OCR or YOLO model files do not fail the build; the runtime package simply excludes absent optional assets. A missing runtime ZIP fails artifact upload.

Winsock and IP Helper are Windows system dependencies linked through `Ws2_32.lib` and
`Iphlpapi.lib`; they do not add DLLs to the runtime archive. The open62541 OPC UA client
is statically linked. Its exact amalgamation source, header, version metadata, and MPL-2.0
license are included in the runtime archive. Vendor camera/PLC SDK DLLs remain optional.

## Release acceptance

- `Windows_imgui.vcxproj` builds with zero errors.
- `Test/RegressionTests.vcxproj` builds with zero errors.
- The complete regression executable passes, not only policy or caliper subsets.
- The packaging smoke check confirms that the runtime archive contains `Windows_imgui.exe`, OpenCV 5.0 DLLs, and the required DirectML/NCNN/ONNX Runtime DLLs.
