# Unified Subpixel Positioning Implementation Plan

> 历史实施计划：记录 2026-07-13 亚像素迁移步骤；清单和代理提示仅属于当时流程，不应重新执行。计划中的 `Algorithm/TemplateMatch.*` 已删除，当前实现为 `Algorithm/TemplateMatchingTool.*`；当前说明见 `../../INSPECTION_PIPELINE_2026.md`。


> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve floating-point image coordinates through every spatial tool, ROI transform, UI overlay, and downstream result-ROI path, then add measured local subpixel refinement where the algorithm supports it.

**Architecture:** `ToolResult` becomes the single floating-point result contract. `ResultGeometry` is the only boundary that converts floating-point geometry to integer raster coordinates, while `CoordinateTransform` composes crop, rotation, resize, and letterbox mappings. Algorithms keep their existing coarse detectors and add bounded local refinement; raster-only tools remain unchanged.

**Tech Stack:** C++17, OpenCV 5.0, ImGui, existing `RegressionTests.vcxproj`, `cv::Point2f`, `cv::Rect2f`, `cv::RotatedRect`, `cv::Matx33d`.

---

## File Map

Create:

- `Core/ResultGeometry.h`: explicit floating-point-to-raster policies and float geometry helpers.
- `Core/CoordinateTransform.h`: public 2D transform API.
- `Core/CoordinateTransform.cpp`: double-precision matrix composition and mapping.

Modify:

- `Algorithm/ToolResult.h`: floating-point result fields and precision metadata.
- `Core/ROI.h`: keep editable float ROI geometry and expose explicit float bounds.
- `Core/RotatedROI.h`, `Core/RotatedROI.cpp`: map float results without rounding.
- `Core/ToolExecutor.cpp`: float result ROI selection, crop conversion, and result publication.
- `UI/ImageViewer.cpp`: draw float boxes, contours, lines, text quads, and coordinates.
- `Core/ResultExporter.cpp`: export decimal pixel coordinates and precision state.
- `Algorithm/TemplateMatch.cpp`, `Core/ToolExecutor.cpp`: template peak refinement.
- `Algorithm/LineDetector.cpp`: floating-point line fitting.
- `Algorithm/ContourDetector.cpp`, `Algorithm/BlobTool.cpp`: floating-point center and contour output.
- `Algorithm/ShapeMatcher.cpp`, `Algorithm/ShapeTools.cpp`: local shape translation refinement.
- `Algorithm/MultiColorFinder.cpp`: bounded color-error refinement with pixel fallback.
- `Algorithm/YOLOTool.cpp`, `Algorithm/OpenCVYoloDetector.cpp`, `Core/LiveYoloRunner.cpp`: exact float letterbox and ROI mapping.
- `Algorithm/OCRTool.cpp`, `Algorithm/WindowsPPOCREngine.cpp`: preserve OCR quads and crop transforms.
- `Algorithm/QRCodeTool.cpp`: preserve decoder corners and optional `cornerSubPix`.
- `Algorithm/GeometryDrawTool.cpp`: publish existing float geometry directly.
- `Test/regression_tests.cpp`: red-green tests for every public coordinate contract.
- `Test/RegressionTests.vcxproj`: add new core source files if the project does not glob them.

Each algorithm batch must compile and pass the regression suite before the next batch begins.

### Task 1: Add Explicit Geometry Policies

**Files:**
- Create: `Core/ResultGeometry.h`
- Modify: `Test/regression_tests.cpp`

- [ ] **Step 1: Write the failing policy test**

Add this test near the existing ROI conversion tests:

