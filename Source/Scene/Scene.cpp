#include "Scene.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "slang-rhi/acceleration-structure-utils.h"

#include <tiny_gltf.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include <functional>

using namespace rhi;

namespace
{
    glm::mat4 GetNodeTransform(const tinygltf::Node& node)
    {
        if (!node.matrix.empty())
        {
            glm::mat4 m;
            for (int i = 0; i < 16; i++)
                reinterpret_cast<float*>(&m)[i] = static_cast<float>(node.matrix[i]);
            return m;
        }

        glm::mat4 T(1.0f), R(1.0f), S(1.0f);
        if (!node.translation.empty())
            T = glm::translate(
                glm::mat4(1.0f),
                glm::vec3(
                    static_cast<float>(node.translation[0]),
                    static_cast<float>(node.translation[1]),
                    static_cast<float>(node.translation[2])
                )
            );
        if (!node.rotation.empty())
        {
            glm::quat q(
                static_cast<float>(node.rotation[3]),
                static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2])
            );
            R = glm::mat4_cast(q);
        }
        if (!node.scale.empty())
            S = glm::scale(
                glm::mat4(1.0f),
                glm::vec3(
                    static_cast<float>(node.scale[0]),
                    static_cast<float>(node.scale[1]),
                    static_cast<float>(node.scale[2])
                )
            );
        return T * R * S;
    }

    struct AccessorView
    {
        const float* data = nullptr;
        size_t stride = 0; // in floats

        explicit operator bool() const { return data != nullptr; }

        static AccessorView Get(const tinygltf::Model& model, const tinygltf::Primitive& prim, const char* attribute)
        {
            auto it = prim.attributes.find(attribute);
            if (it == prim.attributes.end())
                return {};
            const auto& accessor = model.accessors[it->second];
            const auto& view = model.bufferViews[accessor.bufferView];
            return {
                reinterpret_cast<const float*>(&model.buffers[view.buffer].data[view.byteOffset + accessor.byteOffset]),
                accessor.ByteStride(view) / sizeof(float)
            };
        }

        glm::vec2 vec2(size_t i) const { return {data[i * stride], data[i * stride + 1]}; }
        glm::vec3 vec3(size_t i) const { return {data[i * stride], data[i * stride + 1], data[i * stride + 2]}; }
    };
}

std::shared_ptr<Scene> Scene::Create(IDevice* device, ICommandQueue* queue, const std::filesystem::path& path)
{
    auto scene = std::shared_ptr<Scene>(new Scene());
    scene->device = device;

    tinygltf::Model model;
    if (!scene->LoadModel(model, path))
        return nullptr;

    scene->LoadTextures(model);
    scene->LoadMaterials(model);
    scene->LoadLights(model);
    scene->LoadMeshes(model);

    // Release host texture data
    scene->textureDataList.clear();
    scene->textureDataList.shrink_to_fit();

    if (!scene->BuildBuffers(device))
        return nullptr;

    // Release host geometry data
    scene->vertices.clear();
    scene->vertices.shrink_to_fit();
    scene->indices.clear();
    scene->indices.shrink_to_fit();
    scene->materials.clear();
    scene->materials.shrink_to_fit();
    scene->lights.clear();
    scene->lights.shrink_to_fit();
    scene->meshInfos.clear();
    scene->meshInfos.shrink_to_fit();

    if (!scene->BuildTextures(device))
        return nullptr;
    if (!scene->BuildAccelerationStructure(device, queue))
        return nullptr;

    // Release BLAS build data
    scene->meshGeometries.clear();
    scene->meshGeometries.shrink_to_fit();

    return scene;
}

bool Scene::LoadModel(tinygltf::Model& model, const std::filesystem::path& path)
{
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool result;
    if (path.extension() == ".glb")
        result = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());
    else
        result = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());

    if (!warn.empty())
        spdlog::warn("{}", warn);
    if (!err.empty())
        spdlog::error("{}", err);
    if (!result)
    {
        spdlog::error("Failed to load scene: {}", path.string());
        return false;
    }

    spdlog::info("Loaded {}", path.filename().string());
    return true;
}

