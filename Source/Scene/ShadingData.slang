import Scene;

struct ShadingData
{
    float3 position;
    float3 normal;
    float3 geometryNormal;
    float2 texCoord;
    Material material;

    float4 baseColor;
    float3 emissive;
    float metallic;
    float roughness;
    float occlusion;
};

float3 TransformPoint(InstanceTransform transform, float3 position)
{
    float4 p = float4(position, 1.0);
    return float3(
        dot(transform.modelRow0, p),
        dot(transform.modelRow1, p),
        dot(transform.modelRow2, p)
    );
}

float3 TransformDirection(InstanceTransform transform, float3 direction)
{
    return float3(
        dot(transform.modelRow0.xyz, direction),
        dot(transform.modelRow1.xyz, direction),
        dot(transform.modelRow2.xyz, direction)
    );
}

float3 TransformNormal(InstanceTransform transform, float3 normal)
{
    return normalize(float3(
        dot(transform.normalRow0.xyz, normal),
        dot(transform.normalRow1.xyz, normal),
        dot(transform.normalRow2.xyz, normal)
    ));
}

Vertex InterpolateVertex(uint instanceID, uint primitiveID, float2 barycentrics)
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
    result.tangent = w * v0.tangent + barycentrics.x * v1.tangent + barycentrics.y * v2.tangent;
    result.tangent = float4(normalize(result.tangent.xyz), result.tangent.w > 0 ? 1.0 : -1.0);
    return result;
}

ShadingData PrepareShadingData(uint instanceID, uint primitiveID, float2 barycentrics)
{
    Vertex v = InterpolateVertex(instanceID, primitiveID, barycentrics);
    Material mat = gScene.materials[gScene.meshInfos[instanceID].materialIndex];
    InstanceTransform transform = gScene.instanceTransforms[instanceID];

    ShadingData sd;
    sd.position = TransformPoint(transform, v.position);
    sd.geometryNormal = TransformNormal(transform, v.normal);
    sd.texCoord = v.texCoord;
    sd.material = mat;

    // Normal mapping
    float3 N = sd.geometryNormal;
    if (mat.normalTextureIndex >= 0)
    {
        float3 T = normalize(TransformDirection(transform, v.tangent.xyz));
        T = normalize(T - N * dot(N, T));
        float3 B = normalize(cross(N, T)) * v.tangent.w;
        float3x3 TBN = float3x3(T, B, N);

        float3 normalSample = gScene.textures[mat.normalTextureIndex].Sample(gScene.sampler, v.texCoord).rgb;
        float3 tangentNormal = normalSample * 2.0 - 1.0;
        tangentNormal.xy *= mat.normalScale;
        N = normalize(mul(tangentNormal, TBN));
    }
    sd.normal = N;

    sd.baseColor = mat.baseColor;
    if (mat.baseColorTextureIndex >= 0)
        sd.baseColor *= gScene.textures[mat.baseColorTextureIndex].Sample(gScene.sampler, v.texCoord);

    sd.metallic = mat.metallic;
    sd.roughness = mat.roughness;
    if (mat.metallicRoughnessTextureIndex >= 0)
    {
        float4 mr = gScene.textures[mat.metallicRoughnessTextureIndex].Sample(gScene.sampler, v.texCoord);
        sd.roughness *= mr.g;
        sd.metallic *= mr.b;
    }

    sd.emissive = mat.emissiveColor * mat.emissiveStrength;
    if (mat.emissiveTextureIndex >= 0)
        sd.emissive *= gScene.textures[mat.emissiveTextureIndex].Sample(gScene.sampler, v.texCoord).rgb;

    sd.occlusion = 1.0;
    if (mat.occlusionTextureIndex >= 0)
    {
        float ao = gScene.textures[mat.occlusionTextureIndex].Sample(gScene.sampler, v.texCoord).r;
        sd.occlusion = lerp(1.0, ao, mat.occlusionStrength);
    }

    return sd;
}

bool IsAlphaMasked(ShadingData sd)
{
    return sd.material.alphaMode == AlphaMode.Mask && sd.baseColor.a < sd.material.alphaCutoff;
}
