#include "ToolInstance.h"

#include <algorithm>
#include <vector>

namespace
{
    using json = nlohmann::json;

    json RoiToJson(const ROI& roi)
    {
        json points = json::array();
        for (const ImVec2& point : roi.points)
            points.push_back({{"x", point.x}, {"y", point.y}});
        return {
            {"startX", roi.start.x}, {"startY", roi.start.y},
            {"endX", roi.end.x}, {"endY", roi.end.y},
            {"angle", roi.angle}, {"type", roi.type}, {"points", std::move(points)}
        };
    }

    ROI RoiFromJson(const json& value)
    {
        ROI roi;
        roi.start.x = value.value("startX", 0.0f);
        roi.start.y = value.value("startY", 0.0f);
        roi.end.x = value.value("endX", 0.0f);
        roi.end.y = value.value("endY", 0.0f);
        roi.angle = value.value("angle", 0.0f);
        roi.type = value.value("type", ROI_TYPE_RECT);
        if (value.contains("points") && value["points"].is_array())
        {
            for (const auto& point : value["points"])
                roi.points.emplace_back(point.value("x", 0.0f), point.value("y", 0.0f));
        }
        return roi;
    }

    json CalibrationSamplesToJson(const std::vector<CalibrationSample>& samples)
    {
        json result = json::array();
        for (const CalibrationSample& sample : samples)
        {
            result.push_back({
                {"pixelX", sample.pixel.x}, {"pixelY", sample.pixel.y},
                {"worldX", sample.world.x}, {"worldY", sample.world.y}
            });
        }
        return result;
    }

    void LoadCalibrationSamples(const json& source, std::vector<CalibrationSample>& target)
    {
        target.clear();
        if (!source.is_array())
            return;
        for (const auto& value : source)
        {
            CalibrationSample sample;
            sample.pixel.x = value.value("pixelX", 0.0);
            sample.pixel.y = value.value("pixelY", 0.0);
            sample.world.x = value.value("worldX", 0.0);
            sample.world.y = value.value("worldY", 0.0);
            target.push_back(sample);
        }
    }
}

