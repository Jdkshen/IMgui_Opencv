#include "TemplateMatch.h"

#include "../Core/TemplateState.h"

#include <opencv2/imgcodecs.hpp>

#include <utility>

namespace TemplateMatch
{
void Clear()
{
    TemplateState::ClearResults();
}

bool SaveTemplate(const char* filepath)
{
    if (!filepath || !*filepath || TemplateState::FrozenTemplate().empty())
        return false;
    return cv::imwrite(filepath, TemplateState::FrozenTemplate());
}

bool LoadTemplate(const char* filepath)
{
    if (!filepath || !*filepath)
        return false;

    cv::Mat templateImage = cv::imread(filepath, cv::IMREAD_COLOR);
    if (templateImage.empty())
        return false;

    TemplateState::FrozenTemplate() = std::move(templateImage);
    return true;
}
}
