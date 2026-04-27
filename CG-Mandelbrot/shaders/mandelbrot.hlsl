struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

#ifndef USE_FP64
#define USE_FP64 0
#endif

#ifndef USE_FP128
#define USE_FP128 0
#endif

// Double-Double (Quad Precision) Implementation
// Uses pairs of doubles to achieve ~128-bit precision
#if USE_FP128

struct dd_real
{
    double hi;
    double lo;
};

struct dd_real2
{
    dd_real x;
    dd_real y;
};

// Create dd_real from double
dd_real dd_from_double(double a)
{
    dd_real result;
    result.hi = a;
    result.lo = 0.0;
    return result;
}

// Quick-Two-Sum: assumes |a| >= |b|
dd_real quick_two_sum(double a, double b)
{
    dd_real result;
    result.hi = a + b;
    result.lo = b - (result.hi - a);
    return result;
}

// Two-Sum: no assumption on magnitudes
dd_real two_sum(double a, double b)
{
    dd_real result;
    result.hi = a + b;
    double v = result.hi - a;
    result.lo = (a - (result.hi - v)) + (b - v);
    return result;
}

// Split a double into high and low parts for exact multiplication
void split(double a, out double hi, out double lo)
{
    const double SPLIT = 134217729.0; // 2^27 + 1
    double temp = SPLIT * a;
    hi = temp - (temp - a);
    lo = a - hi;
}

// Two-Product: exact product of two doubles
dd_real two_product(double a, double b)
{
    dd_real result;
    result.hi = a * b;

    double a_hi, a_lo, b_hi, b_lo;
    split(a, a_hi, a_lo);
    split(b, b_hi, b_lo);

    result.lo = ((a_hi * b_hi - result.hi) + a_hi * b_lo + a_lo * b_hi) + a_lo * b_lo;
    return result;
}

// Add two dd_reals
dd_real dd_add(dd_real a, dd_real b)
{
    dd_real s = two_sum(a.hi, b.hi);
    dd_real t = two_sum(a.lo, b.lo);
    s.lo += t.hi;
    s = quick_two_sum(s.hi, s.lo);
    s.lo += t.lo;
    s = quick_two_sum(s.hi, s.lo);
    return s;
}

// Add dd_real and double
dd_real dd_add_d(dd_real a, double b)
{
    dd_real s = two_sum(a.hi, b);
    s.lo += a.lo;
    s = quick_two_sum(s.hi, s.lo);
    return s;
}

// Multiply two dd_reals
dd_real dd_mul(dd_real a, dd_real b)
{
    dd_real p = two_product(a.hi, b.hi);
    p.lo += a.hi * b.lo;
    p.lo += a.lo * b.hi;
    p = quick_two_sum(p.hi, p.lo);
    return p;
}

// Multiply dd_real by double
dd_real dd_mul_d(dd_real a, double b)
{
    dd_real p = two_product(a.hi, b);
    p.lo += a.lo * b;
    p = quick_two_sum(p.hi, p.lo);
    return p;
}

// Square a dd_real
dd_real dd_sqr(dd_real a)
{
    dd_real p = two_product(a.hi, a.hi);
    p.lo += 2.0 * a.hi * a.lo;
    p = quick_two_sum(p.hi, p.lo);
    return p;
}

// Compare magnitude squared with threshold
bool dd_mag_sq_gt(dd_real2 z, double threshold)
{
    // Compute z.x^2 + z.y^2 and compare with threshold
    dd_real x_sq = dd_sqr(z.x);
    dd_real y_sq = dd_sqr(z.y);
    dd_real mag_sq = dd_add(x_sq, y_sq);
    return mag_sq.hi > threshold;
}

// Get magnitude squared as float
float dd_mag_sq_to_float(dd_real2 z)
{
    dd_real x_sq = dd_sqr(z.x);
    dd_real y_sq = dd_sqr(z.y);
    dd_real mag_sq = dd_add(x_sq, y_sq);
    return (float)(mag_sq.hi + mag_sq.lo);
}

