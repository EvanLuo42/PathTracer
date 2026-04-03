#pragma once

#include <slang-rhi.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

class CacheBlob : public ISlangBlob
{
public:
    virtual ~CacheBlob() = default;
    explicit CacheBlob(size_t size) : m_data(size) {}

    SLANG_NO_THROW void const* SLANG_MCALL getBufferPointer() override { return m_data.data(); }
    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override { return m_data.size(); }

    void* getMutablePointer() { return m_data.data(); }

    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const&, void** outObject) override
    {
        *outObject = nullptr;
        return SLANG_E_NO_INTERFACE;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++m_refCount; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override
    {
        const auto r = --m_refCount;
        if (r == 0)
            delete this;
        return r;
    }

private:
    std::vector<uint8_t> m_data;
    std::atomic<uint32_t> m_refCount{1};
};

class PipelineCache : public rhi::IPersistentCache
{
public:
    virtual ~PipelineCache() = default;

    explicit PipelineCache(std::filesystem::path cacheDir)
        : m_cacheDir(std::move(cacheDir))
    {
        std::filesystem::create_directories(m_cacheDir);
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const&, void** outObject) override
    {
        *outObject = nullptr;
        return SLANG_E_NO_INTERFACE;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++m_refCount; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override
    {
        const auto r = --m_refCount;
        if (r == 0)
            delete this;
        return r;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL writeCache(ISlangBlob* key, ISlangBlob* data) override
    {
        const auto path = getCachePath(key);
        std::ofstream file(path.string(), std::ios::binary);
        if (!file)
            return SLANG_FAIL;

        file.write(
            static_cast<const char*>(data->getBufferPointer()),
            static_cast<std::streamsize>(data->getBufferSize())
        );
        return SLANG_OK;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL queryCache(ISlangBlob* key, ISlangBlob** outData) override
    {
        const auto path = getCachePath(key);
        std::ifstream file(path.string(), std::ios::binary | std::ios::ate);
        if (!file)
            return SLANG_E_NOT_FOUND;

        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0);

        auto* blob = new CacheBlob(size);
        file.read(static_cast<char*>(blob->getMutablePointer()), static_cast<std::streamsize>(size));
        *outData = blob;
        return SLANG_OK;
    }

private:
    std::filesystem::path getCachePath(ISlangBlob* key) const
    {
        const auto* bytes = static_cast<const uint8_t*>(key->getBufferPointer());
        const auto size = key->getBufferSize();

        uint64_t hash = 14695981039346656037ull;
        for (size_t i = 0; i < size; i++)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }

        char filename[24];
        snprintf(filename, sizeof(filename), "%016llx.cache", hash);
        return m_cacheDir / filename;
    }

    std::filesystem::path m_cacheDir;
    std::atomic<uint32_t> m_refCount{1};
};
