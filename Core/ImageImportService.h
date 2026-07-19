#pragma once

#include <cstddef>
#include <string>

struct ImageImportResult
{
    bool success = false;
    std::string message;
    std::string imagePath;
    std::size_t imageCount = 0;
    int imageIndex = -1;
};

namespace ImageImportService
{
    ImageImportResult ImportSingleImage(const std::string& imagePath);
    ImageImportResult ImportFolder(const std::string& folderPath, bool recursive = true);
    ImageImportResult NavigateToImage(int index);
    ImageImportResult NavigatePreviousImage();
    ImageImportResult NavigateNextImage();
    void ClearCurrentInput();
}