// Get magnitude as float
float dd_mag_to_float(dd_real2 z)
{
    return sqrt(dd_mag_sq_to_float(z));
}

#define REAL dd_real
#define REAL2 dd_real2

#elif USE_FP64
#define REAL double
#define REAL2 double2
#else
#define REAL float
#define REAL2 float2
#endif

cbuffer MandelbrotParams : register(b0)
{
    // Always use the same layout (4 doubles = 32 bytes)
    // In FP32/FP64 mode, only first 2 floats (centerX_hi as float2) are used
    double centerX_hi;
    double centerX_lo;
    double centerY_hi;
    double centerY_lo;

    float scale;
    uint maxIterations;

    float2 resolution;
    float paletteCycle;
    float deaaStrength;
    float colorPeriod;
    float _padding0;
    float _padding0b;
    float _padding0c;
    float _padding_before_colorA; // Align colorA to 16-byte boundary

    float3 colorA;
    float _padding1;
    float3 colorB;
    float _padding2;
    float3 colorC;
    float _padding3;
};

float3 Palette(float t)
{
    t = frac(t);
    if (t < 0.5)
    {
        return lerp(colorA, colorB, t * 2.0);
    }

    return lerp(colorB, colorC, (t - 0.5) * 2.0);
}

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 pos;
    if (vertexId == 0) pos = float2(-1.0, -1.0);
    else if (vertexId == 1) pos = float2(-1.0, 3.0);
    else pos = float2(3.0, -1.0);

    VSOutput output;
    output.position = float4(pos, 0.0, 1.0);
    output.uv = pos * 0.5 + 0.5;
    return output;
}

float ComputeSmoothEscape(REAL2 c, out float distanceEstimate)
{
#if USE_FP128
    // Double-double arithmetic path
    dd_real2 z;
    z.x = dd_from_double(0.0);
    z.y = dd_from_double(0.0);

    dd_real2 dz;
    dz.x = dd_from_double(0.0);
    dz.y = dd_from_double(0.0);

    uint i = 0;
    for (i = 0; i < maxIterations; ++i)
    {
        // dz = 2*z*dz + 1
        // Real: 2*(zx*dzx - zy*dzy) + 1
        // Imag: 2*(zx*dzy + zy*dzx)
        dd_real zx_dzx = dd_mul(z.x, dz.x);
        dd_real zy_dzy = dd_mul(z.y, dz.y);
        dd_real zx_dzy = dd_mul(z.x, dz.y);
        dd_real zy_dzx = dd_mul(z.y, dz.x);

        dd_real temp = dd_add(zx_dzx, dd_mul_d(zy_dzy, -1.0));
        dz.x = dd_add_d(dd_mul_d(temp, 2.0), 1.0);
        dz.y = dd_mul_d(dd_add(zx_dzy, zy_dzx), 2.0);

        // this is the mandelbrot equation
        // z = z^2 + c
        dd_real zx_sq = dd_sqr(z.x);
        dd_real zy_sq = dd_sqr(z.y);
        dd_real zx_zy = dd_mul(z.x, z.y);

        dd_real new_x = dd_add(dd_add(zx_sq, dd_mul_d(zy_sq, -1.0)), c.x);
        dd_real new_y = dd_add(dd_mul_d(zx_zy, 2.0), c.y);

        z.x = new_x;
        z.y = new_y;

        // if magnitude exceeds 2 (^2 > 4), it escaped and stop iterating
        if (dd_mag_sq_gt(z, 4.0))
        {
            break;
        }
    }

    // point is in the set, return -1 to indicate no escape
    if (i >= maxIterations)
    {
        distanceEstimate = 0.0;
        return -1.0;
    }

    float magSq = max(dd_mag_sq_to_float(z), 1.00001);
    float nu = log2(0.5 * log2(magSq));
    float zMag = sqrt(magSq);
    float dzMag = max(dd_mag_to_float(dz), 1e-12);
    distanceEstimate = 0.5 * log(zMag) * zMag / dzMag;
    return ((float)i + 1.0 - nu);

#else
    // Standard float/double path
    REAL2 z = REAL2((REAL)0.0, (REAL)0.0);
    REAL2 dz = REAL2((REAL)0.0, (REAL)0.0);

    uint i = 0;
    for (i = 0; i < maxIterations; ++i)
    {
        dz = REAL2((REAL)2.0 * (z.x * dz.x - z.y * dz.y) + (REAL)1.0,
                   (REAL)2.0 * (z.x * dz.y + z.y * dz.x));

        // this is the mandelbrot equation
        REAL x = z.x * z.x - z.y * z.y + c.x;
        REAL y = (REAL)2.0 * z.x * z.y + c.y;
        z = REAL2(x, y);

        // if magnitude exceeds 2 (^2 > 4), it escaped and stop iterating
        if (z.x * z.x + z.y * z.y > (REAL)4.0)
        {
            break;
        }
    }

    // point is in the set, return -1 to indicate no escape
    if (i >= maxIterations)
    {
        distanceEstimate = 0.0;
        return -1.0;
    }

    float magSq = max((float)(z.x * z.x + z.y * z.y), 1.00001);
    float nu = log2(0.5 * log2(magSq));
    float zMag = sqrt(magSq);
    float dzMag = max((float)sqrt(dz.x * dz.x + dz.y * dz.y), 1e-12);
    distanceEstimate = 0.5 * log(zMag) * zMag / dzMag;
    return ((float)i + 1.0 - nu);
#endif
}

