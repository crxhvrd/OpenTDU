#include "shared.h"
#include "pso_library.h"

#if OTDU_VULKAN
#include "pso_factory.h"
#include "render_device.h"

static constexpr uint32_t kPSOLibraryMagic = 0x4c50544f; // 'OTPL'

static constexpr uint32_t kPSOLibraryVersion =
    3; // Bump me whenever a new render change (new shader/renderpass) is added

namespace {
    struct RecordWriter {
        std::vector<uint32_t> &Words;

        inline void push( const uint32_t value ) { Words.push_back( value ); }
        inline void push64( const uint64_t value ) {
            Words.push_back( static_cast<uint32_t>( value ) );
            Words.push_back( static_cast<uint32_t>( value >> 32 ) );
        }
        inline void pushFloat( const float value ) {
            uint32_t bits = 0;
            memcpy( &bits, &value, sizeof( uint32_t ) );
            Words.push_back( bits );
        }
        inline void pushStencilOp( const VkStencilOpState &state ) {
            push( state.failOp );
            push( state.passOp );
            push( state.depthFailOp );
            push( state.compareOp );
            push( state.compareMask );
            push( state.writeMask );
            push( state.reference );
        }
    };

    struct RecordReader {
        const uint32_t *pWords;
        size_t NumWords;
        size_t Cursor = 0;
        bool bValid = true;

        inline uint32_t read() {
            if ( Cursor >= NumWords ) {
                bValid = false;
                return 0u;
            }
            return pWords[Cursor++];
        }
        inline uint64_t read64() {
            const uint64_t low = read();
            const uint64_t high = read();
            return low | ( high << 32 );
        }
        inline float readFloat() {
            const uint32_t bits = read();
            float value = 0.0f;
            memcpy( &value, &bits, sizeof( float ) );
            return value;
        }
        // Reads an element count, rejecting anything larger than maxCount
        inline uint32_t readCount( const uint32_t maxCount ) {
            const uint32_t count = read();
            if ( count > maxCount ) {
                bValid = false;
                return 0u;
            }
            return count;
        }
        inline void readStencilOp( VkStencilOpState &state ) {
            state.failOp = static_cast<VkStencilOp>( read() );
            state.passOp = static_cast<VkStencilOp>( read() );
            state.depthFailOp = static_cast<VkStencilOp>( read() );
            state.compareOp = static_cast<VkCompareOp>( read() );
            state.compareMask = read();
            state.writeMask = read();
            state.reference = read();
        }
    };

    // Upper bounds used to reject corrupted records
    static constexpr uint32_t kMaxRecordedBindings = 16;
    static constexpr uint32_t kMaxRecordedSemantics = 32;

