#pragma once

#include "Camera.h"

#include <slang-rhi.h>

#include <utility>

struct Resources
{
    Slang::ComPtr<rhi::ITexture> backBuffer;
    Slang::ComPtr<rhi::ITexture> vBuffer;
    Slang::ComPtr<rhi::IBuffer> cameraBuffer;
    Slang::ComPtr<rhi::ITexture> envMap;
    Slang::ComPtr<rhi::ISampler> envSampler;
    CameraData cameraData{};

    explicit Resources(rhi::IDevice* device, const uint32_t width, const uint32_t height)
        : device(device), width(width), height(height)
    {
        CreateTextures();
    }

    void Resize(const uint32_t newWidth, const uint32_t newHeight)
    {
        if (newWidth == width && newHeight == height)
            return;
        width = newWidth;
        height = newHeight;
        CreateTextures();
    }

private:
    void CreateTextures()
    {
        // V-Buffer: R32G32_UINT = (instanceID | primitiveID packed, barycentric packed)
        rhi::TextureDesc vbufDesc = {};
        vbufDesc.type = rhi::TextureType::Texture2D;
        vbufDesc.size = {width, height, 1};
        vbufDesc.format = rhi::Format::RG32Uint;
        vbufDesc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
        vbufDesc.defaultState = rhi::ResourceState::UnorderedAccess;
        vbufDesc.memoryType = rhi::MemoryType::DeviceLocal;

        vBuffer = device->createTexture(vbufDesc);

        // Camera constant buffer
        rhi::BufferDesc camDesc = {};
        camDesc.size = sizeof(CameraData);
        camDesc.usage = rhi::BufferUsage::ConstantBuffer;
        camDesc.defaultState = rhi::ResourceState::ConstantBuffer;
        camDesc.memoryType = rhi::MemoryType::Upload;
        cameraBuffer = device->createBuffer(camDesc);
    }

    Slang::ComPtr<rhi::IDevice> device;
    uint32_t width, height;
};
