// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

// Minimal implementation of DirectXMath using OIIO's SIMD library, for use with DirectXTex's BC compression code.

#include <cstdint>

#include <OpenImageIO/simd.h>

namespace DirectX {

struct OIIO_SIMD4_ALIGN XMVECTOR {
    union {
        OIIO::simd::vfloat4 vf;
        OIIO::simd::vint4 vi;
    };
};
static_assert(std::is_trivially_copyable_v<XMVECTOR>);
static_assert(sizeof(XMVECTOR) == 16);

using XMVECTORF32 = XMVECTOR;
using XMVECTORU32 = XMVECTOR;
using XMFLOAT4    = XMVECTOR;
using XMFLOAT4A   = XMVECTOR;
using XMU565      = uint16_t;

constexpr uint32_t XM_SELECT_0 = 0;
constexpr uint32_t XM_SELECT_1 = ~0;
inline const XMVECTORU32 g_XMSelect1110
    = { { { XM_SELECT_1, XM_SELECT_1, XM_SELECT_1, XM_SELECT_0 } } };
inline const XMVECTORF32 g_XMIdentityR3 = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };

inline XMVECTOR
XMVectorMultiply(const XMVECTOR& a, const XMVECTOR& b) noexcept
{
    return { a.vf * b.vf };
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
    return { { { 0.0f, 0.0f, 0.0f, 0.0f } } };
}

inline XMVECTOR
XMVectorLerp(const XMVECTOR& a, const XMVECTOR& b, float t) noexcept
{
    return { a.vf * (1 - t) + b.vf * t };
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

inline void
XMStoreFloat4(XMFLOAT4* dest, const XMVECTOR& V) noexcept
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

}  // namespace DirectX
