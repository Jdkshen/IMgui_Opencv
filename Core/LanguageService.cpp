#include "LanguageService.h"

#include "UiPreferencesService.h"

namespace
{
LanguageService::Language g_language = LanguageService::Language::Chinese;
bool g_loaded = false;

void EnsureLoaded()
{
    if (g_loaded)
        return;
    g_loaded = true;
    g_language = UiPreferencesService::LoadLanguage(
        LanguageService::Language::Chinese);
}
}

namespace LanguageService
{
Language Get()
{
    EnsureLoaded();
    return g_language;
}

bool IsChinese()
{
    return Get() == Language::Chinese;
}

void Set(Language language, bool persist)
{
    EnsureLoaded();
    g_language = language;
    if (persist)
        UiPreferencesService::SaveLanguage(language);
}

const char* Text(const char* chinese, const char* english)
{
    return IsChinese() ? chinese : english;
}
}