    void WriteRecord( const PSORecord &record, std::vector<uint32_t> &words ) {
        RecordWriter writer{ words };
        writer.push64( record.Hashcode );
        writer.push64( record.VSHashcode );
        writer.push64( record.PSHashcode );
        writer.push( record.PrimitiveType );
        writer.push( record.VSClipPlaneCount );
        writer.push( record.PSSpecDwords[0] );
        writer.push( record.PSSpecDwords[1] );
        writer.push( record.LampsEnabled );

        writer.push( static_cast<uint32_t>( record.InputBindings.size() ) );
        for ( const VkVertexInputBindingDescription &binding :
              record.InputBindings ) {
            writer.push( binding.binding );
            writer.push( binding.stride );
            writer.push( binding.inputRate );
        }

        writer.push( static_cast<uint32_t>( record.Semantics.size() ) );
        for ( const PSORecord::Semantic &semantic : record.Semantics ) {
            writer.push( semantic.Binding );
            writer.push( semantic.Offset );
            writer.push( semantic.Format );
            writer.push( semantic.Usage );
            writer.push( semantic.UsageIndex );
        }

        const VkPipelineRasterizationStateCreateInfo &raster =
            record.RasterState;
        writer.push( raster.flags );
        writer.push( raster.depthClampEnable );
        writer.push( raster.rasterizerDiscardEnable );
        writer.push( raster.polygonMode );
        writer.push( raster.cullMode );
        writer.push( raster.frontFace );
        writer.push( raster.depthBiasEnable );
        writer.pushFloat( raster.depthBiasConstantFactor );
        writer.pushFloat( raster.depthBiasClamp );
        writer.pushFloat( raster.depthBiasSlopeFactor );
        writer.pushFloat( raster.lineWidth );

        const VkPipelineDepthStencilStateCreateInfo &depthStencil =
            record.DepthStencil;
        writer.push( depthStencil.flags );
        writer.push( depthStencil.depthTestEnable );
        writer.push( depthStencil.depthWriteEnable );
        writer.push( depthStencil.depthCompareOp );
        writer.push( depthStencil.depthBoundsTestEnable );
        writer.push( depthStencil.stencilTestEnable );
        writer.pushStencilOp( depthStencil.front );
        writer.pushStencilOp( depthStencil.back );
        writer.pushFloat( depthStencil.minDepthBounds );
        writer.pushFloat( depthStencil.maxDepthBounds );

        writer.push( record.SampleCount );

        writer.push( static_cast<uint32_t>( record.BlendAttachments.size() ) );
        for ( const VkPipelineColorBlendAttachmentState &attachment :
              record.BlendAttachments ) {
            writer.push( attachment.blendEnable );
            writer.push( attachment.srcColorBlendFactor );
            writer.push( attachment.dstColorBlendFactor );
            writer.push( attachment.colorBlendOp );
            writer.push( attachment.srcAlphaBlendFactor );
            writer.push( attachment.dstAlphaBlendFactor );
            writer.push( attachment.alphaBlendOp );
            writer.push( attachment.colorWriteMask );
        }
        for ( const float blendConstant : record.BlendConstants ) {
            writer.pushFloat( blendConstant );
        }

        const RenderPassDesc &renderPass = record.RenderPass;
        writer.push( renderPass.NumColorAttachments );
        writer.push( renderPass.bUseDepthStencil );
        for ( uint32_t i = 0; i < renderPass.getNumAttachments(); i++ ) {
            const VkAttachmentDescription &attachment =
                renderPass.Attachments[i];
            writer.push( attachment.flags );
            writer.push( GetPortableAttachmentFormat( attachment.format ) );
            writer.push( attachment.samples );
            writer.push( attachment.loadOp );
            writer.push( attachment.storeOp );
            writer.push( attachment.stencilLoadOp );
            writer.push( attachment.stencilStoreOp );
            writer.push( attachment.initialLayout );
            writer.push( attachment.finalLayout );

            const VkAttachmentReference &reference = renderPass.References[i];
            writer.push( reference.attachment );
            writer.push( reference.layout );
        }
    }

