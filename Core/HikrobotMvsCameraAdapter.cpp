#include "HikrobotMvsCameraAdapter.h"
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
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
constexpr int kMvsOk = 0;
constexpr unsigned int kMvsGigEDevice = 0x00000001U;
constexpr unsigned int kMvsUsbDevice = 0x00000004U;
constexpr unsigned int kMvsExclusiveAccess = 1U;
constexpr std::size_t kMvsMaxDeviceCount = 256;

constexpr std::uint32_t kPixelMono8 = 0x01080001U;
constexpr std::uint32_t kPixelBayerGr8 = 0x01080008U;
constexpr std::uint32_t kPixelBayerRg8 = 0x01080009U;
constexpr std::uint32_t kPixelBayerGb8 = 0x0108000AU;
constexpr std::uint32_t kPixelBayerBg8 = 0x0108000BU;
constexpr std::uint32_t kPixelRgb8Packed = 0x02180014U;
constexpr std::uint32_t kPixelBgr8Packed = 0x02180015U;

struct MvsDeviceInfoList
{
    unsigned int deviceCount = 0;
    void* devices[kMvsMaxDeviceCount]{};
};

struct MvsGigEDeviceInfo
{
    std::uint32_t ipConfigOptions;
    std::uint32_t currentIpConfig;
    std::uint32_t currentIp;
    std::uint32_t currentSubnetMask;
    std::uint32_t defaultGateway;
    unsigned char manufacturerName[32];
    unsigned char modelName[32];
    unsigned char deviceVersion[32];
    unsigned char manufacturerSpecificInfo[48];
    unsigned char serialNumber[16];
    unsigned char userDefinedName[16];
    std::uint32_t networkInterfaceIp;
    std::uint32_t reserved[4];
};

struct MvsDeviceInfo
{
    std::uint16_t majorVersion;
    std::uint16_t minorVersion;
    std::uint32_t macAddressHigh;
    std::uint32_t macAddressLow;
    std::uint32_t transportLayerType;
    std::uint32_t reserved[4];
    union
    {
        MvsGigEDeviceInfo gigE;
        std::byte storage[512];
    } specialInfo;
};

struct MvsIntValue
{
    std::uint32_t currentValue = 0;
    std::uint32_t maximum = 0;
    std::uint32_t minimum = 0;
    std::uint32_t increment = 0;
    std::uint32_t reserved[4]{};
};

struct MvsPixelConvertParam
{
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t sourcePixelType = 0;
    unsigned char* sourceData = nullptr;
    std::uint32_t sourceDataLength = 0;
    std::uint32_t reserved0[3]{};
    std::uint32_t destinationPixelType = 0;
    unsigned char* destinationBuffer = nullptr;
    std::uint32_t destinationBufferSize = 0;
    std::uint32_t destinationDataLength = 0;
    std::uint32_t reserved1[4]{};
};

struct MvsFrameInfoPrefix
{
    std::uint16_t width;
    std::uint16_t height;
    std::uint32_t pixelType;
    std::uint32_t frameNumber;
    std::uint32_t deviceTimestampHigh;
    std::uint32_t deviceTimestampLow;
    std::uint32_t reserved0;
    std::uint64_t hostTimestamp;
    std::uint32_t frameLength;
};

static_assert(offsetof(MvsDeviceInfo, specialInfo) == 32);
static_assert(offsetof(MvsFrameInfoPrefix, hostTimestamp) == 24);
static_assert(offsetof(MvsFrameInfoPrefix, frameLength) == 32);

std::string MvsError(int code)
{
    std::ostringstream text;
    text << "MVS error 0x" << std::uppercase << std::hex
         << static_cast<std::uint32_t>(code);
    return text.str();
}

std::string Trim(std::string value)
{
    const auto isSpace = [](unsigned char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
        [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
        [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }).base(), value.end());
    return value;
}

template <std::size_t Size>
std::string FixedString(const unsigned char (&value)[Size])
{
    const auto end = std::find(value, value + Size, static_cast<unsigned char>(0));
    return Trim(std::string(reinterpret_cast<const char*>(value),
        static_cast<std::size_t>(end - value)));
}

std::string IpAddress(std::uint32_t value)
{
    return std::to_string((value >> 24U) & 0xffU) + "." +
        std::to_string((value >> 16U) & 0xffU) + "." +
        std::to_string((value >> 8U) & 0xffU) + "." +
        std::to_string(value & 0xffU);
}

std::string MacAddress(const MvsDeviceInfo& info)
{
    const std::uint64_t value =
        (static_cast<std::uint64_t>(info.macAddressHigh & 0xffffU) << 32U) |
        info.macAddressLow;
    std::ostringstream text;
    text << std::uppercase << std::hex << std::setfill('0');
    for (int shift = 40; shift >= 0; shift -= 8)
    {
        if (shift != 40)
            text << ':';
        text << std::setw(2) << ((value >> shift) & 0xffU);
    }
    return text.str();
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

std::filesystem::path EnvironmentDirectory(const wchar_t* name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required <= 1)
        return {};
    std::vector<wchar_t> value(required);
    if (GetEnvironmentVariableW(name, value.data(), required) == 0)
        return {};
    return std::filesystem::path(value.data());
}

