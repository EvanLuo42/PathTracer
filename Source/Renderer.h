#pragma once

#include "Resources.h"
#include "Camera.h"
#include "Scene.h"
#include "RenderPasses/IRenderPass.h"
#include "RenderPasses/ImGuiPass.h"

#include <slang-rhi.h>

#include <slang-com-ptr.h>
#include <GLFW/glfw3.h>
#if SLANG_WINDOWS_FAMILY
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <filesystem>
#include <memory>
#include <vector>

class Renderer
{
public:
    explicit Renderer(GLFWwindow* window, Slang::ComPtr<rhi::IDevice> device, Slang::ComPtr<rhi::ISurface> surface);
    ~Renderer();

    void LoadScene(const std::filesystem::path& path);
    void AddRenderPass(std::unique_ptr<IRenderPass> pass);

    void OnRender();
    void OnRenderUI();
    void OnUpdate(double deltaTime);
    void OnResize(uint32_t width, uint32_t height) const;
    void OnScroll(double yOffset) { camera.OnScroll(yOffset); }

private:
    bool BeginFrame();
    void EndFrame();

    GLFWwindow* window;
    Slang::ComPtr<rhi::IDevice> device;
    Slang::ComPtr<rhi::ISurface> surface;
    Slang::ComPtr<rhi::ICommandQueue> queue;
    Slang::ComPtr<rhi::ICommandEncoder> encoder;

    std::unique_ptr<Resources> resources;
    Camera camera;
    std::shared_ptr<Scene> scene;
    std::unique_ptr<ImGuiPass> imguiPass;
    std::vector<std::unique_ptr<IRenderPass>> renderPasses;
    bool showUI = true;
    bool vsync = true;
    bool pendingVSyncChange = false;
    std::filesystem::path pendingScenePath;
};
