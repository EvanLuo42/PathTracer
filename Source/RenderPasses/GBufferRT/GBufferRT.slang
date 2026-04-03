import Renderer.Camera;
import Scene.Scene;
import Scene.ShadingData;
import RenderPasses.GBufferUtils;

ConstantBuffer<Camera> gCamera;
RWTexture2D<float4> gBaseColor;
RWTexture2D<float2> gNormal;
RWTexture2D<float4> gMaterialData;
RWTexture2D<float> gDepth;

struct GBufferPayload
{
    uint instanceID;
    uint primitiveID;
    float2 barycentrics;
};

[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    float2 uv = (float2(pixel) + 0.5) / float2(dims);
    Ray ray = gCamera.GenerateRay(uv);

    RayDesc rayDesc;
    rayDesc.Origin = ray.Origin;
    rayDesc.Direction = ray.Direction;
    rayDesc.TMin = ray.TMin;
    rayDesc.TMax = ray.TMax;

    GBufferPayload payload;
    payload.instanceID = 0xFFFFFFFF;
    payload.primitiveID = 0;
    payload.barycentrics = float2(0);

    TraceRay(gScene.tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, rayDesc, payload);

    if (payload.instanceID == 0xFFFFFFFF)
    {
        gBaseColor[pixel] = float4(0, 0, 0, 0);
        gNormal[pixel] = float2(0, 0);
        gMaterialData[pixel] = float4(0, 0, 0, 0);
        gDepth[pixel] = 1.0;
        return;
    }

    ShadingData sd = PrepareShadingData(payload.instanceID, payload.primitiveID, payload.barycentrics);

    // Compute depth: project hit position to clip space
    float4 clipPos = mul(gCamera.viewProjMatrix, float4(sd.position, 1.0));
    float depth = clipPos.z / clipPos.w;

    float3 emissive = sd.emissive;
    float emissiveScale = saturate(length(emissive) / 10.0);

    gBaseColor[pixel] = sd.baseColor;
    gNormal[pixel] = EncodeNormalOctahedral(sd.normal);
    gMaterialData[pixel] = float4(sd.metallic, sd.roughness, emissiveScale, 0);
    gDepth[pixel] = depth;
}

[shader("closesthit")]
void closestHit(inout GBufferPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    payload.instanceID = InstanceID();
    payload.primitiveID = PrimitiveIndex();
    payload.barycentrics = attribs.barycentrics;
}

[shader("miss")]
void miss(inout GBufferPayload payload)
{
    payload.instanceID = 0xFFFFFFFF;
}
