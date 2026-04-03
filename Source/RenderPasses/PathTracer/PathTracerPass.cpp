#include "PathTracerPass.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <cstring>

using namespace rhi;

PathTracerPass::PathTracerPass(IDevice* device, Format colorFormat, std::shared_ptr<Scene> scene, Camera* camera)
    : device(device), colorFormat(colorFormat), scene(std::move(scene)), camera(camera)
{
    CreatePipeline();
}

void PathTracerPass::SetEnvMap(ITexture* texture, ISampler* sampler,
                               IBuffer* importanceCdf, uint32_t width, uint32_t height)
{
    envMap = texture;
    envSampler = sampler;
    envImportanceCdf = importanceCdf;
    envWidth = width;
    envHeight = height;
    frameCount = 0;
}

void PathTracerPass::CreatePipeline()
{
    program = std::make_unique<Program>(device, "RenderPasses/PathTracer/PathTracer");
    if (!program->GetShaderProgram())
        return;

    HitGroupDesc hitGroup = {};
    hitGroup.hitGroupName = "hitGroup";
    hitGroup.closestHitEntryPoint = "closestHit";

    RayTracingPipelineDesc rtDesc = {};
    rtDesc.program = program->GetShaderProgram();
    rtDesc.maxRecursion = 2;
    rtDesc.hitGroupCount = 1;
    rtDesc.hitGroups = &hitGroup;

    auto result = device->createRayTracingPipeline(rtDesc, rtPipeline.writeRef());
    if (SLANG_FAILED(result))
    {
        spdlog::error("Failed to create PathTracer RT pipeline: {}", static_cast<int>(result));
        return;
    }

    const char* rayGenNames[] = {"rayGen"};
    const char* missNames[] = {"miss"};
    const char* hitGroupNames[] = {"hitGroup"};

    ShaderTableDesc tableDesc = {};
    tableDesc.program = program->GetShaderProgram();
    tableDesc.rayGenShaderCount = 1;
    tableDesc.rayGenShaderEntryPointNames = rayGenNames;
    tableDesc.missShaderCount = 1;
    tableDesc.missShaderEntryPointNames = missNames;
    tableDesc.hitGroupCount = 1;
    tableDesc.hitGroupNames = hitGroupNames;

    result = device->createShaderTable(tableDesc, shaderTable.writeRef());
    if (SLANG_FAILED(result))
        spdlog::error("Failed to create PathTracer shader table: {}", static_cast<int>(result));
}

void PathTracerPass::Setup()
{
    vbufferIn = AddInput("vbuffer", PassSlot::Access::ShaderResource);
    output = AddOutput("output", colorFormat, PassSlot::Access::UnorderedAccess,
                       SizePolicy::BackBuffer(), LoadOp::Load, 0);
    MarkSideEffect();
}

void PathTracerPass::Execute(ICommandEncoder* encoder, const RenderGraphResources& resources)
{
    if (!rtPipeline || !shaderTable || !scene)
        return;

    auto* vbufTex = resources.GetTexture("vbuffer");
    auto* outTex = resources.GetTexture("output");
    if (!vbufTex || !outTex)
        return;

    auto outDesc = outTex->getDesc();
    bool needNewAccum = !accumTexture;
    if (accumTexture)
    {
        auto accumDesc = accumTexture->getDesc();
        if (accumDesc.size.width != outDesc.size.width || accumDesc.size.height != outDesc.size.height)
            needNewAccum = true;
    }
    if (needNewAccum)
    {
        TextureDesc accumDesc = {};
        accumDesc.type = TextureType::Texture2D;
        accumDesc.size = {outDesc.size.width, outDesc.size.height, 1};
        accumDesc.format = Format::RGBA32Float;
        accumDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::ShaderResource;
        accumDesc.defaultState = ResourceState::UnorderedAccess;
        accumTexture = device->createTexture(accumDesc);
        frameCount = 0;
    }

    if (camera)
    {
        const auto& camData = camera->GetData();
        if (camData.position != lastCameraData.position ||
            camData.forward != lastCameraData.forward ||
            camData.fovY != lastCameraData.fovY)
        {
            frameCount = 0;
            lastCameraData = camData;
        }
    }

    auto* pass = encoder->beginRayTracingPass();
    auto* shaderObj = pass->bindPipeline(rtPipeline, shaderTable);
    if (!shaderObj)
    {
        spdlog::error("PathTracerPass: bindPipeline returned null");
        pass->end();
        return;
    }

    ShaderVar vars(shaderObj);

    if (camera)
        camera->Bind(vars);

    scene->Bind(vars);
    vars["gVBuffer"] = Slang::ComPtr<ITexture>(vbufTex);
    vars["gOutput"] = Slang::ComPtr<ITexture>(outTex);
    vars["gAccum"] = accumTexture;
    vars["gFrameCount"] = frameCount;
    vars["gMaxBounces"] = maxBounces;
    vars["gSeed"] = frameCount * 19937u + 1u;
    vars["gExposure"] = exposure;

    bool hasEnv = envMap && envSampler;
    vars["gHasEnvMap"] = hasEnv ? 1 : 0;
    vars["gHasEnvImportance"] = hasEnv && envImportanceCdf ? 1 : 0;
    vars["gEnvWidth"] = envWidth;
    vars["gEnvHeight"] = envHeight;
    if (hasEnv)
    {
        vars["gEnvMap"] = Slang::ComPtr<ITexture>(envMap);
        vars["gEnvSampler"] = Slang::ComPtr<ISampler>(envSampler);
        if (envImportanceCdf)
            vars["gEnvCdf"] = Slang::ComPtr<IBuffer>(envImportanceCdf);
    }

    pass->dispatchRays(0, outDesc.size.width, outDesc.size.height, 1);
    pass->end();

    frameCount++;
}

void PathTracerPass::OnRenderUI()
{
    int bounces = static_cast<int>(maxBounces);
    if (ImGui::SliderInt("Max Bounces", &bounces, 1, 16))
    {
        maxBounces = static_cast<uint32_t>(bounces);
        frameCount = 0;
    }
    if (ImGui::SliderFloat("Exposure", &exposure, 0.1f, 10.0f))
        frameCount = 0;
    ImGui::Text("Samples: %u", frameCount);
}
