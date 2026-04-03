#include "VBufferRTPass.h"

#include <spdlog/spdlog.h>

using namespace rhi;

VBufferRTPass::VBufferRTPass(IDevice* device, std::shared_ptr<Scene> scene, Camera* camera)
    : device(device), scene(std::move(scene)), camera(camera)
{
    CreatePipeline();
}

void VBufferRTPass::CreatePipeline()
{
    program = std::make_unique<Program>(device, "RenderPasses/VBufferRT/VBufferRT");
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
    pipelineDesc.label = "VBufferRTPass";

    auto result = device->createRayTracingPipeline(pipelineDesc, pipeline.writeRef());
    if (SLANG_FAILED(result))
    {
        spdlog::error("Failed to create VBufferRT pipeline: {}", static_cast<int>(result));
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
        spdlog::error("Failed to create VBufferRT shader table: {}", static_cast<int>(result));
}

void VBufferRTPass::Setup()
{
    vbuffer = AddOutput("vbuffer", Format::RGBA32Uint, PassSlot::Access::UnorderedAccess,
                        SizePolicy::BackBuffer(), LoadOp::Load, 0);
    MarkSideEffect();
}

void VBufferRTPass::Execute(ICommandEncoder* encoder, const RenderGraphResources& resources)
{
    if (!pipeline || !shaderTable || !scene)
        return;

    auto* vbufTex = resources.GetTexture("vbuffer");
    if (!vbufTex)
        return;

    auto* pass = encoder->beginRayTracingPass();
    auto* shaderObj = pass->bindPipeline(pipeline, shaderTable);
    if (!shaderObj)
    {
        spdlog::error("VBufferRTPass: bindPipeline returned null");
        pass->end();
        return;
    }

    ShaderVar vars(shaderObj);

    if (camera)
        camera->Bind(vars);

    scene->Bind(vars);
    vars["gVBuffer"] = ComPtr(vbufTex);

    auto desc = vbufTex->getDesc();
    pass->dispatchRays(0, desc.size.width, desc.size.height, 1);
    pass->end();
}
