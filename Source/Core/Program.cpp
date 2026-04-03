#include "Program.h"

#include <spdlog/spdlog.h>
#include <vector>

void Program::PrintDiagnostics(const Slang::ComPtr<slang::IBlob>& diagnostics)
{
    if (diagnostics && diagnostics->getBufferSize() > 0)
        spdlog::warn("{}", static_cast<const char*>(diagnostics->getBufferPointer()));
}

rhi::Format Program::GetVertexFormat(const slang::TypeReflection::ScalarType scalarType, const unsigned columnCount)
{
    if (scalarType == slang::TypeReflection::ScalarType::Float32)
    {
        switch (columnCount)
        {
        case 1:
            return rhi::Format::R32Float;
        case 2:
            return rhi::Format::RG32Float;
        case 3:
            return rhi::Format::RGB32Float;
        case 4:
            return rhi::Format::RGBA32Float;
        default:
            break;
        }
    }
    else if (scalarType == slang::TypeReflection::ScalarType::UInt32)
    {
        switch (columnCount)
        {
        case 1:
            return rhi::Format::R32Uint;
        case 2:
            return rhi::Format::RG32Uint;
        case 3:
            return rhi::Format::RGB32Uint;
        case 4:
            return rhi::Format::RGBA32Uint;
        default:
            break;
        }
    }
    else if (scalarType == slang::TypeReflection::ScalarType::Int32)
    {
        switch (columnCount)
        {
        case 1:
            return rhi::Format::R32Sint;
        case 2:
            return rhi::Format::RG32Sint;
        case 3:
            return rhi::Format::RGB32Sint;
        case 4:
            return rhi::Format::RGBA32Sint;
        default:
            break;
        }
    }
    else if (scalarType == slang::TypeReflection::ScalarType::Float16)
    {
        switch (columnCount)
        {
        case 1:
            return rhi::Format::R16Float;
        case 2:
            return rhi::Format::RG16Float;
        case 4:
            return rhi::Format::RGBA16Float;
        default:
            break;
        }
    }
    else if (scalarType == slang::TypeReflection::ScalarType::UInt8)
    {
        switch (columnCount)
        {
        case 1:
            return rhi::Format::R8Uint;
        case 2:
            return rhi::Format::RG8Uint;
        case 4:
            return rhi::Format::RGBA8Uint;
        default:
            break;
        }
    }

    spdlog::error("Unsupported vertex attribute format (scalar={}, columns={})", static_cast<int>(scalarType), columnCount);
    return rhi::Format::Undefined;
}

void Program::Reflect(slang::IModule* module, slang::IComponentType** entryPoints, const size_t entryPointCount)
{
    // Compose module + entry points for reflection
    const auto componentCount = 1 + entryPointCount;
    auto components = std::make_unique<slang::IComponentType*[]>(componentCount);
    components[0] = module;
    for (size_t i = 0; i < entryPointCount; i++)
        components[1 + i] = entryPoints[i];

    slang::IComponentType* composed = nullptr;
    if (const auto session = device->getSlangSession();
        SLANG_FAILED(session->createCompositeComponentType(components.get(), static_cast<SlangInt>(componentCount), &composed)))
        return;

    slang::IComponentType* linked = nullptr;
    if (SLANG_FAILED(composed->link(&linked)))
    {
        composed->release();
        return;
    }

    auto* layout = linked->getLayout();
    if (!layout)
    {
        linked->release();
        composed->release();
        return;
    }

    const auto epCount = layout->getEntryPointCount();
    for (SlangUInt i = 0; i < epCount; i++)
    {
        auto* ep = layout->getEntryPointByIndex(i);
        switch (ep->getStage())
        {
        case SLANG_STAGE_VERTEX:
            ReflectVertexInputLayout(ep);
            break;
        case SLANG_STAGE_FRAGMENT:
            ReflectRenderTargetCount(ep);
            break;
        case SLANG_STAGE_COMPUTE:
            ReflectThreadGroupSize(ep);
            break;
        default:
            break;
        }
    }

    linked->release();
    composed->release();
}