std::vector<std::filesystem::path> MvsRuntimeCandidates()
{
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(ExecutableDirectory() / L"MvCameraControl.dll");
    for (const wchar_t* variable : {L"MVCAM_COMMON_RUNENV", L"MVS_RUNTIME", L"MVSDK_PATH"})
    {
        const std::filesystem::path directory = EnvironmentDirectory(variable);
        if (!directory.empty())
            candidates.push_back(directory / L"MvCameraControl.dll");
    }
    candidates.emplace_back(
        L"C:\\Program Files (x86)\\Common Files\\MVS\\Runtime\\Win64_x64\\MvCameraControl.dll");
    candidates.emplace_back(
        L"C:\\Program Files\\Common Files\\MVS\\Runtime\\Win64_x64\\MvCameraControl.dll");
    candidates.emplace_back(
        L"C:\\Program Files (x86)\\MVS\\Runtime\\Win64_x64\\MvCameraControl.dll");
    candidates.emplace_back(
        L"C:\\Program Files\\MVS\\Runtime\\Win64_x64\\MvCameraControl.dll");
    return candidates;
}

class MvsApi
{
public:
    using EnumDevicesFn = int(WINAPI*)(unsigned int, MvsDeviceInfoList*);
    using CreateHandleFn = int(WINAPI*)(void**, const void*);
    using DestroyHandleFn = int(WINAPI*)(void*);
    using OpenDeviceFn = int(WINAPI*)(void*, unsigned int, std::uint16_t);
    using CloseDeviceFn = int(WINAPI*)(void*);
    using StartGrabbingFn = int(WINAPI*)(void*);
    using StopGrabbingFn = int(WINAPI*)(void*);
    using GetOneFrameTimeoutFn = int(WINAPI*)(void*, unsigned char*, unsigned int,
        void*, unsigned int);
    using GetIntValueFn = int(WINAPI*)(void*, const char*, MvsIntValue*);
    using SetIntValueFn = int(WINAPI*)(void*, const char*, std::uint32_t);
    using SetEnumValueFn = int(WINAPI*)(void*, const char*, std::uint32_t);
    using SetEnumValueByStringFn = int(WINAPI*)(void*, const char*, const char*);
    using SetFloatValueFn = int(WINAPI*)(void*, const char*, float);
    using SetCommandValueFn = int(WINAPI*)(void*, const char*);
    using GetOptimalPacketSizeFn = int(WINAPI*)(void*);
    using ConvertPixelTypeFn = int(WINAPI*)(void*, MvsPixelConvertParam*);
    using IsDeviceAccessibleFn = bool(WINAPI*)(const void*, unsigned int);
    using GetSdkVersionFn = unsigned int(WINAPI*)();
    using ForceIpExFn = int(WINAPI*)(void*, unsigned int, unsigned int, unsigned int);
    using SetGrabStrategyFn = int(WINAPI*)(void*, unsigned int);

    ~MvsApi()
    {
        if (module_)
            FreeLibrary(module_);
    }

