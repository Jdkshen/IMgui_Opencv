#define NOMINMAX
#include "PreviewTextureCache.h"

#include "../Core/GraphicsBackend.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <opencv2/imgproc.hpp>

namespace
{
constexpr std::size_t kMaximumPreviewTextures = 48;

struct CacheKey
{
    std::uint64_t toolId = 0;
    PreviewTextureKind kind = PreviewTextureKind::TemplateMatch;

    bool operator==(const CacheKey& other) const
    {
        return toolId == other.toolId && kind == other.kind;
    }
};

struct CacheKeyHash
{
    std::size_t operator()(const CacheKey& key) const
    {
        return static_cast<std::size_t>(key.toolId ^
            (static_cast<std::uint64_t>(key.kind) * 0x9e3779b97f4a7c15ULL));
    }
};

struct CacheEntry
{
    RenderTextureHandle texture = kInvalidRenderTexture;
    std::uint64_t signature = 0;
    std::uint64_t pendingSignature = 0;
    std::uint64_t lastUseSerial = 0;
    cv::Mat pendingRgba;
    int width = 0;
    int height = 0;
    bool pending = false;
    bool removeRequested = false;
};

std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> g_entries;
std::uint64_t g_useSerial = 0;

CacheKey MakeKey(std::uint64_t toolId, PreviewTextureKind kind)
{
    return {toolId, kind};
}

cv::Mat MakeRgbaThumbnail(const cv::Mat& source, int maxDimension)
{
    if (source.empty())
        return {};

    cv::Mat image = source;
    cv::Mat convertedDepth;
    if (image.depth() != CV_8U)
    {
        image.convertTo(convertedDepth, CV_8U);
        image = convertedDepth;
    }

    maxDimension = std::max(2, maxDimension);
    const float scale = std::min(1.0f,
        static_cast<float>(maxDimension) /
            static_cast<float>(std::max(image.cols, image.rows)));
    const int width = std::max(2, static_cast<int>(image.cols * scale));
    const int height = std::max(2, static_cast<int>(image.rows * scale));

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(width, height), 0.0, 0.0,
        cv::INTER_NEAREST);

    cv::Mat rgba;
    if (resized.channels() == 1)
        cv::cvtColor(resized, rgba, cv::COLOR_GRAY2RGBA);
    else if (resized.channels() == 3)
        cv::cvtColor(resized, rgba, cv::COLOR_BGR2RGBA);
    else if (resized.channels() == 4)
        cv::cvtColor(resized, rgba, cv::COLOR_BGRA2RGBA);
    return rgba;
}

void RequestOldestEviction()
{
    auto oldest = g_entries.end();
    std::uint64_t oldestSerial = (std::numeric_limits<std::uint64_t>::max)();
    for (auto it = g_entries.begin(); it != g_entries.end(); ++it)
    {
        if (!it->second.removeRequested && it->second.lastUseSerial < oldestSerial)
        {
            oldest = it;
            oldestSerial = it->second.lastUseSerial;
        }
    }
    if (oldest != g_entries.end())
        oldest->second.removeRequested = true;
}
}

namespace PreviewTextureCache
{
std::uint64_t CombineSignature(std::uint64_t seed, std::uint64_t value)
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

std::uint64_t ImageSignature(const cv::Mat& image)
{
    std::uint64_t signature = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(image.data));
    signature = CombineSignature(signature, static_cast<std::uint64_t>(image.rows));
    signature = CombineSignature(signature, static_cast<std::uint64_t>(image.cols));
    signature = CombineSignature(signature, static_cast<std::uint64_t>(image.type()));
    signature = CombineSignature(signature, static_cast<std::uint64_t>(image.step));
    return signature;
}

bool NeedsUpdate(std::uint64_t toolId, PreviewTextureKind kind,
    std::uint64_t signature)
{
    const auto it = g_entries.find(MakeKey(toolId, kind));
    if (it == g_entries.end())
        return true;
    const CacheEntry& entry = it->second;
    return entry.removeRequested ||
        (entry.pending ? entry.pendingSignature != signature :
            entry.signature != signature);
}

void Queue(std::uint64_t toolId, PreviewTextureKind kind,
    std::uint64_t signature, const cv::Mat& image, int maxDimension)
{
    if (toolId == 0 || image.empty())
        return;

    const CacheKey key = MakeKey(toolId, kind);
    auto it = g_entries.find(key);
    if (it == g_entries.end())
    {
        if (g_entries.size() >= kMaximumPreviewTextures)
            RequestOldestEviction();
        it = g_entries.emplace(key, CacheEntry{}).first;
    }

    CacheEntry& entry = it->second;
    entry.pendingRgba = MakeRgbaThumbnail(image, maxDimension);
    if (entry.pendingRgba.empty())
        return;
    entry.pendingSignature = signature;
    entry.pending = true;
    entry.removeRequested = false;
    entry.lastUseSerial = ++g_useSerial;
}

PreviewTextureView Get(std::uint64_t toolId, PreviewTextureKind kind)
{
    const auto it = g_entries.find(MakeKey(toolId, kind));
    if (it == g_entries.end())
        return {};

    CacheEntry& entry = it->second;
    entry.lastUseSerial = ++g_useSerial;
    PreviewTextureView view;
    view.textureId = GraphicsBackend::TextureId(entry.texture);
    view.width = entry.pending ? entry.pendingRgba.cols : entry.width;
    view.height = entry.pending ? entry.pendingRgba.rows : entry.height;
    view.ready = GraphicsBackend::TextureReady(entry.texture);
    return view;
}

void ForgetTool(std::uint64_t toolId)
{
    for (auto& pair : g_entries)
    {
        if (pair.first.toolId == toolId)
            pair.second.removeRequested = true;
    }
}

void Prune(const std::vector<std::uint64_t>& activeToolIds)
{
    const std::unordered_set<std::uint64_t> active(
        activeToolIds.begin(), activeToolIds.end());
    for (auto& pair : g_entries)
    {
        if (pair.first.kind != PreviewTextureKind::RunResult &&
            active.find(pair.first.toolId) == active.end())
            pair.second.removeRequested = true;
    }
}

void UploadPending()
{
    for (auto it = g_entries.begin(); it != g_entries.end();)
    {
        if (!it->second.removeRequested)
        {
            ++it;
            continue;
        }
        GraphicsBackend::ReleaseTexture(it->second.texture);
        it = g_entries.erase(it);
    }

    for (auto& pair : g_entries)
    {
        CacheEntry& entry = pair.second;
        if (!entry.pending || entry.pendingRgba.empty())
            continue;
        if (entry.texture == kInvalidRenderTexture)
            entry.texture = GraphicsBackend::CreateTexture();
        if (!GraphicsBackend::UploadTexture(entry.texture, entry.pendingRgba))
            continue;
        entry.width = entry.pendingRgba.cols;
        entry.height = entry.pendingRgba.rows;
        entry.signature = entry.pendingSignature;
        entry.pendingRgba.release();
        entry.pending = false;
    }
}

void Shutdown()
{
    for (auto& pair : g_entries)
        GraphicsBackend::ReleaseTexture(pair.second.texture);
    g_entries.clear();
    g_useSerial = 0;
}
}
