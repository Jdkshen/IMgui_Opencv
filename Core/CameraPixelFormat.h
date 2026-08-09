#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

struct CameraPixelFormatDescription
{
    std::string name;
    int bitDepth = 0;
    int storageBitsPerPixel = 0;
    bool bayer = false;
    bool monochrome = false;
};

inline CameraPixelFormatDescription DescribeCameraPixelFormat(std::uint32_t value)
{
    CameraPixelFormatDescription result;
    result.storageBitsPerPixel = static_cast<int>((value >> 16U) & 0xffU);
    switch (value)
    {
    case 0x01080001U: result = {"Mono8", 8, 8, false, true}; break;
    case 0x01100003U: result = {"Mono10", 10, 16, false, true}; break;
    case 0x010C0004U: result = {"Mono10Packed", 10, 12, false, true}; break;
    case 0x01100005U: result = {"Mono12", 12, 16, false, true}; break;
    case 0x010C0006U: result = {"Mono12Packed", 12, 12, false, true}; break;
    case 0x01100025U: result = {"Mono14", 14, 16, false, true}; break;
    case 0x01100007U: result = {"Mono16", 16, 16, false, true}; break;
    case 0x01080008U: result = {"BayerGR8", 8, 8, true, false}; break;
    case 0x01080009U: result = {"BayerRG8", 8, 8, true, false}; break;
    case 0x0108000AU: result = {"BayerGB8", 8, 8, true, false}; break;
    case 0x0108000BU: result = {"BayerBG8", 8, 8, true, false}; break;
    case 0x0110000CU: result = {"BayerGR10", 10, 16, true, false}; break;
    case 0x0110000DU: result = {"BayerRG10", 10, 16, true, false}; break;
    case 0x0110000EU: result = {"BayerGB10", 10, 16, true, false}; break;
    case 0x0110000FU: result = {"BayerBG10", 10, 16, true, false}; break;
    case 0x01100010U: result = {"BayerGR12", 12, 16, true, false}; break;
    case 0x01100011U: result = {"BayerRG12", 12, 16, true, false}; break;
    case 0x01100012U: result = {"BayerGB12", 12, 16, true, false}; break;
    case 0x01100013U: result = {"BayerBG12", 12, 16, true, false}; break;
    case 0x010C0026U: result = {"BayerGR10Packed", 10, 12, true, false}; break;
    case 0x010C0027U: result = {"BayerRG10Packed", 10, 12, true, false}; break;
    case 0x010C0028U: result = {"BayerGB10Packed", 10, 12, true, false}; break;
    case 0x010C0029U: result = {"BayerBG10Packed", 10, 12, true, false}; break;
    case 0x010C002AU: result = {"BayerGR12Packed", 12, 12, true, false}; break;
    case 0x010C002BU: result = {"BayerRG12Packed", 12, 12, true, false}; break;
    case 0x010C002CU: result = {"BayerGB12Packed", 12, 12, true, false}; break;
    case 0x010C002DU: result = {"BayerBG12Packed", 12, 12, true, false}; break;
    case 0x0110002EU: result = {"BayerGR16", 16, 16, true, false}; break;
    case 0x0110002FU: result = {"BayerRG16", 16, 16, true, false}; break;
    case 0x01100030U: result = {"BayerGB16", 16, 16, true, false}; break;
    case 0x01100031U: result = {"BayerBG16", 16, 16, true, false}; break;
    case 0x02180014U: result = {"RGB8", 8, 24, false, false}; break;
    case 0x02180015U: result = {"BGR8", 8, 24, false, false}; break;
    case 0x02200016U: result = {"RGBA8", 8, 32, false, false}; break;
    case 0x02200017U: result = {"BGRA8", 8, 32, false, false}; break;
    default:
    {
        char text[32] = {};
        std::snprintf(text, sizeof(text), "PFNC-0x%08X", value);
        result.name = text;
        result.monochrome = (value & 0xff000000U) == 0x01000000U;
        break;
    }
    }
    return result;
}
