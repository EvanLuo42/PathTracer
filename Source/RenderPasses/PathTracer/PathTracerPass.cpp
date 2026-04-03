#include "PathTracerPass.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>

using namespace rhi;

namespace
{
    bool NearlyEqual(const float a, const float b, const float epsilon = 1e-5f)
    {
        return std::abs(a - b) <= epsilon;
    }

    bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, const float epsilon = 1e-5f)
    {
        const glm::vec3 delta = a - b;
        return glm::dot(delta, delta) <= epsilon * epsilon;
    }

    bool HasCameraStateChanged(const CameraData& current, const CameraData& previous)
    {
        return !NearlyEqual(current.position, previous.position) ||
               !NearlyEqual(current.forward, previous.forward) ||
               !NearlyEqual(current.up, previous.up) ||
               !NearlyEqual(current.fovY, previous.fovY) ||
               !NearlyEqual(current.aspectRatio, previous.aspectRatio);
    }
}

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
    accumulatedSamples = 0;
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
        accumulatedSamples = 0;
    }

    bool cameraChanged = false;
    if (camera)
    {
        const auto& camData = camera->GetData();
        cameraChanged = camera->DidChangeThisFrame() ||
                        !hasLastCameraData ||
                        HasCameraStateChanged(camData, lastCameraData);

        lastCameraData = camData;
        hasLastCameraData = true;
    }

    if (cameraChanged)
        accumulatedSamples = 0;

    const uint32_t currentSamplesPerFrame = cameraChanged ? movingSamplesPerFrame : samplesPerFrame;

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
    vars["gAccumulatedSamples"] = accumulatedSamples;
    vars["gSamplesPerFrame"] = currentSamplesPerFrame;
    vars["gMaxBounces"] = maxBounces;
    vars["gSeed"] = renderFrameIndex * 19937u + 1u;
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

    accumulatedSamples += currentSamplesPerFrame;
    renderFrameIndex++;
}

void PathTracerPass::OnRenderUI()
{
    int bounces = static_cast<int>(maxBounces);
    if (ImGui::SliderInt("Max Bounces", &bounces, 1, 16))
    {
        maxBounces = static_cast<uint32_t>(bounces);
        accumulatedSamples = 0;
    }

    int spp = static_cast<int>(samplesPerFrame);
    if (ImGui::SliderInt("Samples / Frame", &spp, 1, 8))
    {
        samplesPerFrame = static_cast<uint32_t>(spp);
        accumulatedSamples = 0;
    }

    int movingSpp = static_cast<int>(movingSamplesPerFrame);
    if (ImGui::SliderInt("Moving Samples / Frame", &movingSpp, 1, 8))
    {
        movingSamplesPerFrame = static_cast<uint32_t>(movingSpp);
        accumulatedSamples = 0;
    }

    if (ImGui::SliderFloat("Exposure", &exposure, 0.1f, 10.0f))
        accumulatedSamples = 0;

    ImGui::Text("Accumulated Samples: %u", accumulatedSamples);
}
