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
        {Format::RGBA8Unorm},
        {Format::RG16Float},
        {Format::RGBA8Unorm},
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
    baseColor = AddOutput("baseColor", Format::RGBA8Unorm, PassSlot::Access::RenderTarget,
                          SizePolicy::BackBuffer(), LoadOp::Clear, 0);
    normal = AddOutput("normal", Format::RG16Float, PassSlot::Access::RenderTarget,
                       SizePolicy::BackBuffer(), LoadOp::Clear, 1);
    materialData = AddOutput("materialData", Format::RGBA8Unorm, PassSlot::Access::RenderTarget,
                             SizePolicy::BackBuffer(), LoadOp::Clear, 2);
    depth = AddOutput("depth", Format::D32Float, PassSlot::Access::DepthStencil,
                      SizePolicy::BackBuffer(), LoadOp::Clear, 0);
    MarkSideEffect();
}

void GBufferRasterPass::Execute(ICommandEncoder* encoder, const RenderGraphResources& resources)
{
    if (!pipeline || !scene || scene->GetMeshCount() == 0)
        return;

    auto* baseTex = resources.GetTexture("baseColor");
    auto* normTex = resources.GetTexture("normal");
    auto* matTex = resources.GetTexture("materialData");
    auto* depthTex = resources.GetTexture("depth");
    if (!baseTex || !normTex || !matTex || !depthTex)
        return;

    SetRenderTarget(0, baseTex);
    SetRenderTarget(1, normTex);
    SetRenderTarget(2, matTex);
    SetDepthStencil(depthTex);
    auto* pass = BeginRenderPass(encoder);

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
