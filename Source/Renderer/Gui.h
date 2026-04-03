#pragma once

#include <slang-rhi.h>
#include <slang-com-ptr.h>

struct ImDrawData;
struct GLFWwindow;

class Gui
{
public:
    Gui(rhi::IDevice* device, rhi::ISurface* surface, GLFWwindow* window);
    ~Gui();

    void BeginFrame();
    void Render(rhi::ICommandEncoder* encoder, rhi::ITexture* backBuffer);

private:
    void CreateFontTexture();
    void CreatePipeline();
    void UpdateBuffers(ImDrawData* drawData);

    Slang::ComPtr<rhi::IDevice> device;
    Slang::ComPtr<rhi::ISurface> surface;
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
