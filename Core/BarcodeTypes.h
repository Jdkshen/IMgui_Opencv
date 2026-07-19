#pragma once

#include <cstdint>

enum BarcodeFormatFlag : std::uint32_t
{
    BarcodeFormatQR = 1u << 0,
    BarcodeFormatCode128 = 1u << 1,
    BarcodeFormatEAN = 1u << 2,
    BarcodeFormatDataMatrix = 1u << 3,
    BarcodeFormatPDF417 = 1u << 4,
    BarcodeFormatAll = BarcodeFormatQR | BarcodeFormatCode128 | BarcodeFormatEAN |
                       BarcodeFormatDataMatrix | BarcodeFormatPDF417,
};

