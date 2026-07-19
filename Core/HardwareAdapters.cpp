#include "HardwareAdapters.h"

#include <map>
#include <utility>

namespace
{
std::unique_ptr<ICameraAdapter> s_camera;
std::map<std::string, std::unique_ptr<IDeviceAdapter>> s_adapters;
}

namespace HardwareAdapterService
{
void SetCamera(std::unique_ptr<ICameraAdapter> camera)
{
    if (s_camera)
    {
        s_camera->StopStream();
        s_camera->Disconnect();
    }
    s_camera = std::move(camera);
}

ICameraAdapter* Camera()
{
    return s_camera.get();
}

const ICameraAdapter* CameraReadOnly()
{
    return s_camera.get();
}

bool Register(const std::string& key, std::unique_ptr<IDeviceAdapter> adapter)
{
    if (key.empty() || !adapter || s_adapters.find(key) != s_adapters.end())
        return false;
    s_adapters.emplace(key, std::move(adapter));
    return true;
}

IDeviceAdapter* Find(const std::string& key)
{
    const auto found = s_adapters.find(key);
    return found == s_adapters.end() ? nullptr : found->second.get();
}

const IDeviceAdapter* FindReadOnly(const std::string& key)
{
    const auto found = s_adapters.find(key);
    return found == s_adapters.end() ? nullptr : found->second.get();
}

std::vector<std::string> Keys()
{
    std::vector<std::string> keys;
    keys.reserve(s_adapters.size());
    for (const auto& item : s_adapters)
        keys.push_back(item.first);
    return keys;
}

bool Remove(const std::string& key)
{
    const auto found = s_adapters.find(key);
    if (found == s_adapters.end())
        return false;
    found->second->Disconnect();
    s_adapters.erase(found);
    return true;
}

void DisconnectAll()
{
    if (s_camera)
    {
        s_camera->StopStream();
        s_camera->Disconnect();
    }
    for (auto& item : s_adapters)
        item.second->Disconnect();
}

void Clear()
{
    DisconnectAll();
    s_camera.reset();
    s_adapters.clear();
}
}