float4 PSMain(VSOutput input) : SV_Target
{
    float2 safeResolution = max(resolution, float2(1.0, 1.0));

#if USE_FP128
    // Double-double coordinate calculation
    double aspect = (double)safeResolution.x / (double)safeResolution.y;

    dd_real uv_x = dd_from_double((double)input.uv.x);
    dd_real uv_y = dd_from_double((double)input.uv.y);

    // Reconstruct center from hi/lo components
    dd_real2 center;
    center.x.hi = centerX_hi;
    center.x.lo = centerX_lo;
    center.y.hi = centerY_hi;
    center.y.lo = centerY_lo;

    // c = center + (uv * 2 - 1) * scale * aspect
    dd_real offset_x = dd_mul_d(dd_add_d(dd_mul_d(uv_x, 2.0), -1.0), (double)scale * aspect);
    dd_real offset_y = dd_mul_d(dd_add_d(dd_mul_d(uv_y, 2.0), -1.0), (double)scale);

    dd_real2 c;
    c.x = dd_add(center.x, offset_x);
    c.y = dd_add(center.y, offset_y);
#else
    // For FP32/FP64 modes, read center from the hi components (they store the full value)
    REAL aspect = (REAL)safeResolution.x / (REAL)safeResolution.y;
    REAL2 uv = REAL2((REAL)input.uv.x, (REAL)input.uv.y);

    // centerX_hi and centerY_hi contain the center position (as doubles or reinterpreted as floats)
    REAL2 centerPos = REAL2((REAL)centerX_hi, (REAL)centerY_hi);

    REAL2 c = centerPos + REAL2((uv.x * (REAL)2.0 - (REAL)1.0) * (REAL)scale * aspect, (uv.y * (REAL)2.0 - (REAL)1.0) * (REAL)scale);
#endif

    float distanceEstimate = 0.0;
    float smoothIter = ComputeSmoothEscape(c, distanceEstimate);

    if (smoothIter < 0.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float period = max(colorPeriod, 1.0);
    float phase = (smoothIter / period) * paletteCycle;
    float3 color = Palette(phase);

    float pixelWorldSize = scale * 2.0 / safeResolution.y;
    float aaWidth = max(pixelWorldSize * deaaStrength, 1e-8);

    // Distance estimate can be conservative; apply a calibrated gain so only
    // near-boundary pixels are softened instead of globally darkening colors.
    float edgeBlend = smoothstep(0.0, aaWidth, distanceEstimate * 6.0);

    // Keep a minimum color contribution to preserve palette richness.
    edgeBlend = 0.35 + 0.65 * edgeBlend;
    color *= edgeBlend;
    return float4(color, 1.0);
}
