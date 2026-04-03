cmake_minimum_required(VERSION 4.1)
project(PathTracer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(External)

add_executable(PathTracer
        Source/main.cpp
        Source/Renderer/Renderer.cpp
        Source/Renderer/Renderer.h
        Source/Application.cpp
        Source/Application.h
        Source/Scene/Scene.h
        Source/Scene/Scene.cpp
        Source/Core/PipelineCache.h
        Source/Core/Program.cpp
        Source/Core/Program.h
        Source/Renderer/Gui.cpp
        Source/Renderer/Gui.h
        Source/RenderGraph/RenderGraph.cpp
        Source/RenderGraph/RenderGraph.h
        Source/RenderGraph/RenderPass.h
        Source/RenderGraph/ResourceDesc.h
        Source/RenderPasses/Forward/ForwardPass.cpp
        Source/RenderPasses/Forward/ForwardPass.h
        Source/RenderPasses/VBufferRaster/VBufferRasterPass.cpp
        Source/RenderPasses/VBufferRaster/VBufferRasterPass.h
        Source/RenderPasses/VBufferRT/VBufferRTPass.cpp
        Source/RenderPasses/VBufferRT/VBufferRTPass.h
        Source/RenderPasses/GBufferRaster/GBufferRasterPass.cpp
        Source/RenderPasses/GBufferRaster/GBufferRasterPass.h
        Source/RenderPasses/GBufferRT/GBufferRTPass.cpp
        Source/RenderPasses/GBufferRT/GBufferRTPass.h
)

target_include_directories(PathTracer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(PathTracer PRIVATE
        SHADER_DIR="${CMAKE_SOURCE_DIR}/Source"
        MEDIA_DIR="${CMAKE_SOURCE_DIR}/media"
)

target_link_libraries(PathTracer
        PUBLIC
        slang-rhi
        PRIVATE
        glfw
        glm
        imgui
        spdlog
        tinygltf
)