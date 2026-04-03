#pragma once

// NOLINTBEGIN(misc-unconventional-assign-operator)

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include <slang-com-ptr.h>

#include <cassert>
#include <type_traits>

struct ShaderVar
{
    rhi::ShaderCursor cursor;

    ShaderVar() = default;
    explicit ShaderVar(rhi::IShaderObject* object) : cursor(object) {}
    explicit ShaderVar(const rhi::ShaderCursor& c) : cursor(c) {}

    ShaderVar operator[](const char* name) const { return ShaderVar(cursor[name]); }
    ShaderVar operator[](int index) const { return ShaderVar(cursor[index]); }
    ShaderVar operator[](uint32_t index) const { return ShaderVar(cursor[index]); }

    const ShaderVar& operator=(const Slang::ComPtr<rhi::ITexture>& v) const { check(cursor.setBinding(v)); return *this; }
    const ShaderVar& operator=(const Slang::ComPtr<rhi::IBuffer>& v) const { check(cursor.setBinding(v)); return *this; }
    const ShaderVar& operator=(const Slang::ComPtr<rhi::ISampler>& v) const { check(cursor.setBinding(v)); return *this; }
    const ShaderVar& operator=(const Slang::ComPtr<rhi::ITextureView>& v) const { check(cursor.setBinding(v)); return *this; }
    const ShaderVar& operator=(const Slang::ComPtr<rhi::IAccelerationStructure>& v) const { check(cursor.setBinding(v)); return *this; }
    const ShaderVar& operator=(const rhi::DescriptorHandle& v) const { check(cursor.setDescriptorHandle(v)); return *this; }
    const ShaderVar& operator=(const rhi::Binding& v) const { check(cursor.setBinding(v)); return *this; }

    template<typename T, std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>, int> = 0>
    const ShaderVar& operator=(const T& data) const { check(cursor.setData(&data, sizeof(T))); return *this; }

private:
    static void check(SlangResult r) { assert(SLANG_SUCCEEDED(r)); }
};

// NOLINTEND(misc-unconventional-assign-operator)
