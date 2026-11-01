#pragma once

#if OTDU_VULKAN
#include "render/render_api.h"
#include "volk.h"

#include "render_pass_manager.h"

struct PSOFactory;
struct GPUShader;
struct GPUVertexLayout;

struct PSORecord {
    uint64_t Hashcode; // PSOFactory hashcode
    uint64_t VSHashcode;
    uint64_t PSHashcode; // 0 if no pixel shader is bound
    uint32_t PrimitiveType;
    uint32_t VSClipPlaneCount;
    uint32_t PSSpecDwords[2];
    uint32_t LampsEnabled;

    std::vector<VkVertexInputBindingDescription> InputBindings;
    struct Semantic {
        uint32_t Binding;
        uint32_t Offset;
        VkFormat Format;
        uint32_t Usage;
        uint32_t UsageIndex;
    };
    std::vector<Semantic> Semantics;

    VkPipelineRasterizationStateCreateInfo RasterState;
    VkPipelineDepthStencilStateCreateInfo DepthStencil;
    VkSampleCountFlagBits SampleCount;
    std::vector<VkPipelineColorBlendAttachmentState> BlendAttachments;
    float BlendConstants[4];

    RenderPassDesc RenderPass;
};

class PSOLibrary {
public:
    inline const std::vector<PSORecord> &getRecords() const { return records; }
    inline bool isDirty() const { return bDirty; }

public:
    bool load( const std::filesystem::path &path );
    bool save( const std::filesystem::path &path );

    void record( const PSOFactory &factory,
                 const uint64_t psoHashcode,
                 const RenderPassDesc &renderPassDesc );
    void discard( const uint64_t psoHashcode );

    void rehash( const uint64_t oldHashcode, const uint64_t newHashcode );

    static void ApplyRecord( const PSORecord &record,
                             PSOFactory &factory,
                             GPUShader *pVertexShader,
                             GPUShader *pPixelShader,
                             GPUVertexLayout *pVertexLayout,
                             const CachedRenderPass &renderPass );

private:
    std::vector<PSORecord> records;
    std::unordered_map<uint64_t, size_t> recordIndexes;
    bool bDirty = false;
};
#endif
