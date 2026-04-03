import Utils.Constants;

struct EnvMap
{
    Texture2D texture;
    SamplerState sampler;

    float4 sample(float3 dir)
    {
        float phi = atan2(dir.z, dir.x);
        float theta = asin(clamp(dir.y, -1.0, 1.0));
        float2 uv = float2(phi / (2.0 * kPI) + 0.5, 0.5 - theta / kPI);
        return texture.SampleLevel(sampler, uv, 0);
    }
};
