// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/parallel.h>

#include "BC.h"
#include "dds_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN

using namespace DDS_pvt;

class DDSOutput final : public ImageOutput {
public:
    DDSOutput() { init(); }
    ~DDSOutput() override { close(); }
    const char* format_name(void) const override { return "dds"; }
    int supports(string_view feature) const override
    {
        return feature == "ioproxy" || feature == "mipmap"
               || feature == "alpha";
    }
    bool open(const std::string& name, const ImageSpec& spec,
              OpenMode mode = Create) override;
    bool close() override;
    bool write_scanline(int y, int z, TypeDesc format, const void* data,
                        stride_t xstride) override;
    // bool write_scanlines(int ybegin, int yend, int z, TypeDesc format,
    //                      const void* data, stride_t xstride = AutoStride,
    //                      stride_t ystride = AutoStride) override;
    // bool write_tile(int x, int y, int z, TypeDesc format, const void* data,
    //                 stride_t xstride, stride_t ystride,
    //                 stride_t zstride) override;

private:
    std::string m_filename;  ///< Stash the filename
    Compression m_compression;
    dds_header m_dds;
    dds_header_dx10 m_dx10;
    int m_Bpp;
    uint32_t m_BitCounts[4];

    uint32_t m_LeftShifts[4];
    int64_t m_image_start;
    std::vector<float>
        m_uncompressed_image;  ///< Temporary buffer for image data when using compression

    unsigned int m_dither;
    std::vector<unsigned char> m_scratch;

    void init(void) { ioproxy_clear(); }

    void* move_to_scratch(const void* data, size_t nbytes)
    {
        if (m_scratch.empty() || (const unsigned char*)data != m_scratch.data())
            m_scratch.assign((const unsigned char*)data,
                             (const unsigned char*)data + nbytes);
        return m_scratch.data();
    }
};

