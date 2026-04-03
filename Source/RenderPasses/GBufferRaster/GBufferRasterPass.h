#pragma once

#include "RenderGraph/RenderPass.h"
#include "Scene/Scene.h"
#include "Core/ShaderVar.h"
#include "Core/Program.h"
#include "Renderer/Camera.h"

#include <slang-rhi.h>
#include <slang-com-ptr.h>

#include <memory>

class GBufferRasterPass : public RenderPass
{
public:
    GBufferRasterPass(rhi::IDevice* device, std::shared_ptr<Scene> scene, Camera* camera);

    void Setup() override;
    void Execute(rhi::ICommandEncoder* encoder, const RenderGraphResources& resources) override;
    const char* GetName() const override { return "GBufferRaster"; }

    RenderGraphSlot baseColor;
    RenderGraphSlot normal;
    RenderGraphSlot materialData;
    RenderGraphSlot depth;

private:
    void CreatePipeline();

    Slang::ComPtr<rhi::IDevice> device;
    std::shared_ptr<Scene> scene;
    Camera* camera;

    std::unique_ptr<Program> program;
    Slang::ComPtr<rhi::IRenderPipeline> pipeline;
    Slang::ComPtr<rhi::IInputLayout> inputLayout;
};
