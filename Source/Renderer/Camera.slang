struct Ray
{
    float3 Origin;
    float3 Direction;
    float TMin;
    float TMax;
};

struct Camera
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4x4 invViewMatrix;
    float4x4 invProjMatrix;
    float4x4 viewProjMatrix;
    float4x4 invViewProjMatrix;

    float3 position;
    float fovY;

    float3 forward;
    float aspectRatio;

    float3 up;
    float nearZ;

    float3 right;
    float farZ;

    float2 jitter;
    uint frameIndex;
    float _pad;

    // Generate a ray from normalized pixel coordinates [0,1]
    Ray generateRay(float2 pixel)
    {
        // Map pixel to [-1, 1] clip space, apply jitter
        float2 ndc = float2(2.0 * pixel.x - 1.0, 1.0 - 2.0 * pixel.y) + jitter;

        // Unproject near and far points
        float4 nearPoint = mul(invViewProjMatrix, float4(ndc, 0.0, 1.0));
        float4 farPoint = mul(invViewProjMatrix, float4(ndc, 1.0, 1.0));
        nearPoint.xyz /= nearPoint.w;
        farPoint.xyz /= farPoint.w;

        Ray ray;
        ray.Origin = nearPoint.xyz;
        ray.Direction = normalize(farPoint.xyz - nearPoint.xyz);
        ray.TMin = 0.0;
        ray.TMax = farZ;
        return ray;
    }

    // Project world position to screen coordinates [0,1]
    float2 projectToScreen(float3 worldPos)
    {
        float4 clip = mul(viewProjMatrix, float4(worldPos, 1.0));
        float2 ndc = clip.xy / clip.w;
        return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    }

    // Get view-space depth of a world position
    float getViewDepth(float3 worldPos)
    {
        float4 viewPos = mul(viewMatrix, float4(worldPos, 1.0));
        return -viewPos.z;
    }
};
