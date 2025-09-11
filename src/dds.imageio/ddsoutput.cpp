// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>

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
        m_dds.fmt.flags |= DDS_PF_FOURCC;
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

    init();  // re-initialize
    return true;
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

    if (m_compression == Compression::None) {
        y -= m_spec.y;
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
        errorfmt("Compression not supported!");
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
