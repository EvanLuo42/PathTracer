import Light;

struct Vertex
{
    float3 position;
    float3 normal;
    float2 texCoord;
};

enum AlphaMode : int
{
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

struct Material
{
    float4 baseColor;

    float3 emissiveColor;
    float emissiveStrength;

    float3 specularColor;
    float specularFactor;

    float3 sheenColor;
    float sheenRoughness;

    float3 attenuationColor;
    float attenuationDistance;

    float metallic;
    float roughness;
    float ior;
    float transmission;

    float alphaCutoff;
    float clearcoatFactor;
    float clearcoatRoughness;
    float thicknessFactor;

    float anisotropyStrength;
    float anisotropyRotation;
    float iridescenceFactor;
    float iridescenceIor;

    float iridescenceThicknessMin;
    float iridescenceThicknessMax;
    float normalScale;
    float occlusionStrength;

    int baseColorTextureIndex;
    int metallicRoughnessTextureIndex;
    int normalTextureIndex;
    int emissiveTextureIndex;

    int occlusionTextureIndex;
    int transmissionTextureIndex;
    int clearcoatTextureIndex;
    int clearcoatRoughnessTextureIndex;

    int clearcoatNormalTextureIndex;
    int sheenColorTextureIndex;
    int sheenRoughnessTextureIndex;
    int specularTextureIndex;

    int specularColorTextureIndex;
    int thicknessTextureIndex;
    int anisotropyTextureIndex;
    int iridescenceTextureIndex;

    int iridescenceThicknessTextureIndex;
    AlphaMode alphaMode;
    int doubleSided;
    int _pad;
};

struct MeshInfo
{
    uint vertexOffset;
    uint indexOffset;
    uint indexCount;
    uint materialIndex;
};

struct Scene
{
    StructuredBuffer<Vertex> vertices;
    StructuredBuffer<uint> indices;
    StructuredBuffer<Material> materials;
    StructuredBuffer<Light> lights;
    uint lightCount;
    StructuredBuffer<MeshInfo> meshInfos;
    RaytracingAccelerationStructure tlas;
    SamplerState sampler;
    Texture2D textures[16];
};

ParameterBlock<Scene> gScene;
