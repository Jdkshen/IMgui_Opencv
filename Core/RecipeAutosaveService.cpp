#include "RecipeAutosaveService.h"

#include "RecipeManager.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace
{
namespace fs = std::filesystem;

struct SaveJob
{
    std::string targetPath;
    RecipeData data;
    std::uint32_t dirtyFlags = 0;
};

std::mutex s_mutex;
std::condition_variable s_condition;
std::condition_variable s_idleCondition;
std::thread s_worker;
std::deque<SaveJob> s_queue;
RecipeAutosaveSnapshot s_snapshot;
std::uint32_t s_dirtyFlags = 0;
std::chrono::steady_clock::time_point s_saveDeadline{};
bool s_initialized = false;
bool s_stop = false;
bool s_workerBusy = false;

std::uint32_t Flags(RecipeDirtyKind kind)
{
    return static_cast<std::uint32_t>(kind);
}

fs::path Utf8Path(const std::string& value)
{
    const std::u8string utf8(reinterpret_cast<const char8_t*>(value.data()), value.size());
    return fs::path(utf8);
}

std::string PathToUtf8(const fs::path& value)
{
    const std::u8string utf8 = value.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::string TimestampNow()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream text;
    text << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return text.str();
}

bool SaveAtomic(const SaveJob& job, std::string& error)
{
    if (job.targetPath.empty())
    {
        error = "recipe target path is empty";
        return false;
    }

    try
    {
        const fs::path target = Utf8Path(job.targetPath);
        const fs::path temporary = target.wstring() + L".tmp";
        const fs::path backup = target.wstring() + L".bak";
        if (!target.parent_path().empty())
            fs::create_directories(target.parent_path());
        std::error_code ec;
        fs::remove(temporary, ec);

        RecipeManager::SaveOptions options;
        options.writeAssets = (job.dirtyFlags & Flags(RecipeDirtyKind::Assets)) != 0;
        const std::string temporaryUtf8 = PathToUtf8(temporary);
        if (!RecipeManager::Save(temporaryUtf8.c_str(), job.data, options))
        {
            error = "RecipeManager failed to write temporary recipe";
            fs::remove(temporary, ec);
            return false;
        }

        const bool hadTarget = fs::exists(target, ec);
        if (hadTarget)
        {
            fs::remove(backup, ec);
            ec.clear();
            fs::rename(target, backup, ec);
            if (ec)
            {
                error = "cannot rotate recipe backup: " + ec.message();
                fs::remove(temporary, ec);
                return false;
            }
        }

        ec.clear();
        fs::rename(temporary, target, ec);
        if (ec)
        {
            error = "cannot replace recipe atomically: " + ec.message();
            if (hadTarget)
            {
                std::error_code restoreError;
                fs::rename(backup, target, restoreError);
            }
            fs::remove(temporary, ec);
            return false;
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

void WorkerLoop()
{
    for (;;)
    {
        SaveJob job;
        {
            std::unique_lock<std::mutex> lock(s_mutex);
            s_condition.wait(lock, [] { return s_stop || !s_queue.empty(); });
            if (s_stop && s_queue.empty())
                break;
            job = std::move(s_queue.front());
            s_queue.pop_front();
            s_workerBusy = true;
            s_snapshot.saving = true;
            s_snapshot.pending = !s_queue.empty();
        }

        std::string error;
        const bool saved = SaveAtomic(job, error);

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (saved)
            {
                ++s_snapshot.completedSaveCount;
                s_snapshot.lastSavedAt = TimestampNow();
                s_snapshot.lastError.clear();
            }
            else
            {
                ++s_snapshot.failedSaveCount;
                s_snapshot.lastError = std::move(error);
                s_dirtyFlags |= job.dirtyFlags;
                s_snapshot.dirty = true;
                s_snapshot.assetDirty =
                    (s_dirtyFlags & Flags(RecipeDirtyKind::Assets)) != 0;
            }
            s_workerBusy = false;
            s_snapshot.saving = false;
            s_snapshot.pending = !s_queue.empty();
            s_idleCondition.notify_all();
        }
    }
}

void EnsureInitializedLocked()
{
    if (s_initialized)
        return;
    s_stop = false;
    s_worker = std::thread(WorkerLoop);
    s_initialized = true;
}
}

namespace RecipeAutosaveService
{
void ConfigureTarget(const std::string& targetPath)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();
    if (s_snapshot.targetPath != targetPath)
    {
        s_snapshot.targetPath = targetPath;
        s_snapshot.backupPath = targetPath.empty() ? std::string() : targetPath + ".bak";
    }
}

void MarkDirty(RecipeDirtyKind kind)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();
    s_dirtyFlags |= Flags(kind);
    s_snapshot.dirty = s_dirtyFlags != 0;
    s_snapshot.assetDirty =
        (s_dirtyFlags & Flags(RecipeDirtyKind::Assets)) != 0;
    s_saveDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
}

bool ShouldCapture(bool editingActive)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_dirtyFlags == 0 || s_snapshot.targetPath.empty())
        return false;
    return !editingActive || std::chrono::steady_clock::now() >= s_saveDeadline;
}

