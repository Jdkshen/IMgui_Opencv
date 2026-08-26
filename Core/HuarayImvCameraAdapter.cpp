#include "HuarayImvCameraAdapter.h"
#include "CameraPixelFormat.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
constexpr int kImvOk = 0;
constexpr unsigned int kInterfaceAll = 0;
constexpr int kModeByIndex = 0;
constexpr int kModeByCameraKey = 1;
constexpr int kModeByDeviceUserId = 2;
constexpr int kModeByIpAddress = 3;

constexpr std::int32_t kPixelMono8 = 0x01080001;
constexpr std::int32_t kPixelRgb8 = 0x02180014;
constexpr std::int32_t kPixelBgr8 = 0x02180015;
constexpr int kDemosaicNearestNeighbor = 0;

using ImvHandle = void*;
using ImvFrameHandle = void*;

struct ImvDeviceList
{
    unsigned int deviceCount = 0;
    void* deviceInfo = nullptr;
};

constexpr std::size_t kImvStringLength = 256;

struct ImvGigEDeviceInfo
{
    unsigned int ipConfigOptions = 0;
    unsigned int currentIpConfig = 0;
    unsigned int reserved[3]{};
    char macAddress[kImvStringLength]{};
    char ipAddress[kImvStringLength]{};
    char subnetMask[kImvStringLength]{};
    char defaultGateway[kImvStringLength]{};
    char protocolVersion[kImvStringLength]{};
    char ipConfiguration[kImvStringLength]{};
    char stringReserved[6][kImvStringLength]{};
};

struct ImvUsbDeviceInfo
{
    bool speedFlags[8]{};
    unsigned int reserved[4]{};
    char configurationValid[kImvStringLength]{};
    char genCpVersion[kImvStringLength]{};
    char u3vVersion[kImvStringLength]{};
    char deviceGuid[kImvStringLength]{};
    char familyName[kImvStringLength]{};
    char u3vSerialNumber[kImvStringLength]{};
    char speed[kImvStringLength]{};
    char maxPower[kImvStringLength]{};
    char stringReserved[4][kImvStringLength]{};
};

struct ImvGigEInterfaceInfo
{
    char values[10][kImvStringLength]{};
};

struct ImvUsbInterfaceInfo
{
    char values[10][kImvStringLength]{};
};

struct ImvDeviceInfo
{
    int cameraType = 255;
    int cameraReserved[5]{};
    char cameraKey[kImvStringLength]{};
    char cameraName[kImvStringLength]{};
    char serialNumber[kImvStringLength]{};
    char vendorName[kImvStringLength]{};
    char modelName[kImvStringLength]{};
    char manufactureInfo[kImvStringLength]{};
    char deviceVersion[kImvStringLength]{};
    char stringReserved[5][kImvStringLength]{};
    union DeviceSpecific
    {
        ImvGigEDeviceInfo gigE;
        ImvUsbDeviceInfo usb;
    } deviceSpecific{};
    int interfaceType = -1;
    int interfaceReserved[5]{};
    char interfaceName[kImvStringLength]{};
    char interfaceStringReserved[5][kImvStringLength]{};
    union InterfaceSpecific
    {
        ImvGigEInterfaceInfo gigE;
        ImvUsbInterfaceInfo usb;
    } interfaceSpecific{};
};

struct ImvFrameInfo
{
    std::uint64_t blockId = 0;
    unsigned int status = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int size = 0;
    std::int32_t pixelFormat = 0;
    std::uint64_t timestamp = 0;
    unsigned int chunkCount = 0;
    unsigned int paddingX = 0;
    unsigned int paddingY = 0;
    unsigned int receiveFrameTime = 0;
    unsigned int reserved[19]{};
};

struct ImvFrame
{
    ImvFrameHandle frameHandle = nullptr;
    unsigned char* data = nullptr;
    ImvFrameInfo frameInfo;
    unsigned int reserved[10]{};
};

struct ImvPixelConvertParam
{
    unsigned int width = 0;
    unsigned int height = 0;
    std::int32_t pixelFormat = 0;
    unsigned char* sourceData = nullptr;
    unsigned int sourceDataLength = 0;
    unsigned int paddingX = 0;
    unsigned int paddingY = 0;
    std::int32_t bayerDemosaic = kDemosaicNearestNeighbor;
    std::int32_t destinationPixelFormat = kPixelBgr8;
    unsigned char* destinationBuffer = nullptr;
    unsigned int destinationBufferSize = 0;
    unsigned int destinationDataLength = 0;
    unsigned int reserved[8]{};
};

static_assert(sizeof(ImvDeviceList) == 16);
static_assert(sizeof(ImvGigEDeviceInfo) == 3092);
static_assert(sizeof(ImvUsbDeviceInfo) == 3096);
static_assert(sizeof(ImvDeviceInfo) == 10312);
static_assert(offsetof(ImvFrameInfo, timestamp) == 32);
static_assert(sizeof(ImvFrameInfo) == 136);
static_assert(sizeof(ImvFrame) == 192);
static_assert(offsetof(ImvPixelConvertParam, sourceData) == 16);
static_assert(offsetof(ImvPixelConvertParam, destinationBuffer) == 48);
static_assert(sizeof(ImvPixelConvertParam) == 96);