static std::vector<uint8_t>
CompressImage(int width, int height, const float* rgbaf, Compression cmp,
              const dds_pixformat& pixelFormat, int nthreads,
              float bc1_alpha_threshold, uint32_t flags)
{
    std::vector<uint8_t> bc(GetStorageRequirements(width, height, cmp));
    const size_t bcSize      = GetBlockCompressedSize(cmp);
    const int channelCount   = GetChannelCount(cmp,
                                               pixelFormat.flags & DDS_PF_NORMAL);
    const int widthInBlocks  = (width + kBlockSize - 1) / kBlockSize;
    const int heightInBlocks = (height + kBlockSize - 1) / kBlockSize;
    paropt opt(nthreads, paropt::SplitDir::Y, 8);
    parallel_for_chunked(
        0, heightInBlocks, 0,
        [&](int64_t ybb, int64_t ybe) {
            DirectX::XMVECTOR block_rgbaf[kBlockSize * kBlockSize];
            const int ybegin   = int(ybb) * kBlockSize;
            const int yend     = std::min(int(ybe) * kBlockSize, height);
            uint8_t* dstBlocks = bc.data() + ybb * widthInBlocks * bcSize;
            for (int y = ybegin; y < yend; y += kBlockSize) {
                for (int x = 0; x < width; x += kBlockSize) {
                    // Extract 4x4 block
                    memset(block_rgbaf, 0, sizeof(block_rgbaf));
                    DirectX::XMVECTOR* dst = block_rgbaf;
                    const float* src       = rgbaf
                                       + channelCount * (size_t(width) * y + x);
                    // TODO(DDS): when width/height is not multiple of 4, we need to finish filling block_rgbaf with colors from existing pixels
                    for (int py = 0; py < kBlockSize && y + py < yend; ++py) {
                        int cols = std::min(kBlockSize, width - x);
                        if (cmp == Compression::BC4) {
                            OIIO_ASSERT(channelCount == 1);
                            for (int c = 0; c < cols; ++c) {
                                dst[c].x = src[c];
                            }
                        } else {
                            OIIO_ASSERT(channelCount == 4);
                            memcpy(dst, src,
                                   cols * channelCount * sizeof(float));
                        }
                        src += channelCount * width;
                        dst += kBlockSize;
                    }

                    switch (cmp) {
                    case Compression::DXT1:
                        DirectX::D3DXEncodeBC1(dstBlocks, block_rgbaf,
                                               bc1_alpha_threshold, flags);
                        break;
                    case Compression::DXT2:
                    case Compression::DXT3:
                        DirectX::D3DXEncodeBC2(dstBlocks, block_rgbaf, flags);
                        break;
                    case Compression::DXT4:
                    case Compression::DXT5:
                        DirectX::D3DXEncodeBC3(dstBlocks, block_rgbaf, flags);
                        break;
                    case Compression::BC4:
                        DirectX::D3DXEncodeBC4U(dstBlocks, block_rgbaf, flags);
                        break;
                    // case Compression::BC5:
                    //     bcdec_bc5(srcBlocks, rgbai, kBlockSize * 2);
                    //     break;
                    // case Compression::BC6HU:
                    // case Compression::BC6HS:
                    //     bcdec_bc6h_half(srcBlocks, rgbh, kBlockSize * 3,
                    //                     cmp == Compression::BC6HS);
                    //     break;
                    // case Compression::BC7:
                    //     bcdec_bc7(srcBlocks, rgbai, kBlockSize * 4);
                    //     break;
                    default: return;
                    }
                    dstBlocks += bcSize;

                    //
                    // // Swap R & A for RXGB format case
                    // if (cmp == Compression::DXT5
                    //     && pixelFormat.fourCC == DDS_4CC_RXGB) {
                    //     for (int i = 0; i < 16; ++i) {
                    //         uint8_t r        = rgbai[i * 4 + 0];
                    //         uint8_t a        = rgbai[i * 4 + 3];
                    //         rgbai[i * 4 + 0] = a;
                    //         rgbai[i * 4 + 3] = r;
                    //     }
                    // }
                    // // Convert into full normal map if needed
                    // else if (pixelFormat.flags & DDS_PF_NORMAL) {
                    //     if (cmp == Compression::BC5) {
                    //         ComputeNormalRG(rgbai);
                    //     } else if (cmp == Compression::DXT5) {
                    //         ComputeNormalAG(rgbai);
                    //     }
                    // }
                    //
                    // // Write the pixels into the destination image location,
                    // // making sure to not go outside of image boundaries (BCn
                    // // blocks always decode to 4x4 pixels, but output image
                    // // might not be multiple of 4).
                    // if (cmp == Compression::BC6HU
                    //     || cmp == Compression::BC6HS) {
                    //     // HDR formats: half
                    //     const uint16_t* src = rgbh;
                    //     uint16_t* dst       = (uint16_t*)rgba
                    //                     + channelCount
                    //                           * (size_t(width) * y + x);
                    //     for (int py = 0; py < kBlockSize && y + py < yend;
                    //          py++) {
                    //         int cols = std::min(kBlockSize, width - x);
                    //         memcpy(dst, src, cols * channelCount * 2);
                    //         src += kBlockSize * channelCount;
                    //         dst += channelCount * width;
                    //     }
                    // } else {
                    //     // LDR formats: uint8
                    //     const uint8_t* src = rgbai;
                    //     uint8_t* dst       = rgba
                    //                    + channelCount * (size_t(width) * y + x);
                    //     for (int py = 0; py < kBlockSize && y + py < yend;
                    //          py++) {
                    //         int cols = std::min(kBlockSize, width - x);
                    //         memcpy(dst, src, cols * channelCount);
                    //         src += kBlockSize * channelCount;
                    //         dst += channelCount * width;
                    //     }
                    // }
                }
            }
        },
        opt);

    return bc;
}

// Obligatory material to make this a recognizable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageOutput*
dds_output_imageio_create()
{
    return new DDSOutput;
}

OIIO_EXPORT const char* dds_output_extensions[] = { "dds", nullptr };

OIIO_PLUGIN_EXPORTS_END