```cpp
void TestResultGeometryRasterPolicies()
{
    const cv::Rect2f source(1.2f, 2.3f, 4.1f, 5.2f);
    const cv::Rect covered = ResultGeometry::CoverRect(source);
    const cv::Rect nearest = ResultGeometry::NearestRect(source);

    Require(covered == cv::Rect(1, 2, 5, 6),
        "cover conversion must include every fractional edge pixel");
    Require(nearest == cv::Rect(1, 2, 4, 5),
        "nearest conversion must not silently use cover semantics");

    const std::array<cv::Point2f, 4> quad = {
        cv::Point2f(1.2f, 2.3f), cv::Point2f(5.3f, 2.3f),
        cv::Point2f(5.3f, 7.5f), cv::Point2f(1.2f, 7.5f)};
    const cv::Rect2f bounds = ResultGeometry::BoundingRect(quad);
    Require(std::abs(bounds.x - 1.2f) < 0.001f &&
            std::abs(bounds.y - 2.3f) < 0.001f &&
            std::abs(bounds.width - 4.1f) < 0.001f &&
            std::abs(bounds.height - 5.2f) < 0.001f,
        "float quad bounds lost fractional coordinates");
}
```

- [ ] **Step 2: Run the test and verify the expected failure**

Run:

```powershell
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' '.\Test\RegressionTests.vcxproj' /t:Build /p:Configuration=Release /p:Platform=x64 /m:1 /v:minimal
```

Expected: compilation fails because `Core/ResultGeometry.h` and its functions do not exist.

- [ ] **Step 3: Add the minimal explicit conversion API**

Implement the header-only API:

```cpp
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/core/types.hpp>

namespace ResultGeometry
{
inline cv::Rect CoverRect(const cv::Rect2f& rect)
{
    const int x1 = static_cast<int>(std::floor(rect.x));
    const int y1 = static_cast<int>(std::floor(rect.y));
    const int x2 = static_cast<int>(std::ceil(rect.x + rect.width));
    const int y2 = static_cast<int>(std::ceil(rect.y + rect.height));
    return cv::Rect(x1, y1, (std::max)(0, x2 - x1), (std::max)(0, y2 - y1));
}

inline cv::Rect NearestRect(const cv::Rect2f& rect)
{
    const int x1 = cvRound(rect.x);
    const int y1 = cvRound(rect.y);
    const int x2 = cvRound(rect.x + rect.width);
    const int y2 = cvRound(rect.y + rect.height);
    return cv::Rect(x1, y1, (std::max)(0, x2 - x1), (std::max)(0, y2 - y1));
}

template <size_t N>
inline cv::Rect2f BoundingRect(const std::array<cv::Point2f, N>& points)
{
    static_assert(N > 0, "BoundingRect requires at least one point");
    float minX = points[0].x, maxX = points[0].x;
    float minY = points[0].y, maxY = points[0].y;
    for (const auto& point : points)
    {
        minX = (std::min)(minX, point.x);
        maxX = (std::max)(maxX, point.x);
        minY = (std::min)(minY, point.y);
        maxY = (std::max)(maxY, point.y);
    }
    return cv::Rect2f(minX, minY, maxX - minX, maxY - minY);
}
}
```

Include `<algorithm>` and use `std::max` with the existing Windows `NOMINMAX` convention.

- [ ] **Step 4: Run the policy test and the existing suite**

Expected: `RegressionTests.exe` reports `all tests passed`.

- [ ] **Step 5: Commit only the new helper and test**

```powershell
git add -- Core/ResultGeometry.h Test/regression_tests.cpp
git commit -m "feat: add explicit result geometry raster policies"
```

### Task 2: Change the Result Contract to Floating Point

**Files:**
- Modify: `Algorithm/ToolResult.h`
- Modify: `Core/ResultGeometry.h`
- Modify: all producers and consumers listed in the File Map
- Test: `Test/regression_tests.cpp`

- [ ] **Step 1: Add a failing fractional result contract test**

Add a test that constructs a `ToolResult` with `Rect2f(10.25f, 20.5f, 30.75f, 40.125f)`, `Point2f` contour points, a `Point2f` line, and a text quad. Publish it through `PublishUnifiedResult`, then assert every value is unchanged within `0.001f`.

- [ ] **Step 2: Run the test before the type migration**

Expected: compilation fails because the current fields are `cv::Rect` and `cv::Point`.

- [ ] **Step 3: Replace spatial result fields**

Use these final types in `Algorithm/ToolResult.h`:

