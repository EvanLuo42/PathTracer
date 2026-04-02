struct CameraData
{
    float4x4 invView;
    float4x4 invProj;
    float3 position;
    float fovY;
};

[format("rg32ui")]
RWTexture2D<uint2> gVBuffer;
RaytracingAccelerationStructure gTLAS;
CameraData gCamera;

struct VBufferPayload
{
    uint hitData;
    uint baryData;
};

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    float2 uv = (float2(pixel) + 0.5) / float2(dims);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Unproject through inverse projection and view
    float4 clipPos = float4(ndc, 0.0, 1.0);
    float4 viewPos = mul(gCamera.invProj, clipPos);
    viewPos /= viewPos.w;
    float3 worldDir = mul(gCamera.invView, float4(viewPos.xyz, 0.0)).xyz;

    RayDesc ray;
    ray.Origin = gCamera.position;
    ray.Direction = normalize(worldDir);
    ray.TMin = 0.001;
    ray.TMax = 1e30;

    VBufferPayload payload;
    payload.hitData = 0xFFFFFFFF;
    payload.baryData = 0;

    TraceRay(gTLAS, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    gVBuffer[pixel] = uint2(payload.hitData, payload.baryData);
}

[shader("closesthit")]
void ClosestHit(inout VBufferPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    payload.hitData = (InstanceID() << 16) | (PrimitiveIndex() & 0xFFFF);
    payload.baryData = f32tof16(attribs.barycentrics.x) | (f32tof16(attribs.barycentrics.y) << 16);
}

[shader("miss")]
void Miss(inout VBufferPayload payload)
{
    payload.hitData = 0xFFFFFFFF;
}
