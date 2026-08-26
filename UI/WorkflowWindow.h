#pragma once

#include <string>
#include <vector>

namespace UI::WorkflowWindow
{
    bool IsOpen();
    void Open();
    void Draw(const std::vector<int>& visibleToolIndices,
        const std::string& chainTitle);
}
