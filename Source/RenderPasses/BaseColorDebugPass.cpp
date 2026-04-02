#include "BaseColorDebugPass.h"
#include "../RenderPipeline.h"

using namespace rhi;

BaseColorDebugPass::BaseColorDebugPass(IDevice* device, std::shared_ptr<Scene> scene)
    : device(device), scene(std::move(scene))
{
    auto program = Program::Create(device, Program::Desc()
        .AddShaderModule("BaseColorDebug")
        .CSEntry("main")
    );
    if (!program)
        return;

    pipeline = ComputePipeline::Create(device, program);
}

void BaseColorDebugPass::Execute(ICommandEncoder* encoder, Resources& resources)
{
    if (!pipeline)
        return;

    const auto& desc = resources.colorOutput->getDesc();
    const uint32_t width = desc.size.width;
    const uint32_t height = desc.size.height;

    const auto passEncoder = encoder->beginComputePass();
    const ShaderVar vars(passEncoder->bindPipeline(pipeline));

    vars["gVBuffer"] = resources.vBuffer;
    vars["gOutput"] = resources.colorOutput;
    scene->Bind(vars["gScene"]);

    passEncoder->dispatchCompute((width + 7) / 8, (height + 7) / 8, 1);
    passEncoder->end();
}