std::string ImvError(int code)
{
    std::ostringstream text;
    text << "IMV error " << code << " (0x" << std::uppercase << std::hex
         << static_cast<std::uint32_t>(code) << ')';
    return text.str();
}

std::string WindowsError(DWORD code)
{
    std::ostringstream text;
    text << "Windows error " << code;
    return text.str();
}

std::string Trim(std::string value)
{
    const auto isSpace = [](unsigned char ch)
    {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
        [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
        [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }).base(), value.end());
    return value;
}

std::string VendorString(const char* value, std::size_t capacity)
{
    if (!value || capacity == 0)
        return {};
    const char* end = static_cast<const char*>(std::memchr(value, 0, capacity));
    return Trim(std::string(value, end ? static_cast<std::size_t>(end - value) : capacity));
}

bool StartsWith(const std::string& value, const char* prefix)
{
    const std::size_t length = std::strlen(prefix);
    return value.size() >= length && value.compare(0, length, prefix) == 0;
}

bool LooksLikeIpAddress(const std::string& value)
{
    int dots = 0;
    for (char ch : value)
    {
        if (ch == '.')
            ++dots;
        else if (ch < '0' || ch > '9')
            return false;
    }
    return dots == 3;
}

std::filesystem::path ExecutableDirectory()
{
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
        static_cast<DWORD>(path.size()));
    return length > 0 && length < path.size()
        ? std::filesystem::path(std::wstring(path.data(), length)).parent_path()
        : std::filesystem::current_path();
}

std::filesystem::path EnvironmentPath(const wchar_t* name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required <= 1)
        return {};
    std::vector<wchar_t> value(required);
    if (GetEnvironmentVariableW(name, value.data(), required) == 0)
        return {};
    return std::filesystem::path(value.data());
}

void AddRuntimeCandidate(std::vector<std::filesystem::path>& candidates,
    const std::filesystem::path& value)
{
    if (value.empty())
        return;
    if (value.filename() == L"MVSDKmd.dll")
        candidates.push_back(value);
    else
    {
        candidates.push_back(value / L"MVSDKmd.dll");
        candidates.push_back(value / L"Runtime" / L"x64" / L"MVSDKmd.dll");
    }
}

std::vector<std::filesystem::path> ImvRuntimeCandidates()
{
    std::vector<std::filesystem::path> candidates;
    AddRuntimeCandidate(candidates, ExecutableDirectory());
    for (const wchar_t* variable : {L"IMV_RUNTIME", L"MV_VIEWER_HOME", L"MVSDK_PATH"})
        AddRuntimeCandidate(candidates, EnvironmentPath(variable));

    AddRuntimeCandidate(candidates, L"F:\\MV Viewer\\Runtime\\x64");
    AddRuntimeCandidate(candidates, L"C:\\Program Files\\MV Viewer\\Runtime\\x64");
    AddRuntimeCandidate(candidates, L"C:\\Program Files (x86)\\MV Viewer\\Runtime\\x64");
    AddRuntimeCandidate(candidates, L"C:\\Program Files\\iRAYPLE\\MV Viewer\\Runtime\\x64");
    AddRuntimeCandidate(candidates, L"C:\\Program Files (x86)\\iRAYPLE\\MV Viewer\\Runtime\\x64");
    return candidates;
}

class ImvApi
{
public:
    using GetVersionFn = const char*(WINAPI*)();
    using EnumDevicesFn = int(WINAPI*)(ImvDeviceList*, unsigned int);
    using EnumDevicesByUnicastFn = int(WINAPI*)(ImvDeviceList*, const char*);
    using CreateHandleFn = int(WINAPI*)(ImvHandle*, int, void*);
    using DestroyHandleFn = int(WINAPI*)(ImvHandle);
    using OpenFn = int(WINAPI*)(ImvHandle);
    using CloseFn = int(WINAPI*)(ImvHandle);
    using StartGrabbingFn = int(WINAPI*)(ImvHandle);
    using StopGrabbingFn = int(WINAPI*)(ImvHandle);
    using GetFrameFn = int(WINAPI*)(ImvHandle, ImvFrame*, unsigned int);
    using ReleaseFrameFn = int(WINAPI*)(ImvHandle, ImvFrame*);
    using PixelConvertFn = int(WINAPI*)(ImvHandle, ImvPixelConvertParam*);
    using SetEnumSymbolFn = int(WINAPI*)(ImvHandle, const char*, const char*);
    using SetDoubleFn = int(WINAPI*)(ImvHandle, const char*, double);
    using ExecuteCommandFn = int(WINAPI*)(ImvHandle, const char*);
    using ClearFrameBufferFn = int(WINAPI*)(ImvHandle);
    using ForceIpAddressFn = int(WINAPI*)(ImvHandle, const char*, const char*, const char*);
    using GetAccessPermissionFn = int(WINAPI*)(ImvHandle, int*);