void Submit(RecipeData data)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    EnsureInitializedLocked();
    if (s_snapshot.targetPath.empty() || s_dirtyFlags == 0)
        return;

    SaveJob job;
    job.targetPath = s_snapshot.targetPath;
    job.data = std::move(data);
    job.dirtyFlags = s_dirtyFlags;
    s_dirtyFlags = 0;
    s_snapshot.dirty = false;
    s_snapshot.assetDirty = false;

    if (!s_queue.empty())
    {
        job.dirtyFlags |= s_queue.back().dirtyFlags;
        s_queue.back() = std::move(job);
    }
    else
    {
        s_queue.push_back(std::move(job));
    }
    s_snapshot.pending = true;
    s_condition.notify_one();
}

bool SaveNow(RecipeData data)
{
    WaitUntilIdle(10000);
    SaveJob job;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        EnsureInitializedLocked();
        job.targetPath = s_snapshot.targetPath;
        job.data = std::move(data);
        job.dirtyFlags = s_dirtyFlags == 0 ? Flags(RecipeDirtyKind::All) : s_dirtyFlags;
        s_dirtyFlags = 0;
        s_snapshot.dirty = false;
        s_snapshot.assetDirty = false;
        s_workerBusy = true;
        s_snapshot.saving = true;
    }

    std::string error;
    const bool saved = SaveAtomic(job, error);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (saved)
        {
            ++s_snapshot.completedSaveCount;
            s_snapshot.lastSavedAt = TimestampNow();
            s_snapshot.lastError.clear();
        }
        else
        {
            ++s_snapshot.failedSaveCount;
            s_snapshot.lastError = std::move(error);
            s_dirtyFlags |= job.dirtyFlags;
            s_snapshot.dirty = true;
            s_snapshot.assetDirty =
                (s_dirtyFlags & Flags(RecipeDirtyKind::Assets)) != 0;
        }
        s_workerBusy = false;
        s_snapshot.saving = false;
        s_idleCondition.notify_all();
    }
    return saved;
}

RecipeAutosaveSnapshot Snapshot()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    RecipeAutosaveSnapshot snapshot = s_snapshot;
    snapshot.dirty = s_dirtyFlags != 0;
    snapshot.assetDirty =
        (s_dirtyFlags & Flags(RecipeDirtyKind::Assets)) != 0;
    snapshot.pending = snapshot.pending || !s_queue.empty();
    return snapshot;
}

bool RestoreBackup(std::string* error)
{
    if (!WaitUntilIdle(10000))
    {
        if (error)
            *error = "autosave worker did not become idle";
        return false;
    }
    try
    {
        const RecipeAutosaveSnapshot snapshot = Snapshot();
        const fs::path target = Utf8Path(snapshot.targetPath);
        const fs::path backup = Utf8Path(snapshot.backupPath);
        if (snapshot.targetPath.empty() || !fs::exists(backup))
        {
            if (error)
                *error = "recipe backup does not exist";
            return false;
        }
        const fs::path temporary = target.wstring() + L".restore.tmp";
        std::error_code ec;
        fs::copy_file(backup, temporary, fs::copy_options::overwrite_existing, ec);
        if (!ec)
        {
            fs::remove(target, ec);
            ec.clear();
            fs::rename(temporary, target, ec);
        }
        if (ec)
        {
            if (error)
                *error = ec.message();
            fs::remove(temporary, ec);
            return false;
        }
        if (error)
            error->clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
}

bool WaitUntilIdle(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(s_mutex);
    return s_idleCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs), []
    {
        return !s_workerBusy && s_queue.empty();
    });
}

void Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_initialized)
            return;
        s_stop = true;
        s_condition.notify_all();
    }
    if (s_worker.joinable())
        s_worker.join();
    std::lock_guard<std::mutex> lock(s_mutex);
    s_queue.clear();
    s_initialized = false;
    s_stop = false;
    s_workerBusy = false;
    s_dirtyFlags = 0;
    s_snapshot = RecipeAutosaveSnapshot{};
}
}
