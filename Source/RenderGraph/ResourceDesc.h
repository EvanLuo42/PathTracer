#pragma once

#include <slang-rhi.h>
#include <string>

struct SizePolicy
{
    enum class Mode { Fixed, BackBufferRelative };
    Mode mode = Mode::BackBufferRelative;
    float widthScale = 1.0f;
    float heightScale = 1.0f;
    uint32_t fixedWidth = 0;
    uint32_t fixedHeight = 0;

    static SizePolicy BackBuffer(float scale = 1.0f)
    {
        return {Mode::BackBufferRelative, scale, scale, 0, 0};
    }

    static SizePolicy Fixed(uint32_t w, uint32_t h)
    {
        return {Mode::Fixed, 0, 0, w, h};
    }

    [[nodiscard]] uint32_t resolveWidth(uint32_t bbWidth) const
    {
        return mode == Mode::Fixed ? fixedWidth : static_cast<uint32_t>(static_cast<float>(bbWidth) * widthScale);
    }

    [[nodiscard]] uint32_t resolveHeight(uint32_t bbHeight) const
    {
        return mode == Mode::Fixed ? fixedHeight : static_cast<uint32_t>(static_cast<float>(bbHeight) * heightScale);
    }
};

struct TextureResourceDesc
{
    std::string name;
    rhi::Format format = rhi::Format::Undefined;
    SizePolicy sizePolicy;
};
