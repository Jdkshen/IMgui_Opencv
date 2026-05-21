#include "OpenFileDialog.h"

#include <windows.h>
#include <commdlg.h>

#include <string>

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
            std::string result(len - 1, '\0'); // 去掉末尾的null终止符
            WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], len, nullptr, nullptr);
            return result;
        }
    }

    return "";
}
