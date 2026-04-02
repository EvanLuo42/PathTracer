public struct Vertex
{
    public float3 position;
    public float3 normal;
    public float2 texCoord;
};

public struct Material
{
    public float4 baseColor;
    public float metallic;
    public float roughness;
    public int baseColorTextureIndex;
    public int metallicRoughnessTextureIndex;
    public int normalTextureIndex;
    public int emissiveTextureIndex;
    public float _pad[2];
};

public struct MeshInfo
{
    public uint vertexOffset;
    public uint indexOffset;
    public uint indexCount;
    public uint materialIndex;
};

public struct SceneData
{
    public StructuredBuffer<Vertex> vertices;
    public StructuredBuffer<uint> indices;
    public StructuredBuffer<Material> materials;
    public StructuredBuffer<MeshInfo> meshInfos;
    public RaytracingAccelerationStructure tlas;
    public SamplerState sampler;
    public Texture2D textures[128];

    public Vertex GetVertex(uint meshIndex, uint primitiveIndex, float2 barycentrics)
    {
        MeshInfo info = meshInfos[meshIndex];

        uint i0 = indices[info.indexOffset + primitiveIndex * 3 + 0];
        uint i1 = indices[info.indexOffset + primitiveIndex * 3 + 1];
        uint i2 = indices[info.indexOffset + primitiveIndex * 3 + 2];

        Vertex v0 = vertices[info.vertexOffset + i0];
        Vertex v1 = vertices[info.vertexOffset + i1];
        Vertex v2 = vertices[info.vertexOffset + i2];

        float w = 1.0 - barycentrics.x - barycentrics.y;

        Vertex result;
        result.position = w * v0.position + barycentrics.x * v1.position + barycentrics.y * v2.position;
        result.normal = normalize(w * v0.normal + barycentrics.x * v1.normal + barycentrics.y * v2.normal);
        result.texCoord = w * v0.texCoord + barycentrics.x * v1.texCoord + barycentrics.y * v2.texCoord;
        return result;
    }

    public Material GetMaterial(uint meshIndex)
    {
        return materials[meshInfos[meshIndex].materialIndex];
    }

    public float4 SampleBaseColor(Material mat, float2 uv)
    {
        if (mat.baseColorTextureIndex >= 0)
            return mat.baseColor * textures[mat.baseColorTextureIndex].Sample(sampler, uv);
        return mat.baseColor;
    }

    public float2 SampleMetallicRoughness(Material mat, float2 uv)
    {
        if (mat.metallicRoughnessTextureIndex >= 0)
        {
            float4 s = textures[mat.metallicRoughnessTextureIndex].Sample(sampler, uv);
            return float2(s.b * mat.metallic, s.g * mat.roughness);
        }
        return float2(mat.metallic, mat.roughness);
    }

    public float3 SampleNormal(Material mat, float2 uv)
    {
        if (mat.normalTextureIndex >= 0)
        {
            float3 n = textures[mat.normalTextureIndex].Sample(sampler, uv).xyz;
            return n * 2.0 - 1.0;
        }
        return float3(0, 0, 1);
    }
};

public struct VBufferHit
{
    public uint instanceID;
    public uint primitiveID;
    public float2 barycentrics;
    public bool isValid;
};

public VBufferHit UnpackVBuffer(uint2 data)
{
    VBufferHit hit;
    hit.isValid = (data.x != 0xFFFFFFFF);
    hit.instanceID = data.x >> 16;
    hit.primitiveID = data.x & 0xFFFF;
    hit.barycentrics.x = f16tof32(data.y);
    hit.barycentrics.y = f16tof32(data.y >> 16);
    return hit;
}
