struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer MandelbrotParams : register(b0)
{
    float2 center;
    float scale;
    uint maxIterations;
    float2 resolution;
    float2 _padding;
};

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
    float aspect = resolution.x / max(resolution.y, 1.0);
    float2 c = center + float2((input.uv.x * 2.0 - 1.0) * scale * aspect, (input.uv.y * 2.0 - 1.0) * scale);
    float2 z = float2(0.0, 0.0);

    uint i = 0;
    for (i = 0; i < maxIterations; ++i)
    {
        float x = z.x * z.x - z.y * z.y + c.x;
        float y = 2.0 * z.x * z.y + c.y;
        z = float2(x, y);

        if (dot(z, z) > 4.0)
        {
            break;
        }
    }

    if (i == maxIterations)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float t = (float)i / (float)maxIterations;
    return float4(0.15 + 0.85 * t, 0.2 * t * t, 0.4 + 0.6 * sqrt(t), 1.0);
}
