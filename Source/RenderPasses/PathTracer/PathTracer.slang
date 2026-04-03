import Utils.Constants;
import Utils.Sampling;
import Renderer.Camera;
import Scene.Scene;
import Scene.Light;
import Scene.ShadingData;
import Scene.BSDF;

ConstantBuffer<Camera> gCamera;
Texture2D<uint4> gVBuffer;
RWTexture2D<float4> gOutput;
RWTexture2D<float4> gAccum;

Texture2D<float4> gEnvMap;
SamplerState gEnvSampler;
int gHasEnvMap;
StructuredBuffer<float> gEnvCdf;
int gHasEnvImportance;
uint gEnvWidth;
uint gEnvHeight;

uint gFrameCount;
uint gMaxBounces;
uint gSeed;
float gExposure;

float2 EnvDirectionToUv(float3 dir)
{
    float u = atan2(dir.z, dir.x) * (0.5 * kInvPI) + 0.5;
    float v = acos(clamp(dir.y, -1.0, 1.0)) * kInvPI;
    return float2(u, v);
}

float3 EnvUvToDirection(float2 uv)
{
    float phi = (uv.x - 0.5) * kTwoPI;
    float theta = uv.y * kPI;
    float sinTheta = sin(theta);
    return float3(cos(phi) * sinTheta, cos(theta), sin(phi) * sinTheta);
}

float3 SampleEnvMapUv(float2 uv)
{
    return gEnvMap.SampleLevel(gEnvSampler, uv, 0).rgb;
}

float3 SampleEnvMap(float3 dir)
{
    if (gHasEnvMap == 0)
        return float3(0);

    return SampleEnvMapUv(EnvDirectionToUv(dir));
}

uint SampleEnvTexel(float u)
{
    uint left = 0;
    uint right = gEnvWidth * gEnvHeight - 1;

    while (left < right)
    {
        uint mid = (left + right) >> 1;
        if (u <= gEnvCdf[mid])
            right = mid;
        else
            left = mid + 1;
    }

    return left;
}

bool SampleEnvironmentLight(inout RNG rng, out float3 dir, out float3 radiance, out float pdf)
{
    dir = float3(0);
    radiance = float3(0);
    pdf = 0.0;

    if (gHasEnvMap == 0 || gHasEnvImportance == 0 || gEnvWidth == 0 || gEnvHeight == 0)
        return false;

    uint texelIndex = SampleEnvTexel(rng.NextFloat());
    uint x = texelIndex % gEnvWidth;
    uint y = texelIndex / gEnvWidth;
    float2 uv = (float2(float(x), float(y)) + rng.NextFloat2()) / float2(float(gEnvWidth), float(gEnvHeight));

    dir = EnvUvToDirection(uv);
    radiance = SampleEnvMapUv(uv);

    float previous = texelIndex > 0 ? gEnvCdf[texelIndex - 1] : 0.0;
    float texelProb = max(gEnvCdf[texelIndex] - previous, 0.0);
    float theta = uv.y * kPI;
    float sinTheta = max(sin(theta), 1e-4);
    float texelSolidAngle =
        (kTwoPI / float(gEnvWidth)) *
        (kPI / float(gEnvHeight)) *
        sinTheta;
    pdf = texelProb / texelSolidAngle;
    return pdf > 0.0;
}

struct PathPayload
{
    uint instanceID;
    uint primitiveID;
    float2 barycentrics;
    float hitT;
    bool missed;
};

bool TraceShadowRay(float3 origin, float3 normal, float3 dir, float maxDist)
{
    RayDesc shadowRay;
    shadowRay.Origin = origin + normal * 0.001;
    shadowRay.Direction = dir;
    shadowRay.TMin = 0.0;
    shadowRay.TMax = maxDist;

    PathPayload shadowPayload;
    shadowPayload.missed = false;

    TraceRay(gScene.tlas,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             0xFF, 0, 0, 0, shadowRay, shadowPayload);

    return shadowPayload.missed;
}