void Scene::LoadTextures(const tinygltf::Model& model)
{
    for (const auto& image : model.images)
    {
        TextureData td;
        td.width = image.width;
        td.height = image.height;
        td.channels = image.component;

        if (image.component == 4)
        {
            td.pixels = image.image;
        }
        else
        {
            td.pixels.resize(image.width * image.height * 4);
            for (int i = 0; i < image.width * image.height; i++)
            {
                for (int c = 0; c < image.component && c < 4; c++)
                    td.pixels[i * 4 + c] = image.image[i * image.component + c];
                for (int c = image.component; c < 4; c++)
                    td.pixels[i * 4 + c] = 255;
            }
            td.channels = 4;
        }
        textureDataList.push_back(std::move(td));
    }
}

void Scene::LoadMaterials(const tinygltf::Model& model)
{
    auto resolveTexture = [&](int textureIndex) -> int32_t
    {
        if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
            return -1;
        return model.textures[textureIndex].source;
    };

    // Helpers for reading KHR extension values
    auto getExt = [](const tinygltf::Material& m, const char* name) -> const tinygltf::Value*
    {
        auto it = m.extensions.find(name);
        return it != m.extensions.end() ? &it->second : nullptr;
    };

    auto getFloat = [](const tinygltf::Value& ext, const char* key, float fallback) -> float
    {
        if (ext.Has(key))
            return static_cast<float>(ext.Get(key).GetNumberAsDouble());
        return fallback;
    };

    auto getFloat3 = [](const tinygltf::Value& ext, const char* key, glm::vec3 fallback) -> glm::vec3
    {
        if (!ext.Has(key))
            return fallback;
        const auto& arr = ext.Get(key);
        if (!arr.IsArray() || arr.ArrayLen() < 3)
            return fallback;
        return {
            static_cast<float>(arr.Get(0).GetNumberAsDouble()),
            static_cast<float>(arr.Get(1).GetNumberAsDouble()),
            static_cast<float>(arr.Get(2).GetNumberAsDouble())
        };
    };

    auto getTexIndex = [&](const tinygltf::Value& ext, const char* key) -> int32_t
    {
        if (!ext.Has(key))
            return -1;
        const auto& texInfo = ext.Get(key);
        if (!texInfo.Has("index"))
            return -1;
        return resolveTexture(texInfo.Get("index").GetNumberAsInt());
    };

    for (const auto& mat : model.materials)
    {
        Material material;

        // Base PBR metallic-roughness
        const auto& pbr = mat.pbrMetallicRoughness;
        material.baseColor = glm::vec4(
            static_cast<float>(pbr.baseColorFactor[0]),
            static_cast<float>(pbr.baseColorFactor[1]),
            static_cast<float>(pbr.baseColorFactor[2]),
            static_cast<float>(pbr.baseColorFactor[3])
        );
        material.metallic = static_cast<float>(pbr.metallicFactor);
        material.roughness = static_cast<float>(pbr.roughnessFactor);
        material.baseColorTextureIndex = resolveTexture(pbr.baseColorTexture.index);
        material.metallicRoughnessTextureIndex = resolveTexture(pbr.metallicRoughnessTexture.index);

        // Core glTF
        material.emissiveColor = glm::vec3(
            static_cast<float>(mat.emissiveFactor[0]),
            static_cast<float>(mat.emissiveFactor[1]),
            static_cast<float>(mat.emissiveFactor[2])
        );
        material.normalTextureIndex = resolveTexture(mat.normalTexture.index);
        material.normalScale = static_cast<float>(mat.normalTexture.scale);
        material.emissiveTextureIndex = resolveTexture(mat.emissiveTexture.index);
        material.occlusionTextureIndex = resolveTexture(mat.occlusionTexture.index);
        material.occlusionStrength = static_cast<float>(mat.occlusionTexture.strength);
        material.doubleSided = mat.doubleSided ? 1 : 0;

        if (mat.alphaMode == "MASK")
        {
            material.alphaMode = AlphaMode::Mask;
            material.alphaCutoff = static_cast<float>(mat.alphaCutoff);
        }
        else if (mat.alphaMode == "BLEND")
        {
            material.alphaMode = AlphaMode::Blend;
        }

        // KHR_materials_emissive_strength
        if (const auto* ext = getExt(mat, "KHR_materials_emissive_strength"))
            material.emissiveStrength = getFloat(*ext, "emissiveStrength", 1.0f);

        // KHR_materials_ior
        if (const auto* ext = getExt(mat, "KHR_materials_ior"))
            material.ior = getFloat(*ext, "ior", 1.5f);

        // KHR_materials_transmission
        if (const auto* ext = getExt(mat, "KHR_materials_transmission"))
        {
            material.transmission = getFloat(*ext, "transmissionFactor", 0.0f);
            material.transmissionTextureIndex = getTexIndex(*ext, "transmissionTexture");
        }

        // KHR_materials_volume
        if (const auto* ext = getExt(mat, "KHR_materials_volume"))
        {
            material.thicknessFactor = getFloat(*ext, "thicknessFactor", 0.0f);
            material.attenuationDistance = getFloat(*ext, "attenuationDistance", std::numeric_limits<float>::max());
            material.attenuationColor = getFloat3(*ext, "attenuationColor", glm::vec3(1.0f));
            material.thicknessTextureIndex = getTexIndex(*ext, "thicknessTexture");
        }

        // KHR_materials_specular
        if (const auto* ext = getExt(mat, "KHR_materials_specular"))
        {
            material.specularFactor = getFloat(*ext, "specularFactor", 1.0f);
            material.specularColor = getFloat3(*ext, "specularColorFactor", glm::vec3(1.0f));
            material.specularTextureIndex = getTexIndex(*ext, "specularTexture");
            material.specularColorTextureIndex = getTexIndex(*ext, "specularColorTexture");
        }

        // KHR_materials_clearcoat
        if (const auto* ext = getExt(mat, "KHR_materials_clearcoat"))
        {
            material.clearcoatFactor = getFloat(*ext, "clearcoatFactor", 0.0f);
            material.clearcoatRoughness = getFloat(*ext, "clearcoatRoughnessFactor", 0.0f);
            material.clearcoatTextureIndex = getTexIndex(*ext, "clearcoatTexture");
            material.clearcoatRoughnessTextureIndex = getTexIndex(*ext, "clearcoatRoughnessTexture");
            material.clearcoatNormalTextureIndex = getTexIndex(*ext, "clearcoatNormalTexture");
        }

        // KHR_materials_sheen
        if (const auto* ext = getExt(mat, "KHR_materials_sheen"))
        {
            material.sheenColor = getFloat3(*ext, "sheenColorFactor", glm::vec3(0.0f));
            material.sheenRoughness = getFloat(*ext, "sheenRoughnessFactor", 0.0f);
            material.sheenColorTextureIndex = getTexIndex(*ext, "sheenColorTexture");
            material.sheenRoughnessTextureIndex = getTexIndex(*ext, "sheenRoughnessTexture");
        }

        // KHR_materials_anisotropy
        if (const auto* ext = getExt(mat, "KHR_materials_anisotropy"))
        {
            material.anisotropyStrength = getFloat(*ext, "anisotropyStrength", 0.0f);
            material.anisotropyRotation = getFloat(*ext, "anisotropyRotation", 0.0f);
            material.anisotropyTextureIndex = getTexIndex(*ext, "anisotropyTexture");
        }

        // KHR_materials_iridescence
        if (const auto* ext = getExt(mat, "KHR_materials_iridescence"))
        {
            material.iridescenceFactor = getFloat(*ext, "iridescenceFactor", 0.0f);
            material.iridescenceIor = getFloat(*ext, "iridescenceIor", 1.3f);
            material.iridescenceThicknessMin = getFloat(*ext, "iridescenceThicknessMinimum", 100.0f);
            material.iridescenceThicknessMax = getFloat(*ext, "iridescenceThicknessMaximum", 400.0f);
            material.iridescenceTextureIndex = getTexIndex(*ext, "iridescenceTexture");
            material.iridescenceThicknessTextureIndex = getTexIndex(*ext, "iridescenceThicknessTexture");
        }

        materials.push_back(material);
    }

    if (materials.empty())
        materials.push_back(Material{});
}