    ~ImvApi()
    {
        if (module_)
            FreeLibrary(module_);
    }

    DeviceOperationResult Load()
    {
        if (module_)
            return {true, {}};

        DWORD lastLoadError = ERROR_MOD_NOT_FOUND;
        for (const std::filesystem::path& path : ImvRuntimeCandidates())
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error))
                continue;
            std::array<wchar_t, 32768> previousDllDirectory{};
            const DWORD previousLength = GetDllDirectoryW(
                static_cast<DWORD>(previousDllDirectory.size()),
                previousDllDirectory.data());
            SetDllDirectoryW(path.parent_path().c_str());
            module_ = LoadLibraryW(path.c_str());
            SetDllDirectoryW(previousLength > 0 ? previousDllDirectory.data() : nullptr);
            if (module_)
            {
                loadedPath_ = path.string();
                break;
            }
            lastLoadError = GetLastError();
        }
        if (!module_)
        {
            module_ = LoadLibraryW(L"MVSDKmd.dll");
            if (module_)
                loadedPath_ = "MVSDKmd.dll (PATH)";
            else
                lastLoadError = GetLastError();
        }
        if (!module_)
        {
            return {false,
                "华睿 iRAYPLE 运行库未找到或依赖不完整（" +
                WindowsError(lastLoadError) +
                "）。请安装 MV Viewer x64，或将 Runtime\\x64 的全部文件放到程序目录。"};
        }

        getVersion = Symbol<GetVersionFn>("IMV_GetVersion");
        enumDevices = Symbol<EnumDevicesFn>("IMV_EnumDevices");
        enumDevicesByUnicast = Symbol<EnumDevicesByUnicastFn>("IMV_EnumDevicesByUnicast");
        createHandle = Symbol<CreateHandleFn>("IMV_CreateHandle");
        destroyHandle = Symbol<DestroyHandleFn>("IMV_DestroyHandle");
        open = Symbol<OpenFn>("IMV_Open");
        close = Symbol<CloseFn>("IMV_Close");
        startGrabbing = Symbol<StartGrabbingFn>("IMV_StartGrabbing");
        stopGrabbing = Symbol<StopGrabbingFn>("IMV_StopGrabbing");
        getFrame = Symbol<GetFrameFn>("IMV_GetFrame");
        releaseFrame = Symbol<ReleaseFrameFn>("IMV_ReleaseFrame");
        pixelConvert = Symbol<PixelConvertFn>("IMV_PixelConvert");
        setEnumSymbol = Symbol<SetEnumSymbolFn>("IMV_SetEnumFeatureSymbol");
        setDouble = Symbol<SetDoubleFn>("IMV_SetDoubleFeatureValue");
        executeCommand = Symbol<ExecuteCommandFn>("IMV_ExecuteCommandFeature");
        clearFrameBuffer = Symbol<ClearFrameBufferFn>("IMV_ClearFrameBuffer");
        forceIpAddress = Symbol<ForceIpAddressFn>("IMV_GIGE_ForceIpAddress");
        getAccessPermission = Symbol<GetAccessPermissionFn>("IMV_GIGE_GetAccessPermission");

        if (!enumDevices || !createHandle || !destroyHandle || !open || !close ||
            !startGrabbing || !stopGrabbing || !getFrame || !releaseFrame ||
            !pixelConvert || !setEnumSymbol || !setDouble || !executeCommand)
        {
            const std::string path = loadedPath_;
            FreeLibrary(module_);
            module_ = nullptr;
            return {false, "华睿 IMV 运行库接口不完整或版本不兼容: " + path};
        }
        return {true, "华睿 IMV 运行库已加载: " + loadedPath_};
    }

    template <typename Function>
    Function Symbol(const char* name) const
    {
        return reinterpret_cast<Function>(GetProcAddress(module_, name));
    }

    GetVersionFn getVersion = nullptr;
    EnumDevicesFn enumDevices = nullptr;
    EnumDevicesByUnicastFn enumDevicesByUnicast = nullptr;
    CreateHandleFn createHandle = nullptr;
    DestroyHandleFn destroyHandle = nullptr;
    OpenFn open = nullptr;
    CloseFn close = nullptr;
    StartGrabbingFn startGrabbing = nullptr;
    StopGrabbingFn stopGrabbing = nullptr;
    GetFrameFn getFrame = nullptr;
    ReleaseFrameFn releaseFrame = nullptr;
    PixelConvertFn pixelConvert = nullptr;
    SetEnumSymbolFn setEnumSymbol = nullptr;
    SetDoubleFn setDouble = nullptr;
    ExecuteCommandFn executeCommand = nullptr;
    ClearFrameBufferFn clearFrameBuffer = nullptr;
    ForceIpAddressFn forceIpAddress = nullptr;
    GetAccessPermissionFn getAccessPermission = nullptr;

    const std::string& LoadedPath() const { return loadedPath_; }
    std::string Version() const
    {
        const char* value = getVersion ? getVersion() : nullptr;
        return value ? value : "unknown";
    }

