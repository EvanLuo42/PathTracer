import Renderer.Camera;
import Scene.Scene;

struct DrawParams
{
    float4x4 modelMatrix;
    uint materialIndex;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 svPosition : SV_Position;
    float2 texCoord : TEXCOORD0;
    nointerpolation uint materialIndex : MATERIAL_ID;
};

ConstantBuffer<Camera> gCamera;
ConstantBuffer<DrawParams> gDraw;

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(gDraw.modelMatrix, float4(input.position, 1.0));
    output.svPosition = mul(gCamera.viewProjMatrix, worldPos);
    output.texCoord = input.texCoord;
    output.materialIndex = gDraw.materialIndex;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    Material mat = gScene.materials[input.materialIndex];

    float4 baseColor = mat.baseColor;
    if (mat.baseColorTextureIndex >= 0)
        baseColor *= gScene.textures[mat.baseColorTextureIndex].Sample(gScene.sampler, input.texCoord);

    if (mat.alphaMode == AlphaMode.Mask && baseColor.a < mat.alphaCutoff)
        discard;

    return baseColor;
}