    DeviceOperationResult Load()
    {
        if (module_)
            return {true, {}};

        for (const std::filesystem::path& path : MvsRuntimeCandidates())
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error))
                continue;
            module_ = LoadLibraryExW(path.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (module_)
            {
                loadedPath_ = path.string();
                break;
            }
        }
        if (!module_)
        {
            module_ = LoadLibraryW(L"MvCameraControl.dll");
            if (module_)
                loadedPath_ = "MvCameraControl.dll (PATH)";
        }
        if (!module_)
        {
            return {false,
                "Hikrobot MVS runtime was not found. Install the x64 MVS SDK/runtime "
                "or copy MvCameraControl.dll and its dependencies beside the application."};
        }

        enumDevices = Symbol<EnumDevicesFn>("MV_CC_EnumDevices");
        createHandle = Symbol<CreateHandleFn>("MV_CC_CreateHandle");
        destroyHandle = Symbol<DestroyHandleFn>("MV_CC_DestroyHandle");
        openDevice = Symbol<OpenDeviceFn>("MV_CC_OpenDevice");
        closeDevice = Symbol<CloseDeviceFn>("MV_CC_CloseDevice");
        startGrabbing = Symbol<StartGrabbingFn>("MV_CC_StartGrabbing");
        stopGrabbing = Symbol<StopGrabbingFn>("MV_CC_StopGrabbing");
        getOneFrameTimeout = Symbol<GetOneFrameTimeoutFn>("MV_CC_GetOneFrameTimeout");
        getIntValue = Symbol<GetIntValueFn>("MV_CC_GetIntValue");
        setIntValue = Symbol<SetIntValueFn>("MV_CC_SetIntValue");
        setEnumValue = Symbol<SetEnumValueFn>("MV_CC_SetEnumValue");
        setEnumValueByString = Symbol<SetEnumValueByStringFn>("MV_CC_SetEnumValueByString");
        setFloatValue = Symbol<SetFloatValueFn>("MV_CC_SetFloatValue");
        setCommandValue = Symbol<SetCommandValueFn>("MV_CC_SetCommandValue");
        getOptimalPacketSize = Symbol<GetOptimalPacketSizeFn>("MV_CC_GetOptimalPacketSize");
        convertPixelType = Symbol<ConvertPixelTypeFn>("MV_CC_ConvertPixelType");
        isDeviceAccessible = Symbol<IsDeviceAccessibleFn>("MV_CC_IsDeviceAccessible");
        getSdkVersion = Symbol<GetSdkVersionFn>("MV_CC_GetSDKVersion");
        forceIpEx = Symbol<ForceIpExFn>("MV_CC_ForceIpEx");
        setGrabStrategy = Symbol<SetGrabStrategyFn>("MV_CC_SetGrabStrategy");

        if (!enumDevices || !createHandle || !destroyHandle || !openDevice ||
            !closeDevice || !startGrabbing || !stopGrabbing || !getOneFrameTimeout ||
            !getIntValue || !setEnumValue || !setFloatValue || !setCommandValue)
        {
            const std::string path = loadedPath_;
            FreeLibrary(module_);
            module_ = nullptr;
            return {false, "The MVS runtime is incompatible or incomplete: " + path};
        }
        return {true, "Hikrobot MVS runtime loaded: " + loadedPath_};
    }

    template <typename Function>
    Function Symbol(const char* name) const
    {
        return reinterpret_cast<Function>(GetProcAddress(module_, name));
    }

    EnumDevicesFn enumDevices = nullptr;
    CreateHandleFn createHandle = nullptr;
    DestroyHandleFn destroyHandle = nullptr;
    OpenDeviceFn openDevice = nullptr;
    CloseDeviceFn closeDevice = nullptr;
    StartGrabbingFn startGrabbing = nullptr;
    StopGrabbingFn stopGrabbing = nullptr;
    GetOneFrameTimeoutFn getOneFrameTimeout = nullptr;
    GetIntValueFn getIntValue = nullptr;
    SetIntValueFn setIntValue = nullptr;
    SetEnumValueFn setEnumValue = nullptr;
    SetEnumValueByStringFn setEnumValueByString = nullptr;
    SetFloatValueFn setFloatValue = nullptr;
    SetCommandValueFn setCommandValue = nullptr;
    GetOptimalPacketSizeFn getOptimalPacketSize = nullptr;
    ConvertPixelTypeFn convertPixelType = nullptr;
    IsDeviceAccessibleFn isDeviceAccessible = nullptr;
    GetSdkVersionFn getSdkVersion = nullptr;
    ForceIpExFn forceIpEx = nullptr;
    SetGrabStrategyFn setGrabStrategy = nullptr;

    const std::string& LoadedPath() const { return loadedPath_; }
    std::string SdkVersion() const
    {
        if (!getSdkVersion)
            return "unknown";
        const std::uint32_t version = getSdkVersion();
        std::ostringstream text;
        text << ((version >> 24U) & 0xffU) << '.'
             << ((version >> 16U) & 0xffU) << '.'
             << ((version >> 8U) & 0xffU) << '.'
             << (version & 0xffU) << " (0x" << std::uppercase << std::hex
             << std::setw(8) << std::setfill('0') << version << ')';
        return text.str();
    }

private:
    HMODULE module_ = nullptr;
    std::string loadedPath_;
};

struct SelectedMvsDevice
{
    void* info = nullptr;
    std::string description;
    bool gigE = false;
};

std::string DeviceDescription(const MvsDeviceInfo& info, std::size_t index)
{
    std::ostringstream description;
    description << '#' << index;
    if ((info.transportLayerType & kMvsGigEDevice) != 0)
    {
        const MvsGigEDeviceInfo& gigE = info.specialInfo.gigE;
        const std::string model = FixedString(gigE.modelName);
        const std::string serial = FixedString(gigE.serialNumber);
        const std::string userName = FixedString(gigE.userDefinedName);
        description << " IP=" << IpAddress(gigE.currentIp);
        if (!model.empty()) description << " model=" << model;
        if (!serial.empty()) description << " serial=" << serial;
        if (!userName.empty()) description << " name=" << userName;
    }
    else
    {
        description << " USB/GenTL device";
    }
    return description.str();
}