    bool ReadRecord( RecordReader &reader, PSORecord &record ) {
        record.Hashcode = reader.read64();
        record.VSHashcode = reader.read64();
        record.PSHashcode = reader.read64();
        record.PrimitiveType = reader.read();
        record.VSClipPlaneCount = reader.read();
        record.PSSpecDwords[0] = reader.read();
        record.PSSpecDwords[1] = reader.read();
        record.LampsEnabled = reader.read();
        if ( record.PrimitiveType >= ePrimitiveType::PT_Count ) {
            return false;
        }

        record.InputBindings.resize( reader.readCount( kMaxRecordedBindings ) );
        for ( VkVertexInputBindingDescription &binding :
              record.InputBindings ) {
            binding.binding = reader.read();
            binding.stride = reader.read();
            binding.inputRate = static_cast<VkVertexInputRate>( reader.read() );
        }

        record.Semantics.resize( reader.readCount( kMaxRecordedSemantics ) );
        for ( PSORecord::Semantic &semantic : record.Semantics ) {
            semantic.Binding = reader.read();
            semantic.Offset = reader.read();
            semantic.Format = static_cast<VkFormat>( reader.read() );
            semantic.Usage = reader.read();
            semantic.UsageIndex = reader.read();
        }

        VkPipelineRasterizationStateCreateInfo &raster = record.RasterState;
        raster = {};
        raster.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.flags = reader.read();
        raster.depthClampEnable = reader.read();
        raster.rasterizerDiscardEnable = reader.read();
        raster.polygonMode = static_cast<VkPolygonMode>( reader.read() );
        raster.cullMode = reader.read();
        raster.frontFace = static_cast<VkFrontFace>( reader.read() );
        raster.depthBiasEnable = reader.read();
        raster.depthBiasConstantFactor = reader.readFloat();
        raster.depthBiasClamp = reader.readFloat();
        raster.depthBiasSlopeFactor = reader.readFloat();
        raster.lineWidth = reader.readFloat();

        VkPipelineDepthStencilStateCreateInfo &depthStencil =
            record.DepthStencil;
        depthStencil = {};
        depthStencil.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.flags = reader.read();
        depthStencil.depthTestEnable = reader.read();
        depthStencil.depthWriteEnable = reader.read();
        depthStencil.depthCompareOp = static_cast<VkCompareOp>( reader.read() );
        depthStencil.depthBoundsTestEnable = reader.read();
        depthStencil.stencilTestEnable = reader.read();
        reader.readStencilOp( depthStencil.front );
        reader.readStencilOp( depthStencil.back );
        depthStencil.minDepthBounds = reader.readFloat();
        depthStencil.maxDepthBounds = reader.readFloat();

        record.SampleCount =
            static_cast<VkSampleCountFlagBits>( reader.read() );

        record.BlendAttachments.resize(
            reader.readCount( kMaxSimultaneousRT ) );
        for ( VkPipelineColorBlendAttachmentState &attachment :
              record.BlendAttachments ) {
            attachment.blendEnable = reader.read();
            attachment.srcColorBlendFactor =
                static_cast<VkBlendFactor>( reader.read() );
            attachment.dstColorBlendFactor =
                static_cast<VkBlendFactor>( reader.read() );
            attachment.colorBlendOp = static_cast<VkBlendOp>( reader.read() );
            attachment.srcAlphaBlendFactor =
                static_cast<VkBlendFactor>( reader.read() );
            attachment.dstAlphaBlendFactor =
                static_cast<VkBlendFactor>( reader.read() );
            attachment.alphaBlendOp = static_cast<VkBlendOp>( reader.read() );
            attachment.colorWriteMask = reader.read();
        }
        for ( float &blendConstant : record.BlendConstants ) {
            blendConstant = reader.readFloat();
        }

        RenderPassDesc &renderPass = record.RenderPass;
        renderPass.Attachments.fill( {} );
        renderPass.References.fill( {} );
        renderPass.NumColorAttachments = reader.readCount( kMaxSimultaneousRT );
        renderPass.bUseDepthStencil = reader.read() != 0;
        for ( uint32_t i = 0; i < renderPass.getNumAttachments(); i++ ) {
            VkAttachmentDescription &attachment = renderPass.Attachments[i];
            attachment.flags = reader.read();
            attachment.format = GetNativeAttachmentFormat(
                static_cast<VkFormat>( reader.read() ) );
            attachment.samples =
                static_cast<VkSampleCountFlagBits>( reader.read() );
            attachment.loadOp =
                static_cast<VkAttachmentLoadOp>( reader.read() );
            attachment.storeOp =
                static_cast<VkAttachmentStoreOp>( reader.read() );
            attachment.stencilLoadOp =
                static_cast<VkAttachmentLoadOp>( reader.read() );
            attachment.stencilStoreOp =
                static_cast<VkAttachmentStoreOp>( reader.read() );
            attachment.initialLayout =
                static_cast<VkImageLayout>( reader.read() );
            attachment.finalLayout =
                static_cast<VkImageLayout>( reader.read() );

            VkAttachmentReference &reference = renderPass.References[i];
            reference.attachment = reader.read();
            reference.layout = static_cast<VkImageLayout>( reader.read() );
        }

        return reader.bValid && reader.Cursor == reader.NumWords;
    }
} // namespace

