#pragma once

#include <slang-rhi.h>
#include <slang-com-ptr.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Program
{
public:
    Program(rhi::IDevice* device, const std::string& moduleName);

    [[nodiscard]] const char* GetName() const { return moduleName.c_str(); }
    [[nodiscard]] rhi::IShaderProgram* GetShaderProgram() const { return shaderProgram; }
    [[nodiscard]] const rhi::InputLayoutDesc& GetInputLayoutDesc() const { return inputLayoutDesc; }
    [[nodiscard]] uint32_t GetRenderTargetCount() const { return renderTargetCount; }
    [[nodiscard]] glm::uvec3 GetThreadGroupSize() const { return threadGroupSize; }

private:
    void Reflect(slang::IModule* module, slang::IComponentType** entryPoints, size_t entryPointCount);
    void ReflectVertexInputLayout(slang::EntryPointReflection* entry);
    void ReflectRenderTargetCount(slang::EntryPointReflection* entry);
    void ReflectThreadGroupSize(slang::EntryPointReflection* entry);

    static rhi::Format GetVertexFormat(slang::TypeReflection::ScalarType scalarType, unsigned columnCount);

    static void PrintDiagnostics(const Slang::ComPtr<slang::IBlob>& diagnostics);

    std::string moduleName;
    Slang::ComPtr<rhi::IShaderProgram> shaderProgram;
    Slang::ComPtr<rhi::IDevice> device;

    std::vector<rhi::InputElementDesc> inputElements;
    rhi::VertexStreamDesc vertexStream = {};
    rhi::InputLayoutDesc inputLayoutDesc = {};
    uint32_t renderTargetCount = 0;
    glm::uvec3 threadGroupSize = {};
};
