#include "UiPreferencesService.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <windows.h>

namespace
{
namespace fs = std::filesystem;

fs::path LocalPreferencesPath()
{
    wchar_t* localAppData = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 &&
        localAppData && length > 1)
    {
        const fs::path path = fs::path(localAppData) / L"IMgui_Opencv" /
            L"ui_preferences.cfg";
        std::free(localAppData);
        return path;
    }
    std::free(localAppData);
    return fs::current_path() / L"ui_preferences.cfg";
}

std::string PathToUtf8(const fs::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}
}

namespace UiPreferencesService
{
bool LoadAutoShowResult(bool defaultValue)
{
    const fs::path preferencesPath = LocalPreferencesPath();
    const fs::path legacyPath = fs::current_path() / L"ui_preferences.cfg";
    std::ifstream input(fs::exists(preferencesPath) ? preferencesPath : legacyPath);
    std::string key;
    int value = defaultValue ? 1 : 0;
    while (input >> key >> value)
    {
        if (key == "auto_show_result")
            return value != 0;
    }
    return defaultValue;
}

bool SaveAutoShowResult(bool enabled, std::string* error)
{
    try
    {
        const fs::path preferencesPath = LocalPreferencesPath();
        const fs::path legacyPath = fs::current_path() / L"ui_preferences.cfg";
        fs::create_directories(preferencesPath.parent_path());
        const fs::path temporaryPath = preferencesPath.wstring() + L".tmp";
        bool languageEnglish = false;
        {
            std::ifstream input(fs::exists(preferencesPath) ? preferencesPath : legacyPath);
            std::string key;
            int value = 0;
            while (input >> key >> value)
                if (key == "language") languageEnglish = value != 0;
        }
        std::ofstream output(temporaryPath, std::ios::trunc);
        output << "auto_show_result " << (enabled ? 1 : 0) << '\n';
        output << "language " << (languageEnglish ? 1 : 0) << '\n';
        output.flush();
        if (!output)
            throw std::runtime_error("cannot write UI preferences");
        output.close();
        if (!MoveFileExW(temporaryPath.c_str(), preferencesPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot atomically replace UI preferences");
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

std::string PreferencesPath()
{
    return PathToUtf8(LocalPreferencesPath());
}

LanguageService::Language LoadLanguage(LanguageService::Language defaultLanguage)
{
    const fs::path preferencesPath = LocalPreferencesPath();
    const fs::path legacyPath = fs::current_path() / L"ui_preferences.cfg";
    std::ifstream input(fs::exists(preferencesPath) ? preferencesPath : legacyPath);
    std::string key;
    int value = defaultLanguage == LanguageService::Language::English ? 1 : 0;
    while (input >> key >> value)
        if (key == "language")
            return value != 0 ? LanguageService::Language::English
                              : LanguageService::Language::Chinese;
    return defaultLanguage;
}

bool SaveLanguage(LanguageService::Language language, std::string* error)
{
    try
    {
        const fs::path preferencesPath = LocalPreferencesPath();
        const fs::path legacyPath = fs::current_path() / L"ui_preferences.cfg";
        fs::create_directories(preferencesPath.parent_path());
        bool autoShowResult = true;
        {
            std::ifstream input(fs::exists(preferencesPath) ? preferencesPath : legacyPath);
            std::string key;
            int value = 1;
            while (input >> key >> value)
                if (key == "auto_show_result") autoShowResult = value != 0;
        }
        const fs::path temporaryPath = preferencesPath.wstring() + L".tmp";
        std::ofstream output(temporaryPath, std::ios::trunc);
        output << "auto_show_result " << (autoShowResult ? 1 : 0) << '\n';
        output << "language " << (language == LanguageService::Language::English ? 1 : 0) << '\n';
        output.flush();
        if (!output)
            throw std::runtime_error("cannot write UI preferences");
        output.close();
        if (!MoveFileExW(temporaryPath.c_str(), preferencesPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot atomically replace UI preferences");
        if (error) error->clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error) *error = exception.what();
        return false;
    }
}
}
