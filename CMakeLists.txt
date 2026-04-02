cmake_minimum_required(VERSION 4.1)
project(PathTracer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(External)

add_executable(PathTracer
        Source/main.cpp
        Source/Renderer.cpp
        Source/Renderer.h
        Source/Application.cpp
        Source/Application.h
        Source/Resources.h
        Source/Scene.h
        Source/Scene.cpp
        Source/RenderPasses/IRenderPass.h
        Source/RenderPasses/ImGuiPass.h
        Source/RenderPasses/ImGuiPass.cpp
        Source/RenderPasses/VBufferPass.h
        Source/RenderPasses/VBufferPass.cpp
        Source/RenderPasses/BaseColorDebugPass.h
        Source/RenderPasses/BaseColorDebugPass.cpp
        Source/RenderPasses/BlitPass.h
        Source/RenderPasses/BlitPass.cpp)

target_compile_definitions(PathTracer PRIVATE
        SHADER_DIR="${CMAKE_SOURCE_DIR}/Shaders"
        MEDIA_DIR="${CMAKE_SOURCE_DIR}/media"
)

target_link_libraries(PathTracer
        PUBLIC
        slang-rhi
        PRIVATE
        glfw
        glm
        imgui
        tinygltf
)