void Scene::LoadLights(const tinygltf::Model& model)
{
    // KHR_lights_punctual stores light definitions at the top level,
    // and nodes reference them via node.extensions["KHR_lights_punctual"].light
    auto extIt = model.extensions.find("KHR_lights_punctual");
    if (extIt == model.extensions.end())
        return;

    const auto& extVal = extIt->second;
    if (!extVal.Has("lights"))
        return;

    const auto& lightsArray = extVal.Get("lights");

    // Parse light definitions
    struct LightDef
    {
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        LightType type = LightType::Point;
        float range = std::numeric_limits<float>::max();
        float innerConeAngle = 0.0f;
        float outerConeAngle = glm::quarter_pi<float>();
    };

    std::vector<LightDef> lightDefs;
    for (int i = 0; i < static_cast<int>(lightsArray.ArrayLen()); i++)
    {
        const auto& l = lightsArray.Get(i);
        LightDef def;

        if (l.Has("color"))
        {
            const auto& c = l.Get("color");
            if (c.IsArray() && c.ArrayLen() >= 3)
                def.color = glm::vec3(
                    static_cast<float>(c.Get(0).GetNumberAsDouble()),
                    static_cast<float>(c.Get(1).GetNumberAsDouble()),
                    static_cast<float>(c.Get(2).GetNumberAsDouble())
                );
        }

        if (l.Has("intensity"))
            def.intensity = static_cast<float>(l.Get("intensity").GetNumberAsDouble());

        if (l.Has("range"))
            def.range = static_cast<float>(l.Get("range").GetNumberAsDouble());

        if (l.Has("type"))
        {
            const auto& typeStr = l.Get("type").Get<std::string>();
            if (typeStr == "directional")
                def.type = LightType::Directional;
            else if (typeStr == "point")
                def.type = LightType::Point;
            else if (typeStr == "spot")
                def.type = LightType::Spot;
        }

        if (l.Has("spot"))
        {
            const auto& spot = l.Get("spot");
            if (spot.Has("innerConeAngle"))
                def.innerConeAngle = static_cast<float>(spot.Get("innerConeAngle").GetNumberAsDouble());
            if (spot.Has("outerConeAngle"))
                def.outerConeAngle = static_cast<float>(spot.Get("outerConeAngle").GetNumberAsDouble());
        }

        lightDefs.push_back(def);
    }

    // Traverse nodes to find light references and apply transforms
    std::function<void(int, const glm::mat4&)> traverseNode = [&](int nodeIndex, const glm::mat4& parentTransform)
    {
        const auto& node = model.nodes[nodeIndex];
        const glm::mat4 worldTransform = parentTransform * GetNodeTransform(node);

        auto lightExtIt = node.extensions.find("KHR_lights_punctual");
        if (lightExtIt != node.extensions.end())
        {
            const auto& lightExt = lightExtIt->second;
            if (lightExt.Has("light"))
            {
                int lightIndex = lightExt.Get("light").GetNumberAsInt();
                if (lightIndex >= 0 && lightIndex < static_cast<int>(lightDefs.size()))
                {
                    const auto& def = lightDefs[lightIndex];
                    Light light;
                    light.color = def.color;
                    light.intensity = def.intensity;
                    light.type = def.type;
                    light.range = def.range;
                    light.innerConeAngle = def.innerConeAngle;
                    light.outerConeAngle = def.outerConeAngle;

                    // Extract position and forward direction from world transform
                    light.position = glm::vec3(worldTransform[3]);
                    // glTF lights point along -Z in local space
                    light.direction = glm::normalize(glm::vec3(worldTransform * glm::vec4(0, 0, -1, 0)));

                    lights.push_back(light);
                }
            }
        }

        for (const int child : node.children)
            traverseNode(child, worldTransform);
    };

    const auto& scene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
    for (const int rootNode : scene.nodes)
        traverseNode(rootNode, glm::mat4(1.0f));

    lightCount = static_cast<uint32_t>(lights.size());
    if (!lights.empty())
        spdlog::info("Loaded {} lights", lightCount);
}

