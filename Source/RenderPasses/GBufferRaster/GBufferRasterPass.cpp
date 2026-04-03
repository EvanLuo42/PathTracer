#include "GBufferRasterPass.h"

#include <spdlog/spdlog.h>

using namespace rhi;

GBufferRasterPass::GBufferRasterPass(IDevice* device, std::shared_ptr<Scene> scene, Camera* camera)
    : device(device), scene(std::move(scene)), camera(camera)
{
    CreatePipeline();
}

void GBufferRasterPass::CreatePipeline()
{
    program = std::make_unique<Program>(device, "RenderPasses/GBufferRaster/GBufferRaster");
    if (!program->GetShaderProgram())
        return;

    const auto& layoutDesc = program->GetInputLayoutDesc();
    device->createInputLayout(layoutDesc, inputLayout.writeRef());

    ColorTargetDesc colorTargets[] = {
        {Format::RGBA8Unorm},   // baseColor
        {Format::RG16Float},    // normal (octahedral)
        {Format::RGBA8Unorm},   // materialData
    };

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
    pipelineDesc.targets = colorTargets;
    pipelineDesc.targetCount = 3;
    pipelineDesc.depthStencil = depthStencil;
    pipelineDesc.rasterizer = rasterizer;
    pipelineDesc.label = "GBufferRaster";

    auto result = device->createRenderPipeline(pipelineDesc, pipeline.writeRef());
    if (SLANG_FAILED(result))
        spdlog::error("Failed to create GBufferRaster pipeline: {}", static_cast<int>(result));
}

void GBufferRasterPass::Setup()
{
    baseColor = addOutput("baseColor", Format::RGBA8Unorm, PassSlot::Access::RenderTarget,
                          SizePolicy::BackBuffer(), LoadOp::Clear, 0);
    normal = addOutput("normal", Format::RG16Float, PassSlot::Access::RenderTarget,
                       SizePolicy::BackBuffer(), LoadOp::Clear, 1);
    materialData = addOutput("materialData", Format::RGBA8Unorm, PassSlot::Access::RenderTarget,
                             SizePolicy::BackBuffer(), LoadOp::Clear, 2);
    depth = addOutput("depth", Format::D32Float, PassSlot::Access::DepthStencil,
                      SizePolicy::BackBuffer(), LoadOp::Clear, 0);
    markSideEffect();
}

void GBufferRasterPass::Execute(ICommandEncoder* encoder, const RenderGraphResources& resources)
{
    if (!pipeline || !scene || scene->GetMeshCount() == 0)
        return;

    auto* baseTex = resources.getTexture("baseColor");
    auto* normTex = resources.getTexture("normal");
    auto* matTex = resources.getTexture("materialData");
    auto* depthTex = resources.getTexture("depth");
    if (!baseTex || !normTex || !matTex || !depthTex)
        return;

    setRenderTarget(0, baseTex);
    setRenderTarget(1, normTex);
    setRenderTarget(2, matTex);
    setDepthStencil(depthTex);
    auto* pass = beginRenderPass(encoder);

    auto* shaderObj = pass->bindPipeline(pipeline);
    if (!shaderObj)
    {
        spdlog::error("GBufferRasterPass: bindPipeline returned null");
        pass->end();
        return;
    }

    ShaderVar vars(shaderObj);

    if (camera)
        camera->Bind(vars);

    scene->Bind(vars);
    scene->Rasterize(pass, vars, baseTex);

    pass->end();
}
