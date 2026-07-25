#pragma once

#include <cstdint>
#include <string>

struct RecipeData;

enum class RecipeDirtyKind : std::uint32_t
{
    None = 0,
    Parameters = 1u << 0,
    Assets = 1u << 1,
    All = Parameters | Assets
};

struct RecipeAutosaveSnapshot
{
    bool dirty = false;
    bool assetDirty = false;
    bool saving = false;
    bool pending = false;
    std::uint64_t completedSaveCount = 0;
    std::uint64_t failedSaveCount = 0;
    std::string targetPath;
    std::string backupPath;
    std::string lastSavedAt;
    std::string lastError;
};

namespace RecipeAutosaveService
{
    void ConfigureTarget(const std::string& targetPath);
    void MarkDirty(RecipeDirtyKind kind = RecipeDirtyKind::Parameters);
    bool ShouldCapture(bool editingActive);
    void Submit(RecipeData data);
    bool SaveNow(RecipeData data);
    RecipeAutosaveSnapshot Snapshot();
    bool RestoreBackup(std::string* error = nullptr);
    bool WaitUntilIdle(int timeoutMs);
    void Shutdown();
}