float3 TracePath(ShadingData sd, float3 rayDir, inout RNG rng)
{
    float3 throughput = float3(1.0);
    float3 radiance = sd.emissive;
    float3 position = sd.position;
    float3 normal = sd.normal;
    float3 V = -rayDir;

    for (uint bounce = 0; bounce < gMaxBounces; bounce++)
    {
        BSDFProperties bsdf = BSDFProperties::Create(sd, V);

        // NEE: sample one scene light
        if (gScene.lightCount > 0)
        {
            uint lightIdx = rng.Next() % gScene.lightCount;
            Light light = gScene.lights[lightIdx];

            float3 lightRadiance, lightDir;
            float lightDist;
            light.Evaluate(position, lightRadiance, lightDir, lightDist);

            float NdotL = dot(normal, lightDir);
            if (NdotL > 0.0 && TraceShadowRay(position, sd.geometryNormal, lightDir, lightDist - 0.002))
            {
                float3 brdf = EvalBSDF(bsdf, normal, V, lightDir);
                radiance += throughput * brdf * lightRadiance * NdotL * float(gScene.lightCount);
            }
        }

        // NEE: sample environment light
        if (gHasEnvImportance != 0)
        {
            float3 envRadiance, envDir;
            float envPdf;
            if (SampleEnvironmentLight(rng, envDir, envRadiance, envPdf))
            {
                float NdotL = dot(normal, envDir);
                if (NdotL > 0.0 && TraceShadowRay(position, sd.geometryNormal, envDir, gCamera.farZ))
                {
                    float3 brdf = EvalBSDF(bsdf, normal, V, envDir);
                    radiance += throughput * brdf * envRadiance * NdotL / envPdf;
                }
            }
        }

        // Sample BSDF for indirect bounce
        BSDFSample sample = SampleBSDF(bsdf, normal, V, rng);
        if (sample.pdf <= 0.0)
            break;

        throughput *= sample.weight;

        // Russian roulette after 2 bounces
        if (bounce >= 2)
        {
            float p = max(Luminance(throughput), 0.05);
            p = min(p, 0.95);
            if (rng.NextFloat() > p)
                break;
            throughput /= p;
        }

        // Trace indirect ray
        RayDesc indirectRay;
        indirectRay.Origin = position + sd.geometryNormal * 0.001;
        indirectRay.Direction = sample.wi;
        indirectRay.TMin = 0.0;
        indirectRay.TMax = gCamera.farZ;

        PathPayload indirectPayload;
        indirectPayload.missed = true;

        TraceRay(gScene.tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, indirectRay, indirectPayload);

        if (indirectPayload.missed)
        {
            if (gHasEnvImportance == 0)
                radiance += throughput * SampleEnvMap(sample.wi);
            break;
        }

        sd = PrepareShadingData(indirectPayload.instanceID, indirectPayload.primitiveID,
                                indirectPayload.barycentrics);
        position = sd.position;
        normal = sd.normal;
        V = -sample.wi;
        radiance += throughput * sd.emissive;
    }

    // Clamp fireflies
    float maxVal = max(radiance.x, max(radiance.y, radiance.z));
    if (maxVal > 50.0)
        radiance *= 50.0 / maxVal;

    return radiance;
}

[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    if (pixel.x >= dims.x || pixel.y >= dims.y)
        return;

    uint4 vbufData = gVBuffer.Load(int3(pixel, 0));
    uint instanceID = vbufData.x;
    uint primitiveID = vbufData.y;
    float2 barycentrics = float2(asfloat(vbufData.z), asfloat(vbufData.w));

    float3 color = float3(0);

    if (instanceID != 0xFFFFFFFF)
    {
        RNG rng = CreateRNG(pixel, gSeed);
        ShadingData sd = PrepareShadingData(instanceID, primitiveID, barycentrics);

        float2 uv = (float2(pixel) + 0.5) / float2(dims);
        Ray primaryRay = gCamera.GenerateRay(uv);

        color = TracePath(sd, primaryRay.Direction, rng);
    }
    else
    {
        float2 uv = (float2(pixel) + 0.5) / float2(dims);
        Ray primaryRay = gCamera.GenerateRay(uv);
        color = SampleEnvMap(primaryRay.Direction);
    }

    if (any(isnan(color)) || any(isinf(color)))
        color = float3(0);

    // Accumulate
    if (gFrameCount > 0)
    {
        float3 prev = gAccum[pixel].rgb;
        float t = 1.0 / float(gFrameCount + 1);
        color = lerp(prev, color, t);
    }

    gAccum[pixel] = float4(color, 1.0);

    // Exposure + ACES tonemapping + gamma
    color *= gExposure;
    float3 tonemapped = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    float3 gammaCorrected = pow(saturate(tonemapped), float3(1.0 / 2.2));
    gOutput[pixel] = float4(gammaCorrected, 1.0);
}

[shader("closesthit")]
void closestHit(inout PathPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    payload.missed = false;
    payload.instanceID = InstanceID();
    payload.primitiveID = PrimitiveIndex();
    payload.barycentrics = attribs.barycentrics;
    payload.hitT = RayTCurrent();
}

[shader("miss")]
void miss(inout PathPayload payload)
{
    payload.missed = true;
}