private:
    HMODULE module_ = nullptr;
    std::string loadedPath_;
};

struct ImvSelector
{
    int mode = kModeByIndex;
    unsigned int index = 0;
    std::string text;
    std::string description;
};

DeviceOperationResult ResolveSelector(ImvApi& api, const std::string& rawSelector,
    ImvSelector& selector)
{
    std::string value = Trim(rawSelector);
    if (value.empty() || value == "*")
        value = "0";

    std::string unicastIp;
    if (StartsWith(value, "ip:"))
        unicastIp = value.substr(3);
    else if (LooksLikeIpAddress(value))
        unicastIp = value;

    ImvDeviceList devices{};
    const int enumCode = !unicastIp.empty() && api.enumDevicesByUnicast
        ? api.enumDevicesByUnicast(&devices, unicastIp.c_str())
        : api.enumDevices(&devices, kInterfaceAll);
    if (enumCode != kImvOk)
        return {false, "枚举华睿相机失败: " + ImvError(enumCode)};
    if (devices.deviceCount == 0)
        return {false, "华睿 IMV SDK 未发现相机，请检查网卡网段、相机供电和 MV Viewer 占用状态。"};

    unsigned int index = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), index);
    if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size())
    {
        if (index >= devices.deviceCount)
        {
            return {false, "华睿相机序号 " + std::to_string(index) +
                " 不存在，当前发现 " + std::to_string(devices.deviceCount) + " 台设备。"};
        }
        selector.mode = kModeByIndex;
        selector.index = index;
        selector.description = "index=" + std::to_string(index);
        return {true, selector.description};
    }

    if (StartsWith(value, "key:"))
    {
        selector.mode = kModeByCameraKey;
        selector.text = value.substr(4);
        selector.description = "cameraKey=" + selector.text;
    }
    else if (StartsWith(value, "user:"))
    {
        selector.mode = kModeByDeviceUserId;
        selector.text = value.substr(5);
        selector.description = "userId=" + selector.text;
    }
    else if (StartsWith(value, "ip:"))
    {
        selector.mode = kModeByIpAddress;
        selector.text = value.substr(3);
        selector.description = "IP=" + selector.text;
    }
    else if (LooksLikeIpAddress(value))
    {
        selector.mode = kModeByIpAddress;
        selector.text = value;
        selector.description = "IP=" + selector.text;
    }
    else
    {
        selector.mode = kModeByDeviceUserId;
        selector.text = value;
        selector.description = "userId=" + selector.text;
    }

    if (selector.text.empty())
        return {false, "华睿相机标识为空"};
    return {true, selector.description};
}

std::uint64_t ReceivedTimestampNanoseconds()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

bool CheckedFrameLayout(unsigned int width, unsigned int height,
    unsigned int channels, unsigned int paddingX,
    std::size_t& stride, std::size_t& requiredBytes)
{
    stride = 0;
    requiredBytes = 0;
    if (width == 0 || height == 0 || channels == 0 ||
        width > static_cast<unsigned int>((std::numeric_limits<int>::max)()) ||
        height > static_cast<unsigned int>((std::numeric_limits<int>::max)()))
    {
        return false;
    }

    const std::size_t widthValue = width;
    const std::size_t channelsValue = channels;
    if (widthValue > ((std::numeric_limits<std::size_t>::max)() - paddingX) /
            channelsValue)
    {
        return false;
    }
    stride = widthValue * channelsValue + paddingX;
    if (stride == 0 || static_cast<std::size_t>(height) >
        (std::numeric_limits<std::size_t>::max)() / stride)
    {
        return false;
    }
    requiredBytes = stride * static_cast<std::size_t>(height);
    return requiredBytes <= (std::numeric_limits<unsigned int>::max)();
}
}

struct HuarayImvCameraAdapter::Impl
{
    ImvApi api;
    ImvHandle handle = nullptr;
    std::vector<unsigned char> convertedFrame;
    std::string selectedDevice;
};

HuarayImvCameraAdapter::HuarayImvCameraAdapter()
    : impl_(std::make_unique<Impl>())
{
}

HuarayImvCameraAdapter::~HuarayImvCameraAdapter()
{
    Disconnect();
}

const char* HuarayImvCameraAdapter::AdapterName() const
{
    return "Huaray iRAYPLE IMV Camera";
}

