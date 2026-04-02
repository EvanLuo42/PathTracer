#pragma once

#include "Program.h"

#include <slang-rhi.h>
#include <slang-com-ptr.h>

#include <iostream>
#include <memory>

class RenderPipeline
{
public:
    struct Desc
    {
        Desc& SetProgram(const std::shared_ptr<Program>& prog)
        {
            program = prog;
            return *this;
        }

        Desc& SetInputLayout(const Slang::ComPtr<rhi::IInputLayout>& layout)
        {
            inputLayout = layout;
            return *this;
        }

        Desc& SetPrimitiveTopology(const rhi::PrimitiveTopology topology)
        {
            primitiveTopology = topology;
            return *this;
        }

        Desc& AddRenderTarget(const rhi::ColorTargetDesc& target)
        {
            targets.push_back(target);
            return *this;
        }

        Desc& SetDepthStencil(const rhi::DepthStencilDesc& ds)
        {
            depthStencil = ds;
            return *this;
        }

        Desc& SetRasterizer(const rhi::RasterizerDesc& rs)
        {
            rasterizer = rs;
            return *this;
        }

        Desc& SetMultisample(const rhi::MultisampleDesc& ms)
        {
            multisample = ms;
            return *this;
        }

        std::shared_ptr<Program> program;
        Slang::ComPtr<rhi::IInputLayout> inputLayout;
        rhi::PrimitiveTopology primitiveTopology = rhi::PrimitiveTopology::TriangleList;
        std::vector<rhi::ColorTargetDesc> targets;
        rhi::DepthStencilDesc depthStencil;
        rhi::RasterizerDesc rasterizer;
        rhi::MultisampleDesc multisample;
    };

    static Slang::ComPtr<rhi::IRenderPipeline> Create(rhi::IDevice* device, const Desc& desc)
    {
        rhi::RenderPipelineDesc rhiDesc = {};
        rhiDesc.program = desc.program->GetShaderProgram();
        rhiDesc.inputLayout = desc.inputLayout;
        rhiDesc.primitiveTopology = desc.primitiveTopology;
        rhiDesc.targets = desc.targets.data();
        rhiDesc.targetCount = static_cast<uint32_t>(desc.targets.size());
        rhiDesc.depthStencil = desc.depthStencil;
        rhiDesc.rasterizer = desc.rasterizer;
        rhiDesc.multisample = desc.multisample;
        rhiDesc.label = desc.program->GetDesc().moduleName.c_str();

        auto pipeline = device->createRenderPipeline(rhiDesc);
        if (!pipeline)
            std::cerr << "[RenderPipeline] Failed to create pipeline" << std::endl;
        return pipeline;
    }
};

class ComputePipeline
{
public:
    static Slang::ComPtr<rhi::IComputePipeline> Create(rhi::IDevice* device, const std::shared_ptr<Program>& program)
    {
        rhi::ComputePipelineDesc desc = {};
        desc.program = program->GetShaderProgram();
        desc.label = program->GetDesc().moduleName.c_str();

        auto pipeline = device->createComputePipeline(desc);
        if (!pipeline)
            std::cerr << "[ComputePipeline] Failed to create pipeline" << std::endl;
        return pipeline;
    }
};

class RayTracingPipeline
{
public:
    struct Desc
    {
        Desc& SetProgram(const std::shared_ptr<Program>& prog)
        {
            program = prog;
            return *this;
        }

        Desc& AddHitGroup(const char* name, const char* closestHit, const char* anyHit = nullptr, const char* intersection = nullptr)
        {
            rhi::HitGroupDesc hg = {};
            hg.hitGroupName = name;
            hg.closestHitEntryPoint = closestHit;
            hg.anyHitEntryPoint = anyHit;
            hg.intersectionEntryPoint = intersection;
            hitGroups.push_back(hg);
            return *this;
        }

        Desc& SetMaxRecursion(const uint32_t depth)
        {
            maxRecursion = depth;
            return *this;
        }

        Desc& SetMaxRayPayloadSize(const uint32_t size)
        {
            maxRayPayloadSize = size;
            return *this;
        }

        Desc& SetMaxAttributeSize(const uint32_t size)
        {
            maxAttributeSize = size;
            return *this;
        }

        Desc& SetFlags(const rhi::RayTracingPipelineFlags f)
        {
            flags = f;
            return *this;
        }

        std::shared_ptr<Program> program;
        std::vector<rhi::HitGroupDesc> hitGroups;
        uint32_t maxRecursion = 1;
        uint32_t maxRayPayloadSize = 0;
        uint32_t maxAttributeSize = 8;
        rhi::RayTracingPipelineFlags flags = rhi::RayTracingPipelineFlags::None;
    };

    static Slang::ComPtr<rhi::IRayTracingPipeline> Create(rhi::IDevice* device, const Desc& desc)
    {
        rhi::RayTracingPipelineDesc rhiDesc = {};
        rhiDesc.program = desc.program->GetShaderProgram();
        rhiDesc.hitGroups = desc.hitGroups.data();
        rhiDesc.hitGroupCount = static_cast<uint32_t>(desc.hitGroups.size());
        rhiDesc.maxRecursion = desc.maxRecursion;
        rhiDesc.maxRayPayloadSize = desc.maxRayPayloadSize;
        rhiDesc.maxAttributeSizeInBytes = desc.maxAttributeSize;
        rhiDesc.flags = desc.flags;
        rhiDesc.label = desc.program->GetDesc().moduleName.c_str();

        auto pipeline = device->createRayTracingPipeline(rhiDesc);
        if (!pipeline)
            std::cerr << "[RayTracingPipeline] Failed to create pipeline" << std::endl;
        return pipeline;
    }
};
