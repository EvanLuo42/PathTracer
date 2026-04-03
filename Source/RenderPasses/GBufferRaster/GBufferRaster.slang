import Renderer.Camera;
import Scene.Scene;
import RenderPasses.GBufferUtils;

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
    float4 tangent  : TANGENT;
};

struct VSOutput
{
    float4 svPosition : SV_Position;
    float3 worldNormal : WORLD_NORMAL;
    float2 texCoord : TEXCOORD0;
    nointerpolation uint materialIndex : MATERIAL_ID;
};

struct GBufferOutput
{
    float4 baseColor : SV_Target0;     // RGBA8Unorm
    float2 normal : SV_Target1;        // RG16Float (octahedral)
    float4 materialData : SV_Target2;  // RGBA8Unorm
};

ConstantBuffer<Camera> gCamera;
ConstantBuffer<DrawParams> gDraw;

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(gDraw.modelMatrix, float4(input.position, 1.0));
    output.svPosition = mul(gCamera.viewProjMatrix, worldPos);
    output.worldNormal = mul((float3x3)gDraw.modelMatrix, input.normal);
    output.texCoord = input.texCoord;
    output.materialIndex = gDraw.materialIndex;
    return output;
}

[shader("fragment")]
GBufferOutput fragmentMain(VSOutput input)
{
    Material mat = gScene.materials[input.materialIndex];

    float4 baseColor = mat.baseColor;
    if (mat.baseColorTextureIndex >= 0)
        baseColor *= gScene.textures[mat.baseColorTextureIndex].Sample(gScene.sampler, input.texCoord);

    if (mat.alphaMode == AlphaMode.Mask && baseColor.a < mat.alphaCutoff)
        discard;

    float metallic = mat.metallic;
    float roughness = mat.roughness;
    if (mat.metallicRoughnessTextureIndex >= 0)
    {
        float4 mr = gScene.textures[mat.metallicRoughnessTextureIndex].Sample(gScene.sampler, input.texCoord);
        roughness *= mr.g;
        metallic *= mr.b;
    }

    float3 emissive = mat.emissiveColor * mat.emissiveStrength;
    if (mat.emissiveTextureIndex >= 0)
        emissive *= gScene.textures[mat.emissiveTextureIndex].Sample(gScene.sampler, input.texCoord).rgb;
    float emissiveScale = saturate(length(emissive) / 10.0);

    float3 N = normalize(input.worldNormal);

    GBufferOutput output;
    output.baseColor = baseColor;
    output.normal = encodeNormalOctahedral(N);
    output.materialData = float4(metallic, roughness, emissiveScale, 0);
    return output;
}
