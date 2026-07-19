#include "OpenFileDialog.h"

#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h> // IFileDialog, IShellItem

#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>

namespace
{
std::string WideToUtf8(const wchar_t* text)
{
    if (!text || !text[0])
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 1)
        return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), length);
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

bool IsImageExtension(const std::wstring& fileName)
{
    const size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return false;
    std::wstring extension = fileName.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".jpg" || extension == L".jpeg" ||
        extension == L".png" || extension == L".bmp" ||
        extension == L".tif" || extension == L".tiff" ||
        extension == L".webp";
}

void ScanDirectory(const std::wstring& folder, bool recursive, std::vector<std::string>& files)
{
    WIN32_FIND_DATAW data{};
    const std::wstring searchPath = folder + L"\\*";
    HANDLE findHandle = FindFirstFileW(searchPath.c_str(), &data);
    if (findHandle == INVALID_HANDLE_VALUE)
        return;

    do
    {
        const std::wstring name(data.cFileName);
        if (name == L"." || name == L"..")
            continue;

        const std::wstring fullPath = folder + L"\\" + name;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if (recursive && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                ScanDirectory(fullPath, true, files);
            continue;
        }

        if (IsImageExtension(name))
        {
            std::string utf8Path = WideToUtf8(fullPath.c_str());
            if (!utf8Path.empty())
                files.push_back(std::move(utf8Path));
        }
    } while (FindNextFileW(findHandle, &data));

    FindClose(findHandle);
}
}

// ========================================
// 打开文件选择对话框（支持中文）
// 返回 Unicode 路径
// ========================================
std::string OpenFileDialog()
{
    wchar_t filename[MAX_PATH] = {};
    filename[0] = L'\0';

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);

    ofn.hwndOwner = nullptr;

    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;

    // Unicode过滤器
    ofn.lpstrFilter =
        L"图片文件 (*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.webp)\0"
        L"*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.webp\0"
        L"所有文件 (*.*)\0"
        L"*.*\0";

    ofn.nFilterIndex = 1;

    ofn.Flags =
        OFN_PATHMUSTEXIST |
        OFN_FILEMUSTEXIST |
        OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn))
    {
        return WideToUtf8(filename);
    }

    return "";
}

// ========================================
// 打开视频文件选择对话框
// ========================================
std::string OpenVideoDialog()
{
    wchar_t filename[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        L"视频文件 (*.mp4;*.avi;*.mov;*.mkv)\0"
        L"*.mp4;*.avi;*.mov;*.mkv\0"
        L"所有文件 (*.*)\0"
        L"*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn))
    {
        return WideToUtf8(filename);
    }
    return "";
}

// ========================================
// 通用文件选择对话框（自定义筛选和标题）
// ========================================
std::string OpenFileDialogWithFilter(const wchar_t *filter, const wchar_t *title)
{
    wchar_t filename[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.lpstrTitle = title;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn))
    {
        return WideToUtf8(filename);
    }
    return "";
}

// ========================================
// 打开文件夹选择对话框（使用 IFileDialog）
// 返回 Unicode 路径转 UTF-8
// ========================================
std::string OpenFolderDialog()
{
    std::string result;

    // 初始化 COM（线程安全），已初始化则复用
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool weInitCom = (hr == S_OK); // S_OK=我们首次初始化, S_FALSE=已被同模式初始化

    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return result; // COM 初始化失败，无法继续

    IFileDialog *pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr) && pfd)
    {
        // 设置为文件夹选择模式
        DWORD flags = 0;
        pfd->GetOptions(&flags);
        pfd->SetOptions(flags | FOS_PICKFOLDERS);

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem *psi = nullptr;
            if (SUCCEEDED(pfd->GetResult(&psi)) && psi)
            {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath)
                {
                    result = WideToUtf8(pszPath);
                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }

    if (weInitCom)
        CoUninitialize();

    return result;
}

// ========================================
// 扫描文件夹中所有图片文件
// 支持常用 OpenCV 图片格式，可选递归扫描，按完整路径排序。
// ========================================
std::vector<std::string> ScanImageFiles(const std::string &folderPath, bool recursive)
{
    std::vector<std::string> files;
    if (folderPath.empty())
        return files;
    const std::wstring wideFolder = Utf8ToWide(folderPath);
    if (wideFolder.empty())
        return files;
    ScanDirectory(wideFolder, recursive, files);
    std::sort(files.begin(), files.end());
    return files;
}
