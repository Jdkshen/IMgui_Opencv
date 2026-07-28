# UI/Core/Algorithm Separation Implementation Plan

> 历史实施计划：记录 2026-06-23 UI/Core/Algorithm 分层迁移步骤；当前分层与残留边界见 `../../CODE_STRUCTURE.md`。


> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move shared vision tool data out of UI headers and progressively reduce Core/UI/Algorithm coupling without changing user-facing behavior.

**Architecture:** First extract common data contracts (`ROI`, tool metadata, `ToolInstance`) into `Core` headers. Then replace direct Core includes of UI shell headers with those contracts and narrower bridge APIs. After that, migrate low-risk algorithm tools from legacy globals toward `VisionContext` and `ITool` inputs.

**Tech Stack:** C++20, MSBuild/Visual C++ v145, ImGui, OpenCV 5, existing regression test project.

---

### Task 1: Extract ROI Data Contract

**Files:**
- Create: `Core/ROI.h`
- Modify: `UI/DockSpaceHost.h`
- Modify: `Core/VisionContext.h`
- Modify includes that only need ROI data

- [ ] Create `Core/ROI.h` containing `ROIType`, `ROI`, `HandleType`, `ROIBox`, and ROI size constants currently in `UI/DockSpaceHost.h`.
- [ ] Update `UI/DockSpaceHost.h` to include `../Core/ROI.h` and keep only UI window/global declarations.
- [ ] Update `Core/VisionContext.h` to include `ROI.h` instead of `../UI/DockSpaceHost.h`.
- [ ] Build Debug x64 and run regression tests.

### Task 2: Extract Tool Types and Tool Instance

**Files:**
- Create: `Core/ToolTypes.h`
- Create: `Core/ToolInstance.h`
- Modify: `UI/ToolsWindow.h`
- Modify: `Core/ToolExecutor.h`
- Modify users that need only the shared types

- [ ] Move `ToolCategory` and `ToolMeta` to `Core/ToolTypes.h`.
- [ ] Move `ToolInstance` to `Core/ToolInstance.h`.
- [ ] Keep UI-only function aliases and globals in `UI/ToolsWindow.h`.
- [ ] Update `Core/ToolExecutor.h` to include `ToolInstance.h` instead of `UI/ToolsWindow.h`.
- [ ] Build Debug x64 and run regression tests.

### Task 3: Start Core Include Cleanup

**Files:**
- Modify: `Core/ToolExecutor.cpp`
- Modify: `Core/ToolController.cpp`
- Modify: `Core/VideoCapture.cpp`
- Modify: `Core/ImageLoadController.cpp`
- Modify: `Core/LiveYoloRunner.cpp`

- [ ] Replace broad `../Windows_imgui.h` includes with narrower headers where possible.
- [ ] Keep required legacy globals declared in a small bridge header if needed.
- [ ] Do not change runtime behavior in this task.
- [ ] Build Debug x64 and run regression tests.

### Task 4: Low-Risk Algorithm Global Reduction

**Files:**
- Modify: `Algorithm/ThresholdITool.cpp`
- Modify: `Algorithm/EdgeTool.cpp`
- Modify: `Algorithm/MorphologyTool.cpp`
- Modify: `Algorithm/BlobTool.cpp`
- Modify: `Algorithm/ColorAnalyzer.cpp`
- Modify/add regression tests in `Test/regression_tests.cpp`

- [ ] Confirm each low-risk tool executes through `VisionContext` with no direct UI state dependency.
- [ ] Add or update focused regression tests for ROI clipping and debug image output.
- [ ] Build Debug x64 and run regression tests.

### Task 5: UI Thinning Follow-Up

**Files:**
- Modify: `UI/ToolsWindow.cpp`
- Modify: `UI/ROIManager.cpp`
- Modify: `UI/ImageViewer.cpp`
- Modify: `Core/ToolController.cpp`

- [ ] Keep UI responsibilities to parameter collection, ROI editing, result display, and run requests.
- [ ] Move execution/state transition logic into Core APIs where practical.
- [ ] Build Debug x64, Release x64, and run regression tests.