void Scene::LoadMeshes(const tinygltf::Model& model)
{
    // Traverse node hierarchy recursively
    std::function<void(int, const glm::mat4&)> traverseNode = [&](int nodeIndex, const glm::mat4& parentTransform)
    {
        const auto& node = model.nodes[nodeIndex];
        const glm::mat4 worldTransform = parentTransform * GetNodeTransform(node);

        if (node.mesh >= 0)
        {
            const auto& mesh = model.meshes[node.mesh];
            for (const auto& primitive : mesh.primitives)
            {
                if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
                    continue;

                LoadPrimitive(model, primitive, worldTransform);
            }
        }

        for (const int child : node.children)
            traverseNode(child, worldTransform);
    };

    // Start from scene root nodes
    const auto& scene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
    for (const int rootNode : scene.nodes)
        traverseNode(rootNode, glm::mat4(1.0f));

    spdlog::info("{} vertices, {} triangles, {} meshes, {} materials",
        vertices.size(), indices.size() / 3, meshInfos.size(), materials.size());
}

void Scene::LoadPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const glm::mat4& transform)
{
    MeshGeometry geom;
    geom.vertexOffset = static_cast<uint32_t>(vertices.size());
    geom.indexOffset = static_cast<uint32_t>(indices.size());

    // Vertex attributes
    auto pos = AccessorView::Get(model, primitive, "POSITION");
    auto norm = AccessorView::Get(model, primitive, "NORMAL");
    auto uv = AccessorView::Get(model, primitive, "TEXCOORD_0");

    const auto& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
    geom.vertexCount = static_cast<uint32_t>(posAccessor.count);

    for (size_t i = 0; i < posAccessor.count; i++)
    {
        Vertex v;
        v.position = pos.vec3(i);
        v.normal = norm ? norm.vec3(i) : glm::vec3(0, 1, 0);
        v.texCoord = uv ? uv.vec2(i) : glm::vec2(0);
        vertices.push_back(v);
    }

    // Indices
    const auto& idxAccessor = model.accessors[primitive.indices];
    const auto& idxView = model.bufferViews[idxAccessor.bufferView];
    const auto* idxRaw = &model.buffers[idxView.buffer].data[idxView.byteOffset + idxAccessor.byteOffset];

    geom.indexCount = static_cast<uint32_t>(idxAccessor.count);
    for (size_t i = 0; i < idxAccessor.count; i++)
    {
        uint32_t index;
        switch (idxAccessor.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            index = reinterpret_cast<const uint16_t*>(idxRaw)[i];
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            index = reinterpret_cast<const uint32_t*>(idxRaw)[i];
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            index = idxRaw[i];
            break;
        default:
            index = 0;
            break;
        }
        indices.push_back(index);
    }

    // MeshInfo
    MeshInfo info;
    info.vertexOffset = geom.vertexOffset;
    info.indexOffset = geom.indexOffset;
    info.indexCount = geom.indexCount;
    info.materialIndex = primitive.material >= 0 ? static_cast<uint32_t>(primitive.material) : 0;
    meshInfos.push_back(info);

    geom.transform = transform;
    meshGeometries.push_back(geom);
    totalIndexCount += geom.indexCount;
}