DeviceOperationResult SelectDevice(const MvsDeviceInfoList& list,
    const std::string& rawSelector, SelectedMvsDevice& selected)
{
    if (list.deviceCount == 0)
        return {false, "MVS did not find a GigE or USB camera."};

    const std::string selector = Trim(rawSelector);
    int requestedIndex = -1;
    const auto parsed = std::from_chars(selector.data(), selector.data() + selector.size(),
        requestedIndex);
    const bool selectByIndex = !selector.empty() && parsed.ec == std::errc{} &&
        parsed.ptr == selector.data() + selector.size() && requestedIndex >= 0;

    std::vector<std::string> discovered;
    const std::size_t count = (std::min)(static_cast<std::size_t>(list.deviceCount),
        kMvsMaxDeviceCount);
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!list.devices[index])
            continue;
        const auto& info = *static_cast<const MvsDeviceInfo*>(list.devices[index]);
        const std::string description = DeviceDescription(info, index);
        discovered.push_back(description);

        bool matches = selectByIndex && requestedIndex == static_cast<int>(index);
        if (!selectByIndex && (selector.empty() || selector == "*"))
            matches = true;
        if (!selectByIndex && (info.transportLayerType & kMvsGigEDevice) != 0)
        {
            const MvsGigEDeviceInfo& gigE = info.specialInfo.gigE;
            matches = selector == IpAddress(gigE.currentIp) ||
                selector == FixedString(gigE.serialNumber) ||
                selector == FixedString(gigE.userDefinedName);
        }
        if (matches)
        {
            selected.info = list.devices[index];
            selected.description = description;
            selected.gigE = (info.transportLayerType & kMvsGigEDevice) != 0;
            return {true, description};
        }
    }

    std::ostringstream message;
    message << "MVS camera '" << selector << "' was not found. Discovered:";
    for (const std::string& item : discovered)
        message << " [" << item << ']';
    return {false, message.str()};
}

bool ParseIpv4(const std::string& text, std::uint32_t& value)
{
    value = 0;
    std::size_t begin = 0;
    for (int partIndex = 0; partIndex < 4; ++partIndex)
    {
        const std::size_t end = text.find('.', begin);
        if ((partIndex < 3 && end == std::string::npos) ||
            (partIndex == 3 && end != std::string::npos))
            return false;
        const std::size_t partEnd = end == std::string::npos ? text.size() : end;
        if (partEnd <= begin)
            return false;
        unsigned int part = 0;
        const char* first = text.data() + begin;
        const char* last = text.data() + partEnd;
        const auto parsed = std::from_chars(first, last, part);
        if (parsed.ec != std::errc{} || parsed.ptr != last || part > 255)
            return false;
        value = (value << 8U) | part;
        begin = partEnd + 1;
    }
    return true;
}

int SetEnum(MvsApi& api, void* handle, const char* key,
    const char* symbolicValue, std::uint32_t numericValue)
{
    if (api.setEnumValueByString)
    {
        const int result = api.setEnumValueByString(handle, key, symbolicValue);
        if (result == kMvsOk)
            return result;
    }
    return api.setEnumValue(handle, key, numericValue);
}

std::uint64_t ReceivedTimestampNanoseconds()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
}

struct HikrobotMvsCameraAdapter::Impl
{
    MvsApi api;
    void* handle = nullptr;
    std::vector<unsigned char> rawFrame;
    std::vector<unsigned char> convertedFrame;
    alignas(8) std::array<std::byte, 1024> frameInfo{};
    std::string selectedDevice;
    std::uint64_t timestampFrequencyHz = 0;
};

HikrobotMvsCameraAdapter::HikrobotMvsCameraAdapter()
    : impl_(std::make_unique<Impl>())
{
}

HikrobotMvsCameraAdapter::~HikrobotMvsCameraAdapter()
{
    Disconnect();
}

const char* HikrobotMvsCameraAdapter::AdapterName() const
{
    return "Hikrobot MVS Camera";
}

DeviceOperationResult HikrobotMvsCameraAdapter::Connect(const DeviceEndpoint& endpoint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisconnectUnlocked();
    state_ = DeviceConnectionState::Connecting;

    DeviceOperationResult result = impl_->api.Load();
    if (!result.success)
        return Fail(std::move(result.message), true);

    MvsDeviceInfoList devices{};
    int code = impl_->api.enumDevices(kMvsGigEDevice | kMvsUsbDevice, &devices);
    if (code != kMvsOk)
        return Fail("Could not enumerate MVS cameras: " + MvsError(code), true);

    SelectedMvsDevice selected;
    result = SelectDevice(devices, endpoint.address, selected);
    if (!result.success)
        return Fail(std::move(result.message), true);

    code = impl_->api.createHandle(&impl_->handle, selected.info);
    if (code != kMvsOk || !impl_->handle)
        return Fail("Could not create MVS camera handle: " + MvsError(code), true);

    code = impl_->api.openDevice(impl_->handle, kMvsExclusiveAccess, 0);
    if (code != kMvsOk)
    {
        impl_->api.destroyHandle(impl_->handle);
        impl_->handle = nullptr;
        return Fail("Could not open MVS camera (close the MVS viewer first): " +
            MvsError(code), true);
    }

    if (selected.gigE && impl_->api.getOptimalPacketSize && impl_->api.setIntValue)
    {
        const int packetSize = impl_->api.getOptimalPacketSize(impl_->handle);
        if (packetSize > 0)
            impl_->api.setIntValue(impl_->handle, "GevSCPSPacketSize",
                static_cast<std::uint32_t>(packetSize));
    }

    MvsIntValue payload{};
    code = impl_->api.getIntValue(impl_->handle, "PayloadSize", &payload);
    if (code != kMvsOk || payload.currentValue == 0)
    {
        DisconnectUnlocked();
        return Fail("Could not read MVS PayloadSize: " + MvsError(code), true);
    }

    impl_->rawFrame.resize(payload.currentValue);
    MvsIntValue timestampFrequency{};
    if (impl_->api.getIntValue(impl_->handle, "GevTimestampTickFrequency",
        &timestampFrequency) == kMvsOk)
    {
        impl_->timestampFrequencyHz = timestampFrequency.currentValue;
    }
    impl_->selectedDevice = selected.description;
    statistics_ = {};
    lastVendorFrameNumber_ = 0;
    triggerConfig_ = {};
    bufferPolicy_ = CameraBufferPolicy::Sequential;
    lastError_.clear();
    state_ = DeviceConnectionState::Connected;
    return {true, "Hikrobot MVS camera connected: " + selected.description};
}

