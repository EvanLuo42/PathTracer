#pragma once

#include "IRenderPass.h"

#include "../ShaderVar.h"

#include <slang-rhi.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <GLFW/glfw3.h>

class ImGuiPass : public IRenderPass
{
public:
    ImGuiPass(rhi::IDevice* device, rhi::ISurface* surface, GLFWwindow* window);
    ~ImGuiPass() override;

    void BeginFrame();
    void Execute(rhi::ICommandEncoder* encoder, Resources& resources) override;

private:
    void CreateFontTexture();
    void CreatePipeline();
    void UpdateBuffers(ImDrawData* drawData);

    rhi::IDevice* device;
    rhi::ISurface* surface;
    GLFWwindow* window;

    Slang::ComPtr<rhi::IRenderPipeline> pipeline;
    Slang::ComPtr<rhi::IInputLayout> inputLayout;
    Slang::ComPtr<rhi::ITexture> fontTexture;
    Slang::ComPtr<rhi::ISampler> fontSampler;
    Slang::ComPtr<rhi::IBuffer> vertexBuffer;
    Slang::ComPtr<rhi::IBuffer> indexBuffer;

    uint64_t vertexBufferSize = 0;
    uint64_t indexBufferSize = 0;
};
