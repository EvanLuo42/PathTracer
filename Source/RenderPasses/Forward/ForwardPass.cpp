#include "ForwardPass.h"

#include <spdlog/spdlog.h>

using namespace rhi;

ForwardPass::ForwardPass(IDevice* device, Format colorFormat, std::shared_ptr<Scene> scene, Camera* camera)
    : device(device), colorFormat(colorFormat), scene(std::move(scene)), camera(camera)
{
    CreatePipeline();
}

void ForwardPass::CreatePipeline()
{
    program = std::make_unique<Program>(device, "RenderPasses/Forward/Forward");
    if (!program->GetShaderProgram())
        return;

    const auto& layoutDesc = program->GetInputLayoutDesc();
    device->createInputLayout(layoutDesc, inputLayout.writeRef());

    ColorTargetDesc colorTarget = {};
    colorTarget.format = colorFormat;

    DepthStencilDesc depthStencil = {};
    depthStencil.format = Format::D32Float;
    depthStencil.depthTestEnable = true;
    depthStencil.depthWriteEnable = true;
    depthStencil.depthFunc = ComparisonFunc::Less;

    RasterizerDesc rasterizer = {};
    rasterizer.cullMode = CullMode::Back;
    rasterizer.frontFace = FrontFaceMode::CounterClockwise;

    RenderPipelineDesc pipelineDesc = {};
    pipelineDesc.program = program->GetShaderProgram();
    pipelineDesc.inputLayout = inputLayout;
    pipelineDesc.primitiveTopology = PrimitiveTopology::TriangleList;
    pipelineDesc.targets = &colorTarget;
    pipelineDesc.targetCount = 1;
    pipelineDesc.depthStencil = depthStencil;
    pipelineDesc.rasterizer = rasterizer;
    pipelineDesc.label = "Forward";

    auto result = device->createRenderPipeline(pipelineDesc, pipeline.writeRef());
    if (SLANG_FAILED(result))
        spdlog::error("Failed to create forward pipeline: {}", static_cast<int>(result));
}

void ForwardPass::Setup()
{
    colorIn = addInput("color", PassSlot::Access::RenderTarget);
    depth = addOutput("depth", Format::D32Float, PassSlot::Access::DepthStencil,
                      SizePolicy::BackBuffer(), LoadOp::Clear, 0);
    markSideEffect();
}

void ForwardPass::Execute(ICommandEncoder* encoder, const RenderGraphResources& resources)
{
    if (!pipeline || !scene || scene->GetMeshCount() == 0)
        return;

    auto* colorTex = resources.getTexture("color");
    auto* depthTex = resources.getTexture("depth");
    if (!colorTex || !depthTex)
        return;

    setRenderTarget(0, colorTex);
    setDepthStencil(depthTex);
    auto* pass = beginRenderPass(encoder);

    auto* shaderObj = pass->bindPipeline(pipeline);
    if (!shaderObj)
    {
        spdlog::error("ForwardPass: bindPipeline returned null");
        pass->end();
        return;
    }

    ShaderVar vars(shaderObj);

    if (camera)
        camera->Bind(vars);

    scene->Bind(vars);
    scene->Rasterize(pass, vars, colorTex);

    pass->end();
}
