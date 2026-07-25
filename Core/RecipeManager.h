#pragma once

#include "ToolInstance.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

struct RecipeThreshold
{
    bool useGray = false;
    int thresholdValue = 128;
    bool binaryInv = false;
    int blurSize = 1;
    int cannyLow = 50;
    int cannyHigh = 150;
    float brightness = 0.0f;
    float contrast = 1.0f;
    int processMode = 0;

    bool pipeBlur = false;
    bool pipeThreshold = false;
    bool pipeCanny = false;
    int pipeBlurSize = 5;
    int pipeThresholdVal = 128;
    int pipeCannyLow = 50;
    int pipeCannyHigh = 150;
};

struct RecipeTemplateMatch
{
    int method = 5;
    int searchMode = 0;
    int maxResults = 10;
    int maxImageDim = 1000;
    float matchThreshold = 0.75f;
    bool enableRotation = false;
    int rotationStart = -5;
    int rotationEnd = 5;
    int rotationStep = 5;
};

struct RecipeROI
{
    float startX = 0.0f;
    float startY = 0.0f;
    float endX = 0.0f;
    float endY = 0.0f;
    float angle = 0.0f;
    int type = 0;
};

struct RecipeTaskGroup
{
    std::string name;
    bool enabled = true;
    std::string imagePath;
};

// Recipe snapshot for one tool. Runtime parameters remain serialized by
// ToolInstance; this DTO only owns that JSON snapshot and recipe asset payloads.
struct RecipeToolInstance
{
    std::string templateFile;
    cv::Mat templateImage;
    std::string differenceReferenceFile;
    cv::Mat differenceReferenceImage;
    std::string multiColorReferenceFile;
    cv::Mat multiColorReferenceImage;
    std::string mcfPointsJson;

    nlohmann::json ToJson() const;
    void LoadToolJson(const nlohmann::json& json);
    void CaptureFrom(const ToolInstance& source, bool includeAssets = true);
    ToolInstance CreateToolInstance() const;

private:
    nlohmann::json toolJson_ = nlohmann::json::object();
};

struct RecipeData
{
    std::string name;
    std::string imagePath;
    int loopIntervalMs = 150;
    std::string templateImage;
    cv::Mat legacyTemplateImage;
    // Deprecated compatibility fields. New recipes use tools[*].ToJson().
    RecipeThreshold threshold;
    RecipeTemplateMatch tmMatch;
    std::vector<RecipeROI> rois;
    std::vector<RecipeTaskGroup> taskGroups;
    std::vector<RecipeToolInstance> tools;
};

namespace RecipeManager
{
    struct SaveOptions
    {
        bool writeAssets = true;
    };

    bool Save(const char* filepath, const RecipeData& data,
        const SaveOptions& options = {});
    bool Load(const char* filepath, RecipeData& data);
    std::vector<std::string> List(const char* exeDir = nullptr);
    RecipeData Capture(const char* name, bool includeAssets = true);
    void Apply(const RecipeData& data);
}
