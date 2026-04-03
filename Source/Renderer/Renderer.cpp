#include "Renderer.h"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#if SLANG_WINDOWS_FAMILY
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

namespace
{
    std::filesystem::path OpenFileDialog(GLFWwindow* window, const char* filter)
    {
#if SLANG_WINDOWS_FAMILY
        char filename[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = glfwGetWin32Window(window);
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn))
            return filename;
#endif
        return {};
    }
}

Renderer::Renderer(GLFWwindow* window, rhi::IDevice* device, rhi::ISurface* surface)
    : window(window), device(device), surface(surface)
{
    queue = device->getQueue(rhi::QueueType::Graphics);
    if (!queue)
    {
        spdlog::error("Failed to get graphics queue");
        return;
    }

    gui = std::make_unique<Gui>(device, surface, window);
    graph = std::make_unique<RenderGraph>(device);

    // Import backBuffer placeholder — will be updated each frame
    graph->importTexture("backBuffer", nullptr);
}

Renderer::~Renderer()
{
    if (queue)
        queue->waitOnHost();
}

void Renderer::LoadScene(const std::filesystem::path& path)
{
    queue->waitOnHost();

    scene = Scene::Create(device, queue, path);
    if (!scene)
    {
        spdlog::error("Failed to load scene: {}", path.string());
        return;
    }

    BuildGraph();
}

void Renderer::BuildGraph()
{
    graph = std::make_unique<RenderGraph>(device);

    auto backBufferSlot = graph->importTexture("backBuffer", nullptr);
    auto colorFormat = surface->getConfig()->format;

    // GBuffer pass
    auto* gbuffer = graph->addPass<GBufferRasterPass>("GBufferRaster", device.get(), scene, &camera);

    // Forward pass — draws baseColor to backBuffer for now
    auto* forward = graph->addPass<ForwardPass>("Forward", device.get(), colorFormat, scene, &camera);

    graph->addEdge(backBufferSlot, forward->colorIn);
    graph->markOutput(forward->colorIn);

    // Mark a gbuffer output so it doesn't get culled
    graph->markOutput(gbuffer->baseColor);

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    graph->compile(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
}

void Renderer::LoadEnvMap(const std::filesystem::path& path)
{
    queue->waitOnHost();

    int width, height, channels;
    float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels)
    {
        spdlog::error("Failed to load environment map: {}", path.string());
        return;
    }

    rhi::TextureDesc desc = {};
    desc.type = rhi::TextureType::Texture2D;
    desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    desc.format = rhi::Format::RGBA32Float;
    desc.usage = rhi::TextureUsage::ShaderResource;
    desc.defaultState = rhi::ResourceState::ShaderResource;

    rhi::SubresourceData initData = {};
    initData.data = pixels;
    initData.rowPitch = width * 4 * sizeof(float);

    envMap = device->createTexture(desc, &initData);
    stbi_image_free(pixels);

    if (!envMap)
    {
        spdlog::error("Failed to create environment map texture");
        return;
    }

    if (!envSampler)
    {
        rhi::SamplerDesc samplerDesc = {};
        samplerDesc.minFilter = rhi::TextureFilteringMode::Linear;
        samplerDesc.magFilter = rhi::TextureFilteringMode::Linear;
        samplerDesc.addressU = rhi::TextureAddressingMode::Wrap;
        samplerDesc.addressV = rhi::TextureAddressingMode::ClampToEdge;
        envSampler = device->createSampler(samplerDesc);
    }

    spdlog::info("Loaded environment map: {} ({}x{})", path.filename().string(), width, height);
}

void Renderer::OnRenderUI()
{
    // Main Menu Bar
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Load Scene...", "Ctrl+O"))
            {
                if (const auto path = OpenFileDialog(window, "glTF Files\0*.gltf;*.glb\0All Files\0*.*\0"); !path.empty())
                {
                    pendingScenePath = path;
                    dirtyFlags |= DirtyScene;
                }
            }
            if (ImGui::MenuItem("Load Environment Map..."))
            {
                if (const auto path = OpenFileDialog(window, "HDR Images\0*.hdr\0All Files\0*.*\0"); !path.empty())
                {
                    pendingEnvMapPath = path;
                    dirtyFlags |= DirtyEnvMap;
                }
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
                dirtyFlags |= DirtyVSync;
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

    // Environment
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (envMap)
            ImGui::Text("Environment map loaded");
        else
            ImGui::TextDisabled("No environment map");
    }

    // Camera
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        camera.OnRenderUI();
    }

    ImGui::Separator();

    // Render passes
    graph->onRenderUI();

    ImGui::End();

    // Floating FPS overlay
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

    camera.Update(aspect);
}

void Renderer::OnResize(const uint32_t width, const uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    queue->waitOnHost();
    graph->resize(width, height);

    rhi::SurfaceConfig config = *surface->getConfig();
    config.width = width;
    config.height = height;
    surface->configure(config);
}

bool Renderer::BeginFrame()
{
    if (!surface->getConfig())
        return false;

    backBuffer = surface->acquireNextImage();
    if (!backBuffer)
        return false;

    encoder = queue->createCommandEncoder();
    if (!encoder)
        return false;

    return true;
}

void Renderer::EndFrame()
{
    queue->submit(encoder->finish());
    encoder = nullptr;
    backBuffer = nullptr;
    surface->present();
}

void Renderer::OnRender()
{
    if (dirtyFlags & DirtyScene)
    {
        LoadScene(pendingScenePath);
        pendingScenePath.clear();
    }

    if (dirtyFlags & DirtyEnvMap)
    {
        LoadEnvMap(pendingEnvMapPath);
        pendingEnvMapPath.clear();
    }

    if (dirtyFlags & DirtyVSync)
    {
        queue->waitOnHost();
        auto config = *surface->getConfig();
        config.vsync = vsync;
        surface->configure(config);
    }

    dirtyFlags = DirtyNone;

    if (!BeginFrame())
        return;

    // Update graph's back buffer import each frame
    graph->importTexture("backBuffer", backBuffer, rhi::ResourceState::Undefined);

    // Execute render graph
    graph->execute(encoder);

    // GUI (outside of graph)
    gui->BeginFrame();
    OnRenderUI();
    gui->Render(encoder, backBuffer);

    EndFrame();
}
