#include "HardwareAdapters.h"

#include <map>
#include <utility>

namespace
{
std::map<std::string, std::unique_ptr<ICameraAdapter>> s_cameras;
std::map<std::string, std::unique_ptr<IDeviceAdapter>> s_adapters;
}

namespace HardwareAdapterService
{
void SetCamera(std::unique_ptr<ICameraAdapter> camera)
{
    RemoveCamera("default");
    if (camera)
        s_cameras.emplace("default", std::move(camera));
}

ICameraAdapter* Camera()
{
    return Camera("default");
}

const ICameraAdapter* CameraReadOnly()
{
    return CameraReadOnly("default");
}

bool RegisterCamera(const std::string& key, std::unique_ptr<ICameraAdapter> camera)
{
    if (key.empty() || !camera || s_cameras.find(key) != s_cameras.end())
        return false;
    s_cameras.emplace(key, std::move(camera));
    return true;
}

ICameraAdapter* Camera(const std::string& key)
{
    const auto found = s_cameras.find(key);
    return found == s_cameras.end() ? nullptr : found->second.get();
}

const ICameraAdapter* CameraReadOnly(const std::string& key)
{
    const auto found = s_cameras.find(key);
    return found == s_cameras.end() ? nullptr : found->second.get();
}

std::vector<std::string> CameraKeys()
{
    std::vector<std::string> keys;
    keys.reserve(s_cameras.size());
    for (const auto& item : s_cameras)
        keys.push_back(item.first);
    return keys;
}

bool RemoveCamera(const std::string& key)
{
    const auto found = s_cameras.find(key);
    if (found == s_cameras.end())
        return false;
    found->second->StopStream();
    found->second->Disconnect();
    s_cameras.erase(found);
    return true;
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
    for (auto& item : s_cameras)
    {
        item.second->StopStream();
        item.second->Disconnect();
    }
    for (auto& item : s_adapters)
        item.second->Disconnect();
}

void Clear()
{
    DisconnectAll();
    s_cameras.clear();
    s_adapters.clear();
}
}