bool Scene::BuildBuffers(IDevice* device)
{
    auto createBuffer = [&](auto& data, BufferUsage usage, Slang::ComPtr<IBuffer>& outBuffer, DescriptorHandle& outHandle)
    {
        BufferDesc desc = {};
        desc.size = data.size() * sizeof(data[0]);
        desc.usage = usage | BufferUsage::ShaderResource | BufferUsage::AccelerationStructureBuildInput;
        desc.defaultState = ResourceState::ShaderResource;
        desc.elementSize = sizeof(data[0]);

        outBuffer = device->createBuffer(desc, data.data());
        if (!outBuffer)
            return false;

        outBuffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::Undefined, kEntireBuffer, &outHandle);
        return true;
    };

    if (!createBuffer(vertices, BufferUsage::VertexBuffer, vertexBuffer, vertexBufferHandle))
        return false;
    if (!createBuffer(indices, BufferUsage::IndexBuffer, indexBuffer, indexBufferHandle))
        return false;
    if (!createBuffer(materials, BufferUsage::None, materialBuffer, materialBufferHandle))
        return false;
    if (!lights.empty() && !createBuffer(lights, BufferUsage::None, lightBuffer, lightBufferHandle))
        return false;
    if (!createBuffer(meshInfos, BufferUsage::None, meshInfoBuffer, meshInfoBufferHandle))
        return false;

    return true;
}