DeviceOperationResult HuarayImvCameraAdapter::Connect(const DeviceEndpoint& endpoint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisconnectUnlocked();
    state_ = DeviceConnectionState::Connecting;

    DeviceOperationResult result = impl_->api.Load();
    if (!result.success)
        return Fail(std::move(result.message), true);

    ImvSelector selector;
    result = ResolveSelector(impl_->api, endpoint.address, selector);
    if (!result.success)
        return Fail(std::move(result.message), true);

    void* identifier = selector.mode == kModeByIndex
        ? static_cast<void*>(&selector.index)
        : static_cast<void*>(selector.text.data());
    int code = impl_->api.createHandle(&impl_->handle, selector.mode, identifier);
    if (code != kImvOk || !impl_->handle)
        return Fail("创建华睿相机句柄失败: " + ImvError(code), true);

    code = impl_->api.open(impl_->handle);
    if (code != kImvOk)
    {
        impl_->api.destroyHandle(impl_->handle);
        impl_->handle = nullptr;
        return Fail("打开华睿相机失败（请先关闭 MV Viewer 对该相机的控制连接）: " +
            ImvError(code), true);
    }

    // Always start from continuous acquisition. Trigger mode can be changed
    // explicitly later through ConfigureTrigger.
    code = impl_->api.setEnumSymbol(impl_->handle, "TriggerMode", "Off");
    if (code != kImvOk)
    {
        DisconnectUnlocked();
        return Fail("设置华睿相机连续采集模式失败: " + ImvError(code), true);
    }

    impl_->selectedDevice = selector.description;
    statistics_ = {};
    lastVendorFrameNumber_ = 0;
    triggerConfig_ = {};
    lastError_.clear();
    state_ = DeviceConnectionState::Connected;

    std::string version;
    if (impl_->api.getVersion)
    {
        const char* value = impl_->api.getVersion();
        if (value)
            version = std::string(" SDK=") + value;
    }
    return {true, "华睿 iRAYPLE 相机已连接: " + selector.description + version};
}

void HuarayImvCameraAdapter::Disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisconnectUnlocked();
}

void HuarayImvCameraAdapter::DisconnectUnlocked()
{
    if (impl_ && impl_->handle)
    {
        if (streaming_)
            impl_->api.stopGrabbing(impl_->handle);
        impl_->api.close(impl_->handle);
        impl_->api.destroyHandle(impl_->handle);
        impl_->handle = nullptr;
    }
    streaming_ = false;
    if (impl_)
    {
        impl_->convertedFrame.clear();
        impl_->selectedDevice.clear();
    }
    state_ = DeviceConnectionState::Disconnected;
}

DeviceConnectionState HuarayImvCameraAdapter::ConnectionState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string HuarayImvCameraAdapter::LastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

DeviceOperationResult HuarayImvCameraAdapter::GrabFrame(cv::Mat& frame, int timeoutMs)
{
    CameraFrameMetadata metadata;
    return GrabFrame(frame, metadata, timeoutMs);
}