nlohmann::json ToolInstance::ToRecipeJson() const
{
    json result;
#define SAVE_FIELD(field) result[#field] = field
    SAVE_FIELD(type);
    SAVE_FIELD(toolId);
    SAVE_FIELD(enabled);
    SAVE_FIELD(label);
    SAVE_FIELD(showResultLabels);
    SAVE_FIELD(skipIfModelMissing);
    SAVE_FIELD(groupName);
    SAVE_FIELD(collapsed);
    SAVE_FIELD(inputSourceMode);
    SAVE_FIELD(showTemplatePreview);
    SAVE_FIELD(hasTemplateROI);
    SAVE_FIELD(useSearchROI);
    SAVE_FIELD(resultRoiMode);
    SAVE_FIELD(resultRoiSourceTool);
    SAVE_FIELD(resultRoiSourceToolId);
    SAVE_FIELD(resultRoiIndex);
    SAVE_FIELD(resultRoiMissingPolicy);
    SAVE_FIELD(resultRoiCategory);
    SAVE_FIELD(resultRoiClassId);
    SAVE_FIELD(resultRoiMinScore);
    SAVE_FIELD(resultRoiMinArea);
    SAVE_FIELD(resultRoiSortMode);
    SAVE_FIELD(resultRoiSortDescending);
    SAVE_FIELD(enableRotation);
    SAVE_FIELD(rotationStart);
    SAVE_FIELD(rotationEnd);
    SAVE_FIELD(rotationStep);
    SAVE_FIELD(maxResults);
    SAVE_FIELD(matchThreshold);
    SAVE_FIELD(maxImageDim);
    SAVE_FIELD(nmsThreshold);
    SAVE_FIELD(searchMode);
    SAVE_FIELD(tplGray);
    SAVE_FIELD(tplBinary);
    SAVE_FIELD(tplBinThresh);
    SAVE_FIELD(tplEdge);
    SAVE_FIELD(tplEdgeLow);
    SAVE_FIELD(tplEdgeHigh);
    SAVE_FIELD(imgUseGray);
    SAVE_FIELD(imgEnableThreshold);
    SAVE_FIELD(imgThreshold);
    SAVE_FIELD(cannyLow);
    SAVE_FIELD(cannyHigh);
    SAVE_FIELD(edgeUseGray);
    SAVE_FIELD(dbgUseGray);
    SAVE_FIELD(dbgEnableBlur);
    SAVE_FIELD(dbgBlurSize);
    SAVE_FIELD(dbgEnableThresh);
    SAVE_FIELD(dbgThreshold);
    SAVE_FIELD(dbgEnableCanny);
    SAVE_FIELD(dbgCannyLow);
    SAVE_FIELD(dbgCannyHigh);
    SAVE_FIELD(blobMinArea);
    SAVE_FIELD(blobMaxArea);
    SAVE_FIELD(blobThresholdMode);
    SAVE_FIELD(blobThreshold);
    SAVE_FIELD(blobInvert);
    SAVE_FIELD(blobConnectivity);
    SAVE_FIELD(blobMinCircularity);
    SAVE_FIELD(blobMaxCircularity);
    SAVE_FIELD(blobMinAspectRatio);
    SAVE_FIELD(blobMaxAspectRatio);
    SAVE_FIELD(blobShowLabels);
    SAVE_FIELD(differenceThreshold);
    SAVE_FIELD(differenceMinArea);
    SAVE_FIELD(differenceBlurSize);
    SAVE_FIELD(differenceMorphKernelSize);
    SAVE_FIELD(differenceMorphIterations);
    SAVE_FIELD(differenceInvert);
    SAVE_FIELD(differenceShowLabels);
    SAVE_FIELD(yoloModelPath);
    SAVE_FIELD(yoloClassesPath);
    SAVE_FIELD(yoloConfThreshold);
    SAVE_FIELD(yoloNmsThreshold);
    SAVE_FIELD(yoloUseROI);
    SAVE_FIELD(yoloUseGPU);
    SAVE_FIELD(cntUseGray);
    SAVE_FIELD(cntBlurSize);
    SAVE_FIELD(cntThreshMode);
    SAVE_FIELD(cntThreshValue);
    SAVE_FIELD(cntAdaptBlock);
    SAVE_FIELD(cntInvert);
    SAVE_FIELD(cntRetrMode);
    SAVE_FIELD(cntApproxMethod);
    SAVE_FIELD(cntMinArea);
    SAVE_FIELD(cntMaxContours);
    SAVE_FIELD(cntFilterConvex);
    SAVE_FIELD(cntApproxEps);
    SAVE_FIELD(cntLineThick);
    SAVE_FIELD(cntShowLabels);
    SAVE_FIELD(cntFillContours);
    SAVE_FIELD(cntMatchROI);
    SAVE_FIELD(cntMatchThresh);
    SAVE_FIELD(shpBlurSize);
    SAVE_FIELD(shpTplRetr);
    SAVE_FIELD(shpTplMinArea);
    SAVE_FIELD(shpMinScore);
    SAVE_FIELD(shpShapeScore);
    SAVE_FIELD(shpLineThick);
    SAVE_FIELD(shpMethod);
    SAVE_FIELD(shpShowLabels);
    SAVE_FIELD(shpMaxResults);
    SAVE_FIELD(shpTplGray);
    SAVE_FIELD(shpTplBinary);
    SAVE_FIELD(shpTplBinThresh);
    SAVE_FIELD(shpTplBlur);
    SAVE_FIELD(shpTplBlurK);
    SAVE_FIELD(shpTplInvert);
    SAVE_FIELD(lineCannyLow);
    SAVE_FIELD(lineCannyHigh);
    SAVE_FIELD(lineMinLength);
    SAVE_FIELD(lineMaxGap);
    SAVE_FIELD(lineMinAngle);
    SAVE_FIELD(lineMaxAngle);
    SAVE_FIELD(lineThickness);
    SAVE_FIELD(lineMaxLines);
    SAVE_FIELD(lineShowLabels);
    SAVE_FIELD(lineUseROI);
    SAVE_FIELD(morphOpType);
    SAVE_FIELD(morphKernelSize);
    SAVE_FIELD(morphKernelShape);
    SAVE_FIELD(morphIterations);
    SAVE_FIELD(morphUseGray);
    SAVE_FIELD(colorSpace);
    SAVE_FIELD(colorHistBins);
    SAVE_FIELD(colorShowHist);
    SAVE_FIELD(colorUseROI);
    SAVE_FIELD(colorHistHeight);
    SAVE_FIELD(mcfShowPreview);
    SAVE_FIELD(mcfAnchorX);
    SAVE_FIELD(mcfAnchorY);
    SAVE_FIELD(mcfImgGray);
    SAVE_FIELD(mcfImgBinary);
    SAVE_FIELD(mcfImgBinThresh);
    SAVE_FIELD(mcfUseROI);
    SAVE_FIELD(mcfMaxResults);
    SAVE_FIELD(mcfMinDist);
    SAVE_FIELD(mcfCrossSize);
    SAVE_FIELD(mcfCrossThick);
    SAVE_FIELD(mcfRoiX);
    SAVE_FIELD(mcfRoiY);
    SAVE_FIELD(mcfRoiW);
    SAVE_FIELD(mcfRoiH);
    SAVE_FIELD(ocrDetModelPath);
    SAVE_FIELD(ocrDetParamPath);
    SAVE_FIELD(ocrRecModelPath);
    SAVE_FIELD(ocrRecParamPath);
    SAVE_FIELD(ocrDictionaryPath);
    SAVE_FIELD(ocrMinConfidence);
    SAVE_FIELD(ocrMaxItems);
    SAVE_FIELD(ocrInputSize);
    SAVE_FIELD(ocrMaxCandidates);
    SAVE_FIELD(ocrMinBoxArea);
    SAVE_FIELD(ocrMinBoxHeight);
    SAVE_FIELD(ocrRoiPadding);
    SAVE_FIELD(ocrFastMode);
    SAVE_FIELD(ocrDetectOnly);
    SAVE_FIELD(ocrUseROI);
    SAVE_FIELD(qrUseROI);
    SAVE_FIELD(qrDetectMulti);
    SAVE_FIELD(qrEnhance);
    SAVE_FIELD(qrMinSize);
    SAVE_FIELD(qrShowText);
    SAVE_FIELD(qrEngine);
    SAVE_FIELD(qrFormatMask);
    SAVE_FIELD(qrFilterDuplicates);
    SAVE_FIELD(measureMode);
    SAVE_FIELD(measureCaliperCount);
    SAVE_FIELD(measureSearchLength);
    SAVE_FIELD(measureProjectionWidth);
    SAVE_FIELD(measureSmoothingSigma);
    SAVE_FIELD(measureEdgeThreshold);
    SAVE_FIELD(measureMinPairDistance);
    SAVE_FIELD(measureEdgePolarity);
    SAVE_FIELD(measureSubpixel);
    SAVE_FIELD(measureFitMethod);
    SAVE_FIELD(measureFitInlierThreshold);
    SAVE_FIELD(measureMinimumValidCalipers);
    SAVE_FIELD(measureMinimumConfidence);
    SAVE_FIELD(measureMmPerPixel);
    SAVE_FIELD(measureCalibrationPixels);
    SAVE_FIELD(measureCalibrationMm);
    SAVE_FIELD(measureToleranceEnabled);
    SAVE_FIELD(measureNominal);
    SAVE_FIELD(measureToleranceMinus);
    SAVE_FIELD(measureTolerancePlus);
    SAVE_FIELD(geometryDrawType);
#undef SAVE_FIELD

    result["geometryItems"] = json::array();
    for (const GeometryPrimitive& primitive : geometryItems)
        result["geometryItems"].push_back(GeometryPrimitiveToJson(primitive));

    result["judgement"] = {
        {"enabled", judgement.enabled}, {"stopOnFailure", judgement.stopOnFailure},
        {"minResultCount", judgement.minResultCount}, {"maxResultCount", judgement.maxResultCount},
        {"minScore", judgement.minScore}, {"minArea", judgement.minArea},
        {"maxArea", judgement.maxArea}, {"requiredText", judgement.requiredText},
        {"measurementRangeEnabled", judgement.measurementRangeEnabled},
        {"measurementName", judgement.measurementName},
        {"minMeasurement", judgement.minMeasurement},
        {"maxMeasurement", judgement.maxMeasurement},
        {"textMatchMode", judgement.textMatchMode},
        {"textCaseSensitive", judgement.textCaseSensitive}
    };
    result["fixture"] = {
        {"enabled", fixture.enabled}, {"sourceToolIndex", fixture.sourceToolIndex},
        {"sourceToolId", fixture.sourceToolId}, {"resultIndex", fixture.resultIndex},
        {"referenceX", fixture.referenceOrigin.x}, {"referenceY", fixture.referenceOrigin.y},
        {"referenceAngle", fixture.referenceAngleDegrees}, {"failOnMissing", fixture.failOnMissing}
    };
    result["templateROI"] = RoiToJson(templateROI);
    result["searchROIs"] = json::array();
    for (const ROI& roi : searchROIs)
        result["searchROIs"].push_back(RoiToJson(roi));
    result["lineSaveROIs"] = json::array();
    for (const ROI& roi : lineSaveROIs)
        result["lineSaveROIs"].push_back(RoiToJson(roi));

    const std::vector<double> homography(
        measureCalibration.pixelToWorldHomography.val,
        measureCalibration.pixelToWorldHomography.val + 9);
    const json samples = CalibrationSamplesToJson(measureCalibrationSamples);
    result["calibrationSamples"] = samples;
    result["measurement"] = {
        {"mode", measureMode}, {"caliperCount", measureCaliperCount},
        {"searchLength", measureSearchLength}, {"projectionWidth", measureProjectionWidth},
        {"smoothingSigma", measureSmoothingSigma}, {"edgeThreshold", measureEdgeThreshold},
        {"minPairDistance", measureMinPairDistance}, {"edgePolarity", measureEdgePolarity},
        {"subpixel", measureSubpixel}, {"fitMethod", measureFitMethod},
        {"fitInlierThreshold", measureFitInlierThreshold},
        {"minimumValidCalipers", measureMinimumValidCalipers},
        {"minimumConfidence", measureMinimumConfidence},
        {"legacyMmPerPixel", measureMmPerPixel},
        {"calibrationEnabled", measureCalibration.enabled},
        {"scaleX", measureCalibration.scaleX}, {"scaleY", measureCalibration.scaleY},
        {"pixelOriginX", measureCalibration.pixelOrigin.x},
        {"pixelOriginY", measureCalibration.pixelOrigin.y},
        {"worldOriginX", measureCalibration.worldOrigin.x},
        {"worldOriginY", measureCalibration.worldOrigin.y},
        {"homographyEnabled", measureCalibration.homographyEnabled},
        {"homography", homography},
        {"distortionEnabled", measureCalibration.distortionEnabled},
        {"fx", measureCalibration.fx}, {"fy", measureCalibration.fy},
        {"cx", measureCalibration.cx}, {"cy", measureCalibration.cy},
        {"k1", measureCalibration.k1}, {"k2", measureCalibration.k2},
        {"p1", measureCalibration.p1}, {"p2", measureCalibration.p2},
        {"k3", measureCalibration.k3},
        {"toleranceEnabled", measureToleranceEnabled}, {"nominal", measureNominal},
        {"toleranceMinus", measureToleranceMinus}, {"tolerancePlus", measureTolerancePlus},
        {"calibrationSamples", samples}
    };
    return result;
}