void HikrobotMvsCameraAdapter::Disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    DisconnectUnlocked();
}

void HikrobotMvsCameraAdapter::DisconnectUnlocked()
{
    if (impl_ && impl_->handle)
    {
        if (streaming_)
            impl_->api.stopGrabbing(impl_->handle);
        impl_->api.closeDevice(impl_->handle);
        impl_->api.destroyHandle(impl_->handle);
        impl_->handle = nullptr;
    }
    streaming_ = false;
    if (impl_)
    {
        impl_->rawFrame.clear();
        impl_->convertedFrame.clear();
        impl_->selectedDevice.clear();
        impl_->timestampFrequencyHz = 0;
    }
    state_ = DeviceConnectionState::Disconnected;
}

DeviceConnectionState HikrobotMvsCameraAdapter::ConnectionState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string HikrobotMvsCameraAdapter::LastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

DeviceOperationResult HikrobotMvsCameraAdapter::GrabFrame(cv::Mat& frame, int timeoutMs)
{
    CameraFrameMetadata metadata;
    return GrabFrame(frame, metadata, timeoutMs);
}

DeviceOperationResult HikrobotMvsCameraAdapter::GrabFrame(cv::Mat& frame,
    CameraFrameMetadata& metadata, int timeoutMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    frame.release();
    metadata = {};
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("Hikrobot MVS camera is not connected.");
    if (!streaming_)
        return Fail("Hikrobot MVS camera is not grabbing.");

    MvsIntValue currentPayload{};
    if (impl_->api.getIntValue(impl_->handle, "PayloadSize", &currentPayload) == kMvsOk &&
        currentPayload.currentValue > impl_->rawFrame.size())
    {
        impl_->rawFrame.resize(currentPayload.currentValue);
    }

    std::fill(impl_->frameInfo.begin(), impl_->frameInfo.end(), std::byte{});
    const int code = impl_->api.getOneFrameTimeout(impl_->handle,
        impl_->rawFrame.data(), static_cast<unsigned int>(impl_->rawFrame.size()),
        impl_->frameInfo.data(), static_cast<unsigned int>((std::max)(1, timeoutMs)));
    if (code != kMvsOk)
    {
        ++statistics_.incompleteFrames;
        return Fail("MVS frame grab failed: " + MvsError(code));
    }

    const auto& info = *reinterpret_cast<const MvsFrameInfoPrefix*>(impl_->frameInfo.data());
    if (info.width == 0 || info.height == 0 || info.frameLength == 0 ||
        info.frameLength > impl_->rawFrame.size())
    {
        ++statistics_.incompleteFrames;
        return Fail("MVS returned invalid frame metadata.");
    }

    const CameraPixelFormatDescription pixelFormat =
        DescribeCameraPixelFormat(info.pixelType);
    metadata.sourcePixelFormat = info.pixelType;
    metadata.sourcePixelFormatName = pixelFormat.name;
    metadata.sourceBitDepth = pixelFormat.bitDepth;
    metadata.sourceStorageBitsPerPixel = pixelFormat.storageBitsPerPixel;
    metadata.sourceIsBayer = pixelFormat.bayer;

    const std::size_t pixels = static_cast<std::size_t>(info.width) * info.height;
    if (info.pixelType == kPixelMono8 && info.frameLength >= pixels)
    {
        frame = cv::Mat(info.height, info.width, CV_8UC1,
            impl_->rawFrame.data()).clone();
        metadata.conversionPath = "native Mono8";
    }
    else if ((info.pixelType == kPixelBgr8Packed || info.pixelType == kPixelRgb8Packed) &&
        info.frameLength >= pixels * 3)
    {
        cv::Mat packed(info.height, info.width, CV_8UC3, impl_->rawFrame.data());
        if (info.pixelType == kPixelRgb8Packed)
        {
            cv::cvtColor(packed, frame, cv::COLOR_RGB2BGR);
            metadata.convertedToDisplay = true;
            metadata.conversionPath = "OpenCV RGB8 -> BGR8";
        }
        else
        {
            frame = packed.clone();
            metadata.conversionPath = "native BGR8";
        }
    }
    else if ((info.pixelType == kPixelBayerGr8 || info.pixelType == kPixelBayerRg8 ||
        info.pixelType == kPixelBayerGb8 || info.pixelType == kPixelBayerBg8) &&
        info.frameLength >= pixels)
    {
        const cv::Mat bayer(info.height, info.width, CV_8UC1, impl_->rawFrame.data());
        int conversion = cv::COLOR_BayerGR2BGR;
        if (info.pixelType == kPixelBayerRg8) conversion = cv::COLOR_BayerRG2BGR;
        if (info.pixelType == kPixelBayerGb8) conversion = cv::COLOR_BayerGB2BGR;
        if (info.pixelType == kPixelBayerBg8) conversion = cv::COLOR_BayerBG2BGR;
        cv::cvtColor(bayer, frame, conversion);
        metadata.convertedToDisplay = true;
        metadata.conversionPath = "OpenCV Bayer8 -> BGR8";
    }
    else if (impl_->api.convertPixelType)
    {
        const bool monochrome = (info.pixelType & 0xff000000U) == 0x01000000U;
        const std::size_t destinationSize = pixels * (monochrome ? 1U : 3U);
        impl_->convertedFrame.resize(destinationSize);
        MvsPixelConvertParam conversion{};
        conversion.width = info.width;
        conversion.height = info.height;
        conversion.sourcePixelType = info.pixelType;
        conversion.sourceData = impl_->rawFrame.data();
        conversion.sourceDataLength = info.frameLength;
        conversion.destinationPixelType = monochrome ? kPixelMono8 : kPixelBgr8Packed;
        conversion.destinationBuffer = impl_->convertedFrame.data();
        conversion.destinationBufferSize = static_cast<std::uint32_t>(destinationSize);
        const int conversionCode = impl_->api.convertPixelType(impl_->handle, &conversion);
        if (conversionCode != kMvsOk || conversion.destinationDataLength == 0)
        {
            ++statistics_.incompleteFrames;
            return Fail("MVS pixel conversion failed: " + MvsError(conversionCode));
        }
        frame = cv::Mat(info.height, info.width, monochrome ? CV_8UC1 : CV_8UC3,
            impl_->convertedFrame.data()).clone();
        metadata.convertedToDisplay = true;
        metadata.conversionPath = monochrome
            ? "MVS ConvertPixelType -> Mono8"
            : "MVS ConvertPixelType -> BGR8";
    }
    else
    {
        ++statistics_.incompleteFrames;
        std::ostringstream message;
        message << "Unsupported MVS pixel format 0x" << std::hex << info.pixelType;
        return Fail(message.str());
    }

    if (frame.empty())
    {
        ++statistics_.incompleteFrames;
        return Fail("MVS produced an empty OpenCV frame.");
    }

    if (lastVendorFrameNumber_ != 0 && info.frameNumber > lastVendorFrameNumber_ + 1)
        statistics_.droppedFrames += info.frameNumber - lastVendorFrameNumber_ - 1;
    lastVendorFrameNumber_ = info.frameNumber;
    ++statistics_.receivedFrames;
    metadata.frameNumber = info.frameNumber;
    const std::uint64_t deviceTicks =
        (static_cast<std::uint64_t>(info.deviceTimestampHigh) << 32U) |
        info.deviceTimestampLow;
    if (deviceTicks > 0 && impl_->timestampFrequencyHz > 0)
    {
        metadata.hardwareTimestampNanoseconds = static_cast<std::uint64_t>(
            static_cast<long double>(deviceTicks) * 1000000000.0L /
            impl_->timestampFrequencyHz);
    }
    else if (info.hostTimestamp > 0)
    {
        metadata.hardwareTimestampNanoseconds = info.hostTimestamp;
    }
    metadata.receivedTimestampNanoseconds = ReceivedTimestampNanoseconds();
    metadata.droppedFrames = statistics_.droppedFrames;
    metadata.exposureComplete = true;
    lastError_.clear();
    return {true, "Hikrobot MVS frame captured."};
}

