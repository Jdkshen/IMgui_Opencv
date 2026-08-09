# Development Plan: P0-P4

> 历史计划：本文保留 2026-07-19 的 P0～P4 英文执行清单，不作为当前未完成任务列表；当前路线见 `ROADMAP.md`，当前架构见 `CODE_STRUCTURE.md`。


This document is the execution checklist for the current architecture cleanup.
The merge gate for every phase is a clean Release x64 build of both projects
and a full run of `Test/x64/Release/RegressionTests.exe`.

## Current Baseline

- P0 input source behavior, original-image deletion, and recipe auto-run are implemented.
- `ToolInstance.toolId` and `ToolResult.sourceToolId` are the stable identity path.
- `ImageState`, `ROIState`, `ToolChainState`, `FrameNavigation`, and `ResultOverlayState` are Core-owned state services.
- `TemplateMatchingTool.cpp` owns template matching without legacy ImGui/global parameters; template assets are managed by Core services.
- Tool parameters are serialized by `ToolInstance::ToRecipeJson()` and `LoadRecipeJson()`.
- `ToolChainState::MoveTool()` and `RemoveTool()` now own index remapping, dependency cleanup, and live-tool index cleanup.
- `ImageViewState` now owns zoom, pan, canvas position, and grid settings; ImageViewer and ROIManager no longer export or consume view globals.
- Main Release x64 build, test Release x64 build, and full regression pass are green.

## P0 Stability

Image import is Core-owned through `ImageImportService`: single-image import,
recursive folder scanning, navigation, empty-folder diagnostics, and stale
inspection-state cleanup are covered by regression tests. `AsyncImageLoader`
also has valid-image and decode-failure coverage.

### P0.1 Import and frame source

Acceptance:

- Open-file filter accepts JPEG, PNG, BMP, TIFF, and WebP.
- `OFN_NOCHANGEDIR` is set on all file dialogs.
- Folder scanning supports recursive subdirectories and ignores reparse-point loops.
- Empty folders and decode failures produce a visible UI error and a log entry.
- The existing recursive scan regression remains in the full test executable.

### P0.2 Tool-chain edits

Acceptance:

- Deleting the original-image tool is allowed.
- Single-tool, step, and all-tool execution use the same input-source rules.
- Move, delete, duplicate, and recipe replacement clear the queue, timing cache,
  result overlays, cached results, and real-time detection state.
- Dependencies are remapped by index while stable IDs remain unchanged.

### P0.3 Recipe execution gate

Acceptance:

- Example recipes load from paths relative to the recipe or repository root.
- Missing image, ROI, template, reference image, OCR model, and YOLO model are
  reported together before execution.
- A recipe can request automatic execution after its image is loaded.
- Full regression is always run; policy-only and caliper-only runs are insufficient.

## P1 Architecture Closeout

### P1.1 Core-owned tool editing

Status: complete.

- UI calls `ToolChainState::MoveTool()` and `RemoveTool()`.
- `ToolsWindow.h` no longer contains mutation algorithms.
- Stable read-only lookup by `toolId` is available. `ToolsWindow` uses explicit
  command/query APIs and no longer exposes UI references to active-tool or YOLO state.

### P1.2 Recipe ownership

Status: complete.

- New tool parameters are written by `ToolInstance::ToRecipeJson()`.
- Legacy top-level `threshold` and `templateMatch` fields remain load-compatible.
- `RecipeManager::Capture()` no longer mirrors current tool parameters into those
  deprecated fields.
- `RecipeToolInstance` is a composition DTO, not a runtime ToolInstance subclass.
- Recipe snapshots own template, difference-reference, and multi-color-reference
  payloads. Save does not query live tools; Load resolves assets before Apply.

### P1.3 UI boundary

Current status:

- Image view transform and grid state are Core-owned through `ImageViewState`.
- Tool cards use Core command/query APIs; measurement ROI restore, synchronization,
  rollback, selection, and removal are owned by `ToolROIService`.
- Template, shape-template, multi-color reference, and difference-reference capture
  are Core-owned through `ToolAssetService`, keyed by stable tool/ROI identities.
- `UI::gROIs` and `UI::gSelectedROI` compatibility exports have been removed.
- Bound search-ROI add/modify/confirm/cancel/clear is Core-owned through
  `ToolROIService` and uses stable tool/ROI identities.
