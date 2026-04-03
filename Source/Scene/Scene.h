#pragma once

#include "../Core/ShaderVar.h"

#include <slang-rhi.h>
#include <slang-com-ptr.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

namespace tinygltf { class Model; struct Primitive; }

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

enum class AlphaMode : int32_t { Opaque = 0, Mask = 1, Blend = 2 };

struct Material
{
    // Base PBR
    glm::vec4 baseColor = glm::vec4(1.0f);

    glm::vec3 emissiveColor = glm::vec3(0.0f);
    float emissiveStrength = 1.0f;

    glm::vec3 specularColor = glm::vec3(1.0f);
    float specularFactor = 1.0f;

    glm::vec3 sheenColor = glm::vec3(0.0f);
    float sheenRoughness = 0.0f;

    glm::vec3 attenuationColor = glm::vec3(1.0f);
    float attenuationDistance = std::numeric_limits<float>::max();

    float metallic = 0.0f;
    float roughness = 1.0f;
    float ior = 1.5f;
    float transmission = 0.0f;

    float alphaCutoff = 0.5f;
    float clearcoatFactor = 0.0f;
    float clearcoatRoughness = 0.0f;
    float thicknessFactor = 0.0f;

    float anisotropyStrength = 0.0f;
    float anisotropyRotation = 0.0f;
    float iridescenceFactor = 0.0f;
    float iridescenceIor = 1.3f;

    float iridescenceThicknessMin = 100.0f;
    float iridescenceThicknessMax = 400.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;

    // Texture indices (-1 = none)
    int32_t baseColorTextureIndex = -1;
    int32_t metallicRoughnessTextureIndex = -1;
    int32_t normalTextureIndex = -1;
    int32_t emissiveTextureIndex = -1;

    int32_t occlusionTextureIndex = -1;
    int32_t transmissionTextureIndex = -1;
    int32_t clearcoatTextureIndex = -1;
    int32_t clearcoatRoughnessTextureIndex = -1;

    int32_t clearcoatNormalTextureIndex = -1;
    int32_t sheenColorTextureIndex = -1;
    int32_t sheenRoughnessTextureIndex = -1;
    int32_t specularTextureIndex = -1;

    int32_t specularColorTextureIndex = -1;
    int32_t thicknessTextureIndex = -1;
    int32_t anisotropyTextureIndex = -1;
    int32_t iridescenceTextureIndex = -1;

    int32_t iridescenceThicknessTextureIndex = -1;
    AlphaMode alphaMode = AlphaMode::Opaque;
    int32_t doubleSided = 0;
    int32_t _pad = 0;
};

enum class LightType : int32_t { Directional = 0, Point = 1, Spot = 2 };

struct Light
{
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;

    glm::vec3 position = glm::vec3(0.0f);
    LightType type = LightType::Point;

    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    float range = std::numeric_limits<float>::max();

    float innerConeAngle = 0.0f;
    float outerConeAngle = glm::radians(45.0f);
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

    [[nodiscard]] rhi::IAccelerationStructure* GetTLAS() const { return tlas; }
    [[nodiscard]] uint32_t GetMeshCount() const { return static_cast<uint32_t>(meshInfos.size()); }
    [[nodiscard]] uint32_t GetTotalIndexCount() const { return totalIndexCount; }
    [[nodiscard]] uint32_t GetLightCount() const { return lightCount; }

private:
    Scene() = default;

    bool LoadModel(tinygltf::Model& model, const std::filesystem::path& path);
    void LoadTextures(const tinygltf::Model& model);
    void LoadMaterials(const tinygltf::Model& model);
    void LoadLights(const tinygltf::Model& model);
    void LoadMeshes(const tinygltf::Model& model);
    void LoadPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const glm::mat4& transform);
    bool BuildBuffers(rhi::IDevice* device);
    bool BuildTextures(rhi::IDevice* device);
    bool BuildAccelerationStructure(rhi::IDevice* device, rhi::ICommandQueue* queue);

    // Host data
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Material> materials;
    std::vector<Light> lights;
    std::vector<MeshInfo> meshInfos;
    uint32_t totalIndexCount = 0;
    uint32_t lightCount = 0;

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
    Slang::ComPtr<rhi::IBuffer> lightBuffer;
    Slang::ComPtr<rhi::IBuffer> meshInfoBuffer;

    // Bindless handles
    rhi::DescriptorHandle vertexBufferHandle;
    rhi::DescriptorHandle indexBufferHandle;
    rhi::DescriptorHandle materialBufferHandle;
    rhi::DescriptorHandle lightBufferHandle;
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