DeviceOperationResult HikrobotMvsCameraAdapter::StartStream()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("Hikrobot MVS camera is not connected.");
    if (streaming_)
        return {true, "Hikrobot MVS camera is already grabbing."};
    const int code = impl_->api.startGrabbing(impl_->handle);
    if (code != kMvsOk)
        return Fail("Could not start MVS acquisition: " + MvsError(code));
    streaming_ = true;
    lastError_.clear();
    return {true, "Hikrobot MVS acquisition started."};
}

void HikrobotMvsCameraAdapter::StopStream()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (streaming_ && impl_->handle)
        impl_->api.stopGrabbing(impl_->handle);
    streaming_ = false;
}

DeviceOperationResult HikrobotMvsCameraAdapter::SetControl(
    CameraControl control, double value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("Hikrobot MVS camera is not connected.");

    int code = kMvsOk;
    const char* controlName = "camera control";
    switch (control)
    {
    case CameraControl::AutoExposure:
        controlName = "ExposureAuto";
        code = value > 0.5
            ? SetEnum(impl_->api, impl_->handle, "ExposureAuto", "Continuous", 2)
            : SetEnum(impl_->api, impl_->handle, "ExposureAuto", "Off", 0);
        break;
    case CameraControl::Exposure:
        controlName = "ExposureTime";
        code = impl_->api.setFloatValue(impl_->handle, "ExposureTime",
            static_cast<float>((std::max)(1.0, value)));
        break;
    case CameraControl::Gain:
        controlName = "Gain";
        code = impl_->api.setFloatValue(impl_->handle, "Gain",
            static_cast<float>((std::max)(0.0, value)));
        break;
    }
    if (code != kMvsOk)
        return Fail(std::string("MVS rejected ") + controlName + ": " + MvsError(code));
    lastError_.clear();
    return {true, std::string("MVS ") + controlName + " updated."};
}

