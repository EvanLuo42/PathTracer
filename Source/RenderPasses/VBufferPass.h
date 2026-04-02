#pragma once

#include "IRenderPass.h"
#include "../Scene.h"
#include "../ShaderVar.h"
#include "../Program.h"
#include "../RenderPipeline.h"

#include <slang-rhi.h>
#include <slang-com-ptr.h>
#include <memory>

class VBufferPass : public IRenderPass
{
public:
    VBufferPass(rhi::IDevice* device, std::shared_ptr<Scene> scene);

    void Execute(rhi::ICommandEncoder* encoder, Resources& resources) override;

private:
    rhi::IDevice* device;
    std::shared_ptr<Scene> scene;

    std::shared_ptr<Program> program;
    Slang::ComPtr<rhi::IRayTracingPipeline> pipeline;
    Slang::ComPtr<rhi::IShaderTable> shaderTable;
};
