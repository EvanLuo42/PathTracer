import Renderer.Camera;
import Scene.Scene;

ConstantBuffer<Camera> gCamera;
RWTexture2D<uint4> gVBuffer; // x=instanceID, y=primitiveID, zw=barycentrics as uint

struct HitPayload
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

    HitPayload payload;
    payload.instanceID = 0xFFFFFFFF;
    payload.primitiveID = 0xFFFFFFFF;
    payload.barycentrics = float2(0);

    TraceRay(gScene.tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, rayDesc, payload);

    gVBuffer[pixel] = uint4(
        payload.instanceID,
        payload.primitiveID,
        asuint(payload.barycentrics.x),
        asuint(payload.barycentrics.y)
    );
}

[shader("closesthit")]
void closestHit(inout HitPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    payload.instanceID = InstanceID();
    payload.primitiveID = PrimitiveIndex();
    payload.barycentrics = attribs.barycentrics;
}

[shader("miss")]
void miss(inout HitPayload payload)
{
    payload.instanceID = 0xFFFFFFFF;
    payload.primitiveID = 0xFFFFFFFF;
}
