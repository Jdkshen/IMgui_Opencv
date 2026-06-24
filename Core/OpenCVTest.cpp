#define NOMINMAX
#include <windows.h>

#include "DX12Context.h"

#include <opencv2/opencv.hpp>

#include <wrl/client.h>
#include <d3dx12.h>
#include "../Log/LogSystem.h"

using Microsoft::WRL::ComPtr;

// ========================================
// 上传OpenCV Mat到DX12纹理
// 注意：纹理资源每次都重新创建（尺寸可能变化）
//       upload 缓冲区按需扩容
// ========================================
void UploadToDX12(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *cmdList,
	ID3D12Resource **texture,
	cv::Mat &rgba,
	DXGI_FORMAT format,
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
	if (!device || !cmdList || rgba.empty())
		return;

	// 确保连续性（不连续内存无法直接上传到 GPU）
	cv::Mat img = rgba;
	if (!img.isContinuous())
		img = img.clone();

	// 纹理缓存：尺寸未变时复用已有纹理，只更新数据
	static UINT64 cachedWidth = 0, cachedHeight = 0;
	bool sizeChanged = (cachedWidth != (UINT64)img.cols || cachedHeight != (UINT64)img.rows);

	if (*texture && sizeChanged)
	{
		(*texture)->Release();
		*texture = nullptr;
	}

	if (*texture == nullptr)
	{
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = img.cols;
		desc.Height = img.rows;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
		HRESULT hr = device->CreateCommittedResource(
			&heapDefault, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			IID_PPV_ARGS(texture));
		if (FAILED(hr))
		{
				LogSystem::Add(LOG_ERROR, "创建纹理资源失败 hr=0x%08X", hr);
			return;
		}

		// 创建着色器资源视图（仅新建纹理时需要）
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format = format;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(*texture, &srv, srvHandle);

		cachedWidth = img.cols;
		cachedHeight = img.rows;
	}
	else
	{
		// 复用已有纹理：仅需从 PRESENT/SRV 切回 COPY_DEST
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			*texture,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COPY_DEST);
		cmdList->ResourceBarrier(1, &barrier);
	}

	// 上传缓冲区按需扩容
	UINT64 uploadSize = GetRequiredIntermediateSize(*texture, 0, 1);
	static ComPtr<ID3D12Resource> upload;
	static UINT64 uploadCapacity = 0;

	if (upload == nullptr || uploadSize > uploadCapacity)
	{
		upload.Reset();
		CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
		HRESULT hr = device->CreateCommittedResource(
			&heapUpload, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&upload));
		if (SUCCEEDED(hr))
			uploadCapacity = uploadSize;
		else
		{
				LogSystem::Add(LOG_ERROR, "创建上传缓冲区失败 hr=0x%08X", hr);
			return;
		}
	}

	D3D12_SUBRESOURCE_DATA sub = {};
	sub.pData = img.data;
	sub.RowPitch = (LONG_PTR)img.step;
	sub.SlicePitch = (LONG_PTR)img.step * img.rows;

	UpdateSubresources(cmdList, *texture, upload.Get(), 0, 0, 1, &sub);

	// 切换纹理状态：COPY_DEST → PIXEL_SHADER_RESOURCE
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		*texture,
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->ResourceBarrier(1, &barrier);
}

// ========================================
// 延迟释放队列（全局）
// 存放待释放的旧纹理资源，防止GPU仍在使用时释放
// ========================================
std::vector<ID3D12Resource *> gPendingReleaseTextures;

void FlushPendingRelease()
{
	for (auto *res : gPendingReleaseTextures)
	{
		if (res)
		{
			res->Release();
			res = nullptr;
		}
	}
	gPendingReleaseTextures.clear();
}