void ToolInstance::LoadRecipeJson(const nlohmann::json& source)
{
#define LOAD_FIELD(field) field = source.value(#field, field)
    LOAD_FIELD(type);
    LOAD_FIELD(toolId);
    LOAD_FIELD(enabled);
    LOAD_FIELD(label);
    LOAD_FIELD(showResultLabels);
    LOAD_FIELD(skipIfModelMissing);
    LOAD_FIELD(groupName);
    LOAD_FIELD(collapsed);
    LOAD_FIELD(inputSourceMode);
    LOAD_FIELD(showTemplatePreview);
    LOAD_FIELD(hasTemplateROI);
    LOAD_FIELD(useSearchROI);
    LOAD_FIELD(resultRoiMode);
    LOAD_FIELD(resultRoiSourceTool);
    LOAD_FIELD(resultRoiSourceToolId);
    LOAD_FIELD(resultRoiIndex);
    LOAD_FIELD(resultRoiMissingPolicy);
    LOAD_FIELD(resultRoiCategory);
    LOAD_FIELD(resultRoiClassId);
    LOAD_FIELD(resultRoiMinScore);
    LOAD_FIELD(resultRoiMinArea);
    LOAD_FIELD(resultRoiSortMode);
    LOAD_FIELD(resultRoiSortDescending);
    LOAD_FIELD(enableRotation);
    LOAD_FIELD(rotationStart);
    LOAD_FIELD(rotationEnd);
    LOAD_FIELD(rotationStep);
    LOAD_FIELD(maxResults);
    LOAD_FIELD(matchThreshold);
    LOAD_FIELD(maxImageDim);
    LOAD_FIELD(nmsThreshold);
    LOAD_FIELD(searchMode);
    LOAD_FIELD(tplGray);
    LOAD_FIELD(tplBinary);
    LOAD_FIELD(tplBinThresh);
    LOAD_FIELD(tplEdge);
    LOAD_FIELD(tplEdgeLow);
    LOAD_FIELD(tplEdgeHigh);
    LOAD_FIELD(imgUseGray);
    LOAD_FIELD(imgEnableThreshold);
    LOAD_FIELD(imgThreshold);
    LOAD_FIELD(cannyLow);
    LOAD_FIELD(cannyHigh);
    LOAD_FIELD(edgeUseGray);
    LOAD_FIELD(dbgUseGray);
    LOAD_FIELD(dbgEnableBlur);
    LOAD_FIELD(dbgBlurSize);
    LOAD_FIELD(dbgEnableThresh);
    LOAD_FIELD(dbgThreshold);
    LOAD_FIELD(dbgEnableCanny);
    LOAD_FIELD(dbgCannyLow);
    LOAD_FIELD(dbgCannyHigh);
    LOAD_FIELD(blobMinArea);
    LOAD_FIELD(blobMaxArea);
    LOAD_FIELD(blobThresholdMode);
    LOAD_FIELD(blobThreshold);
    LOAD_FIELD(blobInvert);
    LOAD_FIELD(blobConnectivity);
    LOAD_FIELD(blobMinCircularity);
    LOAD_FIELD(blobMaxCircularity);
    LOAD_FIELD(blobMinAspectRatio);
    LOAD_FIELD(blobMaxAspectRatio);
    LOAD_FIELD(blobShowLabels);
    LOAD_FIELD(differenceThreshold);
    LOAD_FIELD(differenceMinArea);
    LOAD_FIELD(differenceBlurSize);
    LOAD_FIELD(differenceMorphKernelSize);
    LOAD_FIELD(differenceMorphIterations);
    LOAD_FIELD(differenceInvert);
    LOAD_FIELD(differenceShowLabels);
    LOAD_FIELD(yoloModelPath);
    LOAD_FIELD(yoloClassesPath);
    LOAD_FIELD(yoloConfThreshold);
    LOAD_FIELD(yoloNmsThreshold);
    LOAD_FIELD(yoloUseROI);
    LOAD_FIELD(yoloUseGPU);
    LOAD_FIELD(cntUseGray);
    LOAD_FIELD(cntBlurSize);
    LOAD_FIELD(cntThreshMode);
    LOAD_FIELD(cntThreshValue);
    LOAD_FIELD(cntAdaptBlock);
    LOAD_FIELD(cntInvert);
    LOAD_FIELD(cntRetrMode);
    LOAD_FIELD(cntApproxMethod);
    LOAD_FIELD(cntMinArea);
    LOAD_FIELD(cntMaxContours);
    LOAD_FIELD(cntFilterConvex);
    LOAD_FIELD(cntApproxEps);
    LOAD_FIELD(cntLineThick);
    LOAD_FIELD(cntShowLabels);
    LOAD_FIELD(cntFillContours);
    LOAD_FIELD(cntMatchROI);
    LOAD_FIELD(cntMatchThresh);
    LOAD_FIELD(shpBlurSize);
    LOAD_FIELD(shpTplRetr);
    LOAD_FIELD(shpTplMinArea);
    LOAD_FIELD(shpMinScore);
    LOAD_FIELD(shpShapeScore);
    LOAD_FIELD(shpLineThick);
    LOAD_FIELD(shpMethod);
    LOAD_FIELD(shpShowLabels);
    LOAD_FIELD(shpMaxResults);
    LOAD_FIELD(shpTplGray);
    LOAD_FIELD(shpTplBinary);
    LOAD_FIELD(shpTplBinThresh);
    LOAD_FIELD(shpTplBlur);
    LOAD_FIELD(shpTplBlurK);
    LOAD_FIELD(shpTplInvert);
    LOAD_FIELD(lineCannyLow);
    LOAD_FIELD(lineCannyHigh);
    LOAD_FIELD(lineMinLength);
    LOAD_FIELD(lineMaxGap);
    LOAD_FIELD(lineMinAngle);
    LOAD_FIELD(lineMaxAngle);
    LOAD_FIELD(lineThickness);
    LOAD_FIELD(lineMaxLines);
    LOAD_FIELD(lineShowLabels);
    LOAD_FIELD(lineUseROI);
    LOAD_FIELD(morphOpType);
    LOAD_FIELD(morphKernelSize);
    LOAD_FIELD(morphKernelShape);
    LOAD_FIELD(morphIterations);
    LOAD_FIELD(morphUseGray);
    LOAD_FIELD(colorSpace);
    LOAD_FIELD(colorHistBins);
    LOAD_FIELD(colorShowHist);
    LOAD_FIELD(colorUseROI);
    LOAD_FIELD(colorHistHeight);
    LOAD_FIELD(mcfShowPreview);
    LOAD_FIELD(mcfAnchorX);
    LOAD_FIELD(mcfAnchorY);
    LOAD_FIELD(mcfImgGray);
    LOAD_FIELD(mcfImgBinary);
    LOAD_FIELD(mcfImgBinThresh);
    LOAD_FIELD(mcfUseROI);
    LOAD_FIELD(mcfMaxResults);
    LOAD_FIELD(mcfMinDist);
    LOAD_FIELD(mcfCrossSize);
    LOAD_FIELD(mcfCrossThick);
    LOAD_FIELD(mcfRoiX);
    LOAD_FIELD(mcfRoiY);
    LOAD_FIELD(mcfRoiW);
    LOAD_FIELD(mcfRoiH);
    LOAD_FIELD(ocrDetModelPath);
    LOAD_FIELD(ocrDetParamPath);
    LOAD_FIELD(ocrRecModelPath);
    LOAD_FIELD(ocrRecParamPath);
    LOAD_FIELD(ocrDictionaryPath);
    LOAD_FIELD(ocrMinConfidence);
    LOAD_FIELD(ocrMaxItems);
    LOAD_FIELD(ocrInputSize);
    LOAD_FIELD(ocrMaxCandidates);
    LOAD_FIELD(ocrMinBoxArea);
    LOAD_FIELD(ocrMinBoxHeight);
    LOAD_FIELD(ocrRoiPadding);
    LOAD_FIELD(ocrFastMode);
    LOAD_FIELD(ocrDetectOnly);
    LOAD_FIELD(ocrUseROI);
    LOAD_FIELD(qrUseROI);
    LOAD_FIELD(qrDetectMulti);
    LOAD_FIELD(qrEnhance);
    LOAD_FIELD(qrMinSize);
    LOAD_FIELD(qrShowText);
    LOAD_FIELD(qrEngine);
    LOAD_FIELD(qrFormatMask);
    LOAD_FIELD(qrFilterDuplicates);
    LOAD_FIELD(measureMode);
    LOAD_FIELD(measureCaliperCount);
    LOAD_FIELD(measureSearchLength);
    LOAD_FIELD(measureProjectionWidth);
    LOAD_FIELD(measureSmoothingSigma);
    LOAD_FIELD(measureEdgeThreshold);
    LOAD_FIELD(measureMinPairDistance);
    LOAD_FIELD(measureEdgePolarity);
    LOAD_FIELD(measureSubpixel);
    LOAD_FIELD(measureFitMethod);
    LOAD_FIELD(measureFitInlierThreshold);
    LOAD_FIELD(measureMinimumValidCalipers);
    LOAD_FIELD(measureMinimumConfidence);
    LOAD_FIELD(measureMmPerPixel);
    LOAD_FIELD(measureCalibrationPixels);
    LOAD_FIELD(measureCalibrationMm);
    LOAD_FIELD(measureToleranceEnabled);
    LOAD_FIELD(measureNominal);
    LOAD_FIELD(measureToleranceMinus);
    LOAD_FIELD(measureTolerancePlus);
    LOAD_FIELD(geometryDrawType);
#undef LOAD_FIELD

    // Recipes created before the common result-label switch stored this option
    // in individual tool fields. Preserve that behavior during migration.
    if (!source.contains("showResultLabels"))
    {
        switch (type)
        {
        case 2: showResultLabels = blobShowLabels; break;
        case 5: showResultLabels = cntShowLabels; break;
        case 6: showResultLabels = shpShowLabels; break;
        case 7: showResultLabels = lineShowLabels; break;
        case 14: showResultLabels = qrShowText; break;
        case 16: showResultLabels = differenceShowLabels; break;
        default: break;
        }
    }

    geometryItems.clear();
    if (source.contains("geometryItems") && source["geometryItems"].is_array())
    {
        for (const auto& value : source["geometryItems"])
        {
            GeometryPrimitive primitive;
            if (GeometryPrimitiveFromJson(value, primitive))
                geometryItems.push_back(std::move(primitive));
        }
    }

    if (source.contains("judgement") && source["judgement"].is_object())
    {
        const auto& value = source["judgement"];
        judgement.enabled = value.value("enabled", judgement.enabled);
        judgement.stopOnFailure = value.value("stopOnFailure", judgement.stopOnFailure);
        judgement.minResultCount = value.value("minResultCount", judgement.minResultCount);
        judgement.maxResultCount = value.value("maxResultCount", judgement.maxResultCount);
        judgement.minScore = value.value("minScore", judgement.minScore);
        judgement.minArea = value.value("minArea", judgement.minArea);
        judgement.maxArea = value.value("maxArea", judgement.maxArea);
        judgement.measurementRangeEnabled = value.value("measurementRangeEnabled", judgement.measurementRangeEnabled);
        judgement.measurementName = value.value("measurementName", judgement.measurementName);
        judgement.minMeasurement = value.value("minMeasurement", judgement.minMeasurement);
        judgement.maxMeasurement = value.value("maxMeasurement", judgement.maxMeasurement);
        judgement.requiredText = value.value("requiredText", judgement.requiredText);
        judgement.textMatchMode = value.value("textMatchMode", judgement.textMatchMode);
        judgement.textCaseSensitive = value.value("textCaseSensitive", judgement.textCaseSensitive);
    }
    if (source.contains("fixture") && source["fixture"].is_object())
    {
        const auto& value = source["fixture"];
        fixture.enabled = value.value("enabled", fixture.enabled);
        fixture.sourceToolIndex = value.value("sourceToolIndex", fixture.sourceToolIndex);
        fixture.sourceToolId = value.value("sourceToolId", fixture.sourceToolId);
        fixture.resultIndex = value.value("resultIndex", fixture.resultIndex);
        fixture.referenceOrigin.x = value.value("referenceX", fixture.referenceOrigin.x);
        fixture.referenceOrigin.y = value.value("referenceY", fixture.referenceOrigin.y);
        fixture.referenceAngleDegrees = value.value("referenceAngle", fixture.referenceAngleDegrees);
        fixture.failOnMissing = value.value("failOnMissing", fixture.failOnMissing);
    }
    if (source.contains("templateROI") && source["templateROI"].is_object())
        templateROI = RoiFromJson(source["templateROI"]);
    if (source.contains("searchROIs") && source["searchROIs"].is_array())
    {
        searchROIs.clear();
        for (const auto& value : source["searchROIs"])
            searchROIs.push_back(RoiFromJson(value));
    }
    if (source.contains("lineSaveROIs") && source["lineSaveROIs"].is_array())
    {
        lineSaveROIs.clear();
        for (const auto& value : source["lineSaveROIs"])
            lineSaveROIs.push_back(RoiFromJson(value));
    }

    const json* measurement = nullptr;
    if (source.contains("measurement") && source["measurement"].is_object())
        measurement = &source["measurement"];
    if (measurement)
    {
        const auto& value = *measurement;
        measureMode = std::clamp(value.value("mode", measureMode), 0, 7);
        measureCaliperCount = value.value("caliperCount", measureCaliperCount);
        measureSearchLength = value.value("searchLength", measureSearchLength);
        measureProjectionWidth = value.value("projectionWidth", measureProjectionWidth);
        measureSmoothingSigma = value.value("smoothingSigma", measureSmoothingSigma);
        measureEdgeThreshold = value.value("edgeThreshold", measureEdgeThreshold);
        measureMinPairDistance = value.value("minPairDistance", measureMinPairDistance);
        measureEdgePolarity = value.value("edgePolarity", measureEdgePolarity);
        measureSubpixel = value.value("subpixel", measureSubpixel);
        measureFitMethod = value.value("fitMethod", measureFitMethod);
        measureFitInlierThreshold = value.value("fitInlierThreshold", measureFitInlierThreshold);
        measureMinimumValidCalipers = value.value("minimumValidCalipers", measureMinimumValidCalipers);
        measureMinimumConfidence = value.value("minimumConfidence", measureMinimumConfidence);
        measureMmPerPixel = value.value("legacyMmPerPixel", measureMmPerPixel);
        measureCalibration.enabled = value.value("calibrationEnabled", measureCalibration.enabled);
        measureCalibration.scaleX = value.value("scaleX", measureCalibration.scaleX);
        measureCalibration.scaleY = value.value("scaleY", measureCalibration.scaleY);
        measureCalibration.pixelOrigin.x = value.value("pixelOriginX", measureCalibration.pixelOrigin.x);
        measureCalibration.pixelOrigin.y = value.value("pixelOriginY", measureCalibration.pixelOrigin.y);
        measureCalibration.worldOrigin.x = value.value("worldOriginX", measureCalibration.worldOrigin.x);
        measureCalibration.worldOrigin.y = value.value("worldOriginY", measureCalibration.worldOrigin.y);
        measureCalibration.homographyEnabled = value.value("homographyEnabled", measureCalibration.homographyEnabled);
        const auto homography = value.value("homography", std::vector<double>());
        if (homography.size() == 9)
            std::copy(homography.begin(), homography.end(), measureCalibration.pixelToWorldHomography.val);
        measureCalibration.distortionEnabled = value.value("distortionEnabled", measureCalibration.distortionEnabled);
        measureCalibration.fx = value.value("fx", measureCalibration.fx);
        measureCalibration.fy = value.value("fy", measureCalibration.fy);
        measureCalibration.cx = value.value("cx", measureCalibration.cx);
        measureCalibration.cy = value.value("cy", measureCalibration.cy);
        measureCalibration.k1 = value.value("k1", measureCalibration.k1);
        measureCalibration.k2 = value.value("k2", measureCalibration.k2);
        measureCalibration.p1 = value.value("p1", measureCalibration.p1);
        measureCalibration.p2 = value.value("p2", measureCalibration.p2);
        measureCalibration.k3 = value.value("k3", measureCalibration.k3);
        measureToleranceEnabled = value.value("toleranceEnabled", measureToleranceEnabled);
        measureNominal = value.value("nominal", measureNominal);
        measureToleranceMinus = value.value("toleranceMinus", measureToleranceMinus);
        measureTolerancePlus = value.value("tolerancePlus", measureTolerancePlus);
    }
    if (measurement && measurement->contains("calibrationSamples"))
        LoadCalibrationSamples((*measurement)["calibrationSamples"], measureCalibrationSamples);
    else if (source.contains("calibrationSamples"))
        LoadCalibrationSamples(source["calibrationSamples"], measureCalibrationSamples);
}

void ToolInstance::ClearRuntimeState()
{
    toolImpl = nullptr;
    lastResult = ToolResult{};
    hasLastResult = false;
    parametersDirty = false;
    measureRuntimeROIIds.clear();
    measureCalibrationRmsError = 0.0;
    measureCalibrationMaxError = 0.0;
    measureCalibrationFitMessage.clear();
}
