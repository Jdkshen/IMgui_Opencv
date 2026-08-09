#include "LogSystem.h"

#include <chrono>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <windows.h>

namespace
{
namespace fs = std::filesystem;

thread_local LogContext s_threadContext;
LogStorageOptions s_storageOptions;
bool s_storageConfigured = false;

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>((std::max)(0, size)), '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string PathToUtf8(const fs::path& value)
{
    return WideToUtf8(value.wstring());
}

fs::path Utf8Path(const std::string& value)
{
    const std::u8string utf8(reinterpret_cast<const char8_t*>(value.data()),
        value.size());
    return fs::path(utf8);
}

fs::path DefaultLogDirectory()
{
    wchar_t buffer[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer,
        static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer))
        return fs::path(buffer) / L"IMgui_Opencv" / L"logs";
    return fs::current_path() / L"logs";
}

const LogStorageOptions& StorageOptions()
{
    if (!s_storageConfigured)
    {
        s_storageOptions.directory = WideToUtf8(DefaultLogDirectory().wstring());
        s_storageConfigured = true;
    }
    return s_storageOptions;
}

std::string DateText(const std::tm& localTm)
{
    char value[16]{};
    snprintf(value, sizeof(value), "%04d-%02d-%02d", localTm.tm_year + 1900,
        localTm.tm_mon + 1, localTm.tm_mday);
    return value;
}

std::string ContextText(const LogContext& context)
{
    std::string text;
    const auto append = [&text](const char* key, const std::string& value)
    {
        if (value.empty())
            return;
        if (!text.empty())
            text += ' ';
        text += key;
        text += '=';
        text += value;
    };
    append("task", context.taskId);
    append("batch", context.batchNumber);
    append("image", context.imageName);
    append("plc", context.plcRequestId);
    return text;
}

void RemoveExpiredLogs(const fs::path& root, int retentionDays)
{
    if (retentionDays <= 0)
        return;
    std::error_code error;
    const auto cutoff = fs::file_time_type::clock::now() -
        std::chrono::hours(24 * retentionDays);
    for (fs::recursive_directory_iterator it(root,
        fs::directory_options::skip_permission_denied, error), end;
        it != end; it.increment(error))
    {
        if (error)
        {
            error.clear();
            continue;
        }
        if (it->is_regular_file(error) && it->path().extension() == L".log" &&
            it->last_write_time(error) < cutoff)
            fs::remove(it->path(), error);
    }
}

void PersistLine(const std::string& date, const std::string& line)
{
    const LogStorageOptions& options = StorageOptions();
    const fs::path root = Utf8Path(options.directory);
    const fs::path dayDirectory = root / fs::path(date);
    std::error_code error;
    fs::create_directories(dayDirectory, error);
    if (error)
        return;

    const std::uint64_t maximumBytes = (std::max<std::uint64_t>)(
        64u * 1024u, options.maximumFileBytes);
    fs::path target;
    for (unsigned index = 1; index < 10000; ++index)
    {
        target = dayDirectory / ("app-" + std::to_string(index) + ".log");
        const std::uint64_t size = fs::exists(target, error)
            ? fs::file_size(target, error) : 0;
        if (!error && size + line.size() + 1 <= maximumBytes)
            break;
        error.clear();
    }

    std::ofstream output(target, std::ios::binary | std::ios::app);
    if (output)
    {
        output.write(line.data(), static_cast<std::streamsize>(line.size()));
        output.put('\n');
        output.flush();
    }

    static std::string lastCleanupDate;
    if (lastCleanupDate != date)
    {
        lastCleanupDate = date;
        RemoveExpiredLogs(root, options.retentionDays);
    }
}

std::uint32_t Crc32(const std::vector<unsigned char>& data)
{
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char byte : data)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void Write16(std::ostream& output, std::uint16_t value)
{
    output.put(static_cast<char>(value));
    output.put(static_cast<char>(value >> 8));
}