bool Scene::BuildTextures(IDevice* device)
{
    for (const auto& td : textureDataList)
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.size = {static_cast<uint32_t>(td.width), static_cast<uint32_t>(td.height), 1};
        desc.format = Format::RGBA8Unorm;
        desc.usage = TextureUsage::ShaderResource;
        desc.defaultState = ResourceState::ShaderResource;

        SubresourceData initData = {};
        initData.data = td.pixels.data();
        initData.rowPitch = td.width * 4;

        auto texture = device->createTexture(desc, &initData);
        if (!texture)
        {
            spdlog::error("Failed to create texture");
            return false;
        }

        DescriptorHandle handle;
        texture->getDefaultView()->getDescriptorHandle(DescriptorHandleAccess::Read, &handle);

        textures.push_back(texture);
        textureHandles.push_back(handle);
    }

    // Shared linear sampler
    SamplerDesc samplerDesc = {};
    samplerDesc.minFilter = TextureFilteringMode::Linear;
    samplerDesc.magFilter = TextureFilteringMode::Linear;
    samplerDesc.mipFilter = TextureFilteringMode::Linear;
    samplerDesc.addressU = TextureAddressingMode::Wrap;
    samplerDesc.addressV = TextureAddressingMode::Wrap;
    linearSampler = device->createSampler(samplerDesc);
    linearSampler->getDescriptorHandle(&linearSamplerHandle);

    spdlog::info("Created {} textures", textures.size());
    return true;
}