bool
DDSOutput::open(const std::string& name, const ImageSpec& userspec,
                OpenMode mode)
{
    // TODO(DDS): zend here should be larger for array images? do we support array images?
    if (!check_open(mode, userspec, { 0, 65535, 0, 65535, 0, 1, 0, 4 }))
        return false;

    ioproxy_retrieve_from_config(m_spec);

    if (!ioproxy_use_or_open(name))
        return false;

    m_dither = (m_spec.format == TypeDesc::UINT8)
                   ? m_spec.get_int_attribute("oiio:dither", 0)
                   : 0;

    m_compression = CompressionFromString(
        m_spec.get_string_attribute("compression"));


    memset(&m_dds, 0, sizeof(m_dds));
    memset(&m_dx10, 0, sizeof(m_dx10));

    m_dds.fourCC   = DDS_MAKE4CC('D', 'D', 'S', ' ');
    m_dds.size     = 124;
    m_dds.fmt.size = 32;

    m_dds.width  = m_spec.width;
    m_dds.height = m_spec.height;
    m_dds.depth  = 0;  // TODO(DDS): depth
    m_dds.flags  = DDS_CAPS | DDS_WIDTH | DDS_HEIGHT
                  | DDS_PIXELFORMAT;  // TODO(DDS): flags
    m_dds.mipmaps     = 1;            // TODO(DDS): mipmaps
    m_dds.caps.flags1 = DDS_CAPS1_TEXTURE;

    if (m_compression != Compression::None) {
        m_dds.flags |= DDS_LINEARSIZE;
        m_dds.pitch = GetStorageRequirements(m_spec.width, m_spec.height,
                                             m_compression);
        m_dds.fmt.flags |= DDS_PF_FOURCC;
        m_uncompressed_image.resize(
            m_spec.width * m_spec.height
            * GetChannelCount(
                m_compression,
                /*isNormal=*/false));  // TODO(DDS): need to consider bc5Normal here?
        if (m_spec.format.basetype != TypeDesc::UINT8) {
            m_spec.set_format(TypeDesc::UINT8);
        }
        switch (m_compression) {
        case Compression::DXT1: m_dds.fmt.fourCC = DDS_4CC_DXT1; break;
        case Compression::DXT2: m_dds.fmt.fourCC = DDS_4CC_DXT2; break;
        case Compression::DXT3: m_dds.fmt.fourCC = DDS_4CC_DXT3; break;
        case Compression::DXT4: m_dds.fmt.fourCC = DDS_4CC_DXT4; break;
        case Compression::DXT5: m_dds.fmt.fourCC = DDS_4CC_DXT5; break;
        case Compression::BC4: m_dds.fmt.fourCC = DDS_4CC_BC4U; break;
        default: {
            errorfmt("Unsupported compression '{}'",
                     CompressionToString(m_compression));
            return false;
        }
        }
    } else if (false /*needs DX10 format*/) {
        m_dds.fmt.fourCC = DDS_4CC_DX10;
        m_dds.fmt.flags |= DDS_PF_FOURCC;
    } else {
        if (m_spec.format.basetype != TypeDesc::UINT8) {
            m_spec.set_format(TypeDesc::UINT8);
        }

        // TODO(DDS): use dds:ChannelOrder attribute

        const int R = m_spec.channelindex("R");
        const int G = m_spec.channelindex("G");
        const int B = m_spec.channelindex("B");
        const int A = m_spec.alpha_channel;

        const int bpp = m_spec.get_int_attribute("oiio:BitsPerSample");
        const int bitsperchannel = bpp / m_spec.nchannels;

        const int Rbits = m_spec.get_int_attribute("dds:BitCountR",
                                                   bitsperchannel);
        const int Gbits = m_spec.get_int_attribute("dds:BitCountG",
                                                   bitsperchannel);
        const int Bbits = m_spec.get_int_attribute("dds:BitCountB",
                                                   bitsperchannel);
        const int Abits = m_spec.get_int_attribute("dds:BitCountA",
                                                   bitsperchannel);
        const int Rmask = (1 << Rbits) - 1;
        const int Gmask = (1 << Gbits) - 1;
        const int Bmask = (1 << Bbits) - 1;
        const int Amask = (1 << Abits) - 1;

        m_Bpp           = bpp / 8;
        m_BitCounts[0]  = (R != -1) ? Rbits : 0;
        m_BitCounts[1]  = (G != -1) ? Gbits : 0;
        m_BitCounts[2]  = (B != -1) ? Bbits : 0;
        m_BitCounts[3]  = (A != -1) ? Abits : 0;
        m_LeftShifts[0] = 0;
        m_LeftShifts[1] = (G != -1) ? m_BitCounts[0] : 0;
        m_LeftShifts[2] = (B != -1) ? m_BitCounts[0] + m_BitCounts[1] : 0;
        m_LeftShifts[3] = (A != -1)
                              ? m_BitCounts[0] + m_BitCounts[1] + m_BitCounts[2]
                              : 0;

        m_dds.flags |= DDS_PITCH;
        m_dds.pitch   = (bpp / 8) * m_spec.width;
        m_dds.fmt.bpp = bpp;
        m_dds.fmt.flags |= DDS_PF_RGB;
        m_dds.fmt.flags |= (A != -1) ? DDS_PF_ALPHA : 0;

        m_dds.fmt.masks[0] = (R != -1) ? Rmask << m_LeftShifts[0] : 0;
        m_dds.fmt.masks[1] = (G != -1) ? Gmask << m_LeftShifts[1] : 0;
        m_dds.fmt.masks[2] = (B != -1) ? Bmask << m_LeftShifts[2] : 0;
        m_dds.fmt.masks[3] = (A != -1) ? Amask << m_LeftShifts[3] : 0;
    }

    if (bigendian()) {
        // DDS files are little-endian
        // only swap values which are not flags or bitmasks
        swap_endian(&m_dds.size);
        swap_endian(&m_dds.height);
        swap_endian(&m_dds.width);
        swap_endian(&m_dds.pitch);
        swap_endian(&m_dds.depth);
        swap_endian(&m_dds.mipmaps);

        swap_endian(&m_dds.fmt.size);
        swap_endian(&m_dds.fmt.bpp);
    }


    if (!iowrite(&m_dds, sizeof(m_dds)))
        return false;

    if (m_dds.fmt.fourCC == DDS_4CC_DX10) {
        if (!iowrite(&m_dx10, sizeof(m_dx10)))
            return false;
    }

    m_image_start = iotell();

    return true;
}

