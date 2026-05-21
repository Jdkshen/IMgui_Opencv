#pragma once
#include <string>
#include <opencv2/opencv.hpp>
#include <d3d12.h>

// ========================================
// OpenCVTest类：图片读取 + DX12纹理上传
// ========================================
class OpenCVTest
{
public:
    // 读取图片并上传到DX12纹理
    void TestReadImage(
        const std::string& path,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource** texture,
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle
    );
};

// ========================================
// 公共上传函数声明
// ========================================
void UploadToDX12(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource** texture,
    cv::Mat& rgba,
    DXGI_FORMAT format,
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle
);

// 延迟释放队列：存放待释放的旧纹理资源
extern std::vector<ID3D12Resource*> gPendingReleaseTextures;
void FlushPendingRelease();  // 释放队列中所有纹理（需在GPU空闲后调用）