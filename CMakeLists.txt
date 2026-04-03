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
        Source/Renderer/Resources.h
        Source/Scene/Scene.h
        Source/Scene/Scene.cpp
        Source/RenderPasses/IRenderPass.h
        Source/Core/PipelineCache.h
        Source/Core/Program.cpp
        Source/Core/Program.h
        Source/Renderer/Gui.cpp
        Source/Renderer/Gui.h
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