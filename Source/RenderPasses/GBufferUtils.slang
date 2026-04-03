float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * select(v >= 0.0, float2(1.0), float2(-1.0));
}

float2 EncodeNormalOctahedral(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 oct = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    return oct;
}

float3 DecodeNormalOctahedral(float2 oct)
{
    float3 n = float3(oct.xy, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0)
        n.xy = OctWrap(n.xy);
    return normalize(n);
}

float3 ReconstructWorldPosition(float2 uv, float depth, float4x4 invViewProj)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}