void Program::ReflectVertexInputLayout(slang::EntryPointReflection* entry)
{
    uint32_t offset = 0;
    const unsigned paramCount = entry->getParameterCount();
    for (unsigned i = 0; i < paramCount; i++)
    {
        auto* param = entry->getParameterByIndex(i);
        if (param->getCategory() != slang::ParameterCategory::VaryingInput)
            continue;

        auto* typeLayout = param->getTypeLayout();

        if (const unsigned fieldCount = typeLayout->getFieldCount(); fieldCount > 0)
        {
            for (unsigned f = 0; f < fieldCount; f++)
            {
                auto* field = typeLayout->getFieldByIndex(f);
                auto* fieldType = field->getTypeLayout();

                inputElements.push_back({
                    field->getSemanticName(),
                    static_cast<uint32_t>(field->getSemanticIndex()),
                    GetVertexFormat(fieldType->getScalarType(), fieldType->getColumnCount()),
                    offset, 0
                });
                offset += static_cast<uint32_t>(fieldType->getSize());
            }
        }
        else
        {
            inputElements.push_back({
                param->getSemanticName(),
                static_cast<uint32_t>(param->getSemanticIndex()),
                GetVertexFormat(typeLayout->getScalarType(), typeLayout->getColumnCount()),
                offset, 0
            });
            offset += static_cast<uint32_t>(typeLayout->getSize());
        }
    }

    vertexStream.stride = offset;
    vertexStream.slotClass = rhi::InputSlotClass::PerVertex;
    vertexStream.instanceDataStepRate = 0;

    inputLayoutDesc.inputElements = inputElements.data();
    inputLayoutDesc.inputElementCount = static_cast<uint32_t>(inputElements.size());
    inputLayoutDesc.vertexStreams = &vertexStream;
    inputLayoutDesc.vertexStreamCount = 1;
}

void Program::ReflectRenderTargetCount(slang::EntryPointReflection* entry)
{
    const unsigned paramCount = entry->getParameterCount();
    for (unsigned i = 0; i < paramCount; i++)
    {
        auto* param = entry->getParameterByIndex(i);
        if (param->getCategory() != slang::ParameterCategory::VaryingOutput)
            continue;

        auto* typeLayout = param->getTypeLayout();
        if (const unsigned fieldCount = typeLayout->getFieldCount(); fieldCount > 0)
            renderTargetCount += fieldCount;
        else
            renderTargetCount++;
    }

    // Also check the result (return value) of the entry point
    if (auto* result = entry->getResultVarLayout())
    {
        if (result->getCategory() == slang::ParameterCategory::VaryingOutput)
            renderTargetCount++;
    }
}

void Program::ReflectThreadGroupSize(slang::EntryPointReflection* entry)
{
    SlangUInt size[3];
    entry->getComputeThreadGroupSize(3, size);
    threadGroupSize = {size[0], size[1], size[2]};
}

Program::Program(rhi::IDevice* device, const std::string& moduleName) : moduleName{moduleName}, device{device}
{
    Slang::ComPtr<slang::IBlob> diagnostics;
    const auto session = device->getSlangSession();

    slang::IModule* module = session->loadModule(moduleName.c_str(), diagnostics.writeRef());
    PrintDiagnostics(diagnostics);
    if (!module)
    {
        spdlog::error("Failed to load module: {}", moduleName);
        return;
    }

    const auto entryCount = static_cast<size_t>(module->getDefinedEntryPointCount());
    auto entryPoints = std::make_unique<slang::IComponentType*[]>(entryCount);
    for (size_t i = 0; i < entryCount; i++)
        module->getDefinedEntryPoint(static_cast<int32_t>(i), reinterpret_cast<slang::IEntryPoint**>(&entryPoints[i]));

    rhi::ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = module;
    programDesc.slangEntryPoints = entryPoints.get();
    programDesc.slangEntryPointCount = static_cast<uint32_t>(entryCount);
    programDesc.label = moduleName.c_str();

    device->createShaderProgram(programDesc, shaderProgram.writeRef(), diagnostics.writeRef());
    PrintDiagnostics(diagnostics);
    if (!shaderProgram)
    {
        spdlog::error("Failed to create shader program: {}", moduleName);
        return;
    }

    Reflect(module, entryPoints.get(), entryCount);

    for (size_t i = 0; i < entryCount; i++)
        entryPoints[i]->release();
}
