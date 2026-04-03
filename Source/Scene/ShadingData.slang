import Scene;

struct ShadingData
{
    float3 position;
    float3 normal;
    float2 texCoord;
    Material material;

    float4 baseColor;
    float3 emissive;
    float metallic;
    float roughness;
};

Vertex interpolateVertex(uint instanceID, uint primitiveID, float2 barycentrics)
{
    MeshInfo mesh = gScene.meshInfos[instanceID];

    uint i0 = gScene.indices[mesh.indexOffset + primitiveID * 3 + 0];
    uint i1 = gScene.indices[mesh.indexOffset + primitiveID * 3 + 1];
    uint i2 = gScene.indices[mesh.indexOffset + primitiveID * 3 + 2];

    Vertex v0 = gScene.vertices[mesh.vertexOffset + i0];
    Vertex v1 = gScene.vertices[mesh.vertexOffset + i1];
    Vertex v2 = gScene.vertices[mesh.vertexOffset + i2];

    float w = 1.0 - barycentrics.x - barycentrics.y;

    Vertex result;
    result.position = w * v0.position + barycentrics.x * v1.position + barycentrics.y * v2.position;
    result.normal = normalize(w * v0.normal + barycentrics.x * v1.normal + barycentrics.y * v2.normal);
    result.texCoord = w * v0.texCoord + barycentrics.x * v1.texCoord + barycentrics.y * v2.texCoord;
    return result;
}

ShadingData prepareShadingData(uint instanceID, uint primitiveID, float2 barycentrics)
{
    Vertex v = interpolateVertex(instanceID, primitiveID, barycentrics);
    Material mat = gScene.materials[gScene.meshInfos[instanceID].materialIndex];

    ShadingData sd;
    sd.position = v.position;
    sd.normal = v.normal;
    sd.texCoord = v.texCoord;
    sd.material = mat;

    // Sample base color
    sd.baseColor = mat.baseColor;
    if (mat.baseColorTextureIndex >= 0)
        sd.baseColor *= gScene.textures[mat.baseColorTextureIndex].Sample(gScene.sampler, v.texCoord);

    // Sample metallic-roughness
    sd.metallic = mat.metallic;
    sd.roughness = mat.roughness;
    if (mat.metallicRoughnessTextureIndex >= 0)
    {
        float4 mr = gScene.textures[mat.metallicRoughnessTextureIndex].Sample(gScene.sampler, v.texCoord);
        sd.roughness *= mr.g;
        sd.metallic *= mr.b;
    }

    // Emissive
    sd.emissive = mat.emissiveColor * mat.emissiveStrength;
    if (mat.emissiveTextureIndex >= 0)
        sd.emissive *= gScene.textures[mat.emissiveTextureIndex].Sample(gScene.sampler, v.texCoord).rgb;

    return sd;
}

bool isAlphaMasked(ShadingData sd)
{
    return sd.material.alphaMode == AlphaMode.Mask && sd.baseColor.a < sd.material.alphaCutoff;
}
