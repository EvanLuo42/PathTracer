#include "Gui.h"
#include "Core/Program.h"
#include "Core/ShaderVar.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <spdlog/spdlog.h>

using namespace rhi;

Gui::Gui(IDevice* device, ISurface* surface, GLFWwindow* window) : device(device), surface(surface), window(window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOther(window, true);

    CreateFontTexture();
    CreatePipeline();
}

Gui::~Gui()
{
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Gui::CreateFontTexture()
{
    unsigned char* pixels;
    int width, height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    TextureDesc desc = {};
    desc.type = TextureType::Texture2D;
    desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::ShaderResource;
    desc.defaultState = ResourceState::ShaderResource;

    SubresourceData initData = {};
    initData.data = pixels;
    initData.rowPitch = width * 4;

    fontTexture = device->createTexture(desc, &initData);
    if (!fontTexture)
    {
        spdlog::error("Failed to create font texture");
        return;
    }

    SamplerDesc samplerDesc = {};
    samplerDesc.minFilter = TextureFilteringMode::Linear;
    samplerDesc.magFilter = TextureFilteringMode::Linear;
    samplerDesc.addressU = TextureAddressingMode::ClampToEdge;
    samplerDesc.addressV = TextureAddressingMode::ClampToEdge;
    fontSampler = device->createSampler(samplerDesc);
}

void Gui::CreatePipeline()
{
    Program program(device, "Renderer/ImGui");
    if (!program.GetShaderProgram())
        return;

    // ImDrawVert layout must be specified manually because the shader declares
    // float4 color (RGBA32Float) while ImDrawVert::col is ImU32 (RGBA8Unorm).
    VertexStreamDesc vertexStream = {sizeof(ImDrawVert), InputSlotClass::PerVertex, 0};
    InputElementDesc inputElements[] = {
        {"POSITION", 0, Format::RG32Float, offsetof(ImDrawVert, pos), 0},
        {"TEXCOORD", 0, Format::RG32Float, offsetof(ImDrawVert, uv), 0},
        {"COLOR", 0, Format::RGBA8Unorm, offsetof(ImDrawVert, col), 0},
    };
    InputLayoutDesc inputLayoutDesc = {};
    inputLayoutDesc.inputElements = inputElements;
    inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
    inputLayoutDesc.vertexStreams = &vertexStream;
    inputLayoutDesc.vertexStreamCount = 1;
    device->createInputLayout(inputLayoutDesc, inputLayout.writeRef());

    ColorTargetDesc colorTarget = {};
    colorTarget.format = surface->getInfo().preferredFormat;
    colorTarget.enableBlend = true;
    colorTarget.color = {BlendFactor::SrcAlpha, BlendFactor::InvSrcAlpha, BlendOp::Add};
    colorTarget.alpha = {BlendFactor::One, BlendFactor::InvSrcAlpha, BlendOp::Add};

    DepthStencilDesc depthStencil = {};
    depthStencil.depthTestEnable = false;
    depthStencil.depthWriteEnable = false;

    RasterizerDesc rasterizer = {};
    rasterizer.cullMode = CullMode::None;

    RenderPipelineDesc pipelineDesc = {};
    pipelineDesc.program = program.GetShaderProgram();
    pipelineDesc.inputLayout = inputLayout;
    pipelineDesc.primitiveTopology = PrimitiveTopology::TriangleList;
    pipelineDesc.targets = &colorTarget;
    pipelineDesc.targetCount = 1;
    pipelineDesc.depthStencil = depthStencil;
    pipelineDesc.rasterizer = rasterizer;
    pipelineDesc.label = program.GetName();

    pipeline = device->createRenderPipeline(pipelineDesc);
    if (!pipeline)
        spdlog::error("Failed to create pipeline");
}

void Gui::UpdateBuffers(ImDrawData* drawData)
{
    const auto vtxSize = static_cast<uint64_t>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const auto idxSize = static_cast<uint64_t>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);

    if (vtxSize == 0 || idxSize == 0)
        return;

    if (vtxSize > vertexBufferSize)
    {
        BufferDesc desc = {};
        desc.size = vtxSize;
        desc.usage = BufferUsage::VertexBuffer;
        desc.defaultState = ResourceState::VertexBuffer;
        desc.memoryType = MemoryType::Upload;
        vertexBuffer = device->createBuffer(desc);
        vertexBufferSize = vtxSize;
    }

    if (idxSize > indexBufferSize)
    {
        BufferDesc desc = {};
        desc.size = idxSize;
        desc.usage = BufferUsage::IndexBuffer;
        desc.defaultState = ResourceState::IndexBuffer;
        desc.memoryType = MemoryType::Upload;
        indexBuffer = device->createBuffer(desc);
        indexBufferSize = idxSize;
    }

    ImDrawVert* vtxDst;
    device->mapBuffer(vertexBuffer, CpuAccessMode::Write, reinterpret_cast<void**>(&vtxDst));
    ImDrawIdx* idxDst;
    device->mapBuffer(indexBuffer, CpuAccessMode::Write, reinterpret_cast<void**>(&idxDst));

    for (int n = 0; n < drawData->CmdListsCount; n++)
    {
        const ImDrawList* cmdList = drawData->CmdLists[n];
        memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtxDst += cmdList->VtxBuffer.Size;
        idxDst += cmdList->IdxBuffer.Size;
    }

    device->unmapBuffer(vertexBuffer);
    device->unmapBuffer(indexBuffer);
}