bool
DDSOutput::close()
{
    if (!ioproxy_opened())  // already closed
        return true;

    bool ok = true;
    if (m_compression != Compression::None) {
        const float alpha_threshold = m_spec.get_float_attribute(
            "dds:BC1AlphaThreshold",
            OIIO::get_float_attribute("dds:BC1AlphaThreshold", 0.5));
        // TODO(DDS): attributes for BC_FLAGS?
        const uint32_t flags
            = 0;  //DirectX::BC_FLAGS_DITHER_RGB;  // | DirectX::BC_FLAGS_DITHER_A;///* | DirectX::BC_FLAGS_UNIFORM*/0;

        auto bc = CompressImage(m_dds.width, m_dds.height,
                                m_uncompressed_image.data(), m_compression,
                                m_dds.fmt, threads(), alpha_threshold, flags);
        ok &= ioseek(m_image_start);
        ok &= iowrite(bc.data(), bc.size());
    }

    init();  // re-initialize
    return ok;
}

bool
DDSOutput::write_scanline(int y, int z, TypeDesc format, const void* data,
                          stride_t xstride)
{
    if (!ioproxy_opened())
        return false;
    if (z) {
        errorfmt("array texture not supported!");
        // TODO(DDS): array textures
        return false;
    }

    m_scratch.clear();
    data = to_native_scanline(format, data, xstride, m_scratch, m_dither, y, z);

    y -= m_spec.y;
    if (m_compression == Compression::None) {
        ioseek(m_image_start + y * m_dds.pitch);

        bool direct_write = m_spec.format == TypeDesc::UINT8
                            && m_Bpp == m_spec.nchannels;
        for (int ch = 0; direct_write && ch < 4; ++ch)
            direct_write &= m_BitCounts[ch] == 0 || m_BitCounts[ch] == 8;

        if (direct_write) {
            return iowrite(data, m_dds.pitch);
        } else {
            // need to fix bit depths
            if (m_spec.format == TypeDesc::UINT8) {
                const imagesize_t nbytes = m_spec.scanline_bytes(true);

                data       = move_to_scratch(data, nbytes);
                uint8_t* v = (uint8_t*)data;

                if (m_Bpp == 1) {
                    for (int x = 0; x < m_spec.width; ++x) {
                        uint8_t pixel = 0;
                        for (int ch = 0; ch < 4; ++ch) {
                            if (m_BitCounts[ch] != 0) {
                                uint8_t chv
                                    = bit_range_convert(*v, 8, m_BitCounts[ch]);
                                pixel |= chv << m_LeftShifts[ch];

                                v++;
                            }
                        }

                        iowrite(&pixel, sizeof(pixel));
                    }

                    return true;

                } else if (m_Bpp == 2) {
                    for (int x = 0; x < m_spec.width; ++x) {
                        uint16_t pixel = 0;
                        for (int ch = 0; ch < 4; ++ch) {
                            if (m_BitCounts[ch] != 0) {
                                uint16_t chv
                                    = bit_range_convert(*v, 8, m_BitCounts[ch]);
                                pixel |= chv << m_LeftShifts[ch];
                                v++;
                            }
                        }

                        iowrite(&pixel, sizeof(pixel));
                    }
                    return true;
                } else if (m_Bpp == 4) {
                    for (int x = 0; x < m_spec.width; ++x) {
                        uint32_t pixel = 0;
                        for (int ch = 0; ch < 4; ++ch) {
                            if (m_BitCounts[ch] != 0) {
                                uint32_t chv
                                    = bit_range_convert(*v, 8, m_BitCounts[ch]);
                                pixel |= chv << m_LeftShifts[ch];
                                v++;
                            }
                        }

                        iowrite(&pixel, sizeof(pixel));
                    }

                    return true;
                }
            }

            errorfmt(
                "Non-8-bit channel not supported! Bpp = {}, BitCounts=[{}, {}, {}, {}]",
                m_Bpp, m_BitCounts[0], m_BitCounts[1], m_BitCounts[2],
                m_BitCounts[3]);
            return false;
        }
    } else {
        if (m_compression == Compression::DXT1
            || m_compression == Compression::DXT2
            || m_compression == Compression::DXT3
            || m_compression == Compression::DXT4
            || m_compression == Compression::DXT5
            || m_compression == Compression::BC4) {
            // TODO(DDS): take into account isNormal?
            size_t offset = y * m_dds.width
                            * GetChannelCount(m_compression,
                                              /*isNormal=*/false);
            if (m_spec.format == TypeDesc::UINT8) {
                uint8_t* v               = (uint8_t*)data;
                const imagesize_t nbytes = m_spec.scanline_bytes(true);
                for (int i = 0; i < nbytes; ++i) {
                    m_uncompressed_image[offset + i] = v[i] / 255.0f;
                }
                return true;
            } else {
                errorfmt("Unsupported spec format");
                return false;
            }
        }
        errorfmt("Unsupported compression '{}'",
                 CompressionToString(m_compression));
        return false;
    }
}
//
// bool
// DDSOutput::write_scanlines(int ybegin, int yend, int z, TypeDesc format,
//                            const void* data, stride_t xstride, stride_t ystride)
// {
//     std::cerr << "DDSOutput::write_scanlines not implemented!" << std::endl;
//     return false;
// }
//
// bool
// DDSOutput::write_tile(int x, int y, int z, TypeDesc format, const void* data,
//                       stride_t xstride, stride_t ystride, stride_t zstride)
// {
//     std::cerr << "DDSOutput::write_tile not implemented!" << std::endl;
//     return false;
// }
//

OIIO_PLUGIN_NAMESPACE_END
