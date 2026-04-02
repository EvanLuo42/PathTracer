#pragma once

#include <slang-rhi.h>
#include <slang-com-ptr.h>

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Program
{
public:
    struct Desc
    {
        Desc& AddShaderModule(const std::string& moduleName)
        {
            this->moduleName = moduleName;
            return *this;
        }

        Desc& VSEntry(const std::string& name)
        {
            entryPoints.emplace_back(name, "vertex");
            return *this;
        }

        Desc& FSEntry(const std::string& name)
        {
            entryPoints.emplace_back(name, "fragment");
            return *this;
        }

        Desc& CSEntry(const std::string& name)
        {
            entryPoints.emplace_back(name, "compute");
            return *this;
        }

        Desc& AddDefine(const std::string& name, const std::string& value = "1")
        {
            defines.emplace_back(name, value);
            return *this;
        }

        std::string moduleName;
        std::vector<std::pair<std::string, std::string>> entryPoints;
        std::vector<std::pair<std::string, std::string>> defines;
    };

    static std::shared_ptr<Program> Create(rhi::IDevice* device, const Desc& desc)
    {
        auto program = std::shared_ptr<Program>(new Program(device, desc));
        if (!program->shaderProgram)
            return nullptr;
        return program;
    }

    rhi::IShaderProgram* GetShaderProgram() const { return shaderProgram; }
    rhi::IDevice* GetDevice() const { return device; }
    const Desc& GetDesc() const { return desc; }

private:
    Program(rhi::IDevice* device, const Desc& desc)
        : device(device), desc(desc)
    {
        Slang::ComPtr<slang::IBlob> diagnostics;
        auto session = device->getSlangSession();

        slang::IModule* module = session->loadModule(desc.moduleName.c_str(), diagnostics.writeRef());
        PrintDiagnostics(diagnostics);
        if (!module)
        {
            std::cerr << "[Program] Failed to load module: " << desc.moduleName << std::endl;
            return;
        }

        // Collect components: module + entry points
        std::vector<slang::IComponentType*> components;
        components.push_back(module);

        std::vector<slang::IEntryPoint*> entryPointPtrs;
        for (const auto& [name, stage] : desc.entryPoints)
        {
            slang::IEntryPoint* ep = nullptr;
            if (SLANG_FAILED(module->findEntryPointByName(name.c_str(), &ep)) || !ep)
            {
                std::cerr << "[Program] Entry point not found: " << name << std::endl;
                return;
            }
            entryPointPtrs.push_back(ep);
            components.push_back(ep);
        }

        // Compose and link all components together
        Slang::ComPtr<slang::IComponentType> composedProgram;
        if (SLANG_FAILED(session->createCompositeComponentType(
            components.data(),
            components.size(),
            composedProgram.writeRef(),
            diagnostics.writeRef())))
        {
            PrintDiagnostics(diagnostics);
            std::cerr << "[Program] Failed to compose program: " << desc.moduleName << std::endl;
            return;
        }

        Slang::ComPtr<slang::IComponentType> linkedProgram;
        if (SLANG_FAILED(composedProgram->link(linkedProgram.writeRef(), diagnostics.writeRef())))
        {
            PrintDiagnostics(diagnostics);
            std::cerr << "[Program] Failed to link program: " << desc.moduleName << std::endl;
            return;
        }

        rhi::ShaderProgramDesc programDesc = {};
        programDesc.slangGlobalScope = linkedProgram;
        programDesc.slangEntryPoints = nullptr;
        programDesc.slangEntryPointCount = 0;

        device->createShaderProgram(programDesc, shaderProgram.writeRef(), diagnostics.writeRef());
        PrintDiagnostics(diagnostics);
        if (!shaderProgram)
            std::cerr << "[Program] Failed to create shader program: " << desc.moduleName << std::endl;
    }

    static void PrintDiagnostics(const Slang::ComPtr<slang::IBlob>& diagnostics)
    {
        if (diagnostics && diagnostics->getBufferSize() > 0)
            std::cerr << static_cast<const char*>(diagnostics->getBufferPointer()) << std::endl;
    }

    rhi::IDevice* device;
    Desc desc;
    Slang::ComPtr<rhi::IShaderProgram> shaderProgram;
};
