#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
std::wstring ModuleDirectory()
{
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return L".";
    std::wstring path(buffer.data(), length);
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

bool Exists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring QuoteArgument(const std::wstring& value)
{
    std::wstring quoted = L"\"";
    unsigned backslashes = 0;
    for (const wchar_t character : value)
    {
        if (character == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (character == L'\"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

void WriteDiagnosticLog(const std::wstring& directory, const std::wstring& message)
{
    const std::wstring path = directory + L"\\startup_diagnostics.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t prefix[96]{};
    wsprintfW(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u] ", now.wYear,
        now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const std::wstring line = std::wstring(prefix) + message + L"\r\n";
    int bytesRequired = WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
        static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>((std::max)(0, bytesRequired)), '\0');
    if (bytesRequired > 0)
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
            utf8.data(), bytesRequired, nullptr, nullptr);
    DWORD written = 0;
    if (!utf8.empty())
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

int Fail(const std::wstring& directory, const std::wstring& detail)
{
    WriteDiagnosticLog(directory, detail);
    const std::wstring message = L"程序启动前检查失败。\r\n\r\n" + detail +
        L"\r\n\r\n请确认整个发布目录已完整解压，不要只复制 EXE。"
        L"\r\n详细日志：startup_diagnostics.log";
    MessageBoxW(nullptr, message.c_str(), L"工业视觉系统 - 启动诊断",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return 1;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const std::wstring directory = ModuleDirectory();
    const std::wstring corePath = directory + L"\\Windows_imgui_core.exe";
    const wchar_t* requiredFiles[] = {
        L"Windows_imgui_core.exe", L"opencv_world500.dll", L"ncnn.dll",
        L"DirectML.dll", L"onnxruntime.dll", L"onnxruntime_providers_shared.dll",
        L"msvcp140.dll", L"msvcp140_1.dll", L"msvcp140_atomic_wait.dll",
        L"vcruntime140.dll", L"vcruntime140_1.dll", L"concrt140.dll"
    };
    std::wstring missing;
    for (const wchar_t* file : requiredFiles)
    {
        if (!Exists(directory + L"\\" + file))
            missing += L"  - " + std::wstring(file) + L"\r\n";
    }
    if (!missing.empty())
        return Fail(directory, L"缺少以下必需文件：\r\n" + missing);

    DWORD binaryType = 0;
    if (!GetBinaryTypeW(corePath.c_str(), &binaryType) || binaryType != SCS_64BIT_BINARY)
        return Fail(directory, L"主程序不是有效的 x64 Windows 可执行文件。");

    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    std::wstring commandLine = QuoteArgument(corePath);
    bool diagnosticsOnly = false;
    for (int index = 1; arguments && index < argumentCount; ++index)
    {
        if (_wcsicmp(arguments[index], L"--diagnostic-only") == 0)
        {
            diagnosticsOnly = true;
            continue;
        }
        commandLine += L" " + QuoteArgument(arguments[index]);
    }
    if (arguments)
        LocalFree(arguments);

    if (diagnosticsOnly)
    {
        WriteDiagnosticLog(directory,
            L"启动前检查通过（diagnostic-only）。");
        return 0;
    }

    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
        LOAD_LIBRARY_SEARCH_USER_DIRS);
    AddDllDirectory(directory.c_str());

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> writable(commandLine.begin(), commandLine.end());
    writable.push_back(L'\0');
    if (!CreateProcessW(corePath.c_str(), writable.data(), nullptr, nullptr, FALSE,
        0, nullptr, directory.c_str(), &startup, &process))
    {
        const DWORD error = GetLastError();
        return Fail(directory, L"主程序无法启动，Windows 错误码：" +
            std::to_wstring(error) +
            L"\r\n常见原因：DLL 版本/位数不匹配、权限或杀毒软件拦截。");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    WriteDiagnosticLog(directory, L"启动前检查通过，已启动 Windows_imgui_core.exe。");
    return 0;
}