CameraCapabilities HikrobotMvsCameraAdapter::Capabilities() const
{
    return {true, true, true, true, true, true};
}

DeviceOperationResult HikrobotMvsCameraAdapter::ConfigurePtp(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("Hikrobot MVS camera is not connected.");
    int code = SetEnum(impl_->api, impl_->handle, "GevIEEE1588",
        enabled ? "On" : "Off", enabled ? 1U : 0U);
    if (code != kMvsOk)
    {
        code = SetEnum(impl_->api, impl_->handle, "PtpEnable",
            enabled ? "On" : "Off", enabled ? 1U : 0U);
    }
    if (code != kMvsOk)
        return Fail("Could not configure MVS PTP: " + MvsError(code));
    return {true, enabled ? "Hikrobot MVS PTP enabled." : "Hikrobot MVS PTP disabled."};
}

DeviceOperationResult HikrobotMvsCameraAdapter::ConfigureTrigger(
    const CameraTriggerConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("Hikrobot MVS camera is not connected.");

    int code = kMvsOk;
    if (config.mode == CameraTriggerMode::Continuous)
    {
        code = SetEnum(impl_->api, impl_->handle, "TriggerMode", "Off", 0);
    }
    else
    {
        code = SetEnum(impl_->api, impl_->handle, "TriggerMode", "On", 1);
        if (code == kMvsOk)
        {
            switch (config.mode)
            {
            case CameraTriggerMode::Software:
                code = SetEnum(impl_->api, impl_->handle,
                    "TriggerSource", "Software", 7);
                break;
            case CameraTriggerMode::HardwareLine1:
                code = SetEnum(impl_->api, impl_->handle,
                    "TriggerSource", "Line1", 1);
                break;
            case CameraTriggerMode::HardwareLine2:
                code = SetEnum(impl_->api, impl_->handle,
                    "TriggerSource", "Line2", 2);
                break;
            default:
                break;
            }
        }
        if (code == kMvsOk && config.delayMicroseconds > 0.0)
        {
            code = impl_->api.setFloatValue(impl_->handle, "TriggerDelay",
                static_cast<float>(config.delayMicroseconds));
        }
    }
    if (code != kMvsOk)
        return Fail("Could not configure MVS trigger: " + MvsError(code));
    triggerConfig_ = config;
    lastError_.clear();
    return {true, "Hikrobot MVS trigger configured."};
}

DeviceOperationResult HikrobotMvsCameraAdapter::ExecuteSoftwareTrigger()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("Hikrobot MVS camera is not connected.");
    if (triggerConfig_.mode != CameraTriggerMode::Software)
        return Fail("MVS camera is not configured for software trigger.");
    const int code = impl_->api.setCommandValue(impl_->handle, "TriggerSoftware");
    if (code != kMvsOk)
        return Fail("MVS software trigger failed: " + MvsError(code));
    lastError_.clear();
    return {true, "Hikrobot MVS software trigger sent."};
}

DeviceOperationResult HikrobotMvsCameraAdapter::FlushQueue()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("Hikrobot MVS camera is not connected.");
    // GetOneFrameTimeout uses an application-owned buffer. Restarting acquisition
    // drops frames still queued in the transport layer.
    if (!streaming_)
        return {true, "MVS acquisition queue is already stopped."};
    int code = impl_->api.stopGrabbing(impl_->handle);
    if (code == kMvsOk)
        code = impl_->api.startGrabbing(impl_->handle);
    if (code != kMvsOk)
    {
        streaming_ = false;
        return Fail("Could not flush MVS acquisition queue: " + MvsError(code));
    }
    return {true, "Hikrobot MVS acquisition queue flushed."};
}

DeviceOperationResult HikrobotMvsCameraAdapter::ConfigureBufferPolicy(
    CameraBufferPolicy policy)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DeviceConnectionState::Connected || !impl_->handle)
        return Fail("Hikrobot MVS camera is not connected.");
    if (!impl_->api.setGrabStrategy)
        return Fail("The installed MVS runtime does not support grab strategy control.");

    const bool wasStreaming = streaming_;
    if (wasStreaming)
    {
        const int stopCode = impl_->api.stopGrabbing(impl_->handle);
        if (stopCode != kMvsOk)
            return Fail("Could not stop MVS acquisition to change buffer policy: " +
                MvsError(stopCode));
        streaming_ = false;
    }

    const int strategyCode = impl_->api.setGrabStrategy(impl_->handle,
        policy == CameraBufferPolicy::LatestFrame ? 1U : 0U);
    int restartCode = kMvsOk;
    if (wasStreaming)
    {
        restartCode = impl_->api.startGrabbing(impl_->handle);
        streaming_ = restartCode == kMvsOk;
    }
    if (strategyCode != kMvsOk)
        return Fail("Could not configure MVS frame buffer policy: " +
            MvsError(strategyCode));
    if (restartCode != kMvsOk)
        return Fail("MVS buffer policy changed but acquisition could not restart: " +
            MvsError(restartCode));

    bufferPolicy_ = policy;
    statistics_.queuedFrames = policy == CameraBufferPolicy::LatestFrame ? 1U : 0U;
    return {true, policy == CameraBufferPolicy::LatestFrame
        ? "MVS frame buffer uses the latest frame only."
        : "MVS frame buffer uses sequential frames."};
}