- `ImageViewer` obtains playback snapshots from `FrameNavigation`, input commands from
  `ImageImportService`, and unified/realtime/Fixture overlays from `ResultOverlayState`.
- Production UI/Core/Algorithm files no longer include the `Windows_imgui.h` umbrella;
  the old UI active-tool and YOLO runtime reference aliases have also been removed.

## P2 Industrial Capability

1. Add Blob quality outputs: center, circularity, aspect ratio, orientation,
   valid count, filtering limits, tolerance, and explicit OK/NG reason.
2. Add difference inspection: reference image, thresholded difference regions,
   difference area, and pass/fail limits.
3. Add calibration wizard UI for multi-point affine, homography, and distortion
   models, with residual/max-error display and JSON import/export.
4. Draw fixture reference and current coordinate frames together.
5. Add inspection history with mean, standard deviation, Cp/Cpk, and CSV export.
6. Extend result ROI selection with category, score, area, and deterministic sorting.

Each industrial operator must return `ToolResult` measurements and quality
metrics; UI must not recompute pass/fail from drawing primitives.

Current P2 progress: Blob aggregate metrics and named measurement-range judgement,
Fixture reference/current coordinate visualization, result ROI filters/sorting, the
complete SPC Core/UI flow, and the multi-point calibration editor with per-sample
residuals and JSON import/export are implemented and regression-tested. Lens
distortion calibration remains a later extension.

## P3 Tool-chain Experience

- Tool enable/disable and instance duplication are implemented.
- Dependency validation rejects invalid upstream references and cycles before execution.
- Tool cards visualize incoming Result ROI/Fixture dependencies, downstream consumers,
  and invalid dependency links using the same Core resolution as validation.
- A single UI preflight panel lists all missing resources and dependency issues.
- Group name, card collapse, group filtering, batch enable/disable, batch result-label
  visibility, and batch stop-on-failure policy are implemented.
- OCR/YOLO model-missing skip policy is implemented with explicit skipped results
  and log status.

## P4 Release and Hardware

- GitHub Actions passes on a clean Windows runner and uploads the verified runtime ZIP.
- `runtime.zip` validates the executable, runtime libraries, example recipes, template
  assets, sample images, hardware documentation, and open62541 source/license entries.
- Camera, PLC, Modbus TCP, and OPC UA contracts plus adapter lifecycle registration are
  implemented in Core.
- `HardwareRuntimeService` publishes registered camera frames into `FrameSourceState` and
  routes Pass/Fail/Error status to PLC tags, Modbus coils, or OPC UA nodes. Offline mock
  adapters cover all three output routes.
- Concrete portable adapters now cover OpenCV camera indexes/stream URLs, Modbus TCP
  functions 01/03/05/06 over Winsock, and named PLC tags mapped to Modbus coils or
  scaled holding registers. Protocol and runtime integration are regression-tested.
- Native OPC UA is implemented with open62541 v1.4.17 and protocol-tested against a real
  local OPC UA TCP server. The bundled build supports anonymous SecurityPolicy None;
  certificate-enabled deployments require an approved encryption-enabled rebuild.
- The device window now connects portable camera/Modbus/PLC/OPC UA adapters through Core.
  Camera reads run asynchronously, completed frames enter the normal image/GPU path on
  the UI thread, and tool batches automatically publish aggregate Pass/Fail/Error status.
- CI uses the Node 24 generations of checkout, MSBuild setup, and artifact upload actions;
  a missing runtime archive fails the workflow instead of producing an empty artifact.
- Vendor-specific camera/PLC SDK adapters remain deployment choices; see
  `docs/HARDWARE_INTEGRATION.md` for the required Core boundary.
- Keep device implementations out of UI and preserve offline recipe/regression operation.

## Per-task Workflow

1. Inspect existing Core/UI ownership and current tests.
2. Make the smallest Core-facing change that removes one UI/global dependency.
3. Add or update a focused regression before changing unrelated behavior.
4. Build `Windows_imgui.vcxproj` and `Test/RegressionTests.vcxproj` in Release x64.
5. Run the complete regression executable.
6. Update this document and `docs/STATUS_2026-07-19.md` with verified status.
