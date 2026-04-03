#include "Renderer.h"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <cmath>
#include <vector>

#if SLANG_WINDOWS_FAMILY
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

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

    graph->ImportTexture("backBuffer", nullptr);
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

    auto colorFormat = surface->getConfig()->format;

    auto* vbuf = graph->AddPass<VBufferRTPass>("VBufferRT", device.get(), scene, &camera);
    pathTracer = graph->AddPass<PathTracerPass>("PathTracer", device.get(), colorFormat, scene, &camera);
    if (envMap && envSampler)
        pathTracer->SetEnvMap(envMap, envSampler, envImportanceCdf, envMapWidth, envMapHeight);

    graph->AddEdge(vbuf->vbuffer, pathTracer->vbufferIn);
    graph->MarkOutput(pathTracer->output);

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    graph->Compile(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
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
    if (!envMap)
    {
        stbi_image_free(pixels);
        spdlog::error("Failed to create environment map texture");
        return;
    }

    std::vector<float> importanceCdf(static_cast<size_t>(width) * static_cast<size_t>(height));
    double totalWeight = 0.0;
    for (int y = 0; y < height; y++)
    {
        const float theta = (static_cast<float>(y) + 0.5f) * kPi / static_cast<float>(height);
        const float sinTheta = (std::max)(std::sin(theta), 1e-4f);

        for (int x = 0; x < width; x++)
        {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const float* texel = pixels + index * 4;
            const float luminance =
                0.2126f * (std::max)(texel[0], 0.0f) +
                0.7152f * (std::max)(texel[1], 0.0f) +
                0.0722f * (std::max)(texel[2], 0.0f);
            totalWeight += (std::max)(luminance, 1e-4f) * sinTheta;
            importanceCdf[index] = static_cast<float>(totalWeight);
        }
    }

    if (totalWeight > 0.0)
    {
        const float invTotalWeight = static_cast<float>(1.0 / totalWeight);
        for (float& value : importanceCdf)
            value *= invTotalWeight;

        rhi::BufferDesc cdfDesc = {};
        cdfDesc.size = importanceCdf.size() * sizeof(float);
        cdfDesc.elementSize = sizeof(float);
        cdfDesc.usage = rhi::BufferUsage::ShaderResource;
        cdfDesc.defaultState = rhi::ResourceState::ShaderResource;
        envImportanceCdf = device->createBuffer(cdfDesc, importanceCdf.data());
        if (!envImportanceCdf)
            spdlog::warn("Failed to create environment importance sampling buffer");
    }
    else
    {
        envImportanceCdf.setNull();
    }

    envMapWidth = static_cast<uint32_t>(width);
    envMapHeight = static_cast<uint32_t>(height);
    stbi_image_free(pixels);

    if (!envSampler)
    {
        rhi::SamplerDesc samplerDesc = {};
        samplerDesc.minFilter = rhi::TextureFilteringMode::Linear;
        samplerDesc.magFilter = rhi::TextureFilteringMode::Linear;
        samplerDesc.addressU = rhi::TextureAddressingMode::Wrap;
        samplerDesc.addressV = rhi::TextureAddressingMode::ClampToEdge;
        envSampler = device->createSampler(samplerDesc);
    }

    if (pathTracer)
        pathTracer->SetEnvMap(envMap, envSampler, envImportanceCdf, envMapWidth, envMapHeight);

    spdlog::info("Loaded environment map: {} ({}x{})", path.filename().string(), width, height);
}

void Renderer::OnRenderUI()
{
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

    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 360, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings");

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

    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (envMap)
            ImGui::Text("Environment map loaded");
        else
            ImGui::TextDisabled("No environment map");
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        camera.OnRenderUI();
    }

    ImGui::Separator();

    graph->OnRenderUI();

    ImGui::End();

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
    graph->Resize(width, height);

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

    graph->Execute(encoder);

    if (pathTracer)
    {
        auto* ptOutput = graph->GetTexture(pathTracer->output);
        if (ptOutput)
        {
            auto desc = ptOutput->getDesc();
            rhi::SubresourceRange subresource = {};
            subresource.layerCount = 1;
            subresource.mipCount = 1;
            rhi::Extent3D extent = {desc.size.width, desc.size.height, 1};
            encoder->copyTexture(backBuffer, subresource, {}, ptOutput, subresource, {}, extent);
        }
    }

    gui->BeginFrame();
    OnRenderUI();
    gui->Render(encoder, backBuffer);

    EndFrame();
}
