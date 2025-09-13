#include "dds_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN


namespace DDS_pvt {

string_view
CompressionToString(Compression compression)
{
    string_view str = "";
    switch (compression) {
    case Compression::None: break;
    case Compression::DXT1: str = "DXT1"; break;
    case Compression::DXT2: str = "DXT2"; break;
    case Compression::DXT3: str = "DXT3"; break;
    case Compression::DXT4: str = "DXT4"; break;
    case Compression::DXT5: str = "DXT5"; break;
    case Compression::BC4: str = "BC4"; break;
    case Compression::BC5: str = "BC5"; break;
    case Compression::BC6HU: str = "BC6HU"; break;
    case Compression::BC6HS: str = "BC6HS"; break;
    case Compression::BC7: str = "BC7"; break;
    }

    return str;
}

Compression
CompressionFromString(string_view str)
{
    if (str == "DXT1")
        return Compression::DXT1;
    if (str == "DXT2")
        return Compression::DXT2;
    if (str == "DXT3")
        return Compression::DXT3;
    if (str == "DXT4")
        return Compression::DXT4;
    if (str == "DXT5")
        return Compression::DXT5;
    if (str == "BC4")
        return Compression::BC4;
    if (str == "BC5")
        return Compression::BC5;
    if (str == "BC6HU")
        return Compression::BC6HU;
    if (str == "BC6HS")
        return Compression::BC6HS;
    if (str == "BC7")
        return Compression::BC7;

    return Compression::None;
}

TypeDesc::BASETYPE
GetBaseType(Compression cmp)
{
    if (cmp == Compression::BC6HU || cmp == Compression::BC6HS)
        return TypeDesc::HALF;
    return TypeDesc::UINT8;
}

int
GetChannelCount(Compression cmp, bool isNormal)
{
    if (cmp == Compression::DXT5)
        return isNormal ? 3 : 4;
    if (cmp == Compression::BC5)
        return isNormal ? 3 : 2;
    if (cmp == Compression::BC4)
        return 1;
    if (cmp == Compression::BC6HU || cmp == Compression::BC6HS)
        return 3;
    return 4;
}

size_t
GetBlockCompressedSize(Compression cmp)
{
    return cmp == Compression::DXT1 || cmp == Compression::BC4 ? 8 : 16;
}

size_t
GetStorageRequirements(size_t width, size_t height, Compression cmp)
{
    size_t blockCount = ((width + kBlockSize - 1) / kBlockSize)
                        * ((height + kBlockSize - 1) / kBlockSize);
    return blockCount * GetBlockCompressedSize(cmp);
}

}  // namespace DDS_pvt


OIIO_PLUGIN_NAMESPACE_END