```cpp
enum class ResultPrecision { Pixel, Subpixel, ModelFloat };

struct Region {
    std::vector<cv::Point2f> contour;
    cv::Rect2f bbox;
    bool hasCenter = false;
    cv::Point2f center;
    bool hasOrientedBox = false;
    cv::RotatedRect orientedBox;
    float area = 0.0f;
    float score = 0.0f;
    std::string label;
};

struct Detection {
    cv::Rect2f box;
    int classId = -1;
    float score = 0.0f;
    std::string label;
};

struct Line {
    cv::Point2f p1, p2;
    float length = 0.0f;
    float angle = 0.0f;
};

struct TextItem {
    std::string text;
    cv::Rect2f box;
    bool hasQuad = false;
    std::array<cv::Point2f, 4> quad{};
    float confidence = 0.0f;
};
```

Add `ResultPrecision precision = ResultPrecision::Pixel;` to `ToolResult`. Include `<array>`.

- [ ] **Step 4: Update compile-time raster boundaries**

Every `cv::Mat(Rect)` or pixel index must call `ResultGeometry::CoverRect` or `NearestRect` explicitly. Update `ResultToROIs`, overlay code, QR/OCR crop code, and export formatting without using implicit `Rect2f -> Rect` conversion.

- [ ] **Step 5: Run the full Release regression suite**

Expected: all existing tests pass, including result ROI ordering and run-all publication tests.

- [ ] **Step 6: Commit the result contract migration**

```powershell
git add -- Algorithm/ToolResult.h Core/ResultGeometry.h Core/ToolExecutor.cpp UI/ImageViewer.cpp Core/ResultExporter.cpp Test/regression_tests.cpp
git commit -m "refactor: preserve floating point tool result coordinates"
```

### Task 3: Add the Shared Coordinate Transform

**Files:**
- Create: `Core/CoordinateTransform.h`
- Create: `Core/CoordinateTransform.cpp`
- Modify: `Core/RotatedROI.h`, `Core/RotatedROI.cpp`, `Core/ROI.h`
- Test: `Test/regression_tests.cpp`

- [ ] **Step 1: Write the failing transform and rotated ROI tests**

Test a known transform with point `(10.25, 20.5)`, a `Rect2f`, and a four-corner quad. Then create a rotated ROI, place a fractional result inside the crop, restore it, and assert the result differs from the expected source coordinate by less than `0.05 px`.

- [ ] **Step 2: Run the tests and confirm the transform API is missing**

Expected: compilation fails because `CoordinateTransform` and float result restoration are not defined.

- [ ] **Step 3: Implement `CoordinateTransform`**

Use a `cv::Matx33d` member and these methods:

```cpp
class CoordinateTransform
{
public:
    CoordinateTransform();
    explicit CoordinateTransform(const cv::Matx33d& matrix);
    cv::Point2f MapPoint(const cv::Point2f& point) const;
    cv::Rect2f MapRectBounds(const cv::Rect2f& rect) const;
    std::array<cv::Point2f, 4> MapQuad(const std::array<cv::Point2f, 4>& quad) const;
    CoordinateTransform Then(const CoordinateTransform& next) const;
    bool IsIdentity() const;
private:
    cv::Matx33d matrix_;
};
```

Map rectangles by transforming all four corners and taking a floating-point bounding box. Do not use `cv::boundingRect` in this class.

- [ ] **Step 4: Migrate `RotatedROI::RestoreResult`**

Replace integer `TransformRect` with a float bounds mapping. Transform `Region::contour`, `Detection::box`, `Line::p1/p2`, `TextItem::box`, and `TextItem::quad` without `cvRound`. Keep integer conversion only in `Extract` when allocating the raster crop size.

- [ ] **Step 5: Add explicit ROI float accessors**

Keep `ROI::ToCvRect()` for raster operations. Add `ROI::Bounds2f()` returning `cv::Rect2f` and use it in result ROI conversion. Preserve `ROI::Corners()` as float points.

- [ ] **Step 6: Run transform, ROI, and full regression tests**

Expected: the new round-trip tests pass with `< 0.05 px` transform error and no existing ROI test regresses.

- [ ] **Step 7: Commit the shared transform layer**

