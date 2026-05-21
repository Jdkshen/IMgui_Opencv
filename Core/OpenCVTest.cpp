#define NOMINMAX
#include <windows.h>

#include "OpenCVTest.h"
#include "DX12Context.h"

#include <opencv2/opencv.hpp>

#include <wrl/client.h>
#include <d3dx12.h>
#include "../log/LogSystem.h"
#include "../OpenCV/ThresholdTool.h"

using Microsoft::WRL::ComPtr;

// ========================================
// 上传OpenCV Mat到DX12纹理
// 注意：纹理资源每次都重新创建（尺寸可能变化）
//       upload 缓冲区按需扩容
// ========================================
void UploadToDX12(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	ID3D12Resource** texture,
	cv::Mat& rgba,
	DXGI_FORMAT format,
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
	if (!device || !cmdList || rgba.empty()) return;

	cv::Mat img = rgba;
	bool isContinuous = img.isContinuous();
	bool wasCloned = false;

	if (!isContinuous)
	{
		img = img.clone();
		wasCloned = true;
	}

	LogSystem::Add(LOG_INFO, color,
		"[图像] continuous=%d cloned=%d size=%dx%d step=%d",
		isContinuous, wasCloned, img.cols, img.rows, (int)img.step);

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = img.cols;
	desc.Height = img.rows;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	// 每次重新创建纹理资源（尺寸可能不同）
	if (*texture)
	{
		(*texture)->Release();
		*texture = nullptr;
	}

	CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
	HRESULT hr = device->CreateCommittedResource(
		&heapDefault, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(texture));
	if (FAILED(hr))
	{
		LogSystem::Add(LOG_ERROR, color, "创建纹理资源失败 hr=0x%08X", hr);
		return;
	}

	// 创建着色器资源视图
	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = format;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(*texture, &srv, srvHandle);

	// 计算所需上传缓冲区大小，按需重新分配
	UINT64 uploadSize = GetRequiredIntermediateSize(*texture, 0, 1);
	static ComPtr<ID3D12Resource> upload;
	static UINT64 uploadCapacity = 0;

	if (upload == nullptr || uploadSize > uploadCapacity)
	{
		upload.Reset();  // 释放旧缓冲区
		CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
		hr = device->CreateCommittedResource(
			&heapUpload, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&upload));
		if (SUCCEEDED(hr))
			uploadCapacity = uploadSize;
		else
		{
			LogSystem::Add(LOG_ERROR, color, "创建上传缓冲区失败 hr=0x%08X", hr);
			return;
		}
	}

	// 设置子资源数据描述
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

	OutputDebugStringA("DX12纹理上传成功\n");
}

// ========================================
// 读取图片并上传到DX12纹理
// path 为 UTF-8 编码（来自 OpenFileDialog）
// ========================================
void OpenCVTest::TestReadImage(
	const std::string& path,
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	ID3D12Resource** texture,
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
	// ========================================
	// 1. 参数检查
	// ========================================
	if (!device || !cmdList || !texture) return;
	if (path.empty())
	{
		LogSystem::Add(LOG_ERROR, color, "imread: 路径为空");
		return;
	}

	LogSystem::Add(LOG_INFO, color, "开始加载图片: %s", path.c_str());
	OutputDebugStringA(("STEP: 开始加载图片: " + path + "\n").c_str());

	// ========================================
	// 2. 释放旧纹理（延迟释放队列，等GPU完成后再释放）
	// ========================================
	if (*texture)
	{
		gPendingReleaseTextures.push_back(*texture);
		*texture = nullptr;
	}

	cv::Mat oldImage = gImage;  // 保留旧图像引用（防止UI读取空指针）

	// ========================================
	// 3. 读取文件到内存（兼容中文路径）
	//    UTF-8 → Wide Char → _wfopen_s
	// ========================================
	int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
	if (wlen <= 0)
	{
		LogSystem::Add(LOG_ERROR, color, "路径UTF-8→WideChar转换失败, len=%d", wlen);
		return;
	}

	std::vector<wchar_t> wpath(wlen);
	MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

	FILE* fp = nullptr;
	errno_t err = _wfopen_s(&fp, wpath.data(), L"rb");
	if (err != 0 || !fp)
	{
		LogSystem::Add(LOG_ERROR, color, "打开文件失败, errno=%d, path=%s", err, path.c_str());
		return;
	}

	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	rewind(fp);

	if (size == 0 || size > 512 * 1024 * 1024)  // 拒绝空文件 / >512MB
	{
		LogSystem::Add(LOG_ERROR, color, "文件大小异常: %zu 字节", size);
		fclose(fp);
		return;
	}

	std::vector<uchar> data(size);
	size_t readBytes = fread(data.data(), 1, size, fp);
	fclose(fp);

	if (readBytes != size)
	{
		LogSystem::Add(LOG_ERROR, color, "文件读取不完整: 期望=%zu 实际=%zu", size, readBytes);
		return;
	}

	// ========================================
	// 4. OpenCV 解码图片
	// ========================================
	cv::Mat img = cv::imdecode(data, cv::IMREAD_UNCHANGED);
	if (img.empty())
	{
		LogSystem::Add(LOG_ERROR, color, "图片解码失败, 文件大小=%zu", size);
		return;
	}

	LogSystem::Add(LOG_INFO, color, "解码成功: %dx%d channels=%d depth=%d",
		img.cols, img.rows, img.channels(), img.depth());

	// ========================================
	// 5. 保存图像数据到全局变量
	// ========================================
	gImage = img;
	gOriginalImage = img;
	gImageWidth = img.cols;
	gImageHeight = img.rows;

	// ========================================
	// 6. 转换为RGBA格式（DX12纹理要求）
	// ========================================
	cv::Mat rgba;
	if (img.channels() == 4)
		cv::cvtColor(img, rgba, cv::COLOR_BGRA2RGBA);
	else if (img.channels() == 3)
		cv::cvtColor(img, rgba, cv::COLOR_BGR2RGBA);
	else
		cv::cvtColor(img, rgba, cv::COLOR_GRAY2RGBA);

	// ========================================
	// 7. 上传到GPU显存
	// ========================================
	UploadToDX12(device, cmdList, texture, rgba, DXGI_FORMAT_R8G8B8A8_UNORM, srvHandle);

	OutputDebugStringA("STEP: 图片加载完成\n");
}

// ========================================
// 延迟释放队列（全局）
// 存放待释放的旧纹理资源，防止GPU仍在使用时释放
// ========================================
std::vector<ID3D12Resource*> gPendingReleaseTextures;

void FlushPendingRelease()
{
	for (auto* res : gPendingReleaseTextures)
	{
		if (res)
		{
			res->Release();
			res = nullptr;
		}
	}
	gPendingReleaseTextures.clear();
}