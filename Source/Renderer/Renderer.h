#pragma once

#include "Resources.h"
#include "Camera.h"
#include "Gui.h"
#include "Scene/Scene.h"
#include "RenderPasses/IRenderPass.h"

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
#include <vector>

class Renderer
{
public:
    explicit Renderer(GLFWwindow* window, rhi::IDevice* device, rhi::ISurface* surface);
    ~Renderer();

    void LoadScene(const std::filesystem::path& path);
    void LoadEnvMap(const std::filesystem::path& path);
    void AddRenderPass(std::unique_ptr<IRenderPass> pass);

    void OnRender();
    void OnRenderUI();
    void OnUpdate(double deltaTime);
    void OnResize(uint32_t width, uint32_t height) const;
    void OnScroll(const double yOffset) { camera.OnScroll(yOffset); }

private:
    bool BeginFrame();
    void EndFrame();

    GLFWwindow* window;
    Slang::ComPtr<rhi::IDevice> device;
    Slang::ComPtr<rhi::ISurface> surface;
    Slang::ComPtr<rhi::ICommandQueue> queue;
    Slang::ComPtr<rhi::ICommandEncoder> encoder;

    std::unique_ptr<Resources> resources;
    std::unique_ptr<Gui> gui;
    Camera camera;
    std::shared_ptr<Scene> scene;
    std::vector<std::unique_ptr<IRenderPass>> renderPasses;
    bool showUI = true;
    bool vsync = true;

    enum DirtyFlags : uint32_t
    {
        DirtyNone   = 0,
        DirtyScene  = 1 << 0,
        DirtyVSync  = 1 << 1,
        DirtyEnvMap = 1 << 2,
    };
    uint32_t dirtyFlags = DirtyNone;
    std::filesystem::path pendingScenePath;
    std::filesystem::path pendingEnvMapPath;
};
