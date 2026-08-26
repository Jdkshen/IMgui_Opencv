#include "RegressionPolicyTests.h"

#include "../Core/CameraPixelFormat.h"
#include "../Core/RenderBackend.h"
#include "../UI/RunResultLayout.h"

#include <cmath>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

void TestRenderBackendSelectionPolicy()
{
    Require(SelectRenderBackend(true, true) == RenderBackendKind::DirectX12,
        "renderer policy did not prefer DX12 when both backends are available");
    Require(SelectRenderBackend(false, true) == RenderBackendKind::DirectX11,
        "renderer policy did not fall back to DX11 after DX12 failure");
    Require(SelectRenderBackend(false, false) == RenderBackendKind::None,
        "renderer policy selected a backend when none were available");
}

void TestRunResultLayoutPolicy()
{
    namespace Layout = UI::RunResultLayout;

    const Layout::Size fullHd =
        Layout::CalculateDashboardWindowSize(1920.0f, 1080.0f);
    Require(std::abs(fullHd.width - 1760.0f) < 0.01f &&
        std::abs(fullHd.height - 993.6f) < 0.01f,
        "run-result dashboard window sizing regressed");

    const Layout::DashboardGrid threeCards =
        Layout::CalculateDashboardGrid(3, 1200.0f, 700.0f, 8.0f);
    Require(threeCards.columns == 3 && threeCards.rowCount == 1 &&
        threeCards.visibleRowCount == 1 &&
        std::abs(threeCards.cardHeight - 520.0f) < 0.01f,
        "run-result single-row grid layout regressed");

    const Layout::DashboardGrid wrappedCards =
        Layout::CalculateDashboardGrid(8, 900.0f, 700.0f, 8.0f);
    Require(wrappedCards.columns == 3 && wrappedCards.rowCount == 3 &&
        wrappedCards.visibleRowCount == 2 &&
        std::abs(wrappedCards.cardHeight - 342.0f) < 0.01f,
        "run-result wrapped grid layout regressed");
    Require(Layout::CalculateDashboardGrid(0, 900.0f, 700.0f, 8.0f).columns == 0,
        "empty run-result grid should not create columns");
    Require(Layout::FormatDuration(0.0f) == "<0.1 ms" &&
        Layout::FormatDuration(11.66f) == "11.7 ms" &&
        Layout::FormatDuration(1239.2f) == "1.239 s" &&
        Layout::FormatDuration(-1.0f) == "--" &&
        Layout::FormatDuration(0.0f, 2) == "<0.01 ms",
        "run-result duration formatting regressed");

    const Layout::Rect imageBounds{0.0f, 0.0f, 200.0f, 100.0f};
    const Layout::LabelPlacement first = Layout::PlaceOverlayLabel(
        {20.0f, 20.0f}, {50.0f, 10.0f}, imageBounds, {});
    Require(first.placed && first.bounds.left >= imageBounds.left &&
        first.bounds.top >= imageBounds.top &&
        first.bounds.right <= imageBounds.right &&
        first.bounds.bottom <= imageBounds.bottom,
        "run-result overlay label was not clamped to the image");

    const Layout::LabelPlacement second = Layout::PlaceOverlayLabel(
        {20.0f, 20.0f}, {50.0f, 10.0f}, imageBounds, {first.bounds});
    Require(second.placed && !Layout::RectsOverlap(first.bounds, second.bounds),
        "run-result overlay labels were not separated");
    Require(!Layout::RectsOverlap(
        {0.0f, 0.0f, 10.0f, 10.0f}, {10.0f, 0.0f, 20.0f, 10.0f}),
        "touching run-result label edges should not overlap");

    const Layout::LabelPlacement saturated = Layout::PlaceOverlayLabel(
        {20.0f, 20.0f}, {50.0f, 10.0f}, imageBounds, {imageBounds});
    Require(!saturated.placed,
        "run-result overlay label should be skipped when no slot is free");
}

void TestIndustrialCameraPixelFormatMatrix()
{
    const CameraPixelFormatDescription mono10 =
        DescribeCameraPixelFormat(0x01100003U);
    const CameraPixelFormatDescription mono12Packed =
        DescribeCameraPixelFormat(0x010C0006U);
    const CameraPixelFormatDescription mono16 =
        DescribeCameraPixelFormat(0x01100007U);
    const CameraPixelFormatDescription bayer12 =
        DescribeCameraPixelFormat(0x01100011U);
    Require(mono10.name == "Mono10" && mono10.bitDepth == 10 &&
        mono10.storageBitsPerPixel == 16 && mono10.monochrome,
        "Mono10 PFNC description regressed");
    Require(mono12Packed.name == "Mono12Packed" && mono12Packed.bitDepth == 12 &&
        mono12Packed.storageBitsPerPixel == 12,
        "Mono12Packed PFNC description regressed");
    Require(mono16.name == "Mono16" && mono16.bitDepth == 16,
        "Mono16 PFNC description regressed");
    Require(bayer12.name == "BayerRG12" && bayer12.bayer &&
        bayer12.bitDepth == 12,
        "Bayer12 PFNC description regressed");
}