void Gui::BeginFrame()
{
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Gui::Render(ICommandEncoder* encoder, ITexture* backBuffer)
{
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->TotalVtxCount == 0)
        return;

    UpdateBuffers(drawData);

    RenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = backBuffer->getDefaultView();
    colorAttachment.loadOp = LoadOp::Load;
    colorAttachment.storeOp = StoreOp::Store;

    RenderPassDesc renderPassDesc = {};
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.colorAttachmentCount = 1;

    IRenderPassEncoder* passEncoder = encoder->beginRenderPass(renderPassDesc);
    passEncoder->pushDebugGroup("ImGui", {1.0f, 0.8f, 0.2f});

    const ShaderVar vars(passEncoder->bindPipeline(pipeline));

    const auto scale = glm::vec2(2.0f / drawData->DisplaySize.x, -2.0f / drawData->DisplaySize.y);
    const auto translate = glm::vec2(-1.0f - drawData->DisplayPos.x * scale.x, 1.0f - drawData->DisplayPos.y * scale.y);
    vars["uniforms"]["scale"] = scale;
    vars["uniforms"]["translate"] = translate;
    vars["fontTexture"] = fontTexture;
    vars["fontSampler"] = fontSampler;

    const auto fbWidth = static_cast<uint32_t>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const auto fbHeight = static_cast<uint32_t>(drawData->DisplaySize.y * drawData->FramebufferScale.y);

    int globalVtxOffset = 0;
    int globalIdxOffset = 0;

    for (int n = 0; n < drawData->CmdListsCount; n++)
    {
        const ImDrawList* cmdList = drawData->CmdLists[n];
        for (int cmdIdx = 0; cmdIdx < cmdList->CmdBuffer.Size; cmdIdx++)
        {
            const ImDrawCmd& cmd = cmdList->CmdBuffer[cmdIdx];
            if (cmd.UserCallback)
            {
                cmd.UserCallback(cmdList, &cmd);
                continue;
            }

            const ImVec2 clipOff = drawData->DisplayPos;
            const ImVec2 clipScale = drawData->FramebufferScale;
            ImVec2 clipMin((cmd.ClipRect.x - clipOff.x) * clipScale.x, (cmd.ClipRect.y - clipOff.y) * clipScale.y);
            ImVec2 clipMax((cmd.ClipRect.z - clipOff.x) * clipScale.x, (cmd.ClipRect.w - clipOff.y) * clipScale.y);
            if (clipMin.x < 0.0f)
                clipMin.x = 0.0f;
            if (clipMin.y < 0.0f)
                clipMin.y = 0.0f;
            if (clipMax.x > static_cast<float>(fbWidth))
                clipMax.x = static_cast<float>(fbWidth);
            if (clipMax.y > static_cast<float>(fbHeight))
                clipMax.y = static_cast<float>(fbHeight);
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
                continue;

            RenderState renderState = {};
            renderState.viewports[0] = Viewport::fromSize(static_cast<float>(fbWidth), static_cast<float>(fbHeight));
            renderState.viewportCount = 1;
            renderState.scissorRects[0] = {
                static_cast<uint32_t>(clipMin.x),
                static_cast<uint32_t>(clipMin.y),
                static_cast<uint32_t>(clipMax.x),
                static_cast<uint32_t>(clipMax.y),
            };
            renderState.scissorRectCount = 1;
            renderState.vertexBuffers[0] = {vertexBuffer, 0};
            renderState.vertexBufferCount = 1;
            renderState.indexBuffer = {indexBuffer, 0};
            renderState.indexFormat = sizeof(ImDrawIdx) == 2 ? IndexFormat::Uint16 : IndexFormat::Uint32;
            passEncoder->setRenderState(renderState);

            DrawArguments drawArgs = {};
            drawArgs.vertexCount = cmd.ElemCount;
            drawArgs.startIndexLocation = cmd.IdxOffset + globalIdxOffset;
            drawArgs.startVertexLocation = cmd.VtxOffset + globalVtxOffset;
            passEncoder->drawIndexed(drawArgs);
        }
        globalVtxOffset += cmdList->VtxBuffer.Size;
        globalIdxOffset += cmdList->IdxBuffer.Size;
    }

    passEncoder->popDebugGroup();
    passEncoder->end();
}
