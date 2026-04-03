import Renderer.Camera;
import Scene.Scene;
import Scene.ShadingData;
import Scene.Light;

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

// ---- RNG (PCG) ----

struct RNG
{
    uint state;

    [mutating] uint next()
    {
        state = state * 747796405u + 2891336453u;
        uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    [mutating] float nextFloat()
    {
        return float(next()) / 4294967296.0;
    }

    [mutating] float2 nextFloat2()
    {
        return float2(nextFloat(), nextFloat());
    }
};

RNG createRNG(uint2 pixel, uint seed)
{
    RNG rng;
    rng.state = pixel.x * 1973u + pixel.y * 9277u + seed * 26699u + 1u;
    rng.next();
    return rng;
}

// ---- Environment map ----

static const float PI = 3.14159265358979323846;

float2 envDirectionToUv(float3 dir)
{
    float u = atan2(dir.z, dir.x) * (0.5 / PI) + 0.5;
    float v = acos(clamp(dir.y, -1.0, 1.0)) * (1.0 / PI);
    return float2(u, v);
}

float3 envUvToDirection(float2 uv)
{
    float phi = (uv.x - 0.5) * (2.0 * PI);
    float theta = uv.y * PI;
    float sinTheta = sin(theta);
    return float3(cos(phi) * sinTheta, cos(theta), sin(phi) * sinTheta);
}

float3 sampleEnvMapUv(float2 uv)
{
    return gEnvMap.SampleLevel(gEnvSampler, uv, 0).rgb;
}

float3 sampleEnvMap(float3 dir)
{
    if (gHasEnvMap == 0)
        return float3(0);

    return sampleEnvMapUv(envDirectionToUv(dir));
}

uint sampleEnvTexel(float u)
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

bool sampleEnvironmentLight(inout RNG rng, out float3 dir, out float3 radiance, out float pdf)
{
    dir = float3(0);
    radiance = float3(0);
    pdf = 0.0;

    if (gHasEnvMap == 0 || gHasEnvImportance == 0 || gEnvWidth == 0 || gEnvHeight == 0)
        return false;

    uint texelIndex = sampleEnvTexel(rng.nextFloat());
    uint x = texelIndex % gEnvWidth;
    uint y = texelIndex / gEnvWidth;
    float2 uv = (float2(float(x), float(y)) + rng.nextFloat2()) / float2(float(gEnvWidth), float(gEnvHeight));

    dir = envUvToDirection(uv);
    radiance = sampleEnvMapUv(uv);

    float previous = texelIndex > 0 ? gEnvCdf[texelIndex - 1] : 0.0;
    float texelProb = max(gEnvCdf[texelIndex] - previous, 0.0);
    float theta = uv.y * PI;
    float sinTheta = max(sin(theta), 1e-4);
    float texelSolidAngle =
        (2.0 * PI / float(gEnvWidth)) *
        (PI / float(gEnvHeight)) *
        sinTheta;
    pdf = texelProb / texelSolidAngle;
    return pdf > 0.0;
}

// ---- Sampling ----

void buildOrthonormalBasis(float3 N, out float3 T, out float3 B)
{
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

float3 toWorld(float3 localDir, float3 N)
{
    float3 T, B;
    buildOrthonormalBasis(N, T, B);
    return T * localDir.x + B * localDir.y + N * localDir.z;
}

float3 sampleCosineHemisphere(float2 u, float3 N)
{
    float phi = 2.0 * PI * u.x;
    float cosTheta = sqrt(1.0 - u.y);
    float sinTheta = sqrt(u.y);

    return toWorld(float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta), N);
}

float pdfCosineHemisphere(float NdotL)
{
    return NdotL > 0.0 ? NdotL / PI : 0.0;
}

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 sampleGGXHalfVector(float2 u, float3 N, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float phi = 2.0 * PI * u.x;
    float cosTheta = sqrt((1.0 - u.y) / max(1.0 + (a2 - 1.0) * u.y, 1e-6));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float3 localH = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return normalize(toWorld(localH, N));
}

// ---- Path payload ----

struct PathPayload
{
    uint instanceID;
    uint primitiveID;
    float2 barycentrics;
    float hitT;
    bool missed;
};

// ---- Path tracing ----

bool traceShadowRay(float3 origin, float3 normal, float3 dir, float maxDist)
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

// GGX NDF
float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Smith G1
float G1_Smith(float NdotX, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    return 2.0 * NdotX / (NdotX + sqrt(a2 + (1.0 - a2) * NdotX * NdotX));
}

// Schlick Fresnel
float3 F_Schlick(float3 f0, float VdotH)
{
    float t = 1.0 - VdotH;
    float t2 = t * t;
    return f0 + (1.0 - f0) * (t2 * t2 * t);
}

float pdfGGXReflection(float NdotH, float VdotH, float roughness)
{
    if (NdotH <= 0.0 || VdotH <= 0.0)
        return 0.0;

    return D_GGX(NdotH, roughness) * NdotH / max(4.0 * VdotH, 1e-4);
}

float3 tracePath(ShadingData sd, float3 rayDir, inout RNG rng)
{
    float3 throughput = float3(1.0);
    float3 radiance = sd.emissive;
    float3 position = sd.position;
    float3 normal = sd.normal;
    float3 V = -rayDir;

    for (uint bounce = 0; bounce < gMaxBounces; bounce++)
    {
        float metallic = sd.metallic;
        float roughness = max(sd.roughness, 0.04);
        float3 baseColor = sd.baseColor.rgb;

        float3 diffuseColor = baseColor * (1.0 - metallic);
        float3 f0 = lerp(float3(0.04), baseColor, metallic);
        float NdotV = max(dot(normal, V), 0.001);

        // NEE: sample one scene light (if any)
        if (gScene.lightCount > 0)
        {
            uint lightIdx = rng.next() % gScene.lightCount;
            Light light = gScene.lights[lightIdx];

            float3 lightRadiance, lightDir;
            float lightDist;
            light.evaluate(position, lightRadiance, lightDir, lightDist);

            float NdotL = dot(normal, lightDir);
            if (NdotL > 0.0 && traceShadowRay(position, sd.geometryNormal, lightDir, lightDist - 0.002))
            {
                float3 H = normalize(V + lightDir);
                float NdotH = max(dot(normal, H), 0.0);
                float VdotH = max(dot(V, H), 0.0);

                float3 F = F_Schlick(f0, VdotH);
                float D = D_GGX(NdotH, roughness);
                float G1V = G1_Smith(NdotV, roughness);
                float G1L = G1_Smith(NdotL, roughness);
                float3 specular = D * G1V * G1L * F / (4.0 * NdotV * NdotL + 0.0001);
                float3 diffuse = diffuseColor * (1.0 - F) / PI;

                radiance += throughput * (diffuse + specular) * lightRadiance * NdotL * float(gScene.lightCount);
            }
        }

        if (gHasEnvImportance != 0)
        {
            float3 envRadiance, envDir;
            float envPdf;
            if (sampleEnvironmentLight(rng, envDir, envRadiance, envPdf))
            {
                float NdotL = dot(normal, envDir);
                if (NdotL > 0.0 && traceShadowRay(position, sd.geometryNormal, envDir, gCamera.farZ))
                {
                    float3 H = normalize(V + envDir);
                    float NdotH = max(dot(normal, H), 0.0);
                    float VdotH = max(dot(V, H), 0.0);

                    float3 F = F_Schlick(f0, VdotH);
                    float D = D_GGX(NdotH, roughness);
                    float G1V = G1_Smith(NdotV, roughness);
                    float G1L = G1_Smith(NdotL, roughness);
                    float3 specular = D * G1V * G1L * F / (4.0 * NdotV * NdotL + 0.0001);
                    float3 diffuse = diffuseColor * (1.0 - F) / PI;

                    radiance += throughput * (diffuse + specular) * envRadiance * NdotL / envPdf;
                }
            }
        }

        float specularWeight = max(max(f0.x, f0.y), f0.z);
        float diffuseWeight = max(max(diffuseColor.x, diffuseColor.y), diffuseColor.z);
        float specularProb = specularWeight / max(specularWeight + diffuseWeight, 1e-4);
        specularProb = clamp(specularProb, 0.1, 0.95);

        float3 newDir;
        if (rng.nextFloat() < specularProb)
        {
            float3 H = sampleGGXHalfVector(rng.nextFloat2(), normal, roughness);
            newDir = reflect(-V, H);
        }
        else
        {
            newDir = sampleCosineHemisphere(rng.nextFloat2(), normal);
        }

        float NdotL = max(dot(normal, newDir), 0.0);
        if (NdotL <= 0.0)
            break;

        float3 H = normalize(V + newDir);
        float NdotH = max(dot(normal, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        float3 F = F_Schlick(f0, VdotH);
        float D = D_GGX(NdotH, roughness);
        float G1V = G1_Smith(NdotV, roughness);
        float G1L = G1_Smith(NdotL, roughness);
        float3 specular = D * G1V * G1L * F / (4.0 * NdotV * NdotL + 0.0001);
        float3 diffuse = diffuseColor * (1.0 - F) / PI;

        float3 brdf = diffuse + specular;

        float diffusePdf = pdfCosineHemisphere(NdotL);
        float specularPdf = pdfGGXReflection(NdotH, VdotH, roughness);
        float pdf = (1.0 - specularProb) * diffusePdf + specularProb * specularPdf;
        if (pdf <= 1e-6)
            break;

        throughput *= brdf * NdotL / pdf;

        // Russian roulette after 2 bounces
        if (bounce >= 2)
        {
            float p = max(luminance(throughput), 0.05);
            p = min(p, 0.95);
            if (rng.nextFloat() > p)
                break;
            throughput /= p;
        }

        // Trace indirect ray
        RayDesc indirectRay;
        indirectRay.Origin = position + sd.geometryNormal * 0.001;
        indirectRay.Direction = newDir;
        indirectRay.TMin = 0.0;
        indirectRay.TMax = gCamera.farZ;

        PathPayload indirectPayload;
        indirectPayload.missed = true;

        TraceRay(gScene.tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, indirectRay, indirectPayload);

        if (indirectPayload.missed)
        {
            if (gHasEnvImportance == 0)
                radiance += throughput * sampleEnvMap(newDir);
            break;
        }

        // Next bounce
        sd = prepareShadingData(indirectPayload.instanceID, indirectPayload.primitiveID,
                                indirectPayload.barycentrics);
        position = sd.position;
        normal = sd.normal;
        V = -newDir;
        radiance += throughput * sd.emissive;
    }

    // Clamp fireflies
    float maxVal = max(radiance.x, max(radiance.y, radiance.z));
    if (maxVal > 50.0)
        radiance *= 50.0 / maxVal;

    return radiance;
}

// ---- Entry points ----

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
        RNG rng = createRNG(pixel, gSeed);
        ShadingData sd = prepareShadingData(instanceID, primitiveID, barycentrics);

        // Primary ray direction for miss (used if primary hit has zero bounces)
        float2 uv = (float2(pixel) + 0.5) / float2(dims);
        Ray primaryRay = gCamera.generateRay(uv);

        color = tracePath(sd, primaryRay.Direction, rng);
    }
    else
    {
        // Primary miss — sample environment
        float2 uv = (float2(pixel) + 0.5) / float2(dims);
        Ray primaryRay = gCamera.generateRay(uv);
        color = sampleEnvMap(primaryRay.Direction);
    }

    // NaN/inf guard
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
