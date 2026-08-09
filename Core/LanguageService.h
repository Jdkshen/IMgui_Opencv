#pragma once

namespace LanguageService
{
    enum class Language
    {
        Chinese,
        English
    };

    Language Get();
    bool IsChinese();
    void Set(Language language, bool persist = true);
    const char* Text(const char* chinese, const char* english);
}
