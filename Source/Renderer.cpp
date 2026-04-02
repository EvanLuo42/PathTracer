#include "Renderer.h"
#include "RenderPasses/VBufferPass.h"
#include "RenderPasses/BaseColorDebugPass.h"
#include "RenderPasses/BlitPass.h"

#include <iostream>

Renderer::Renderer(GLFWwindow* window, Slang::ComPtr<rhi::IDevice> device, Slang::ComPtr<rhi::ISurface> surface)
    : window(window), device(device), surface(std::move(surface))
{
    queue = device->getQueue(rhi::QueueType::Graphics);
    if (!queue)
    {
        std::cerr << "[Renderer] Failed to get graphics queue" << std::endl;
        return;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    resources = std::make_unique<Resources>(device, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    imguiPass = std::make_unique<ImGuiPass>(device, this->surface, window);
}

Renderer::~Renderer()
{
    if (queue)
        queue->waitOnHost();

    imguiPass.reset();
}

void Renderer::LoadScene(const std::filesystem::path& path)
{
    queue->waitOnHost();
    renderPasses.clear();

    scene = Scene::Create(device, queue, path);
    if (!scene)
    {
        std::cerr << "[Renderer] Failed to load scene: " << path << std::endl;
        return;
    }

    AddRenderPass(std::make_unique<VBufferPass>(device, scene));
    AddRenderPass(std::make_unique<BaseColorDebugPass>(device, scene));
    AddRenderPass(std::make_unique<BlitPass>(device, this->surface));
}

void Renderer::AddRenderPass(std::unique_ptr<IRenderPass> pass)
{
    renderPasses.push_back(std::move(pass));
}

void Renderer::OnUpdate(const double deltaTime)
{
    camera.OnUpdate(window, static_cast<float>(deltaTime));

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);

    resources->cameraData = camera.GetCameraData(aspect);
}

void Renderer::OnResize(const uint32_t width, const uint32_t height) const
{
    if (width == 0 || height == 0)
        return;

    queue->waitOnHost();
    resources->Resize(width, height);

    rhi::SurfaceConfig config = *surface->getConfig();
    config.width = width;
    config.height = height;
    surface->configure(config);
}

bool Renderer::BeginFrame()
{
    if (!surface->getConfig())
        return false;

    resources->backBuffer = surface->acquireNextImage();
    if (!resources->backBuffer)
        return false;

    encoder = queue->createCommandEncoder();
    if (!encoder)
        return false;

    imguiPass->BeginFrame();
    return true;
}

void Renderer::EndFrame()
{
    queue->submit(encoder->finish());
    encoder = nullptr;
    resources->backBuffer = nullptr;
    surface->present();
}

void Renderer::OnRender()
{
    if (!BeginFrame())
        return;

    for (const auto& pass : renderPasses)
        pass->Execute(encoder, *resources);

    // ImGui always renders last (on top of everything)
    ImGui::ShowDemoWindow();
    imguiPass->Execute(encoder, *resources);

    EndFrame();
}
