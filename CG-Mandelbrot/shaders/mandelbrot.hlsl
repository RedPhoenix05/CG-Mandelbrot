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
    float _padding0;

    float3 colorA;
    float _padding1;
    float3 colorB;
    float _padding2;
    float3 colorC;
    float _padding3;
};

float3 Palette(float t)
{
    t = frac(t * paletteCycle);
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

float4 PSMain(VSOutput input) : SV_Target
{
    REAL aspect = (REAL)resolution.x / (REAL)max(resolution.y, 1.0);
    REAL2 c = (REAL2)center + REAL2(((REAL)input.uv.x * (REAL)2.0 - (REAL)1.0) * (REAL)scale * aspect, ((REAL)input.uv.y * (REAL)2.0 - (REAL)1.0) * (REAL)scale);
    REAL2 z = REAL2((REAL)0.0, (REAL)0.0);

    uint i = 0;
    for (i = 0; i < maxIterations; ++i)
    {
        REAL x = z.x * z.x - z.y * z.y + c.x;
        REAL y = (REAL)2.0 * z.x * z.y + c.y;
        z = REAL2(x, y);

        if (z.x * z.x + z.y * z.y > (REAL)4.0)
        {
            break;
        }
    }

    if (i == maxIterations)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float smoothIter = (float)i;
    float magSq = max((float)(z.x * z.x + z.y * z.y), 1.00001);
    float nu = log2(0.5 * log2(magSq));
    smoothIter = smoothIter + 1.0 - nu;

    float t = saturate(smoothIter / (float)maxIterations);
    float3 color = Palette(t);
    return float4(color, 1.0);
}
