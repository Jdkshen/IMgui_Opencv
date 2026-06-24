#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
struct ShapeMatch { cv::Rect bbox; double score=0,shapeScore=0; int tmplIdx=-1; std::vector<cv::Point> points; double area=0; bool isGreen=false; };
namespace ShapeMatcher {
    struct Params { int blurSize=5,tplRetrMode=0; double tplMinArea=30,minScore=0.5,minShapeScore=0.3; int lineThickness=2,maxResults=1; bool showLabels=true;
        int shapeMethod=0; // 0=Hu矩 1=ShapeContext 2=Hausdorff
        // 模板预处理
        bool tplGray=false,tplBinary=false; int tplBinThresh=128; bool tplBlur=false; int tplBlurK=5; bool tplInvert=false; };
    cv::Mat Preprocess(const cv::Mat&,const Params&);
    std::vector<std::vector<cv::Point>> ExtractTemplates(const cv::Mat&,const Params&);
    std::vector<ShapeMatch> Search(const cv::Mat& image,const cv::Mat& tplImage,const Params&,const std::vector<std::vector<cv::Point>>& tplContours={});
    cv::Mat DrawMatches(cv::Mat&,const std::vector<ShapeMatch>&,const Params&);
    std::string Summary(const std::vector<ShapeMatch>&);
    extern float g_MatchTimeMs; extern int g_MatchCount; extern std::vector<ShapeMatch> g_LastMatches;
}
