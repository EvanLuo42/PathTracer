#pragma once

#include "ShaderVar.h"

#include <slang-rhi.h>
#include <slang-rhi/acceleration-structure-utils.h>
#include <slang-com-ptr.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

struct Material
{
    glm::vec4 baseColor = glm::vec4(1.0f);
    float metallic = 0.0f;
    float roughness = 1.0f;
    int32_t baseColorTextureIndex = -1;
    int32_t metallicRoughnessTextureIndex = -1;
    int32_t normalTextureIndex = -1;
    int32_t emissiveTextureIndex = -1;
    float _pad[2] = {};
};

struct MeshInfo
{
    uint32_t vertexOffset;
    uint32_t indexOffset;
    uint32_t indexCount;
    uint32_t materialIndex;
};

class Scene
{
public:
    static std::shared_ptr<Scene> Create(
        rhi::IDevice* device,
        rhi::ICommandQueue* queue,
        const std::filesystem::path& path
    );

    void Bind(const ShaderVar& var) const;

    rhi::IAccelerationStructure* GetTLAS() const { return tlas; }
    uint32_t GetMeshCount() const { return static_cast<uint32_t>(meshInfos.size()); }
    uint32_t GetTotalIndexCount() const { return totalIndexCount; }

private:
    Scene() = default;

    bool Load(const std::filesystem::path& path);
    bool BuildBuffers(rhi::IDevice* device);
    bool BuildTextures(rhi::IDevice* device);
    bool BuildAccelerationStructure(rhi::IDevice* device, rhi::ICommandQueue* queue);

    // Host data
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Material> materials;
    std::vector<MeshInfo> meshInfos;
    uint32_t totalIndexCount = 0;

    // Per-mesh data for BLAS building
    struct MeshGeometry
    {
        uint32_t vertexOffset;
        uint32_t vertexCount;
        uint32_t indexOffset;
        uint32_t indexCount;
        glm::mat4 transform;
    };
    std::vector<MeshGeometry> meshGeometries;

    // GPU resources
    rhi::IDevice* device = nullptr;
    Slang::ComPtr<rhi::IBuffer> vertexBuffer;
    Slang::ComPtr<rhi::IBuffer> indexBuffer;
    Slang::ComPtr<rhi::IBuffer> materialBuffer;
    Slang::ComPtr<rhi::IBuffer> meshInfoBuffer;

    // Bindless handles
    rhi::DescriptorHandle vertexBufferHandle;
    rhi::DescriptorHandle indexBufferHandle;
    rhi::DescriptorHandle materialBufferHandle;
    rhi::DescriptorHandle meshInfoBufferHandle;

    // Textures
    struct TextureData
    {
        std::vector<uint8_t> pixels;
        int width, height, channels;
    };
    std::vector<TextureData> textureDataList;
    std::vector<Slang::ComPtr<rhi::ITexture>> textures;
    std::vector<rhi::DescriptorHandle> textureHandles;
    Slang::ComPtr<rhi::ISampler> linearSampler;
    rhi::DescriptorHandle linearSamplerHandle;

    // Acceleration structures
    std::vector<Slang::ComPtr<rhi::IAccelerationStructure>> blasList;
    Slang::ComPtr<rhi::IAccelerationStructure> tlas;
    rhi::DescriptorHandle tlasHandle;
};
