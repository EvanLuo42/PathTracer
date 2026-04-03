import Utils.Constants;
import Utils.Sampling;
import Scene.ShadingData;

struct BSDFSample
{
    float3 wi;
    float3 weight;
    float pdf;
};

struct BSDFProperties
{
    float3 diffuseColor;
    float3 f0;
    float roughness;
    float NdotV;

    static BSDFProperties Create(ShadingData sd, float3 V)
    {
        BSDFProperties props;
        props.roughness = max(sd.roughness, 0.04);
        props.diffuseColor = sd.baseColor.rgb * (1.0 - sd.metallic);
        props.f0 = lerp(float3(0.04), sd.baseColor.rgb, sd.metallic);
        props.NdotV = max(dot(sd.normal, V), 0.001);
        return props;
    }
};

float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (kPI * d * d);
}

float G1_Smith(float NdotX, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    return 2.0 * NdotX / (NdotX + sqrt(a2 + (1.0 - a2) * NdotX * NdotX));
}

float3 F_Schlick(float3 f0, float VdotH)
{
    float t = 1.0 - VdotH;
    float t2 = t * t;
    return f0 + (1.0 - f0) * (t2 * t2 * t);
}

float PdfGGXReflection(float NdotH, float VdotH, float roughness)
{
    if (NdotH <= 0.0 || VdotH <= 0.0)
        return 0.0;

    return D_GGX(NdotH, roughness) * NdotH / max(4.0 * VdotH, 1e-4);
}

float3 EvalBSDF(BSDFProperties props, float3 N, float3 V, float3 L)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float3 F = F_Schlick(props.f0, VdotH);
    float D = D_GGX(NdotH, props.roughness);
    float G1V = G1_Smith(props.NdotV, props.roughness);
    float G1L = G1_Smith(NdotL, props.roughness);
    float3 specular = D * G1V * G1L * F / (4.0 * props.NdotV * NdotL + 0.0001);
    float3 diffuse = props.diffuseColor * (1.0 - F) * kInvPI;

    return diffuse + specular;
}

float GetSpecularProbability(BSDFProperties props)
{
    float specularWeight = max(max(props.f0.x, props.f0.y), props.f0.z);
    float diffuseWeight = max(max(props.diffuseColor.x, props.diffuseColor.y), props.diffuseColor.z);
    return clamp(specularWeight / max(specularWeight + diffuseWeight, 1e-4), 0.1, 0.95);
}

float EvalPdf(BSDFProperties props, float3 N, float3 V, float3 L)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float specularProb = GetSpecularProbability(props);
    float diffusePdf = PdfCosineHemisphere(NdotL);
    float specularPdf = PdfGGXReflection(NdotH, VdotH, props.roughness);
    return (1.0 - specularProb) * diffusePdf + specularProb * specularPdf;
}

BSDFSample SampleBSDF(BSDFProperties props, float3 N, float3 V, inout RNG rng)
{
    BSDFSample result;
    result.wi = float3(0);
    result.weight = float3(0);
    result.pdf = 0.0;

    float specularProb = GetSpecularProbability(props);

    if (rng.NextFloat() < specularProb)
    {
        float3 H = SampleGGXHalfVector(rng.NextFloat2(), N, props.roughness);
        result.wi = reflect(-V, H);
    }
    else
    {
        result.wi = SampleCosineHemisphere(rng.NextFloat2(), N);
    }

    float NdotL = max(dot(N, result.wi), 0.0);
    if (NdotL <= 0.0)
        return result;

    float3 brdf = EvalBSDF(props, N, V, result.wi);
    result.pdf = EvalPdf(props, N, V, result.wi);

    if (result.pdf <= 1e-6)
    {
        result.pdf = 0.0;
        return result;
    }

    result.weight = brdf * NdotL / result.pdf;
    return result;
}
