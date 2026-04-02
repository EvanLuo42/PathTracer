#include "Application.h"

Application::Application()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    const std::vector deviceTypes = {rhi::DeviceType::Vulkan, rhi::DeviceType::D3D12};

    const std::vector requiredFeatures = {
        rhi::Feature::Surface,
        rhi::Feature::RayTracing,
        rhi::Feature::RayQuery,
        rhi::Feature::AccelerationStructure,
        rhi::Feature::ParameterBlock,
        rhi::Feature::Bindless,
        rhi::Feature::PipelineCache,
        rhi::Feature::Rasterization,
    };

    for (const auto deviceType : deviceTypes)
    {
        const auto* deviceTypeName = rhi::getRHI()->getDeviceTypeName(deviceType);

        if (!rhi::getRHI()->isDeviceTypeSupported(deviceType))
        {
            std::cerr << "[App] " << deviceTypeName << " is not supported, skipping" << std::endl;
            continue;
        }

        std::vector<std::pair<std::string, std::string>> macros;

        if (SLANG_FAILED(InitDevice(deviceType, requiredFeatures, macros)))
        {
            std::cerr << "[App] Failed to create " << deviceTypeName << " device, skipping" << std::endl;
            continue;
        }

        if (device->hasFeature(rhi::Feature::WaveOps))
            macros.emplace_back("HAS_WAVE_OPS", "1");
        if (device->hasFeature(rhi::Feature::AtomicFloat))
            macros.emplace_back("HAS_ATOMIC_FLOAT", "1");

        if (!macros.empty())
        {
            device.setNull();
            if (SLANG_FAILED(InitDevice(deviceType, requiredFeatures, macros)))
            {
                std::cerr << "[App] Failed to recreate " << deviceTypeName << " device with macros, skipping" << std::endl;
                continue;
            }
        }

        if (SLANG_FAILED(InitWindow("Path Tracer", 1280, 720)))
        {
            std::cerr << "[App] Failed to create window for " << deviceTypeName << ", skipping" << std::endl;
            continue;
        }

        if (SLANG_FAILED(InitSurface(rhi::Format::Undefined)))
        {
            std::cerr << "[App] Failed to create surface for " << deviceTypeName << ", skipping" << std::endl;
            continue;
        }

        std::cerr << "[App] Initialized with " << deviceTypeName << std::endl;
        renderer = std::make_unique<Renderer>(window, device, surface);
        renderer->LoadScene(MEDIA_DIR "/FlightHelmet/FlightHelmet.gltf");

        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, const int width, const int height)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
            if (app->renderer)
                app->renderer->OnResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        });

        glfwSetScrollCallback(window, [](GLFWwindow* w, double, double yOffset)
        {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
            if (app->renderer)
                app->renderer->OnScroll(yOffset);
        });

        break;
    }
}

Application::~Application()
{
    if (window)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
    device.setNull();
}

void Application::Run()
{
    if (!window || !renderer)
        return;

    while (true)
    {
        if (glfwWindowShouldClose(window))
        {
            break;
        }

        glfwPollEvents();

        const double time = glfwGetTime();
        const double deltaTime = time - lastTime;
        lastTime = time;

        renderer->OnUpdate(deltaTime);
        renderer->OnRender();
    }
}