DeviceOperationResult HuarayImvCameraAdapter::GrabFrame(cv::Mat& frame,
    CameraFrameMetadata& metadata, int timeoutMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    frame.release();
    metadata = {};
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("华睿相机未连接");
    if (!streaming_)
        return Fail("华睿相机尚未开始取流");

    ImvFrame vendorFrame{};
    const int getCode = impl_->api.getFrame(impl_->handle, &vendorFrame,
        static_cast<unsigned int>((std::max)(1, timeoutMs)));
    if (getCode != kImvOk)
        return Fail("华睿相机取帧失败: " + ImvError(getCode));
    const ImvFrameInfo frameInfo = vendorFrame.frameInfo;
    const CameraPixelFormatDescription pixelFormat = DescribeCameraPixelFormat(
        static_cast<std::uint32_t>(frameInfo.pixelFormat));
    metadata.sourcePixelFormat = static_cast<std::uint32_t>(frameInfo.pixelFormat);
    metadata.sourcePixelFormatName = pixelFormat.name;
    metadata.sourceBitDepth = pixelFormat.bitDepth;
    metadata.sourceStorageBitsPerPixel = pixelFormat.storageBitsPerPixel;
    metadata.sourceIsBayer = pixelFormat.bayer;

    DeviceOperationResult result{true, "华睿相机取帧成功"};
    if (!vendorFrame.data || vendorFrame.frameInfo.width == 0 ||
        vendorFrame.frameInfo.height == 0 || vendorFrame.frameInfo.status != 0)
    {
        ++statistics_.incompleteFrames;
        result = {false, "华睿相机返回了不完整图像，frame status=" +
            std::to_string(vendorFrame.frameInfo.status)};
    }
    else
    {
        try
        {
            const int width = static_cast<int>(vendorFrame.frameInfo.width);
            const int height = static_cast<int>(vendorFrame.frameInfo.height);
            std::size_t stride = 0;
            std::size_t requiredBytes = 0;

            if (vendorFrame.frameInfo.pixelFormat == kPixelMono8)
            {
                if (!CheckedFrameLayout(vendorFrame.frameInfo.width,
                        vendorFrame.frameInfo.height, 1,
                        vendorFrame.frameInfo.paddingX, stride, requiredBytes) ||
                    requiredBytes > vendorFrame.frameInfo.size)
                {
                    result = {false, "华睿 Mono8 帧尺寸或缓存长度无效"};
                }
                else
                {
                    frame = cv::Mat(height, width, CV_8UC1,
                        vendorFrame.data, stride).clone();
                    metadata.conversionPath = "native Mono8";
                }
            }
            else if (vendorFrame.frameInfo.pixelFormat == kPixelBgr8 ||
                vendorFrame.frameInfo.pixelFormat == kPixelRgb8)
            {
                if (!CheckedFrameLayout(vendorFrame.frameInfo.width,
                        vendorFrame.frameInfo.height, 3,
                        vendorFrame.frameInfo.paddingX, stride, requiredBytes) ||
                    requiredBytes > vendorFrame.frameInfo.size)
                {
                    result = {false, "华睿 RGB/BGR 帧尺寸或缓存长度无效"};
                }
                else if (vendorFrame.frameInfo.pixelFormat == kPixelBgr8)
                {
                    frame = cv::Mat(height, width, CV_8UC3,
                        vendorFrame.data, stride).clone();
                    metadata.conversionPath = "native BGR8";
                }
                else
                {
                    cv::Mat rgb(height, width, CV_8UC3,
                        vendorFrame.data, stride);
                    cv::cvtColor(rgb, frame, cv::COLOR_RGB2BGR);
                    metadata.convertedToDisplay = true;
                    metadata.conversionPath = "OpenCV RGB8 -> BGR8";
                }
            }
            else
            {
                std::size_t destinationStride = 0;
                std::size_t destinationSize = 0;
                if (vendorFrame.frameInfo.size == 0 ||
                    !CheckedFrameLayout(vendorFrame.frameInfo.width,
                        vendorFrame.frameInfo.height, 3, 0,
                        destinationStride, destinationSize))
                {
                    result = {false, "华睿像素转换目标尺寸无效"};
                }
                else
                {
                    impl_->convertedFrame.resize(destinationSize);
                    ImvPixelConvertParam conversion{};
                    conversion.width = vendorFrame.frameInfo.width;
                    conversion.height = vendorFrame.frameInfo.height;
                    conversion.pixelFormat = vendorFrame.frameInfo.pixelFormat;
                    conversion.sourceData = vendorFrame.data;
                    conversion.sourceDataLength = vendorFrame.frameInfo.size;
                    conversion.paddingX = vendorFrame.frameInfo.paddingX;
                    conversion.paddingY = vendorFrame.frameInfo.paddingY;
                    conversion.bayerDemosaic = kDemosaicNearestNeighbor;
                    conversion.destinationPixelFormat = kPixelBgr8;
                    conversion.destinationBuffer = impl_->convertedFrame.data();
                    conversion.destinationBufferSize =
                        static_cast<unsigned int>(destinationSize);
                    const int convertCode = impl_->api.pixelConvert(
                        impl_->handle, &conversion);
                    if (convertCode != kImvOk ||
                        conversion.destinationDataLength > destinationSize)
                    {
                        result = {false,
                            "华睿相机像素格式转换失败: " + ImvError(convertCode)};
                    }
                    else
                    {
                        frame = cv::Mat(height, width, CV_8UC3,
                            impl_->convertedFrame.data(), destinationStride).clone();
                        metadata.convertedToDisplay = true;
                        metadata.conversionPath = "IMV_PixelConvert -> BGR8";
                    }
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            result = {false, "华睿相机帧内存分配失败"};
        }
        catch (const cv::Exception& exception)
        {
            result = {false,
                std::string("华睿相机图像转换异常: ") + exception.what()};
        }
        catch (const std::exception& exception)
        {
            result = {false,
                std::string("华睿相机帧处理异常: ") + exception.what()};
        }
        catch (...)
        {
            result = {false, "华睿相机帧处理发生未知异常"};
        }
    }

    // The IMV SDK owns the frame buffer. Every successful GetFrame must be
    // paired with ReleaseFrame, including conversion and validation failures.
    const int releaseCode = impl_->api.releaseFrame(impl_->handle, &vendorFrame);
    if (releaseCode != kImvOk)
    {
        frame.release();
        return Fail("释放华睿相机帧缓存失败: " + ImvError(releaseCode), true);
    }
    if (!result.success)
        return Fail(std::move(result.message));

    metadata.frameNumber = frameInfo.blockId;
    metadata.hardwareTimestampNanoseconds = frameInfo.timestamp;
    metadata.receivedTimestampNanoseconds = ReceivedTimestampNanoseconds();
    metadata.exposureComplete = true;
    if (lastVendorFrameNumber_ > 0 && metadata.frameNumber > lastVendorFrameNumber_ + 1)
        statistics_.droppedFrames += metadata.frameNumber - lastVendorFrameNumber_ - 1;
    lastVendorFrameNumber_ = metadata.frameNumber;
    metadata.droppedFrames = statistics_.droppedFrames;
    ++statistics_.receivedFrames;
    lastError_.clear();
    return result;
}

DeviceOperationResult HuarayImvCameraAdapter::StartStream()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("华睿相机未连接");
    if (streaming_)
        return {true, "华睿相机已在取流"};
    const int code = impl_->api.startGrabbing(impl_->handle);
    if (code != kImvOk)
        return Fail("启动华睿相机取流失败: " + ImvError(code), true);
    streaming_ = true;
    lastError_.clear();
    return {true, "华睿相机取流已启动"};
}

void HuarayImvCameraAdapter::StopStream()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (impl_->handle && streaming_)
        impl_->api.stopGrabbing(impl_->handle);
    streaming_ = false;
}

