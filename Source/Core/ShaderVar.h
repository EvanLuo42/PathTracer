#pragma once

// NOLINTBEGIN(misc-unconventional-assign-operator)

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include <slang-com-ptr.h>

#include <cassert>
#include <cstdio>
#include <type_traits>

struct ShaderVar
{
    rhi::ShaderCursor cursor;

    ShaderVar() = default;
    explicit ShaderVar(rhi::IShaderObject* object) : cursor(object) {}
    explicit ShaderVar(const rhi::ShaderCursor& c) : cursor(c) {}

    ShaderVar operator[](const char* name) const
    {
        auto child = ShaderVar(cursor[name]);
        if (!child.cursor.isValid())
            fprintf(stderr, "ShaderVar: field '%s' not found\n", name);
        return child;
    }
    ShaderVar operator[](int index) const
    {
        auto child = ShaderVar(cursor[index]);
        if (!child.cursor.isValid())
            fprintf(stderr, "ShaderVar: index [%d] not found\n", index);
        return child;
    }
    ShaderVar operator[](uint32_t index) const
    {
        auto child = ShaderVar(cursor[index]);
        if (!child.cursor.isValid())
            fprintf(stderr, "ShaderVar: index [%u] not found\n", index);
        return child;
    }

    const ShaderVar& operator=(const Slang::ComPtr<rhi::ITexture>& v) const { check(cursor.setBinding(v), "setBinding(ITexture)"); return *this; }
    const ShaderVar& operator=(const Slang::ComPtr<rhi::IBuffer>& v) const { check(cursor.setBinding(v), "setBinding(IBuffer)"); return *this; }
    const ShaderVar& operator=(const Slang::ComPtr<rhi::ISampler>& v) const { check(cursor.setBinding(v), "setBinding(ISampler)"); return *this; }
    const ShaderVar& operator=(const Slang::ComPtr<rhi::ITextureView>& v) const { check(cursor.setBinding(v), "setBinding(ITextureView)"); return *this; }
    const ShaderVar& operator=(const Slang::ComPtr<rhi::IAccelerationStructure>& v) const { check(cursor.setBinding(v), "setBinding(IAccelerationStructure)"); return *this; }
    const ShaderVar& operator=(const rhi::DescriptorHandle& v) const { check(cursor.setDescriptorHandle(v), "setDescriptorHandle"); return *this; }
    const ShaderVar& operator=(const rhi::Binding& v) const { check(cursor.setBinding(v), "setBinding(Binding)"); return *this; }

    template<typename T, std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>, int> = 0>
    const ShaderVar& operator=(const T& data) const { check(cursor.setData(&data, sizeof(T)), "setData"); return *this; }

private:
    static void check(SlangResult r, const char* op)
    {
        if (!SLANG_SUCCEEDED(r))
        {
            fprintf(stderr, "ShaderVar error: %s failed (result=0x%08x)\n", op, static_cast<unsigned>(r));
            assert(false);
        }
    }
};

// NOLINTEND(misc-unconventional-assign-operator)
