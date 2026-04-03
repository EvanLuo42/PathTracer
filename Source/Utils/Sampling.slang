import Utils.Constants;

struct RNG
{
    uint state;

    [mutating] uint Next()
    {
        state = state * 747796405u + 2891336453u;
        uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    [mutating] float NextFloat()
    {
        return float(Next()) / 4294967296.0;
    }

    [mutating] float2 NextFloat2()
    {
        return float2(NextFloat(), NextFloat());
    }
};

RNG CreateRNG(uint2 pixel, uint seed)
{
    RNG rng;
    rng.state = pixel.x * 1973u + pixel.y * 9277u + seed * 26699u + 1u;
    rng.Next();
    return rng;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

void BuildOrthonormalBasis(float3 N, out float3 T, out float3 B)
{
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

float3 ToWorld(float3 localDir, float3 N)
{
    float3 T, B;
    BuildOrthonormalBasis(N, T, B);
    return T * localDir.x + B * localDir.y + N * localDir.z;
}

float3 SampleCosineHemisphere(float2 u, float3 N)
{
    float phi = kTwoPI * u.x;
    float cosTheta = sqrt(1.0 - u.y);
    float sinTheta = sqrt(u.y);

    return ToWorld(float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta), N);
}

float PdfCosineHemisphere(float NdotL)
{
    return NdotL > 0.0 ? NdotL * kInvPI : 0.0;
}

float3 SampleGGXHalfVector(float2 u, float3 N, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float phi = kTwoPI * u.x;
    float cosTheta = sqrt((1.0 - u.y) / max(1.0 + (a2 - 1.0) * u.y, 1e-6));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float3 localH = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return normalize(ToWorld(localH, N));
}
