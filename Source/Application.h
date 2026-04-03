#pragma once

#include "Renderer/Renderer.h"
#include "Core/PipelineCache.h"
#include <slang-rhi.h>

#if SLANG_WINDOWS_FAMILY
#define GLFW_EXPOSE_NATIVE_WIN32
#elif SLANG_LINUX_FAMILY
#define GLFW_EXPOSE_NATIVE_X11
#elif SLANG_APPLE_FAMILY
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "slang-rhi/glfw.h"

#include <vector>

#include <format>
#include <spdlog/spdlog.h>

class DebugCallback : public rhi::IDebugCallback
{
public:
    SLANG_NO_THROW void SLANG_MCALL
    handleMessage(const rhi::DebugMessageType type, const rhi::DebugMessageSource source, const char* message) override
    {
        std::string_view msg(message);
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' '))
            msg.remove_suffix(1);

        if (const auto pos = msg.find(" - "); msg.starts_with("message: ") && pos != std::string_view::npos)
            msg = msg.substr(pos + 3);

        auto sourceStr = "";
        switch (source)
        {
        case rhi::DebugMessageSource::Layer:
            sourceStr = "Layer";
            break;
        case rhi::DebugMessageSource::Driver:
            sourceStr = "Driver";
            break;
        case rhi::DebugMessageSource::Slang:
            sourceStr = "Slang";
            break;
        }

        switch (type)
        {
        case rhi::DebugMessageType::Info:
            if (source == rhi::DebugMessageSource::Driver)
                spdlog::trace("RHI ({}) {}", sourceStr, msg);
            else
                spdlog::info("RHI ({}) {}", sourceStr, msg);
            break;
        case rhi::DebugMessageType::Warning:
            spdlog::warn("RHI ({}) {}", sourceStr, msg);
            break;
        case rhi::DebugMessageType::Error:
            spdlog::error("RHI ({}) {}", sourceStr, msg);
            break;
        }
    }
};

class Application
{
public:
    Application();
    ~Application();

    void Run();

private:
    rhi::Result InitWindow(const char* title, const uint32_t width, const uint32_t height)
    {
        const auto& deviceInfo = device->getInfo();
        const auto fullTitle =
            std::format("{} | {} ({})", title, rhi::getRHI()->getDeviceTypeName(deviceInfo.deviceType), deviceInfo.adapterName);

        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), fullTitle.c_str(), nullptr, nullptr);
        if (!window)
        {
            spdlog::error("Failed to create GLFW window");
            return SLANG_FAIL;
        }

        return SLANG_OK;
    }

    rhi::Result InitDevice(
        const rhi::DeviceType deviceType,
        const std::vector<rhi::Feature>& requiredFeatures,
        const std::vector<std::pair<std::string, std::string>>& preprocessorMacros
    )
    {
        rhi::DeviceDesc deviceDesc = {};
        deviceDesc.deviceType = deviceType;
#if !NDEBUG
        rhi::DebugLayerOptions debugLayerOptions;
        debugLayerOptions.coreValidation = true;
        rhi::getRHI()->setDebugLayerOptions(debugLayerOptions);
        deviceDesc.enableValidation = true;
        deviceDesc.debugCallback = &debugCallback;
#endif
        const char* searchPaths[] = {SHADER_DIR};
        deviceDesc.slang.searchPaths = searchPaths;
        deviceDesc.slang.searchPathCount = SLANG_COUNT_OF(searchPaths);

        deviceDesc.persistentPipelineCache = &pipelineCache;
        deviceDesc.persistentShaderCache = &shaderCache;

        std::vector<slang::PreprocessorMacroDesc> preprocessorMacrosDescs;
        for (const auto& [name, value] : preprocessorMacros)
        {
            slang::PreprocessorMacroDesc desc{};
            desc.name = name.c_str();
            desc.value = value.c_str();
            preprocessorMacrosDescs.push_back(desc);
        }
        deviceDesc.slang.preprocessorMacros = preprocessorMacrosDescs.data();
        deviceDesc.slang.preprocessorMacroCount = preprocessorMacrosDescs.size();

        if (SLANG_FAILED(rhi::getRHI()->createDevice(deviceDesc, device.writeRef())))
        {
            spdlog::error("Failed to create device");
            return SLANG_FAIL;
        }

        for (const auto& feature : requiredFeatures)
        {
            if (!device->hasFeature(feature))
            {
                spdlog::error("Missing required feature: {}", rhi::getRHI()->getFeatureName(feature));
                return SLANG_E_NOT_AVAILABLE;
            }
        }

        return SLANG_OK;
    }

    rhi::Result InitSurface(const rhi::Format format)
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        if (SLANG_FAILED(device->createSurface(rhi::getWindowHandleFromGLFW(window), surface.writeRef())))
        {
            spdlog::error("Failed to create surface");
            return SLANG_FAIL;
        }
        rhi::SurfaceConfig surfaceConfig;
        surfaceConfig.width = width;
        surfaceConfig.height = height;
        surfaceConfig.format = format;
        if (SLANG_FAILED(surface->configure(surfaceConfig)))
        {
            spdlog::error("Failed to configure surface");
            return SLANG_FAIL;
        }

        return SLANG_OK;
    }

    GLFWwindow* window = nullptr;
    DebugCallback debugCallback;
    PipelineCache pipelineCache{"cache/pipeline"};
    PipelineCache shaderCache{"cache/shader"};
    Slang::ComPtr<rhi::IDevice> device;
    Slang::ComPtr<rhi::ISurface> surface;

    std::unique_ptr<Renderer> renderer;

    double lastTime = 0.0;
};