CameraStatistics HikrobotMvsCameraAdapter::Statistics() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return statistics_;
}

DeviceOperationResult HikrobotMvsCameraAdapter::EnumerateDevices(
    std::vector<CameraDeviceInfo>& devices)
{
    std::lock_guard<std::mutex> lock(mutex_);
    devices.clear();
    DeviceOperationResult loaded = impl_->api.Load();
    if (!loaded.success)
        return loaded;

    MvsDeviceInfoList list{};
    const int code = impl_->api.enumDevices(
        kMvsGigEDevice | kMvsUsbDevice, &list);
    if (code != kMvsOk)
        return {false, "Could not enumerate MVS cameras: " + MvsError(code)};

    const std::size_t count = (std::min)(
        static_cast<std::size_t>(list.deviceCount), kMvsMaxDeviceCount);
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!list.devices[index])
            continue;
        const auto& source = *static_cast<const MvsDeviceInfo*>(list.devices[index]);
        CameraDeviceInfo device;
        device.runtimePath = impl_->api.LoadedPath();
        device.runtimeVersion = impl_->api.SdkVersion();
        device.accessible = !impl_->api.isDeviceAccessible ||
            impl_->api.isDeviceAccessible(list.devices[index], kMvsExclusiveAccess);
        device.status = device.accessible ? "available" : "occupied";
        if ((source.transportLayerType & kMvsGigEDevice) != 0)
        {
            const MvsGigEDeviceInfo& gigE = source.specialInfo.gigE;
            device.transport = "GigE";
            device.model = FixedString(gigE.modelName);
            device.serialNumber = FixedString(gigE.serialNumber);
            device.userDefinedName = FixedString(gigE.userDefinedName);
            device.ipAddress = IpAddress(gigE.currentIp);
            device.macAddress = MacAddress(source);
            device.selector = !device.serialNumber.empty()
                ? device.serialNumber : device.ipAddress;
        }
        else
        {
            device.transport = "USB/GenTL";
            device.selector = std::to_string(index);
        }
        if (device.selector.empty())
            device.selector = std::to_string(index);
        devices.push_back(std::move(device));
    }
    return {true, devices.empty()
        ? "MVS runtime loaded; no camera was found."
        : "MVS camera scan completed."};
}

DeviceOperationResult HikrobotMvsCameraAdapter::ForceIp(
    const std::string& selector, const std::string& ipAddress,
    const std::string& subnetMask, const std::string& defaultGateway)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceOperationResult loaded = impl_->api.Load();
    if (!loaded.success)
        return loaded;
    if (!impl_->api.forceIpEx)
        return {false, "The installed MVS runtime does not export MV_CC_ForceIpEx."};

    std::uint32_t ip = 0;
    std::uint32_t subnet = 0;
    std::uint32_t gateway = 0;
    if (!ParseIpv4(ipAddress, ip) || !ParseIpv4(subnetMask, subnet) ||
        !ParseIpv4(defaultGateway, gateway))
        return {false, "ForceIP requires valid IPv4 address, subnet mask and gateway."};

    MvsDeviceInfoList list{};
    int code = impl_->api.enumDevices(kMvsGigEDevice, &list);
    if (code != kMvsOk)
        return {false, "Could not enumerate MVS GigE cameras: " + MvsError(code)};
    SelectedMvsDevice selected;
    DeviceOperationResult selectedResult = SelectDevice(list, selector, selected);
    if (!selectedResult.success)
        return selectedResult;
    if (!selected.gigE)
        return {false, "ForceIP is available only for GigE cameras."};

    void* handle = nullptr;
    code = impl_->api.createHandle(&handle, selected.info);
    if (code != kMvsOk || !handle)
        return {false, "Could not create MVS ForceIP handle: " + MvsError(code)};
    code = impl_->api.forceIpEx(handle, ip, subnet, gateway);
    impl_->api.destroyHandle(handle);
    if (code != kMvsOk)
        return {false, "MVS ForceIP failed: " + MvsError(code)};
    return {true, "MVS ForceIP completed. Rescan the devices before connecting."};
}

DeviceOperationResult HikrobotMvsCameraAdapter::Fail(std::string message, bool fault)
{
    lastError_ = message;
    if (fault)
        state_ = DeviceConnectionState::Fault;
    return {false, std::move(message)};
}
