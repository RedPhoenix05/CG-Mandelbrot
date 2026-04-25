struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

#ifndef USE_FP64
#define USE_FP64 0
#endif

#if USE_FP64
#define REAL double
#define REAL2 double2
#else
#define REAL float
#define REAL2 float2
#endif

cbuffer MandelbrotParams : register(b0)
{
    float2 center;
    float scale;
    uint maxIterations;

    float2 resolution;
    float paletteCycle;
    float deaaStrength;
    float colorPeriod;
    float _padding0;
    float _padding0b;
    float _padding0c;

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
    REAL2 z = REAL2((REAL)0.0, (REAL)0.0);
    REAL2 dz = REAL2((REAL)0.0, (REAL)0.0);

    uint i = 0;
    for (i = 0; i < maxIterations; ++i)
    {
        dz = REAL2((REAL)2.0 * (z.x * dz.x - z.y * dz.y) + (REAL)1.0,
                   (REAL)2.0 * (z.x * dz.y + z.y * dz.x));

        REAL x = z.x * z.x - z.y * z.y + c.x;
        REAL y = (REAL)2.0 * z.x * z.y + c.y;
        z = REAL2(x, y);

        if (z.x * z.x + z.y * z.y > (REAL)4.0)
        {
            break;
        }
    }

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
}

float4 PSMain(VSOutput input) : SV_Target
{
    REAL aspect = (REAL)resolution.x / (REAL)max(resolution.y, 1.0);
    REAL2 uv = REAL2((REAL)input.uv.x, (REAL)input.uv.y);

    REAL2 c = (REAL2)center + REAL2((uv.x * (REAL)2.0 - (REAL)1.0) * (REAL)scale * aspect, (uv.y * (REAL)2.0 - (REAL)1.0) * (REAL)scale);

    float distanceEstimate = 0.0;
    float smoothIter = ComputeSmoothEscape(c, distanceEstimate);

    if (smoothIter < 0.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float period = max(colorPeriod, 1.0);
    float phase = (smoothIter / period) * paletteCycle;
    float3 color = Palette(phase);

    float pixelWorldSize = scale * 2.0 / max(resolution.y, 1.0);
    float aaWidth = max(pixelWorldSize * deaaStrength, 1e-8);

    // Distance estimate can be conservative; apply a calibrated gain so only
    // near-boundary pixels are softened instead of globally darkening colors.
    float edgeBlend = smoothstep(0.0, aaWidth, distanceEstimate * 6.0);

    // Keep a minimum color contribution to preserve palette richness.
    edgeBlend = 0.35 + 0.65 * edgeBlend;
    color *= edgeBlend;
    return float4(color, 1.0);
}