bool PSOLibrary::load( const std::filesystem::path &path ) {
    records.clear();
    recordIndexes.clear();
    bDirty = false;

    std::ifstream file( path, std::ios::binary | std::ios::ate );
    if ( !file.is_open() ) {
        return false;
    }

    const size_t fileSize = static_cast<size_t>( file.tellg() );
    std::vector<uint32_t> words( fileSize / sizeof( uint32_t ) );
    file.seekg( 0 );
    file.read( reinterpret_cast<char *>( words.data() ),
               words.size() * sizeof( uint32_t ) );
    if ( !file || words.size() < 3 ) {
        return false;
    }

    if ( words[0] != kPSOLibraryMagic || words[1] != kPSOLibraryVersion ) {
        OTDU_LOG_INFO( "PSO library '%s' is outdated; discarding it\n",
                       path.string().c_str() );
        bDirty = true;
        return false;
    }

    const uint32_t numRecords = words[2];
    size_t cursor = 3;
    uint32_t numInvalidRecords = 0;
    for ( uint32_t i = 0; i < numRecords && cursor < words.size(); i++ ) {
        const uint32_t numRecordWords = words[cursor++];
        if ( numRecordWords > words.size() - cursor ) {
            numInvalidRecords += numRecords - i;
            break;
        }

        RecordReader reader{ &words[cursor], numRecordWords };
        cursor += numRecordWords;

        PSORecord record;
        if ( !ReadRecord( reader, record ) ||
             recordIndexes.find( record.Hashcode ) != recordIndexes.end() ) {
            numInvalidRecords++;
            continue;
        }

        recordIndexes.insert(
            std::make_pair( record.Hashcode, records.size() ) );
        records.push_back( std::move( record ) );
    }

    if ( numInvalidRecords != 0 ) {
        OTDU_LOG_WARN( "PSO library '%s': discarded %u invalid record(s)\n",
                       path.string().c_str(), numInvalidRecords );
        bDirty = true;
    }

    OTDU_LOG_INFO( "Loaded PSO library '%s' (%zu PSOs)\n",
                   path.string().c_str(), records.size() );
    return true;
}

bool PSOLibrary::save( const std::filesystem::path &path ) {
    std::vector<uint32_t> words;
    words.push_back( kPSOLibraryMagic );
    words.push_back( kPSOLibraryVersion );
    words.push_back( static_cast<uint32_t>( records.size() ) );

    std::vector<uint32_t> recordWords;
    for ( const PSORecord &record : records ) {
        recordWords.clear();
        WriteRecord( record, recordWords );
        words.push_back( static_cast<uint32_t>( recordWords.size() ) );
        words.insert( words.end(), recordWords.begin(), recordWords.end() );
    }

    std::error_code errorCode;
    std::filesystem::create_directories( path.parent_path(), errorCode );

    // Write to a temporary file first so an interrupted save never leaves a
    // truncated library behind
    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";
    {
        std::ofstream file( tmpPath, std::ios::binary | std::ios::trunc );
        file.write( reinterpret_cast<const char *>( words.data() ),
                    words.size() * sizeof( uint32_t ) );
        if ( !file ) {
            OTDU_LOG_WARN( "Failed to write PSO library '%s'\n",
                           tmpPath.string().c_str() );
            return false;
        }
    }

    std::filesystem::rename( tmpPath, path, errorCode );
    if ( errorCode ) {
        OTDU_LOG_WARN( "Failed to save PSO library '%s': %s\n",
                       path.string().c_str(), errorCode.message().c_str() );
        std::filesystem::remove( tmpPath, errorCode );
        return false;
    }

    bDirty = false;
    OTDU_LOG_INFO( "Saved PSO library (%zu PSOs)\n", records.size() );
    return true;
}