DeviceOperationResult HuarayImvCameraAdapter::SetControl(CameraControl control, double value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("华睿相机未连接");

    int code = kImvOk;
    switch (control)
    {
    case CameraControl::AutoExposure:
        code = impl_->api.setEnumSymbol(impl_->handle, "ExposureAuto",
            value > 0.5 ? "Continuous" : "Off");
        break;
    case CameraControl::Exposure:
        code = impl_->api.setDouble(impl_->handle, "ExposureTime", value);
        break;
    case CameraControl::Gain:
        code = impl_->api.setDouble(impl_->handle, "Gain", value);
        if (code != kImvOk)
            code = impl_->api.setDouble(impl_->handle, "GainRaw", value);
        break;
    }
    if (code != kImvOk)
        return Fail("设置华睿相机参数失败: " + ImvError(code));
    lastError_.clear();
    return {true, "华睿相机参数已更新"};
}

CameraCapabilities HuarayImvCameraAdapter::Capabilities() const
{
    CameraCapabilities capabilities;
    capabilities.softwareTrigger = true;
    capabilities.hardwareTrigger = true;
    capabilities.hardwareTimestamp = true;
    capabilities.exposureCompletion = true;
    capabilities.queueControl = true;
    capabilities.ptp = true;
    return capabilities;
}

DeviceOperationResult HuarayImvCameraAdapter::ConfigurePtp(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("Huaray camera is not connected.");
    int code = impl_->api.setEnumSymbol(impl_->handle, "GevIEEE1588",
        enabled ? "On" : "Off");
    if (code != kImvOk)
        code = impl_->api.setEnumSymbol(impl_->handle, "PtpEnable",
            enabled ? "On" : "Off");
    if (code != kImvOk)
        return Fail("Could not configure Huaray PTP: " + ImvError(code));
    return {true, enabled ? "Huaray PTP enabled." : "Huaray PTP disabled."};
}

DeviceOperationResult HuarayImvCameraAdapter::ConfigureTrigger(
    const CameraTriggerConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("华睿相机未连接");

    int code = kImvOk;
    if (config.mode == CameraTriggerMode::Continuous)
    {
        code = impl_->api.setEnumSymbol(impl_->handle, "TriggerMode", "Off");
    }
    else
    {
        code = impl_->api.setEnumSymbol(impl_->handle, "TriggerSelector", "FrameStart");
        if (code == kImvOk)
        {
            const char* source = config.mode == CameraTriggerMode::Software
                ? "Software"
                : (config.mode == CameraTriggerMode::HardwareLine2 ? "Line2" : "Line1");
            code = impl_->api.setEnumSymbol(impl_->handle, "TriggerSource", source);
        }
        if (code == kImvOk)
            code = impl_->api.setEnumSymbol(impl_->handle, "TriggerMode", "On");
        if (code == kImvOk && config.delayMicroseconds > 0.0)
            code = impl_->api.setDouble(impl_->handle, "TriggerDelay", config.delayMicroseconds);
    }
    if (code != kImvOk)
        return Fail("配置华睿相机触发模式失败: " + ImvError(code));
    triggerConfig_ = config;
    lastError_.clear();
    return {true, "华睿相机触发模式已更新"};
}

DeviceOperationResult HuarayImvCameraAdapter::ExecuteSoftwareTrigger()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("华睿相机未连接");
    if (triggerConfig_.mode != CameraTriggerMode::Software)
        return Fail("华睿相机当前不是软件触发模式");
    const int code = impl_->api.executeCommand(impl_->handle, "TriggerSoftware");
    if (code != kImvOk)
        return Fail("执行华睿相机软件触发失败: " + ImvError(code));
    lastError_.clear();
    return {true, "华睿相机软件触发已执行"};
}

DeviceOperationResult HuarayImvCameraAdapter::FlushQueue()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("华睿相机未连接");
    if (!impl_->api.clearFrameBuffer)
        return Fail("当前华睿 IMV 运行库不支持清空帧缓存");
    const int code = impl_->api.clearFrameBuffer(impl_->handle);
    if (code != kImvOk)
        return Fail("清空华睿相机帧缓存失败: " + ImvError(code));
    lastError_.clear();
    return {true, "华睿相机帧缓存已清空"};
}

