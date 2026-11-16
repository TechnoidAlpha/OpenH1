/*
METAL_SHIM.H

What of the Metal Shading Language the Metal emitter's display.ambient_
occlusion "world" functions use (port/linux/src/xgpu_msl.c's world_ao_*),
in C++, so that tools/world_ao_light/msl_check.cpp runs those functions'
very text on the CPU: float3 and float4 (their components, .xyz, .yzw, a
component by index), their arithmetic, the few library functions, the
constant address space, and a 3D texture sampled linear and clamped to its
edge as a GPU samples it (of signed bytes, or of four floats a texel), and
one read by texel (and of two signed bytes a texel: the distance-field
scene's cascades, tools/world_trace/trace_check.cpp).
*/

#ifndef METAL_SHIM_H
#define METAL_SHIM_H

#include <algorithm>
#include <cmath>

#define constant const
#define thread

struct float3
{
	float x, y, z;
	float3() = default;
	float3(float s) : x(s), y(s), z(s) {}
	float3(float a, float b, float c) : x(a), y(b), z(c) {}
	float &operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
	float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
	float3 &operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
	float3 &operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

struct uint3
{
	unsigned int x, y, z;
	explicit uint3(float3 v) : x((unsigned int)v.x), y((unsigned int)v.y), z((unsigned int)v.z) {}
};

/* (its parts kept apart, each set by the constructor: the functions only
read a float4's, and never assign one part) */
struct float4
{
	float x, y, z, w;
	float r;
	float3 xyz, yzw;
	float4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f), r(0.0f), xyz(0.0f), yzw(0.0f) {}
	float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d), r(a), xyz(a, b, c), yzw(b, c, d) {}
};