bool Scene::BuildAccelerationStructure(IDevice* device, ICommandQueue* queue)
{
    auto instanceDescType = getAccelerationStructureInstanceDescType(device);
    auto instanceDescSize = getAccelerationStructureInstanceDescSize(instanceDescType);

    // Build all BLAS in a single command buffer
    {
        auto encoder = queue->createCommandEncoder();

        for (const auto& geom : meshGeometries)
        {
            AccelerationStructureBuildInput buildInput = {};
            buildInput.type = AccelerationStructureBuildInputType::Triangles;
            buildInput.triangles.vertexBuffers[0] = {vertexBuffer, geom.vertexOffset * sizeof(Vertex)};
            buildInput.triangles.vertexBufferCount = 1;
            buildInput.triangles.vertexFormat = Format::RGB32Float;
            buildInput.triangles.vertexCount = geom.vertexCount;
            buildInput.triangles.vertexStride = sizeof(Vertex);
            buildInput.triangles.indexBuffer = {indexBuffer, geom.indexOffset * sizeof(uint32_t)};
            buildInput.triangles.indexFormat = IndexFormat::Uint32;
            buildInput.triangles.indexCount = geom.indexCount;
            buildInput.triangles.flags = AccelerationStructureGeometryFlags::Opaque;

            AccelerationStructureBuildDesc buildDesc = {};
            buildDesc.inputs = &buildInput;
            buildDesc.inputCount = 1;
            buildDesc.flags = AccelerationStructureBuildFlags::PreferFastTrace;

            AccelerationStructureSizes sizes;
            if (SLANG_FAILED(device->getAccelerationStructureSizes(buildDesc, &sizes)))
            {
                spdlog::error("Failed to get BLAS sizes");
                return false;
            }

            BufferDesc scratchDesc = {};
            scratchDesc.usage = BufferUsage::UnorderedAccess;
            scratchDesc.defaultState = ResourceState::UnorderedAccess;
            scratchDesc.size = sizes.scratchSize;
            auto scratchBuffer = device->createBuffer(scratchDesc);

            AccelerationStructureDesc asDesc = {};
            asDesc.kind = AccelerationStructureKind::BottomLevel;
            asDesc.size = sizes.accelerationStructureSize;

            Slang::ComPtr<IAccelerationStructure> blas;
            if (SLANG_FAILED(device->createAccelerationStructure(asDesc, blas.writeRef())))
            {
                spdlog::error("Failed to create BLAS");
                return false;
            }

            encoder->buildAccelerationStructure(buildDesc, blas, nullptr, scratchBuffer, 0, nullptr);
            blasList.push_back(blas);
        }

        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    // Build TLAS
    std::vector<AccelerationStructureInstanceDescGeneric> genericInstances(blasList.size());
    for (size_t i = 0; i < blasList.size(); i++)
    {
        auto& inst = genericInstances[i];
        const auto& m = meshGeometries[i].transform;
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 4; col++)
                inst.transform[row][col] = m[col][row];
        inst.instanceID = static_cast<uint32_t>(i);
        inst.instanceMask = 0xFF;
        inst.instanceContributionToHitGroupIndex = 0;
        inst.flags = AccelerationStructureInstanceFlags::None;
        inst.accelerationStructure = blasList[i]->getHandle();
    }

    std::vector<uint8_t> nativeInstances(genericInstances.size() * instanceDescSize);
    convertAccelerationStructureInstanceDescs(
        genericInstances.size(),
        instanceDescType,
        nativeInstances.data(),
        instanceDescSize,
        genericInstances.data(),
        sizeof(AccelerationStructureInstanceDescGeneric)
    );

    BufferDesc instanceBufferDesc = {};
    instanceBufferDesc.size = nativeInstances.size();
    instanceBufferDesc.usage = BufferUsage::ShaderResource;
    instanceBufferDesc.defaultState = ResourceState::ShaderResource;
    auto instanceBuffer = device->createBuffer(instanceBufferDesc, nativeInstances.data());

    AccelerationStructureBuildInput buildInput = {};
    buildInput.type = AccelerationStructureBuildInputType::Instances;
    buildInput.instances.instanceBuffer = instanceBuffer;
    buildInput.instances.instanceCount = static_cast<uint32_t>(genericInstances.size());
    buildInput.instances.instanceStride = static_cast<uint32_t>(instanceDescSize);

    AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.inputs = &buildInput;
    buildDesc.inputCount = 1;

    AccelerationStructureSizes sizes;
    if (SLANG_FAILED(device->getAccelerationStructureSizes(buildDesc, &sizes)))
    {
        spdlog::error("Failed to get TLAS sizes");
        return false;
    }

    BufferDesc scratchDesc = {};
    scratchDesc.usage = BufferUsage::UnorderedAccess;
    scratchDesc.defaultState = ResourceState::UnorderedAccess;
    scratchDesc.size = sizes.scratchSize;
    auto scratchBuffer = device->createBuffer(scratchDesc);

    AccelerationStructureDesc asDesc = {};
    asDesc.kind = AccelerationStructureKind::TopLevel;
    asDesc.size = sizes.accelerationStructureSize;

    if (SLANG_FAILED(device->createAccelerationStructure(asDesc, tlas.writeRef())))
    {
        spdlog::error("Failed to create TLAS");
        return false;
    }

    auto encoder = queue->createCommandEncoder();
    encoder->buildAccelerationStructure(buildDesc, tlas, nullptr, scratchBuffer, 0, nullptr);
    queue->submit(encoder->finish());
    queue->waitOnHost();

    tlas->getDescriptorHandle(&tlasHandle);

    spdlog::info("Built {} BLAS + TLAS", blasList.size());
    return true;
}

void Scene::Bind(const ShaderVar& var) const
{
    auto scene = var["gScene"];
    scene["vertices"] = vertexBuffer;
    scene["indices"] = indexBuffer;
    scene["materials"] = materialBuffer;
    scene["meshInfos"] = meshInfoBuffer;
    if (lightBuffer)
        scene["lights"] = lightBuffer;
    scene["lightCount"] = lightCount;
    scene["tlas"] = tlas;
    scene["sampler"] = linearSampler;

    for (size_t i = 0; i < textures.size(); i++)
        scene["textures"][static_cast<uint32_t>(i)] = textures[i];
}
