#include "BlitPass.h"
#include "../ShaderVar.h"

using namespace rhi;

BlitPass::BlitPass(IDevice* device, ISurface* surface)
    : device(device)
{
    auto program = Program::Create(device, Program::Desc()
        .AddShaderModule("Blit")
        .VSEntry("vertexMain")
        .FSEntry("fragmentMain")
    );
    if (!program)
        return;

    ColorTargetDesc colorTarget = {};
    colorTarget.format = surface->getInfo().preferredFormat;

    DepthStencilDesc depthStencil = {};
    depthStencil.depthTestEnable = false;
    depthStencil.depthWriteEnable = false;

    pipeline = RenderPipeline::Create(device, RenderPipeline::Desc()
        .SetProgram(program)
        .AddRenderTarget(colorTarget)
        .SetDepthStencil(depthStencil)
    );

    SamplerDesc samplerDesc = {};
    samplerDesc.minFilter = TextureFilteringMode::Linear;
    samplerDesc.magFilter = TextureFilteringMode::Linear;
    sampler = device->createSampler(samplerDesc);
}

void BlitPass::Execute(ICommandEncoder* encoder, Resources& resources)
{
    if (!pipeline || !resources.backBuffer)
        return;

    const auto& desc = resources.backBuffer->getDesc();

    RenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = resources.backBuffer->getDefaultView();
    colorAttachment.loadOp = LoadOp::DontCare;
    colorAttachment.storeOp = StoreOp::Store;

    RenderPassDesc renderPassDesc = {};
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.colorAttachmentCount = 1;

    auto passEncoder = encoder->beginRenderPass(renderPassDesc);
    ShaderVar vars(passEncoder->bindPipeline(pipeline));

    vars["gInput"] = resources.colorOutput;
    vars["gSampler"] = sampler;

    RenderState renderState = {};
    renderState.viewports[0] = Viewport::fromSize(static_cast<float>(desc.size.width), static_cast<float>(desc.size.height));
    renderState.viewportCount = 1;
    renderState.scissorRects[0] = ScissorRect::fromSize(desc.size.width, desc.size.height);
    renderState.scissorRectCount = 1;
    passEncoder->setRenderState(renderState);

    DrawArguments drawArgs = {};
    drawArgs.vertexCount = 3;
    passEncoder->draw(drawArgs);
    passEncoder->end();
}