void Write32(std::ostream& output, std::uint32_t value)
{
    Write16(output, static_cast<std::uint16_t>(value));
    Write16(output, static_cast<std::uint16_t>(value >> 16));
}

struct ZipEntry
{
    std::string name;
    std::vector<unsigned char> data;
    std::uint32_t crc = 0;
    std::uint32_t offset = 0;
};

bool ReadFile(const fs::path& path, std::vector<unsigned char>& data)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size < 0 || size > 100 * 1024 * 1024)
        return false;
    data.resize(static_cast<std::size_t>(size));
    if (size > 0)
        input.read(reinterpret_cast<char*>(data.data()), size);
    return input.good() || input.eof();
}

bool WriteZip(const fs::path& target, std::vector<ZipEntry>& entries)
{
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    for (ZipEntry& entry : entries)
    {
        entry.crc = Crc32(entry.data);
        entry.offset = static_cast<std::uint32_t>(output.tellp());
        Write32(output, 0x04034b50u);
        Write16(output, 20); Write16(output, 0); Write16(output, 0);
        Write16(output, 0); Write16(output, 0);
        Write32(output, entry.crc);
        Write32(output, static_cast<std::uint32_t>(entry.data.size()));
        Write32(output, static_cast<std::uint32_t>(entry.data.size()));
        Write16(output, static_cast<std::uint16_t>(entry.name.size()));
        Write16(output, 0);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        output.write(reinterpret_cast<const char*>(entry.data.data()),
            static_cast<std::streamsize>(entry.data.size()));
    }
    const std::uint32_t centralOffset = static_cast<std::uint32_t>(output.tellp());
    for (const ZipEntry& entry : entries)
    {
        Write32(output, 0x02014b50u);
        Write16(output, 20); Write16(output, 20); Write16(output, 0); Write16(output, 0);
        Write16(output, 0); Write16(output, 0); Write32(output, entry.crc);
        Write32(output, static_cast<std::uint32_t>(entry.data.size()));
        Write32(output, static_cast<std::uint32_t>(entry.data.size()));
        Write16(output, static_cast<std::uint16_t>(entry.name.size()));
        Write16(output, 0); Write16(output, 0); Write16(output, 0); Write16(output, 0);
        Write32(output, 0); Write32(output, entry.offset);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(output.tellp()) - centralOffset;
    Write32(output, 0x06054b50u);
    Write16(output, 0); Write16(output, 0);
    Write16(output, static_cast<std::uint16_t>(entries.size()));
    Write16(output, static_cast<std::uint16_t>(entries.size()));
    Write32(output, centralSize); Write32(output, centralOffset); Write16(output, 0);
    return output.good();
}
}

void LogSystem::AddFormatted(LogLevel level, const ImVec4* customColor, const char* fmt, va_list args)
{
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, args);

    auto now = std::chrono::system_clock::now();
    auto timeNow = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm localTm{};
    localtime_s(&localTm, &timeNow);

    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "[%02d:%02d:%02d.%03lld]",
        localTm.tm_hour, localTm.tm_min, localTm.tm_sec,
        static_cast<long long>(ms.count()));

    const char* levelStr = "INFO";
    if (level == LOG_WARN)
        levelStr = "WARN";
    if (level == LOG_ERROR)
        levelStr = "ERROR";

    const std::string context = ContextText(s_threadContext);
    LogEntry entry;
    entry.displayText = std::string(timeStr) + " [" + levelStr + "]" +
        (context.empty() ? "" : " [" + context + "]") + " " + buf;
    entry.level = level;
    entry.threadId = static_cast<int>(std::hash<std::thread::id>{}(
        std::this_thread::get_id()) & 0x7fffffff);
    entry.timestamp = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count()) / 1000.0;
    if (customColor)
    {
        entry.color = *customColor;
        entry.useCustomColor = true;
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_logs.size() >= 2000)
            s_logs.erase(s_logs.begin(), s_logs.begin() + (s_logs.size() - 1999));
        s_logs.push_back(std::move(entry));
        s_snapshot.reset();
        PersistLine(DateText(localTm), s_logs.back().displayText);
    }
}

