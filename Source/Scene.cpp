#include "Scene.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>

using namespace rhi;

std::shared_ptr<Scene> Scene::Create(IDevice* device, ICommandQueue* queue, const std::filesystem::path& path)
{
    auto scene = std::shared_ptr<Scene>(new Scene());
    scene->device = device;

    if (!scene->Load(path))
        return nullptr;
    if (!scene->BuildBuffers(device))
        return nullptr;
    if (!scene->BuildTextures(device))
        return nullptr;
    if (!scene->BuildAccelerationStructure(device, queue))
        return nullptr;

    return scene;
}

bool Scene::Load(const std::filesystem::path& path)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool result;
    if (path.extension() == ".glb")
        result = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());
    else
        result = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());

    if (!warn.empty())
        std::cerr << "[Scene] Warning: " << warn << std::endl;
    if (!err.empty())
        std::cerr << "[Scene] Error: " << err << std::endl;
    if (!result)
    {
        std::cerr << "[Scene] Failed to load: " << path << std::endl;
        return false;
    }

    // Load textures
    auto resolveTexture = [&](int textureIndex) -> int32_t
    {
        if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
            return -1;
        return model.textures[textureIndex].source;
    };

    for (const auto& image : model.images)
    {
        TextureData td;
        td.width = image.width;
        td.height = image.height;
        td.channels = image.component;
        // Ensure RGBA
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

    // Load materials
    for (const auto& mat : model.materials)
    {
        Material material;
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
        material.normalTextureIndex = resolveTexture(mat.normalTexture.index);
        material.emissiveTextureIndex = resolveTexture(mat.emissiveTexture.index);
        materials.push_back(material);
    }

    if (materials.empty())
        materials.push_back(Material{});

    // Compute node transforms
    auto getNodeTransform = [](const tinygltf::Node& node) -> glm::mat4
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
            T = glm::translate(glm::mat4(1.0f), glm::vec3(
                static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]),
                static_cast<float>(node.translation[2])
            ));
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
            S = glm::scale(glm::mat4(1.0f), glm::vec3(
                static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]),
                static_cast<float>(node.scale[2])
            ));
        return T * R * S;
    };

    // Process meshes from all nodes
    for (const auto& node : model.nodes)
    {
        if (node.mesh < 0)
            continue;

        const glm::mat4 nodeTransform = getNodeTransform(node);
        const auto& mesh = model.meshes[node.mesh];
        for (const auto& primitive : mesh.primitives)
        {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
                continue;

            MeshGeometry geom;
            geom.vertexOffset = static_cast<uint32_t>(vertices.size());
            geom.indexOffset = static_cast<uint32_t>(indices.size());

            // Positions
            const auto& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
            const auto& posView = model.bufferViews[posAccessor.bufferView];
            const auto* posData = reinterpret_cast<const float*>(
                &model.buffers[posView.buffer].data[posView.byteOffset + posAccessor.byteOffset]
            );
            const auto posStride = posAccessor.ByteStride(posView) / sizeof(float);

            // Normals (optional)
            const float* normalData = nullptr;
            size_t normalStride = 0;
            if (primitive.attributes.count("NORMAL"))
            {
                const auto& normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
                const auto& normView = model.bufferViews[normAccessor.bufferView];
                normalData = reinterpret_cast<const float*>(
                    &model.buffers[normView.buffer].data[normView.byteOffset + normAccessor.byteOffset]
                );
                normalStride = normAccessor.ByteStride(normView) / sizeof(float);
            }

            // TexCoords (optional)
            const float* uvData = nullptr;
            size_t uvStride = 0;
            if (primitive.attributes.count("TEXCOORD_0"))
            {
                const auto& uvAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                const auto& uvView = model.bufferViews[uvAccessor.bufferView];
                uvData = reinterpret_cast<const float*>(
                    &model.buffers[uvView.buffer].data[uvView.byteOffset + uvAccessor.byteOffset]
                );
                uvStride = uvAccessor.ByteStride(uvView) / sizeof(float);
            }

            geom.vertexCount = static_cast<uint32_t>(posAccessor.count);
            for (size_t i = 0; i < posAccessor.count; i++)
            {
                Vertex v;
                v.position = glm::vec3(posData[i * posStride], posData[i * posStride + 1], posData[i * posStride + 2]);
                v.normal = normalData
                    ? glm::vec3(normalData[i * normalStride], normalData[i * normalStride + 1], normalData[i * normalStride + 2])
                    : glm::vec3(0, 1, 0);
                v.texCoord = uvData
                    ? glm::vec2(uvData[i * uvStride], uvData[i * uvStride + 1])
                    : glm::vec2(0);
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
                    index = reinterpret_cast<const uint8_t*>(idxRaw)[i];
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

            geom.transform = nodeTransform;
            meshGeometries.push_back(geom);
            totalIndexCount += geom.indexCount;
        }
    }

    std::cerr << "[Scene] Loaded " << path.filename()
              << ": " << vertices.size() << " vertices, "
              << indices.size() / 3 << " triangles, "
              << meshInfos.size() << " meshes, "
              << materials.size() << " materials" << std::endl;

    return true;
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
            std::cerr << "[Scene] Failed to create texture" << std::endl;
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

    std::cerr << "[Scene] Created " << textures.size() << " textures" << std::endl;
    return true;
}

bool Scene::BuildAccelerationStructure(IDevice* device, ICommandQueue* queue)
{
    auto instanceDescType = getAccelerationStructureInstanceDescType(device);
    auto instanceDescSize = getAccelerationStructureInstanceDescSize(instanceDescType);

    // Build one BLAS per mesh
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
            std::cerr << "[Scene] Failed to get BLAS sizes" << std::endl;
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
            std::cerr << "[Scene] Failed to create BLAS" << std::endl;
            return false;
        }

        auto encoder = queue->createCommandEncoder();
        encoder->buildAccelerationStructure(buildDesc, blas, nullptr, scratchBuffer, 0, nullptr);
        queue->submit(encoder->finish());
        queue->waitOnHost();

        blasList.push_back(blas);
    }

    // Build TLAS
    std::vector<AccelerationStructureInstanceDescGeneric> genericInstances(blasList.size());
    for (size_t i = 0; i < blasList.size(); i++)
    {
        auto& inst = genericInstances[i];
        // glm is column-major, instance transform is row-major 3x4
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
        std::cerr << "[Scene] Failed to get TLAS sizes" << std::endl;
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
        std::cerr << "[Scene] Failed to create TLAS" << std::endl;
        return false;
    }

    auto encoder = queue->createCommandEncoder();
    encoder->buildAccelerationStructure(buildDesc, tlas, nullptr, scratchBuffer, 0, nullptr);
    queue->submit(encoder->finish());
    queue->waitOnHost();

    tlas->getDescriptorHandle(&tlasHandle);

    std::cerr << "[Scene] Built " << blasList.size() << " BLAS + TLAS" << std::endl;
    return true;
}

void Scene::Bind(const ShaderVar& var) const
{
    var["vertices"] = vertexBuffer;
    var["indices"] = indexBuffer;
    var["materials"] = materialBuffer;
    var["meshInfos"] = meshInfoBuffer;
    var["tlas"] = tlas;
    var["sampler"] = linearSampler;

    for (size_t i = 0; i < textures.size(); i++)
        var["textures"][static_cast<uint32_t>(i)] = textures[i];
}
