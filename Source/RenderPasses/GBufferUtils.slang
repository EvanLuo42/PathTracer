// Octahedral normal encoding/decoding
// Maps unit sphere normal to [-1,1]^2, compact and GPU-friendly.

float2 octWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * select(v >= 0.0, float2(1.0), float2(-1.0));
}

float2 encodeNormalOctahedral(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 oct = n.z >= 0.0 ? n.xy : octWrap(n.xy);
    return oct;
}

float3 decodeNormalOctahedral(float2 oct)
{
    float3 n = float3(oct.xy, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0)
        n.xy = octWrap(n.xy);
    return normalize(n);
}

// Reconstruct world position from depth buffer + inverse view-projection
float3 reconstructWorldPosition(float2 uv, float depth, float4x4 invViewProj)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}
