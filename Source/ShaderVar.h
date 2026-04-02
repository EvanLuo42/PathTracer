#pragma once

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include <slang-com-ptr.h>

struct ShaderVar
{
    rhi::ShaderCursor cursor;

    ShaderVar() = default;
    ShaderVar(rhi::IShaderObject* object) : cursor(object) {}
    ShaderVar(const rhi::ShaderCursor& c) : cursor(c) {}

    ShaderVar operator[](const char* name) const { return {cursor[name]}; }
    ShaderVar operator[](int index) const { return {cursor[index]}; }
    ShaderVar operator[](uint32_t index) const { return {cursor[index]}; }

    // Textures
    const ShaderVar& operator=(const Slang::ComPtr<rhi::ITexture>& tex) const
    {
        cursor.setBinding(tex);
        return *this;
    }

    // Buffers
    const ShaderVar& operator=(const Slang::ComPtr<rhi::IBuffer>& buf) const
    {
        cursor.setBinding(buf);
        return *this;
    }

    // Samplers
    const ShaderVar& operator=(const Slang::ComPtr<rhi::ISampler>& sampler) const
    {
        cursor.setBinding(sampler);
        return *this;
    }

    // Texture views
    const ShaderVar& operator=(const Slang::ComPtr<rhi::ITextureView>& view) const
    {
        cursor.setBinding(view);
        return *this;
    }

    // Acceleration structures
    const ShaderVar& operator=(const Slang::ComPtr<rhi::IAccelerationStructure>& as) const
    {
        cursor.setBinding(as);
        return *this;
    }

    // Descriptor handles (bindless)
    const ShaderVar& operator=(const rhi::DescriptorHandle& handle) const
    {
        cursor.setDescriptorHandle(handle);
        return *this;
    }

    // Raw binding
    const ShaderVar& operator=(const rhi::Binding& binding) const
    {
        cursor.setBinding(binding);
        return *this;
    }

    // POD data (float, int, vec, mat, structs)
    template<typename T, std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>, int> = 0>
    const ShaderVar& operator=(const T& data) const
    {
        cursor.setData(&data, sizeof(T));
        return *this;
    }

    // Explicit setData for arrays/custom sizes
    void setData(const void* data, size_t size) const { cursor.setData(data, size); }
};
