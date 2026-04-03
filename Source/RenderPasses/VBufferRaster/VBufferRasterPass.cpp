#include "VBufferRasterPass.h"

#include <spdlog/spdlog.h>

using namespace rhi;

VBufferRasterPass::VBufferRasterPass(IDevice* device, std::shared_ptr<Scene> scene, Camera* camera)
    : device(device), scene(std::move(scene)), camera(camera)
{
    CreatePipeline();
}

void VBufferRasterPass::CreatePipeline()
{
    program = std::make_unique<Program>(device, "RenderPasses/VBufferRaster/VBufferRaster");
    if (!program->GetShaderProgram())
        return;

    const auto& layoutDesc = program->GetInputLayoutDesc();
    device->createInputLayout(layoutDesc, inputLayout.writeRef());

    ColorTargetDesc colorTarget = {};
    colorTarget.format = Format::RG32Uint;

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
    pipelineDesc.label = "VBufferRaster";

    auto result = device->createRenderPipeline(pipelineDesc, pipeline.writeRef());
    if (SLANG_FAILED(result))
        spdlog::error("Failed to create VBufferRaster pipeline: {}", static_cast<int>(result));
}

void VBufferRasterPass::Setup()
{
    vbuffer = AddOutput("vbuffer", Format::RG32Uint, PassSlot::Access::RenderTarget,
                        SizePolicy::BackBuffer(), LoadOp::Clear, 0);
    depth = AddOutput("depth", Format::D32Float, PassSlot::Access::DepthStencil,
                      SizePolicy::BackBuffer(), LoadOp::Clear, 0);
    MarkSideEffect();
}

void VBufferRasterPass::Execute(ICommandEncoder* encoder, const RenderGraphResources& resources)
{
    if (!pipeline || !scene || scene->GetMeshCount() == 0)
        return;

    auto* vbufTex = resources.GetTexture("vbuffer");
    auto* depthTex = resources.GetTexture("depth");
    if (!vbufTex || !depthTex)
        return;

    SetRenderTarget(0, vbufTex);
    SetDepthStencil(depthTex);
    auto* pass = BeginRenderPass(encoder);

    auto* shaderObj = pass->bindPipeline(pipeline);
    if (!shaderObj)
    {
        spdlog::error("VBufferRasterPass: bindPipeline returned null");
        pass->end();
        return;
    }

    ShaderVar vars(shaderObj);

    if (camera)
        camera->Bind(vars);

    scene->Bind(vars);

    const auto& meshInfos = scene->GetMeshInfos();
    auto texDesc = vbufTex->getDesc();

    RenderState state = {};
    state.viewports[0] = Viewport::fromSize(
        static_cast<float>(texDesc.size.width), static_cast<float>(texDesc.size.height));
    state.viewportCount = 1;
    state.scissorRects[0] = {0, 0, texDesc.size.width, texDesc.size.height};
    state.scissorRectCount = 1;
    state.vertexBuffers[0] = {scene->GetVertexBuffer(), 0};
    state.vertexBufferCount = 1;
    state.indexBuffer = {scene->GetIndexBuffer(), 0};
    state.indexFormat = IndexFormat::Uint32;

    for (uint32_t i = 0; i < scene->GetMeshCount(); i++)
    {
        const auto& mesh = meshInfos[i];

        vars["gDraw"]["modelMatrix"] = glm::transpose(glm::mat4(1.0f));
        vars["gDraw"]["materialIndex"] = mesh.materialIndex;
        vars["gMeshIndex"] = i;

        pass->setRenderState(state);

        DrawArguments args = {};
        args.vertexCount = mesh.indexCount;
        args.startIndexLocation = mesh.indexOffset;
        args.startVertexLocation = mesh.vertexOffset;
        pass->drawIndexed(args);
    }

    pass->end();
}