```powershell
git add -- Core/CoordinateTransform.h Core/CoordinateTransform.cpp Core/RotatedROI.h Core/RotatedROI.cpp Core/ROI.h Core/ToolExecutor.cpp Test/regression_tests.cpp
git commit -m "feat: preserve float coordinates through ROI transforms"
```

### Task 4: Migrate UI, Result ROI, and Export Consumers

**Files:**
- Modify: `UI/ImageViewer.cpp`
- Modify: `Core/ToolExecutor.cpp`
- Modify: `Core/ResultExporter.cpp`
- Modify: `Test/regression_tests.cpp`

- [ ] **Step 1: Add a failing end-to-end float result ROI test**

Create a source `ToolResult` with two fractional `Rect2f` regions, use `roiSourceMode == 2`, execute a probe tool, and assert the downstream `VisionContext::rois` keeps fractional `start/end` values. Also assert the unified overlay result contains the source and dependent result.

- [ ] **Step 2: Run the test and confirm integer truncation**

Expected: the downstream ROI coordinates are currently integers or the test fails to compile against the old result fields.

- [ ] **Step 3: Update `ResultToROIs` and result publishing**

Build `ROI.start/end` from `Rect2f` directly. For oriented boxes, preserve `RotatedRect.center`, `size`, and `angle` as floats. Do not call `ToCvRect()` unless the tool is about to crop pixels.

- [ ] **Step 4: Update `DrawUnifiedResults`**

Use `float` coordinates directly with `UI::ImageToScreenPos`. Draw text quads as polylines when `hasQuad` is true, otherwise draw `TextItem::box`. Do not format overlay anchors through integer rectangles.

- [ ] **Step 5: Update JSON and Markdown output**

Write coordinates using fixed three-decimal formatting and include:

```json
"coordinateUnit": "pixel",
"precision": "subpixel"
```

Do not change recipe loading semantics because runtime `ToolResult` is not part of the recipe format.

- [ ] **Step 6: Run UI-contract regression tests**

Expected: result ROI selection, overlay label, result export, and recipe round-trip tests all pass.

- [ ] **Step 7: Commit the consumer migration**

```powershell
git add -- UI/ImageViewer.cpp Core/ToolExecutor.cpp Core/ResultExporter.cpp Test/regression_tests.cpp
git commit -m "refactor: draw and chain float tool results"
```

### Task 5: Add Classical Algorithm Refinement

**Files:**
- Modify: `Algorithm/TemplateMatch.cpp`, `Core/ToolExecutor.cpp`
- Modify: `Algorithm/LineDetector.cpp`
- Modify: `Algorithm/BlobTool.cpp`, `Algorithm/ContourDetector.cpp`
- Modify: `Algorithm/ShapeMatcher.cpp`, `Algorithm/ShapeTools.cpp`
- Test: `Test/regression_tests.cpp`

- [ ] **Step 1: Add synthetic fractional-shift fixtures**

Generate a high-contrast reference image, apply `cv::warpAffine` with shifts from `0.0f` to `0.9f` in `0.1f` increments, and retain the known shift as the expected result. Add one test per algorithm family using the existing sample-image test style.

- [ ] **Step 2: Verify the new accuracy tests fail with coarse integer outputs**

Expected: template peaks, line endpoints, contour points, or shape centers remain at integer coordinates and at least one median error exceeds `0.5 px`.

- [ ] **Step 3: Implement template peak refinement**

After the integer response peak is selected, read the response values at left/right/up/down neighbors. Use the vertex of the one-dimensional parabola on each axis, clamp each offset to `[-0.5, 0.5]`, and add it to the match location. Preserve the original integer candidate for bounds and NMS.

- [ ] **Step 4: Implement floating line fitting**

Use the Hough segment as a coarse band, collect nearby Canny edge points, call `cv::fitLine`, and intersect the fitted line with the band endpoints. Store `Point2f`, float length, and angle. Fall back to the coarse segment when fewer than two valid edge points exist.

- [ ] **Step 5: Implement Blob and contour centers**

