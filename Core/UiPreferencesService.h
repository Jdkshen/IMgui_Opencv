#pragma once

#include <string>

#include "LanguageService.h"

namespace UiPreferencesService
{
    bool LoadAutoShowResult(bool defaultValue = true);
    bool SaveAutoShowResult(bool enabled, std::string* error = nullptr);
    std::string PreferencesPath();
    LanguageService::Language LoadLanguage(
        LanguageService::Language defaultLanguage = LanguageService::Language::Chinese);
    bool SaveLanguage(LanguageService::Language language, std::string* error = nullptr);
}
