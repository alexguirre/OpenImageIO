// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

// Minimal implementation of DirectXMath using Imath, for use with DirectXTex's BC compression code.

#include <cstdint>

#include <OpenImageIO/Imath.h>

namespace DirectX {

struct XMVECTOR {
    union {
        float vf[4];
        uint32_t vu[4];
    };

    inline const Imath::V4f& f() const noexcept
    {
        return reinterpret_cast<const Imath::V4f&>(*this);
    }
};
static_assert(std::is_trivially_copyable_v<XMVECTOR>);
static_assert(sizeof(XMVECTOR) == 16);
static_assert(sizeof(XMVECTOR) == sizeof(Imath::V4f));

inline XMVECTOR
from_imath(const Imath::V4f& v) noexcept
{
    return reinterpret_cast<const XMVECTOR&>(v);
}


using XMVECTORF32 = XMVECTOR;
using XMVECTORU32 = XMVECTOR;
using XMFLOAT4    = XMVECTOR;
using XMU565      = uint16_t;

inline XMVECTORU32
from_u32x4(uint32_t x, uint32_t y, uint32_t z, uint32_t w) noexcept
{
    XMVECTORU32 v;
    v.vu[0] = x;
    v.vu[1] = y;
    v.vu[2] = z;
    v.vu[3] = w;
    return v;
}

constexpr uint32_t XM_SELECT_0 = 0;
constexpr uint32_t XM_SELECT_1 = ~0;
constexpr XMVECTORU32 g_XMSelect1110
    = { { { XM_SELECT_1, XM_SELECT_1, XM_SELECT_1, XM_SELECT_0 } } };
constexpr XMVECTORF32 g_XMIdentityR3 = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };

inline XMVECTOR
XMVectorMultiply(const XMVECTOR& a, const XMVECTOR& b) noexcept
{
    return from_imath(a.f() * b.f());
}

template<uint32_t X, uint32_t Y, uint32_t Z, uint32_t W>
inline XMVECTOR
XMVectorSwizzle(const XMVECTOR& V) noexcept
{
    const auto x = V.vu[X];
    const auto y = V.vu[Y];
    const auto z = V.vu[Z];
    const auto w = V.vu[W];
    return from_u32x4(x, y, z, w);
}

inline XMVECTOR
XMVectorZero() noexcept
{
    return { { { 0.0f, 0.0f, 0.0f, 0.0f } } };
}

inline XMVECTOR
XMVectorLerp(const XMVECTOR& a, const XMVECTOR& b, float t) noexcept
{
    return from_imath(Imath::lerp(a.f(), b.f(), t));
}

inline XMVECTOR
XMVectorSelect(const XMVECTOR& a, const XMVECTOR& b,
               const XMVECTORU32& control) noexcept
{
    const uint32_t xs[] = { a.vu[0], b.vu[0] };
    const uint32_t ys[] = { a.vu[1], b.vu[1] };
    const uint32_t zs[] = { a.vu[2], b.vu[2] };
    const uint32_t ws[] = { a.vu[3], b.vu[3] };
    XMVECTOR r;
    r.vu[0] = xs[control.vu[0] == XM_SELECT_0 ? 0 : 1];
    r.vu[1] = ys[control.vu[1] == XM_SELECT_0 ? 0 : 1];
    r.vu[2] = zs[control.vu[2] == XM_SELECT_0 ? 0 : 1];
    r.vu[3] = ws[control.vu[3] == XM_SELECT_0 ? 0 : 1];
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
XMVectorSetW(const XMVECTOR& V, float w) noexcept
{
    XMVECTOR r = V;
    r.vf[3]    = w;
    return r;
}

}  // namespace DirectX