Use `cv::moments` for float centroid values. Convert contour vertices to `Point2f`; keep `CoverRect` only for raster masks. Preserve area as a float.

- [ ] **Step 6: Implement local shape translation refinement**

For each retained shape candidate, sample a bounded local neighborhood in the existing edge/distance representation, fit a quadratic to the best score in x and y, and update the candidate center and contour points. Keep the current candidate if the neighborhood is invalid.

- [ ] **Step 7: Run accuracy and full regression tests**

Expected: high-contrast classical tests meet median `< 0.5 px`, P95 `< 1.0 px`, and the full suite passes.

- [ ] **Step 8: Commit the classical algorithm batch**

```powershell
git add -- Algorithm/TemplateMatch.cpp Core/ToolExecutor.cpp Algorithm/LineDetector.cpp Algorithm/BlobTool.cpp Algorithm/ContourDetector.cpp Algorithm/ShapeMatcher.cpp Algorithm/ShapeTools.cpp Test/regression_tests.cpp
git commit -m "feat: add subpixel refinement for classical tools"
```

### Task 6: Migrate Color, QR, OCR, and Geometry Outputs

**Files:**
- Modify: `Algorithm/MultiColorFinder.cpp`
- Modify: `Algorithm/QRCodeTool.cpp`
- Modify: `Algorithm/OCRTool.cpp`, `Algorithm/WindowsPPOCREngine.cpp`
- Modify: `Algorithm/GeometryDrawTool.cpp`
- Test: `Test/regression_tests.cpp`

- [ ] **Step 1: Add fallback-aware tests**

Test a stable color-error surface that refines to a fractional anchor and a binary surface that must remain `Pixel`. Test OCR and QR quad transforms with known fractional ROI offsets. Test geometry output preserves the original ImGui float points.

- [ ] **Step 2: Verify the tests fail before implementation**

Expected: color results are integer `cv::Point`, OCR/QR results only expose integer boxes, and geometry results are converted through integer bounding boxes.

- [ ] **Step 3: Implement multi-color local refinement**

Evaluate bilinear color error at the best integer anchor and its four neighbors. Fit a bounded quadratic only when the curvature is positive and finite; otherwise keep the integer anchor and set `result.precision = ResultPrecision::Pixel`.

- [ ] **Step 4: Preserve QR and OCR quads**

Convert detector corners to `Point2f`, map them through `CoordinateTransform`, calculate display `Rect2f` bounds, and set `hasQuad`. Use `cornerSubPix` only for OpenCV QR points when the local grayscale patch is valid; keep ZXing integer corners as `Pixel` precision.

- [ ] **Step 5: Publish geometry directly**

Copy geometry primitive float points into `Point2f` results and calculate float bounds from them. Do not rasterize only to recover result positions.

- [ ] **Step 6: Run the fallback and full regression suite**

Expected: stable color data reports `Subpixel`, binary color data reports `Pixel`, quads survive ROI transforms, and geometry chaining retains fractional coordinates.

- [ ] **Step 7: Commit the batch**

```powershell
git add -- Algorithm/MultiColorFinder.cpp Algorithm/QRCodeTool.cpp Algorithm/OCRTool.cpp Algorithm/WindowsPPOCREngine.cpp Algorithm/GeometryDrawTool.cpp Test/regression_tests.cpp
git commit -m "feat: preserve subpixel color text qr and geometry results"
```

### Task 7: Correct YOLO Float Mapping

**Files:**
- Modify: `Algorithm/YOLOTool.cpp`
- Modify: `Algorithm/OpenCVYoloDetector.cpp`, `Algorithm/OpenCVYoloDetector.h`
- Modify: `Core/LiveYoloRunner.cpp`
- Test: `Test/regression_tests.cpp`

- [ ] **Step 1: Add a simulated letterbox mapping test**

Feed a known float model box through a resize plus asymmetric padding transform, reverse it with the production mapping, and assert error `< 0.05 px`. Repeat with ROI offsets and a rotated ROI transform.

- [ ] **Step 2: Verify current integer mapping fails the fractional test**

Expected: at least one coordinate is rounded or differs by more than `0.05 px`.

