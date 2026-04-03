#pragma once

#include "Camera.h"
#include "Gui.h"
#include "Scene/Scene.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderPasses/Forward/ForwardPass.h"
#include "RenderPasses/GBufferRaster/GBufferRasterPass.h"

#include <slang-rhi.h>

#include <slang-com-ptr.h>
#include <GLFW/glfw3.h>
#if SLANG_WINDOWS_FAMILY
#define GLFW_EXPOSE_NATIVE_WIN32
#elif SLANG_LINUX_FAMILY
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

#include <filesystem>
#include <memory>

class Renderer
{
public:
    explicit Renderer(GLFWwindow* window, rhi::IDevice* device, rhi::ISurface* surface);
    ~Renderer();

    void LoadScene(const std::filesystem::path& path);
    void LoadEnvMap(const std::filesystem::path& path);

    void OnRender();
    void OnRenderUI();
    void OnUpdate(double deltaTime);
    void OnResize(uint32_t width, uint32_t height);
    void OnScroll(const double yOffset) { camera.OnScroll(yOffset); }

private:
    void BuildGraph();
    bool BeginFrame();
    void EndFrame();

    GLFWwindow* window;
    Slang::ComPtr<rhi::IDevice> device;
    Slang::ComPtr<rhi::ISurface> surface;
    Slang::ComPtr<rhi::ICommandQueue> queue;
    Slang::ComPtr<rhi::ICommandEncoder> encoder;

    // Frame resources
    Slang::ComPtr<rhi::ITexture> backBuffer;

    // Camera
    Camera camera;

    // Environment map
    Slang::ComPtr<rhi::ITexture> envMap;
    Slang::ComPtr<rhi::ISampler> envSampler;

    // Subsystems
    std::unique_ptr<Gui> gui;
    std::unique_ptr<RenderGraph> graph;
    std::shared_ptr<Scene> scene;

    bool showUI = true;
    bool vsync = true;

    enum DirtyFlags : uint32_t
    {
        DirtyNone = 0,
        DirtyScene = 1 << 0,
        DirtyVSync = 1 << 1,
        DirtyEnvMap = 1 << 2,
    };
    uint32_t dirtyFlags = DirtyNone;
    std::filesystem::path pendingScenePath;
    std::filesystem::path pendingEnvMapPath;
};
