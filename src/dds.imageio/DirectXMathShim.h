// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

// Minimal implementation of DirectXMath using OIIO's SIMD library, for use with DirectXTex's BC compression code.

#include <cstdint>

#include <OpenImageIO/fmath.h>
#include <OpenImageIO/half.h>
#include <OpenImageIO/simd.h>

namespace DirectX {

struct OIIO_SIMD4_ALIGN XMVECTOR {
    union {
        OIIO::simd::vfloat4 vf;
        OIIO::simd::vint4 vi;
        struct {
            float x, y, z, w;
        };
    };
};
static_assert(std::is_trivially_copyable_v<XMVECTOR>);
static_assert(sizeof(XMVECTOR) == 16);

using XMVECTORF32 = XMVECTOR;
using XMVECTORU32 = XMVECTOR;
using XMFLOAT4    = XMVECTOR;
using XMFLOAT4A   = XMVECTOR;
using XMU565      = uint16_t;

struct XMINT4 {
    int x, y, z, w;
};

constexpr uint32_t XM_SELECT_0 = 0;
constexpr uint32_t XM_SELECT_1 = ~0;
inline const XMVECTORU32 g_XMSelect1110
    = { { { XM_SELECT_1, XM_SELECT_1, XM_SELECT_1, XM_SELECT_0 } } };
inline const XMVECTORF32 g_XMIdentityR3 = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };

inline XMVECTOR
XMVectorSubtract(const XMVECTOR& a, const XMVECTOR& b) noexcept
{
    return { a.vf - b.vf };
}

inline XMVECTOR
XMVectorMultiply(const XMVECTOR& a, const XMVECTOR& b) noexcept
{
    return { a.vf * b.vf };
}

inline XMVECTOR
XMVector4Dot(const XMVECTOR& a, const XMVECTOR& b) noexcept
{
    return { OIIO::simd::vdot(a.vf, b.vf) };
}

inline XMVECTOR
XMVector3Dot(const XMVECTOR& a, const XMVECTOR& b) noexcept
{
    return { OIIO::simd::vdot3(a.vf, b.vf) };
}

template<uint32_t X, uint32_t Y, uint32_t Z, uint32_t W>
inline XMVECTOR
XMVectorSwizzle(const XMVECTOR& V) noexcept
{
    XMVECTOR r;
    r.vi = OIIO::simd::shuffle<X, Y, Z, W>(V.vi);
    return r;
}

inline XMVECTOR
XMVectorZero() noexcept
{
    return { OIIO::simd::vfloat4::Zero() };
}

inline XMVECTOR
XMVectorLerp(const XMVECTOR& a, const XMVECTOR& b, float t) noexcept
{
    return { OIIO::lerp(a.vf, b.vf, t) };
}

inline XMVECTOR
XMVectorSelect(const XMVECTOR& a, const XMVECTOR& b,
               const XMVECTORU32& control) noexcept
{
    XMVECTOR r;
    r.vi = OIIO::simd::select(control.vi == XM_SELECT_0, a.vi, b.vi);
    return r;
}

inline XMVECTOR
XMLoadU565(const XMU565* source) noexcept
{
    return { { { static_cast<float>((*source >> 0) & 0x1F),
                 static_cast<float>((*source >> 5) & 0x3F),
                 static_cast<float>((*source >> 11) & 0x1F), 0.0f } } };
}

inline XMVECTOR
XMLoadSInt4(const XMINT4* source) noexcept
{
    return { { { static_cast<float>(source->x), static_cast<float>(source->y),
                 static_cast<float>(source->z),
                 static_cast<float>(source->w) } } };
}

inline XMVECTOR
XMLoadFloat4(const XMFLOAT4* source)
{
    return *source;
}

inline void
XMStoreFloat4(XMFLOAT4* dest, const XMVECTOR& V) noexcept
{
    *dest = V;
}

inline void
XMStoreFloat4A(XMFLOAT4A* dest, const XMVECTOR& V) noexcept
{
    *dest = V;
}

inline XMVECTOR
XMVectorSet(float x, float y, float z, float w) noexcept
{
    return { { { x, y, z, w } } };
}

inline XMVECTOR
XMVectorSetW(const XMVECTOR& V, float w) noexcept
{
    XMVECTOR r = V;
    r.vf.set_w(w);
    return r;
}

inline float
XMVectorGetX(const XMVECTOR& V) noexcept
{
    return V.vf.x();
}

namespace PackedVector {
    using HALF = half;
    static_assert(std::is_trivially_copyable_v<HALF>);
    static_assert(sizeof(HALF) == 2);

    inline float XMConvertHalfToFloat(HALF v) noexcept { return v; }

    struct XMHALF4 {
        union {
            half v[4];
            struct {
                half x, y, z, w;
            };
        };
    };
    static_assert(std::is_trivially_copyable_v<XMHALF4>);
    static_assert(sizeof(XMHALF4) == 8);

    inline void XMStoreHalf4(XMHALF4* dest, const XMVECTOR& V) noexcept
    {
        V.vf.store(dest->v);
    }


    struct XMUBYTE4 {
        union {
            uint8_t v[4];
            struct {
                uint8_t x, y, z, w;
            };
        };
    };

    inline XMVECTOR XMLoadUByte4(const XMUBYTE4* source) noexcept
    {
        XMVECTOR r;
        r.vf.load(source->v);
        return r;
    }
}  // namespace PackedVector

}  // namespace DirectX
