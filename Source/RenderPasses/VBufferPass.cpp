#include "VBufferPass.h"

#include <iostream>

using namespace rhi;

VBufferPass::VBufferPass(IDevice* device, std::shared_ptr<Scene> scene)
    : device(device), scene(std::move(scene))
{
    program = Program::Create(device, Program::Desc()
        .AddShaderModule("VBuffer")
        .CSEntry("RayGen")
        .CSEntry("ClosestHit")
        .CSEntry("Miss")
    );
    if (!program)
        return;

    pipeline = RayTracingPipeline::Create(device, RayTracingPipeline::Desc()
        .SetProgram(program)
        .AddHitGroup("HitGroup", "ClosestHit")
        .SetMaxRecursion(1)
        .SetMaxRayPayloadSize(8)
    );
    if (!pipeline)
        return;

    const char* rayGenNames[] = {"RayGen"};
    const char* missNames[] = {"Miss"};
    const char* hitGroupNames[] = {"HitGroup"};

    ShaderTableDesc tableDesc = {};
    tableDesc.program = program->GetShaderProgram();
    tableDesc.rayGenShaderCount = 1;
    tableDesc.rayGenShaderEntryPointNames = rayGenNames;
    tableDesc.missShaderCount = 1;
    tableDesc.missShaderEntryPointNames = missNames;
    tableDesc.hitGroupCount = 1;
    tableDesc.hitGroupNames = hitGroupNames;

    if (SLANG_FAILED(device->createShaderTable(tableDesc, shaderTable.writeRef())))
    {
        std::cerr << "[VBufferPass] Failed to create shader table" << std::endl;
        return;
    }
}

void VBufferPass::Execute(ICommandEncoder* encoder, Resources& resources)
{
    if (!pipeline || !shaderTable)
        return;

    const auto& vbufDesc = resources.vBuffer->getDesc();
    const uint32_t width = vbufDesc.size.width;
    const uint32_t height = vbufDesc.size.height;

    const auto passEncoder = encoder->beginRayTracingPass();
    passEncoder->pushDebugGroup(GetName(), kPassColor);

    const ShaderVar vars(passEncoder->bindPipeline(pipeline, shaderTable));
    vars["gVBuffer"] = resources.vBuffer;
    vars["gTLAS"] = scene->GetTLAS();
    vars["gCamera"] = resources.cameraData;

    passEncoder->dispatchRays(0, width, height, 1);
    passEncoder->popDebugGroup();
    passEncoder->end();
}
