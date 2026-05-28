#include "OpenFileDialog.h"

#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>   // IFileDialog, IShellItem

#include <string>
#include <vector>
#include <algorithm>

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
        L"图片文件 (*.jpg;*.png;*.bmp)\0"
        L"*.jpg;*.png;*.bmp\0"
        L"所有文件 (*.*)\0"
        L"*.*\0";

    ofn.nFilterIndex = 1;

    ofn.Flags =
        OFN_PATHMUSTEXIST |
        OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        // 将宽字符路径转换为 UTF-8 窄字符串
        int len = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0)
        {
            std::string result(len, '\0');  // len 含末尾 \0
            WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], len, nullptr, nullptr);
            result.resize(len - 1);          // 去掉末尾 \0
            return result;
        }
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
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0)
        {
            std::string result(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], len, nullptr, nullptr);
            result.resize(len - 1);
            return result;
        }
    }
    return "";
}

// ========================================
// 通用文件选择对话框（自定义筛选和标题）
// ========================================
std::string OpenFileDialogWithFilter(const wchar_t* filter, const wchar_t* title)
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
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0)
        {
            std::string result(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], len, nullptr, nullptr);
            result.resize(len - 1);
            return result;
        }
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
    bool weInitCom = (hr == S_OK);  // S_OK=我们首次初始化, S_FALSE=已被同模式初始化

    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return result;  // COM 初始化失败，无法继续

    IFileDialog* pfd = nullptr;
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
            IShellItem* psi = nullptr;
            if (SUCCEEDED(pfd->GetResult(&psi)) && psi)
            {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        result.resize(len - 1);
                        WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, &result[0], len, nullptr, nullptr);
                    }
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
// 支持的格式：jpg, jpeg, png, bmp（大小写不敏感）
// 按文件名升序排列
// ========================================
std::vector<std::string> ScanImageFiles(const std::string& folderPath)
{
    std::vector<std::string> files;
    if (folderPath.empty()) return files;

    // 构造搜索路径（支持 Unicode）
    int wlen = MultiByteToWideChar(CP_UTF8, 0, folderPath.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return files;

    // wlen 包含末尾 null，resize 去掉它，否则拼接路径会嵌入 \0
    std::wstring wfolder(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, folderPath.c_str(), -1, &wfolder[0], wlen);

    std::wstring searchPath = wfolder + L"\\*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return files;

    do
    {
        // 跳过目录
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        std::wstring wname(fd.cFileName);

        // 检查扩展名（转小写比较）
        size_t dotPos = wname.find_last_of(L'.');
        if (dotPos == std::wstring::npos) continue;

        std::wstring ext = wname.substr(dotPos);
        for (auto& c : ext) c = (wchar_t)towlower(c);

        if (ext != L".jpg" && ext != L".jpeg" && ext != L".png" && ext != L".bmp")
            continue;

        // 构造完整路径 → UTF-8
        std::wstring wfullPath = wfolder + L"\\" + wname;
        int ulen = WideCharToMultiByte(CP_UTF8, 0, wfullPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (ulen > 0)
        {
            std::string utf8Path(ulen - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wfullPath.c_str(), -1, &utf8Path[0], ulen, nullptr, nullptr);
            files.push_back(utf8Path);
        }

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    // 按文件名排序
    std::sort(files.begin(), files.end());

    return files;
}