void PSOLibrary::record( const PSOFactory &factory,
                         const uint64_t psoHashcode,
                         const RenderPassDesc &renderPassDesc ) {
    if ( recordIndexes.find( psoHashcode ) != recordIndexes.end() ||
         factory.pVertexLayout == nullptr ||
         factory.pGraphicsShaderStages[0] == nullptr ) {
        return;
    }

    PSORecord record;
    record.Hashcode = psoHashcode;
    record.VSHashcode = factory.pGraphicsShaderStages[0]->Hashcode;
    record.PSHashcode = ( factory.bIsStageBound[1] )
                            ? factory.pGraphicsShaderStages[1]->Hashcode
                            : 0ull;
    record.PrimitiveType = static_cast<uint32_t>( factory.PrimitiveType );
    record.VSClipPlaneCount = factory.VSClipPlaneCount;
    record.PSSpecDwords[0] = factory.PSSpecDwords[0];
    record.PSSpecDwords[1] = factory.PSSpecDwords[1];
    record.LampsEnabled = factory.LampsEnabled;

    record.InputBindings = factory.pVertexLayout->InputBindings;
    for ( const GPUVertexLayoutSemantic &semantic :
          factory.pVertexLayout->Semantics ) {
        record.Semantics.push_back( { semantic.Binding, semantic.Offset,
                                      semantic.Format, semantic.Usage,
                                      semantic.UsageIndex } );
    }

    record.RasterState = factory.RasterState;
    record.DepthStencil = factory.DepthStencil;
    record.SampleCount = factory.MultisampleState.rasterizationSamples;
    record.BlendAttachments.assign( factory.BlendState.pAttachments,
                                    factory.BlendState.pAttachments +
                                        factory.BlendState.attachmentCount );
    memcpy( record.BlendConstants, factory.BlendState.blendConstants,
            sizeof( record.BlendConstants ) );
    record.RenderPass = renderPassDesc;

    recordIndexes.insert( std::make_pair( psoHashcode, records.size() ) );
    records.push_back( std::move( record ) );
    bDirty = true;
}

void PSOLibrary::discard( const uint64_t psoHashcode ) {
    auto it = recordIndexes.find( psoHashcode );
    if ( it == recordIndexes.end() ) {
        return;
    }

    const size_t index = it->second;
    recordIndexes.erase( it );
    if ( index != records.size() - 1 ) {
        records[index] = std::move( records.back() );
        recordIndexes[records[index].Hashcode] = index;
    }
    records.pop_back();
    bDirty = true;
}

void PSOLibrary::rehash( const uint64_t oldHashcode,
                         const uint64_t newHashcode ) {
    auto it = recordIndexes.find( oldHashcode );
    if ( it == recordIndexes.end() ) {
        return;
    }

    if ( recordIndexes.find( newHashcode ) != recordIndexes.end() ) {
        discard( oldHashcode );
        return;
    }

    const size_t index = it->second;
    recordIndexes.erase( it );
    records[index].Hashcode = newHashcode;
    recordIndexes[newHashcode] = index;
    bDirty = true;
}

void PSOLibrary::ApplyRecord( const PSORecord &record,
                              PSOFactory &factory,
                              GPUShader *pVertexShader,
                              GPUShader *pPixelShader,
                              GPUVertexLayout *pVertexLayout,
                              const CachedRenderPass &renderPass ) {
    factory.setShader( eShaderType::ST_Vertex, pVertexShader );
    factory.setShader( eShaderType::ST_Pixel, pPixelShader );
    factory.pVertexLayout = pVertexLayout;
    factory.PrimitiveType = static_cast<ePrimitiveType>( record.PrimitiveType );
    factory.VSClipPlaneCount = record.VSClipPlaneCount;
    factory.PSSpecDwords[0] = record.PSSpecDwords[0];
    factory.PSSpecDwords[1] = record.PSSpecDwords[1];
    factory.LampsEnabled = record.LampsEnabled;

    factory.RasterState = record.RasterState;
    factory.DepthStencil = record.DepthStencil;
    factory.MultisampleState.rasterizationSamples = record.SampleCount;
    factory.BlendState.attachmentCount =
        static_cast<uint32_t>( record.BlendAttachments.size() );
    factory.BlendState.pAttachments = record.BlendAttachments.data();
    memcpy( factory.BlendState.blendConstants, record.BlendConstants,
            sizeof( record.BlendConstants ) );

    factory.renderPass = renderPass.Instance;
    factory.renderPassHashcode = renderPass.Hashcode;
    factory.bDirty = true;
}
#endif