static inline float3 operator+(float3 a, float3 b) { return float3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline float3 operator-(float3 a, float3 b) { return float3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline float3 operator*(float3 a, float s) { return float3(a.x * s, a.y * s, a.z * s); }
static inline float3 operator*(float s, float3 a) { return float3(a.x * s, a.y * s, a.z * s); }
static inline float3 operator*(float3 a, float3 b) { return float3(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline float3 operator/(float3 a, float s) { return float3(a.x / s, a.y / s, a.z / s); }
static inline float3 operator/(float3 a, float3 b) { return float3(a.x / b.x, a.y / b.y, a.z / b.z); }
static inline float3 operator+(float3 a, float s) { return float3(a.x + s, a.y + s, a.z + s); }
static inline float3 operator-(float3 a, float s) { return float3(a.x - s, a.y - s, a.z - s); }
static inline float4 operator*(float4 a, float s) { return float4(a.x * s, a.y * s, a.z * s, a.w * s); }
static inline float4 operator+(float4 a, float s) { return float4(a.x + s, a.y + s, a.z + s, a.w + s); }
static inline float4 floor(float4 a)
{
	return float4(std::floor(a.x), std::floor(a.y), std::floor(a.z), std::floor(a.w));
}
static inline float3 floor(float3 a) { return float3(std::floor(a.x), std::floor(a.y), std::floor(a.z)); }
static inline float3 min(float3 a, float3 b) { return float3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)); }
static inline float3 max(float3 a, float3 b) { return float3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)); }
static inline float3 clamp(float3 v, float3 low, float3 high)
{
	return float3(std::min(std::max(v.x, low.x), high.x), std::min(std::max(v.y, low.y), high.y),
		std::min(std::max(v.z, low.z), high.z));
}
static inline float3 operator-(float3 a) { return float3(-a.x, -a.y, -a.z); }
static inline float3 abs(float3 a) { return float3(std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)); }
static inline float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float dot(float4 a, float4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline float length(float3 a) { return std::sqrt(dot(a, a)); }
static inline float3 cross(float3 a, float3 b)
{
	return float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float saturate(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
/* (any floats: Metal's literals are floats, C++'s doubles) */
template <typename X, typename L, typename H> static inline float clamp(X x, L low, H high)
{
	return (float)x < (float)low ? (float)low : (float)x > (float)high ? (float)high : (float)x;
}
static inline float mix(float a, float b, float t) { return a + (b - a) * t; }
static inline float rsqrt(float x) { return 1.0f / std::sqrt(x); }
using std::abs;
using std::acos;
using std::atan;
using std::atan2;
using std::max;
using std::min;
using std::sqrt;
static inline float max(float a, double b) { return a > (float)b ? a : (float)b; }

/* (the proxies' grid: its cells' masks, the cell's index) */
typedef unsigned int uint;
struct uint4
{
	uint x, y, z, w;
};
static inline float3 operator+(double s, float3 a) { return float3((float)s + a.x, (float)s + a.y, (float)s + a.z); }

static inline float3 log2(float3 a) { return float3(std::log2(a.x), std::log2(a.y), std::log2(a.z)); }
static inline float3 sign(float3 a)
{
	return float3(a.x > 0.0f ? 1.0f : a.x < 0.0f ? -1.0f : 0.0f, a.y > 0.0f ? 1.0f : a.y < 0.0f ? -1.0f : 0.0f,
		a.z > 0.0f ? 1.0f : a.z < 0.0f ? -1.0f : 0.0f);
}
static inline int clamp(int x, int low, int high) { return x < low ? low : x > high ? high : x; }
using std::floor;
static inline uint ctz(uint x) { return (uint)__builtin_ctz(x); }
/* a SIMD-group's reduction, as a lane sees it: its own bits and the other
lanes' (simd_lanes_others, the check's: their cells' shapes) */
static uint simd_lanes_others;
static inline uint simd_or(uint x) { return x | simd_lanes_others; }
static inline uint simd_broadcast_first(uint x) { return x; }

namespace filter { enum value { linear }; }
namespace address { enum value { clamp_to_edge }; }
namespace coord { enum value { normalized }; }

struct sampler
{
	constexpr sampler(filter::value, address::value, coord::value) {}
};

struct level
{
	float lod;
	level(float l) : lod(l) {}
};

/* the field's atlas: signed bytes, x fastest (world_sdf.h), sampled as R8
snorm; its table: four bytes a texel, read as RGBA8 */
template <typename T> struct texture3d
{
	const signed char *voxels = nullptr;
	const unsigned char *texels = nullptr;
	/* (or four floats a texel, filtered: the vehicles' fields' atlas; or
	two signed bytes a texel, sampled as RG8 snorm: the distance-field
	scene's cascades) */
	const float *values = nullptr;
	const signed char *pairs = nullptr;
	int width, height, depth;
	float4 read(uint3 at) const
	{
		const unsigned char *texel = texels + ((at.z * height + at.y) * width + at.x) * 4;

		return float4(texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f, texel[3] / 255.0f);
	}
	float4 sample(sampler, float3 uvw, level) const
	{
		int size[3] = { width, height, depth }, base[3];
		float fraction[3], result = 0.0f, results[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		for (int axis = 0; axis < 3; axis++)
		{
			float coordinate = uvw[axis] * (float)size[axis] - 0.5f;
			float floor_value = std::floor(coordinate);

			base[axis] = (int)floor_value;
			fraction[axis] = coordinate - floor_value;
		}
		for (int corner = 0; corner < 8; corner++)
		{
			int at[3];
			float weight = 1.0f;

			for (int axis = 0; axis < 3; axis++)
			{
				int bit = (corner >> axis) & 1;

				at[axis] = base[axis] + bit;
				at[axis] = at[axis] < 0 ? 0 : at[axis] >= size[axis] ? size[axis] - 1 : at[axis];
				weight *= bit ? fraction[axis] : 1.0f - fraction[axis];
			}
			if (values)
			{
				for (int part = 0; part < 4; part++)
					results[part] += weight * values[((at[2] * height + at[1]) * width + at[0]) * 4 + part];
				continue;
			}
			if (pairs)
			{
				for (int part = 0; part < 2; part++)
				{
					float texel = (float)pairs[((at[2] * height + at[1]) * width + at[0]) * 2 + part] / 127.0f;

					results[part] += weight * (texel < -1.0f ? -1.0f : texel);
				}
				continue;
			}
			float texel = (float)voxels[(at[2] * height + at[1]) * width + at[0]] / 127.0f;

			result += weight * (texel < -1.0f ? -1.0f : texel);
		}
		if (values || pairs)
			return float4(results[0], results[1], results[2], results[3]);
		return float4(result, 0.0f, 0.0f, 1.0f);
	}
};

#endif
