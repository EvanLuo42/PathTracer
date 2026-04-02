#pragma once

#include "Camera.h"

#include <slang-rhi.h>

#include <utility>

struct Resources
{
    Slang::ComPtr<rhi::ITexture> backBuffer;
    Slang::ComPtr<rhi::ITexture> vBuffer;
    Slang::ComPtr<rhi::ITexture> colorOutput;
    Slang::ComPtr<rhi::IBuffer> cameraBuffer;
    CameraData cameraData;

    explicit Resources(Slang::ComPtr<rhi::IDevice> device, const uint32_t width, const uint32_t height)
        : device(std::move(device)), width(width), height(height)
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
        // V-Buffer: R32G32_UINT = (instanceID | primitiveID packed, barycentrics packed)
        rhi::TextureDesc vbufDesc = {};
        vbufDesc.type = rhi::TextureType::Texture2D;
        vbufDesc.size = {width, height, 1};
        vbufDesc.format = rhi::Format::RG32Uint;
        vbufDesc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
        vbufDesc.defaultState = rhi::ResourceState::UnorderedAccess;
        vbufDesc.memoryType = rhi::MemoryType::DeviceLocal;

        vBuffer = device->createTexture(vbufDesc);

        // Color output: RGBA8 for shading result
        rhi::TextureDesc colorDesc = {};
        colorDesc.type = rhi::TextureType::Texture2D;
        colorDesc.size = {width, height, 1};
        colorDesc.format = rhi::Format::RGBA32Float;
        colorDesc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
        colorDesc.defaultState = rhi::ResourceState::UnorderedAccess;
        colorDesc.memoryType = rhi::MemoryType::DeviceLocal;

        colorOutput = device->createTexture(colorDesc);

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
