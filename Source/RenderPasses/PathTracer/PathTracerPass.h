#pragma once

#include "RenderGraph/RenderPass.h"
#include "Scene/Scene.h"
#include "Core/ShaderVar.h"
#include "Core/Program.h"
#include "Renderer/Camera.h"

#include <slang-rhi.h>
#include <slang-com-ptr.h>

#include <memory>

class PathTracerPass : public RenderPass
{
public:
    PathTracerPass(rhi::IDevice* device, rhi::Format colorFormat, std::shared_ptr<Scene> scene, Camera* camera);

    void Setup() override;
    void Execute(rhi::ICommandEncoder* encoder, const RenderGraphResources& resources) override;
    void OnRenderUI() override;
    const char* GetName() const override { return "PathTracer"; }

    void ResetAccumulation() { accumulatedSamples = 0; }
    void SetEnvMap(rhi::ITexture* texture, rhi::ISampler* sampler,
                   rhi::IBuffer* importanceCdf = nullptr, uint32_t width = 0, uint32_t height = 0);

    RenderGraphSlot vbufferIn;
    RenderGraphSlot output;

private:
    void CreatePipeline();

    Slang::ComPtr<rhi::IDevice> device;
    rhi::Format colorFormat;
    std::shared_ptr<Scene> scene;
    Camera* camera;

    std::unique_ptr<Program> program;
    Slang::ComPtr<rhi::IRayTracingPipeline> rtPipeline;
    Slang::ComPtr<rhi::IShaderTable> shaderTable;

    // Accumulation
    Slang::ComPtr<rhi::ITexture> accumTexture;
    uint32_t accumulatedSamples = 0;
    uint32_t renderFrameIndex = 0;
    uint32_t samplesPerFrame = 1;
    uint32_t movingSamplesPerFrame = 4;
    uint32_t maxBounces = 4;
    float exposure = 1.0f;

    // Environment map
    rhi::ITexture* envMap = nullptr;
    rhi::ISampler* envSampler = nullptr;
    rhi::IBuffer* envImportanceCdf = nullptr;
    uint32_t envWidth = 0;
    uint32_t envHeight = 0;

    // Keep a copy of the last bound camera state so accumulation reset does
    // not depend solely on input-event bookkeeping.
    CameraData lastCameraData{};
    bool hasLastCameraData = false;
};
