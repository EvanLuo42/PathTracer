#include "GBufferRTPass.h"

#include <spdlog/spdlog.h>

using namespace rhi;

GBufferRTPass::GBufferRTPass(IDevice* device, std::shared_ptr<Scene> scene, Camera* camera)
    : device(device), scene(std::move(scene)), camera(camera)
{
    CreatePipeline();
}

void GBufferRTPass::CreatePipeline()
{
    program = std::make_unique<Program>(device, "RenderPasses/GBufferRT/GBufferRT");
    if (!program->GetShaderProgram())
        return;

    HitGroupDesc hitGroup = {};
    hitGroup.hitGroupName = "hitGroup";
    hitGroup.closestHitEntryPoint = "closestHit";

    RayTracingPipelineDesc pipelineDesc = {};
    pipelineDesc.program = program->GetShaderProgram();
    pipelineDesc.maxRecursion = 1;
    pipelineDesc.hitGroupCount = 1;
    pipelineDesc.hitGroups = &hitGroup;

    auto result = device->createRayTracingPipeline(pipelineDesc, pipeline.writeRef());
    if (SLANG_FAILED(result))
    {
        spdlog::error("Failed to create GBufferRT pipeline: {}", static_cast<int>(result));
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
        spdlog::error("Failed to create GBufferRT shader table: {}", static_cast<int>(result));
}

void GBufferRTPass::Setup()
{
    baseColor = addOutput("baseColor", Format::RGBA8Unorm, PassSlot::Access::UnorderedAccess,
                          SizePolicy::BackBuffer(), LoadOp::Load, 0);
    normal = addOutput("normal", Format::RG16Float, PassSlot::Access::UnorderedAccess,
                       SizePolicy::BackBuffer(), LoadOp::Load, 0);
    materialData = addOutput("materialData", Format::RGBA8Unorm, PassSlot::Access::UnorderedAccess,
                             SizePolicy::BackBuffer(), LoadOp::Load, 0);
    depth = addOutput("depth", Format::R32Float, PassSlot::Access::UnorderedAccess,
                      SizePolicy::BackBuffer(), LoadOp::Load, 0);
    markSideEffect();
}

void GBufferRTPass::Execute(ICommandEncoder* encoder, const RenderGraphResources& resources)
{
    if (!pipeline || !shaderTable || !scene)
        return;

    auto* baseTex = resources.getTexture("baseColor");
    auto* normTex = resources.getTexture("normal");
    auto* matTex = resources.getTexture("materialData");
    auto* depthTex = resources.getTexture("depth");
    if (!baseTex || !normTex || !matTex || !depthTex)
        return;

    auto* pass = encoder->beginRayTracingPass();
    auto* shaderObj = pass->bindPipeline(pipeline, shaderTable);
    if (!shaderObj)
    {
        spdlog::error("GBufferRTPass: bindPipeline returned null");
        pass->end();
        return;
    }

    ShaderVar vars(shaderObj);

    if (camera)
        camera->Bind(vars);

    scene->Bind(vars);
    vars["gBaseColor"] = Slang::ComPtr<ITexture>(baseTex);
    vars["gNormal"] = Slang::ComPtr<ITexture>(normTex);
    vars["gMaterialData"] = Slang::ComPtr<ITexture>(matTex);
    vars["gDepth"] = Slang::ComPtr<ITexture>(depthTex);

    auto desc = baseTex->getDesc();
    pass->dispatchRays(0, desc.size.width, desc.size.height, 1);
    pass->end();
}