- [ ] **Step 3: Replace manual scale and offset arithmetic**

Construct one `CoordinateTransform` for model input to source image and apply it to every box. Keep NMS in floating-point coordinates and clamp only after mapping.

- [ ] **Step 4: Update live YOLO publication**

Use the same mapping helper in `LiveYoloRunner`, then publish `Detection::box` as `Rect2f`. Do not maintain a second live-only mapping formula.

- [ ] **Step 5: Run mapping and full regression tests**

Expected: simulated mapping error `< 0.05 px`, existing YOLO tests pass, and no extra image copy is introduced.

- [ ] **Step 6: Commit the YOLO batch**

```powershell
git add -- Algorithm/YOLOTool.cpp Algorithm/OpenCVYoloDetector.cpp Algorithm/OpenCVYoloDetector.h Core/LiveYoloRunner.cpp Test/regression_tests.cpp
git commit -m "feat: preserve float YOLO coordinates through letterbox mapping"
```

### Task 8: Remove Integer Result Bridges and Finish UI Precision

**Files:**
- Modify: all files still using `ToolResult` integer geometry
- Modify: `UI/ToolsWindow.cpp`, `UI/ImageViewer.cpp`, `Core/ResultExporter.cpp`
- Test: `Test/regression_tests.cpp`

- [ ] **Step 1: Search for remaining implicit integer result conversions**

Run:

```powershell
rg -n "\.bbox|\.contour|\.box|\.p1|\.p2|\.quad|cvRound|boundingRect|ToCvRect" Algorithm Core UI Test
```

For each result field use, classify it as display, ROI transform, raster crop, or algorithm output. Only raster crop may use `ResultGeometry::CoverRect` or `NearestRect`.

- [ ] **Step 2: Add precision labels to the tool result UI**

Display `Pixel`, `Subpixel`, or `ModelFloat` beside the latest result count and format coordinate details with `%.3f`. Do not display decimals for values whose precision state is `Pixel`.

- [ ] **Step 3: Delete compatibility integer fields**

Remove any temporary integer fields and conversion code after every producer and consumer is using the float contract. Keep `ROI::ToCvRect()` only as the explicit raster boundary.

- [ ] **Step 4: Run the complete regression suite and Release build**

```powershell
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' '.\Test\RegressionTests.vcxproj' /t:Build /p:Configuration=Release /p:Platform=x64 /m:1 /v:minimal
& '.\Test\x64\Release\RegressionTests.exe'
& 'D:\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe' '.\Windows_imgui.vcxproj' /t:Build /p:Configuration=Release /p:Platform=x64 /p:OutDir='C:\Users\18101\Documents\IMgui_Opencv\x64\Release_verify\' /p:TargetName=Windows_imgui_verify /m:1 /v:minimal
```

Expected: regression output is `all tests passed`, and the main project links successfully.

- [ ] **Step 5: Run a manual sample-image check**

Use one template, one line, one contour/blob, one QR, one OCR, and one YOLO sample. Confirm that overlay coordinates, result ROI chaining, and exported JSON agree to three decimal places. Record measured median and P95 errors in the run log.

- [ ] **Step 6: Commit the cleanup batch**

```powershell
git add -- Algorithm Core UI Test
git commit -m "refactor: remove integer result coordinate bridges"
```

## Plan Self-Review

- The design spec requirement for a shared floating-point result contract is covered by Tasks 1 and 2.
- ROI, rotation, resize, and letterbox mapping are covered by Tasks 3 and 7.
- UI, result ROI chaining, JSON, and runtime precision labels are covered by Tasks 4 and 8.
- Template, line, Blob, contour, shape, and color refinement are covered by Tasks 5 and 6.
- OCR, QR, YOLO, and geometry output preservation are covered by Tasks 6 and 7.
- Raster-only tools remain unchanged except for explicit raster conversion boundaries.
- Synthetic accuracy tests, fallback behavior, performance constraints, Release build, and regression verification are covered by Tasks 5 through 8.
- No step relies on an unspecified file, a hidden fallback, or an implicit integer conversion.