void LogSystem::ConfigureStorage(const LogStorageOptions& options)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_storageOptions = options;
    if (s_storageOptions.directory.empty())
        s_storageOptions.directory = WideToUtf8(DefaultLogDirectory().wstring());
    s_storageConfigured = true;
}

std::string LogSystem::GetLogDirectory()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return StorageOptions().directory;
}

void LogSystem::SetContext(const LogContext& context)
{
    s_threadContext = context;
}

LogContext LogSystem::GetContext()
{
    return s_threadContext;
}

void LogSystem::ClearContext()
{
    s_threadContext = {};
}

bool LogSystem::ExportDiagnosticPackage(const std::string& targetPath,
    std::string* error)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    try
    {
        std::vector<ZipEntry> entries;
        const fs::path logRoot = Utf8Path(StorageOptions().directory);
        std::error_code iteratorError;
        if (fs::exists(logRoot))
        {
            for (fs::recursive_directory_iterator it(logRoot,
                fs::directory_options::skip_permission_denied, iteratorError), end;
                it != end; it.increment(iteratorError))
            {
                if (iteratorError)
                {
                    iteratorError.clear();
                    continue;
                }
                if (!it->is_regular_file() || it->path().extension() != L".log")
                    continue;
                ZipEntry entry;
                entry.name = "logs/" + PathToUtf8(
                    it->path().lexically_relative(logRoot).generic_wstring());
                std::replace(entry.name.begin(), entry.name.end(), '\\', '/');
                if (ReadFile(it->path(), entry.data))
                    entries.push_back(std::move(entry));
            }
        }

        const fs::path localApplicationDirectory = logRoot.parent_path();
        const std::vector<fs::path> configCandidates = {
            localApplicationDirectory / L"hardware_settings.json",
            localApplicationDirectory / L"hardware_settings.json.bak",
            localApplicationDirectory / L"ui_preferences.cfg",
            fs::current_path() / L"run_settings.json",
            fs::current_path() / L"theme.cfg"
        };
        for (const fs::path& config : configCandidates)
        {
            ZipEntry entry;
            entry.name = "config/" + PathToUtf8(config.filename());
            if (ReadFile(config, entry.data))
                entries.push_back(std::move(entry));
        }

        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);
        OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
#pragma warning(push)
#pragma warning(disable:4996)
        GetVersionExW(&version);
#pragma warning(pop)
        std::ostringstream manifest;
        manifest << "diagnostic_format=1\r\n"
            << "log_directory=" << StorageOptions().directory << "\r\n"
            << "os_version=" << version.dwMajorVersion << '.' << version.dwMinorVersion
            << '.' << version.dwBuildNumber << "\r\n"
            << "processor_architecture=" << systemInfo.wProcessorArchitecture << "\r\n"
            << "logical_processors=" << systemInfo.dwNumberOfProcessors << "\r\n";
        ZipEntry manifestEntry;
        manifestEntry.name = "diagnostic_info.txt";
        const std::string manifestText = manifest.str();
        manifestEntry.data.assign(manifestText.begin(), manifestText.end());
        entries.push_back(std::move(manifestEntry));

        fs::path target = Utf8Path(targetPath);
        if (target.extension() != L".zip")
            target += L".zip";
        if (!target.parent_path().empty())
            fs::create_directories(target.parent_path());
        if (!WriteZip(target, entries))
            throw std::runtime_error("无法写入诊断包");
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
}

void LogSystem::Add(LogLevel level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    AddFormatted(level, nullptr, fmt, args);
    va_end(args);
}

void LogSystem::Add(LogLevel level, const ImVec4& color, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    AddFormatted(level, &color, fmt, args);
    va_end(args);
}

void LogSystem::Clear()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_logs.clear();
    s_snapshot.reset();
}

std::shared_ptr<std::vector<LogEntry>> LogSystem::GetLogs()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_snapshot)
        s_snapshot = std::make_shared<std::vector<LogEntry>>(s_logs);
    return s_snapshot;
}