CameraStatistics HuarayImvCameraAdapter::Statistics() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return statistics_;
}

DeviceOperationResult HuarayImvCameraAdapter::EnumerateDevices(
    std::vector<CameraDeviceInfo>& devices)
{
    std::lock_guard<std::mutex> lock(mutex_);
    devices.clear();
    DeviceOperationResult loaded = impl_->api.Load();
    if (!loaded.success)
        return loaded;

    ImvDeviceList list{};
    const int code = impl_->api.enumDevices(&list, kInterfaceAll);
    if (code != kImvOk)
        return {false, "枚举华睿相机失败: " + ImvError(code)};
    if (list.deviceCount > 0 && !list.deviceInfo)
        return {false, "华睿 IMV 返回了无效的设备列表"};

    const auto* sourceDevices = static_cast<const ImvDeviceInfo*>(list.deviceInfo);
    for (unsigned int index = 0; index < list.deviceCount; ++index)
    {
        const ImvDeviceInfo& source = sourceDevices[index];
        CameraDeviceInfo device;
        const std::string cameraKey = VendorString(source.cameraKey, kImvStringLength);
        device.model = VendorString(source.modelName, kImvStringLength);
        device.serialNumber = VendorString(source.serialNumber, kImvStringLength);
        device.userDefinedName = VendorString(source.cameraName, kImvStringLength);
        device.runtimePath = impl_->api.LoadedPath();
        device.runtimeVersion = impl_->api.Version();
        device.transport = source.cameraType == 0 ? "GigE" :
            (source.cameraType == 1 ? "USB3" :
            (source.cameraType == 2 ? "CameraLink" :
            (source.cameraType == 3 ? "PCIe" : "Unknown")));
        if (source.cameraType == 0)
        {
            const ImvGigEDeviceInfo& gigE = source.deviceSpecific.gigE;
            device.ipAddress = VendorString(gigE.ipAddress, kImvStringLength);
            device.macAddress = VendorString(gigE.macAddress, kImvStringLength);
        }
        device.selector = !cameraKey.empty() ? "key:" + cameraKey :
            (!device.ipAddress.empty() ? "ip:" + device.ipAddress :
            std::to_string(index));

        device.accessible = true;
        device.status = "available";
        if (source.cameraType == 0 && impl_->api.getAccessPermission &&
            !cameraKey.empty())
        {
            ImvHandle probe = nullptr;
            std::string keyCopy = cameraKey;
            if (impl_->api.createHandle(&probe, kModeByCameraKey,
                keyCopy.data()) == kImvOk && probe)
            {
                int permission = 254;
                if (impl_->api.getAccessPermission(probe, &permission) == kImvOk)
                {
                    device.accessible = permission == 0;
                    device.status = device.accessible ? "available" : "occupied";
                }
                impl_->api.destroyHandle(probe);
            }
        }
        devices.push_back(std::move(device));
    }
    return {true, devices.empty()
        ? "华睿 IMV 运行库已加载，但未发现相机"
        : "华睿相机扫描完成"};
}

DeviceOperationResult HuarayImvCameraAdapter::ForceIp(
    const std::string& selectorText, const std::string& ipAddress,
    const std::string& subnetMask, const std::string& defaultGateway)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceOperationResult loaded = impl_->api.Load();
    if (!loaded.success)
        return loaded;
    if (!impl_->api.forceIpAddress)
        return {false, "当前华睿 IMV 运行库不支持 IMV_GIGE_ForceIpAddress"};
    if (!LooksLikeIpAddress(ipAddress) || !LooksLikeIpAddress(subnetMask) ||
        !LooksLikeIpAddress(defaultGateway))
        return {false, "ForceIP 需要有效的 IPv4 地址、子网掩码和默认网关"};

    ImvSelector selector;
    DeviceOperationResult resolved = ResolveSelector(impl_->api, selectorText, selector);
    if (!resolved.success)
        return resolved;
    void* identifier = selector.mode == kModeByIndex
        ? static_cast<void*>(&selector.index)
        : static_cast<void*>(selector.text.data());
    ImvHandle handle = nullptr;
    int code = impl_->api.createHandle(&handle, selector.mode, identifier);
    if (code != kImvOk || !handle)
        return {false, "创建华睿 ForceIP 句柄失败: " + ImvError(code)};
    code = impl_->api.forceIpAddress(handle, ipAddress.c_str(),
        subnetMask.c_str(), defaultGateway.c_str());
    impl_->api.destroyHandle(handle);
    if (code != kImvOk)
        return {false, "华睿 ForceIP 失败: " + ImvError(code)};
    return {true, "华睿 ForceIP 已完成，请重新扫描设备后再连接"};
}

DeviceOperationResult HuarayImvCameraAdapter::Fail(std::string message, bool fault)
{
    lastError_ = std::move(message);
    if (fault)
        state_ = DeviceConnectionState::Fault;
    return {false, lastError_};
}
