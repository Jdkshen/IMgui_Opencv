#pragma once
#include <string>
#include <opencv2/opencv.hpp>
#include <d3d12.h>

// ========================================
// DX12 纹理上传 + 延迟释放
// ========================================
void UploadToDX12(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *cmdList,
    ID3D12Resource **texture,
    cv::Mat &rgba,
    DXGI_FORMAT format,
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);

void FlushPendingRelease();

extern std::vector<ID3D12Resource *> gPendingReleaseTextures;

// ========================================
// 公共上传函数声明
// ========================================
void UploadToDX12(
    ID3D12Device *device,
    ID3D12GraphicsCommandList *cmdList,
    ID3D12Resource **texture,
    cv::Mat &rgba,
    DXGI_FORMAT format,
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);

// 延迟释放队列：存放待释放的旧纹理资源
extern std::vector<ID3D12Resource *> gPendingReleaseTextures;
void FlushPendingRelease(); // 释放队列中所有纹理（需在GPU空闲后调用）