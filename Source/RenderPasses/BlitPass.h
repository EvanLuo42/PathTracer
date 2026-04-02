#pragma once

#include "IRenderPass.h"
#include "../Program.h"
#include "../RenderPipeline.h"

#include <slang-rhi.h>
#include <slang-com-ptr.h>

class BlitPass : public IRenderPass
{
public:
    BlitPass(rhi::IDevice* device, rhi::ISurface* surface);
    void Execute(rhi::ICommandEncoder* encoder, Resources& resources) override;
    const char* GetName() const override { return "Blit"; }

private:
    rhi::IDevice* device;
    Slang::ComPtr<rhi::IRenderPipeline> pipeline;
    Slang::ComPtr<rhi::ISampler> sampler;
};
