#include "Renderer.h"
#include "RenderPasses/VBufferPass.h"
#include "RenderPasses/BaseColorDebugPass.h"
#include "RenderPasses/BlitPass.h"

#include <imgui.h>
#include <iostream>

#if SLANG_WINDOWS_FAMILY
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

namespace
{
    std::filesystem::path OpenFileDialog(GLFWwindow* window)
    {
#if SLANG_WINDOWS_FAMILY
        char filename[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = glfwGetWin32Window(window);
        ofn.lpstrFilter = "glTF Files\0*.gltf;*.glb\0All Files\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn))
            return filename;
#endif
        return {};
    }
}

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

void Renderer::OnRenderUI()
{
    // Main menu bar
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Load Scene...", "Ctrl+O"))
            {
                auto path = OpenFileDialog(window);
                if (!path.empty())
                    pendingScenePath = path;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Show UI", nullptr, &showUI);
            if (ImGui::MenuItem("VSync", nullptr, &vsync))
                pendingVSyncChange = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (!showUI)
        return;

    // Settings panel
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 360, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings");

    // Scene info
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (scene)
        {
            ImGui::Text("Meshes: %u", scene->GetMeshCount());
            ImGui::Text("Triangles: %u", scene->GetTotalIndexCount() / 3);
        }
        else
        {
            ImGui::TextDisabled("No scene loaded");
        }
    }

    // Camera
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        camera.OnRenderUI();
    }

    ImGui::Separator();

    // Render passes
    for (const auto& pass : renderPasses)
    {
        if (ImGui::CollapsingHeader(pass->GetName()))
        {
            ImGui::PushID(pass->GetName());
            pass->OnRenderUI();
            ImGui::PopID();
        }
    }

    ImGui::End();

    // Floating FPS overlay (top-left, like Falcor)
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##fps", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove);
    ImGui::Text("%.1f FPS (%.3f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::End();
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
    if (!pendingScenePath.empty())
    {
        LoadScene(pendingScenePath);
        pendingScenePath.clear();
    }

    if (pendingVSyncChange)
    {
        pendingVSyncChange = false;
        queue->waitOnHost();
        auto config = *surface->getConfig();
        config.vsync = vsync;
        surface->configure(config);
    }

    if (!BeginFrame())
        return;

    for (const auto& pass : renderPasses)
        pass->Execute(encoder, *resources);

    OnRenderUI();
    imguiPass->Execute(encoder, *resources);

    EndFrame();
}
