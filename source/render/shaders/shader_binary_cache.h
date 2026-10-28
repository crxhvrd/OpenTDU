#pragma once

#include <unordered_map>

#include "render/gs_render_helper.h"
#include "render/material.h"

struct Material;
struct GPUShader;
struct ShaderTableEntry;
class RenderDevice;

static constexpr uint64_t kInvalidMetadataOffset = 0xffffffffffffffffull;

struct ShaderTableHeaderEntry {
    uint64_t Hashcode; // Hashcode (from the game; not the actual bytecode hash)
    uint64_t Offset;   // Offset in file (absolute)
    uint64_t Size;     // Size (in bytes)
    uint64_t MetadataOffset; // Metadata offset in file (absolute).
                             // kInvalidMetadataOffset if unavailable/unused
};

// Shader interface metadata. The runtime reflects it from the SPIR-V module;
// the asset extractor serializes it (as a dxvk DxvkShaderCreateInfo subset) in
// the optional dxvk translated tables. NOTE:
// This struct does not include the binding reflection (stored right after
// NumBindings). During parsing, you must make sure to read
// 'sizeof(DxvkBindingInfo) * NumBindings' before reading the next metadata
// entry.
struct ShaderMetadataSPIRV {
    uint32_t InputMask;
    uint32_t OutputMask;
    uint32_t FlatShadingInputs;
    uint32_t SamplerPushConstantsSize; // samplerDwordCount * sizeof(uint32_t)
    uint32_t NumBindings; // (SPIRV) Num of binding metadata (DxvkBindingInfo)
                          // to read
    uint32_t NumInputElements; // ISGN element count (VS only; 0 for PS).
                               // Stored after bindings in the metadata blob.
};

#if OTDU_VULKAN
// Layout of DxvkBindingInfo (the dxvk translated tables serialize it as-is).
struct SPIRVBindingInfo {
    uint32_t set = 0u;
    uint32_t binding = 0u;
    uint32_t resourceIndex = 0u;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint32_t descriptorCount = 1u;
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    VkAccessFlags access = 0u;
    uint8_t flags = 0u;
    uint16_t accessOp = 0;
    uint32_t blockOffset = 0u;
};
OTDU_SIZE_MUST_MATCH( SPIRVBindingInfo, 0x24 );

// One ISGN (Input Shader Signature) entry: maps a SPIRV vertex input location
// to its D3D9 Usage semantic so the runtime can match vertex declaration
// elements to shader inputs by semantic instead of by sequential stream index.
struct SPIRVInputElement {
    uint32_t Location; // SPIRV layout(location=N) (D3D9_IN_*)
    uint8_t Usage;     // D3DDECLUSAGE value (matches dxvk::DxsoUsage)
    uint8_t UsageIndex;
    uint8_t _pad[2];
};
OTDU_SIZE_MUST_MATCH( SPIRVInputElement, 8 );

// Used to store metadata at runtime
struct ShaderMetadataSPIRVRuntime : public ShaderMetadataSPIRV {
    std::vector<SPIRVBindingInfo> Bindings;
    std::vector<SPIRVInputElement> InputElements; // empty for PS
};
#endif

struct ShaderTableHeader {
    // NOTE: Serialized as-is at the very beginning of a shader table; must be
    // a fixed width type (a 32bits build would otherwise read 4 bytes where a
    // 64bits extractor wrote 8, and walk the entry array way out of bounds).
    uint64_t NumEntries;
    std::vector<ShaderTableHeaderEntry> Entries;

    inline uint64_t GetHeaderSize() const {
        return NumEntries * sizeof( ShaderTableHeaderEntry ) +
               sizeof( uint64_t );
    }

    inline void Clear() {
        NumEntries = 0ull;
        Entries.clear();
    }

    inline bool Empty() { return NumEntries == 0ull; }

    inline void Add( ShaderTableHeaderEntry entry ) {
        Entries.push_back( entry );
        NumEntries++;
    }
};

struct ShaderTable {
    ShaderTableHeader Header;
    std::vector<int8_t> Shaders;
    std::vector<int8_t> Metadata;

    inline void Clear() {
        Header.Clear();
        Shaders.clear();
        Metadata.clear();
    }
};

static constexpr const char *kShadersRootFolder = "opentdu_data/";
static constexpr const char *kShadersD3D9Folder = "d3d9/";
static constexpr const char *kShadersRebuiltPack = "shader_cache";
static constexpr const char *kShaderTableExtension = ".bin";

using ShaderBytecode_t = std::tuple<void *, uint64_t>;

class ShaderBinaryCache {
public:
    ShaderBinaryCache();
    ~ShaderBinaryCache();

    ShaderBytecode_t getOrLoadBytecode( const ShaderTableEntry *pTableEntry );
    ShaderMetadataSPIRVRuntime *
    getMetadataSPIRV( const ShaderTableEntry *pTableEntry );

private:
    // Loaded shader tables (keyed by path).
    std::unordered_map<std::string, void *> shaderTableBinary;
    std::unordered_map<uint64_t, ShaderBytecode_t> shaderBinaries;

private:
    bool loadShaderTable(
        const std::string &shaderTablePath,
        std::unordered_map<uint64_t, ShaderBytecode_t> &outBinaries );

#if OTDU_VULKAN
    std::unordered_map<uint64_t, ShaderMetadataSPIRVRuntime> shaderMetadata;
#endif
};

extern ShaderBinaryCache gShaderBinaryCache;
