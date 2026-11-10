

#if OTDU_VULKAN
#include "shared.h"
#include <unordered_map>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <thread>
#include "render_device.h"
#include "core/arg_parser.h"
#include "filesystem/gs_file.h"
#include "render/2db.h"
#include "render/render_target.h"
#include "render/shaders/shader_binary_cache.h"
#include "render/shaders/shader_table_entry.h"
#include "render/state_cache.h"
#include "system/ed_sys.h"

#if defined( OTDU_USE_SDL )
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#endif

#define VK_NO_PROTOTYPES 1
#ifdef OTDU_WIN
#define VK_USE_PLATFORM_WIN32_KHR 1
#elif defined( OTDU_MACOS )
#define VK_USE_PLATFORM_MACOS_MVK 1
#elif defined( OTDU_UNIX )
#define VK_USE_PLATFORM_XLIB_KHR 1
#endif

#define VOLK_IMPLEMENTATION 1
#include "volk.h"

#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION 1
#include "vk_mem_alloc.h"

// TODO: FIXME: Remove once A/B testing for draw batching is done
static bool gDisableDrawBatching = false;
static CmdLineArg CmdLineArgNoDrawBatching( "nodrawbatching",
                                            []( const char *pArg ) {
                                                gDisableDrawBatching = true;
                                            } );

// TODO: FIXME: Remove once placement has been validated on other
// configurations (Mac, AMD/Intel, UMA)
static bool gAutoBufferPlacement = false;
static CmdLineArg CmdLineArgAutoBufferPlacement( "vk_auto_buffer_placement",
                                                 []( const char *pArg ) {
                                                     gAutoBufferPlacement =
                                                         true;
                                                 } );

VmaMemoryUsage GetPerFrameBufferMemoryUsage() {
    return gAutoBufferPlacement ? VMA_MEMORY_USAGE_AUTO
                                : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
}

void LogBufferPlacement( VmaAllocator allocator,
                         VmaAllocation allocation,
                         const char *pCategory ) {
    static std::mutex loggedCategoriesLock;
    static std::unordered_set<std::string> loggedCategories;
    {
        std::lock_guard<std::mutex> lock( loggedCategoriesLock );
        if ( !loggedCategories.insert( pCategory ).second ) {
            return;
        }
    }

    VmaAllocationInfo info = {};
    vmaGetAllocationInfo( allocator, allocation, &info );

    VkMemoryPropertyFlags flags = 0;
    vmaGetMemoryTypeProperties( allocator, info.memoryType, &flags );

    OTDU_LOG_DEBUG(
        "%s buffers placed in memory type %u (%s%s%s)\n", pCategory,
        info.memoryType,
        ( flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) ? "device " : "",
        ( flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) ? "host " : "",
        ( flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ) ? "cached" : "" );
}

// Per frame slot (VkDrawIndexedIndirectCommand)
static constexpr uint32_t kMaxIndirectDrawsPerFrame = 32768u;
static constexpr VkDeviceSize kTransientRingSize = 4ull * 1024ull * 1024ull;

static bool gVulkanEnableValidationLayer = false;
static CmdLineArg CmdLineArgVulkanValidationLayer(
    "vk_enable_validation_layer",
    []( const char *pArg ) { gVulkanEnableValidationLayer = true; } );

// OTDU_ADDITION: -vk_sync_validation: the validation layer also reports
// synchronization hazards (missing barriers); implies the validation layer
static bool gVulkanEnableSyncValidation = false;
static CmdLineArg CmdLineArgVulkanSyncValidation(
    "vk_sync_validation", []( const char *pArg ) {
        gVulkanEnableValidationLayer = true;
        gVulkanEnableSyncValidation = true;
    } );

#ifdef OTDU_MACOS
static constexpr const VkFormat kDepth24Stencil8 =
    VK_FORMAT_D32_SFLOAT_S8_UINT; // VF_D24S8
static constexpr const VkFormat kDepth24Stencil8Float =
    VK_FORMAT_D32_SFLOAT_S8_UINT; // VF_D24S8F
#else
static constexpr const VkFormat kDepth24Stencil8 =
    VK_FORMAT_D24_UNORM_S8_UINT; // VF_D24S8
static constexpr const VkFormat kDepth24Stencil8Float =
    VK_FORMAT_D32_SFLOAT_S8_UINT; // VF_D24S8F
#endif

static constexpr const VkFormat kDepth16 = VK_FORMAT_D16_UNORM; // VF_D16

static constexpr size_t kShaderSpecsUBOSize = 5 * sizeof( uint32_t );

static constexpr const VkFormat kViewFormatLUT[eViewFormat::VF_Count] = {
    VK_FORMAT_R8G8B8_SINT,              // VF_R8G8B8
    VK_FORMAT_B8G8R8A8_UNORM,           // VF_R8G8B8A8
    VK_FORMAT_B8G8R8A8_UNORM,           // VF_X8R8G8B8
    VK_FORMAT_B8G8R8A8_UNORM,           // VF_A8R8G8B8
    VK_FORMAT_R5G6B5_UNORM_PACK16,      // VF_R5G6B5
    VK_FORMAT_A1R5G5B5_UNORM_PACK16,    // VF_A1R5G5B5
    VK_FORMAT_R8_UNORM,                 // VF_A8
    VK_FORMAT_UNDEFINED,                // VF_A8R3G3B2
    VK_FORMAT_A2R10G10B10_UNORM_PACK32, // VF_A2R10G10B10
    VK_FORMAT_R16G16_UNORM,             // VF_G16R16
    VK_FORMAT_R16G16B16A16_UNORM,       // VF_A16B16G16R16
    VK_FORMAT_R8_UNORM,                 // VF_L8
    VK_FORMAT_R16_UNORM,                // VF_L16
    VK_FORMAT_R8G8_SNORM,               // VF_V8U8
    VK_FORMAT_R8G8B8A8_SNORM,           // VF_Q8W8V8U8
    VK_FORMAT_R16G16_SNORM,             // VF_V16U16
    VK_FORMAT_R16G16B16A16_SNORM,       // VF_Q16W16V16U16
    VK_FORMAT_R16_SFLOAT,               // VF_R16F
    VK_FORMAT_R16G16_SFLOAT,            // VF_G16FR16F
    VK_FORMAT_R16G16B16A16_SFLOAT,      // VF_A16FB16FG16FR16F
    VK_FORMAT_R32_SFLOAT,               // VF_R32F
    VK_FORMAT_R32G32_SFLOAT,            // VF_G32FR32F
    VK_FORMAT_R32G32B32A32_SFLOAT,      // VF_A32FB32FG32FR32F
    VK_FORMAT_BC1_RGBA_UNORM_BLOCK,     // VF_DXT1
    VK_FORMAT_BC2_UNORM_BLOCK,          // VF_DXT2 (= BC2, premultiplied DXT3)
    VK_FORMAT_BC2_UNORM_BLOCK,          // VF_DXT3 (= BC2, explicit 4-bit alpha)
    VK_FORMAT_BC3_UNORM_BLOCK,          // VF_DXT4 (= BC3, premultiplied DXT5)
    VK_FORMAT_BC3_UNORM_BLOCK,          // VF_DXT5 (= BC3, interpolated alpha)

    // TODO: Need to figure out what those are (sRGB formats? linear formats?)
    VK_FORMAT_R8G8B8_SINT,              // VF_R8G8B8
    VK_FORMAT_B8G8R8A8_UNORM,           // VF_R8G8B8A8
    VK_FORMAT_B8G8R8A8_UNORM,           // VF_X8R8G8B8
    VK_FORMAT_B8G8R8A8_UNORM,           // VF_A8R8G8B8
    VK_FORMAT_R5G6B5_UNORM_PACK16,      // VF_R5G6B5
    VK_FORMAT_A1R5G5B5_UNORM_PACK16,    // VF_A1R5G5B5
    VK_FORMAT_R8_UNORM,                 // VF_A8
    VK_FORMAT_UNDEFINED,                // VF_A8R3G3B2
    VK_FORMAT_A2R10G10B10_UNORM_PACK32, // VF_A2R10G10B10
    VK_FORMAT_R16G16_UNORM,             // VF_G16R16
    VK_FORMAT_R16G16B16A16_UNORM,       // VF_A16B16G16R16
    VK_FORMAT_R8_UNORM,                 // VF_L8
    VK_FORMAT_R16_UNORM,                // VF_L16
    VK_FORMAT_R8G8_SNORM,               // VF_V8U8
    VK_FORMAT_R8G8B8A8_SNORM,           // VF_Q8W8V8U8
    VK_FORMAT_R16G16_SNORM,             // VF_V16U16
    VK_FORMAT_R16G16B16A16_SNORM,       // VF_Q16W16V16U16
    VK_FORMAT_R16_SFLOAT,               // VF_R16F
    VK_FORMAT_R16G16_SFLOAT,            // VF_G16FR16F
    VK_FORMAT_R16G16B16A16_SFLOAT,      // VF_A16FB16FG16FR16F
    VK_FORMAT_R32_SFLOAT,               // VF_R32F
    VK_FORMAT_R32G32_SFLOAT,            // VF_G32FR32F
    VK_FORMAT_R32G32B32A32_SFLOAT,      // VF_A32FB32FG32FR32F
    VK_FORMAT_BC1_RGBA_UNORM_BLOCK,     // VF_SRGB_DXT1
    VK_FORMAT_BC2_UNORM_BLOCK, // VF_SRGB_DXT2 (= BC2, premultiplied DXT3)
    VK_FORMAT_BC2_UNORM_BLOCK, // VF_SRGB_DXT3 (= BC2, explicit 4-bit alpha)
    VK_FORMAT_BC3_UNORM_BLOCK, // VF_SRGB_DXT4 (= BC3, premultiplied DXT5)
    VK_FORMAT_BC3_UNORM_BLOCK, // VF_SRGB_DXT5 (= BC3, interpolated alpha)
    VK_FORMAT_UNDEFINED,       // Unused
    VK_FORMAT_UNDEFINED,       // Unused
    VK_FORMAT_UNDEFINED,       // Unused
    VK_FORMAT_UNDEFINED,       // Unused
    VK_FORMAT_UNDEFINED,       // Unused
    VK_FORMAT_UNDEFINED,       // VF_ATI2
    VK_FORMAT_R8G8B8A8_UNORM,  // VF_R8G8B8A8_UINT
};

static float HalfToFloat( const uint16_t value ) {
    const uint32_t sign = uint32_t( value & 0x8000u ) << 16;
    const uint32_t exponent = ( value >> 10 ) & 0x1fu;
    const uint32_t mantissa = value & 0x3ffu;

    uint32_t bits = sign;
    if ( exponent == 0x1fu ) {
        bits |= 0x7f800000u | ( mantissa << 13 );
    } else if ( exponent != 0 ) {
        bits |= ( ( exponent + 127 - 15 ) << 23 ) | ( mantissa << 13 );
    } else if ( mantissa != 0 ) {
        // Denormal: renormalize
        uint32_t shift = 0;
        uint32_t m = mantissa;
        while ( ( m & 0x400u ) == 0 ) {
            m <<= 1;
            shift++;
        }
        bits |= ( ( 127 - 15 + 1 - shift ) << 23 ) | ( ( m & 0x3ffu ) << 13 );
    }

    float result = 0.0f;
    memcpy( &result, &bits, sizeof( result ) );
    return result;
}

static uint32_t GetVulkanFormatTexelSize( const VkFormat format ) {
    switch ( format ) {
    case VK_FORMAT_R8_UNORM:
        return 1;
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R16_SFLOAT:
        return 2;
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SNORM:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT:
        return 4;
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SNORM:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT:
        return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        return 0;
    }
}

// True if a 2DB texture can be rendered to directly (i.e. without going
// through a dedicated render target + resolveMSAA copy). Restricted to the
// single mip 2D case so that the default image view can be bound as a
// framebuffer attachment.
static bool IsRenderableSurface( const Texture *pTexture,
                                 const FormatCapabilities &caps ) {
    const bool bRenderTarget = ( pTexture->Flags >> 9 & 1 ) != 0;
    const bool bCubemap = ( pTexture->Flags >> 6 & 1 ) != 0;
    const bool bVolume = ( pTexture->Flags >> 0xb & 1 ) != 0;
    const bool bAutoGenMips = ( pTexture->Flags >> 0xe & 1 ) != 0;

    return bRenderTarget && !bCubemap && !bVolume && !bAutoGenMips &&
           pTexture->NumMipmap <= 1 && pTexture->Depth <= 1 &&
           !IsDepthStencilFormat( pTexture->Format ) && caps.bSupported;
}

static constexpr const VkFormat
    kVertexAttributeFormats[eVertexAttributeFormat::VAF_Count] = {
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_UNDEFINED,
        VK_FORMAT_A2B10G10R10_SNORM_PACK32,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R16G16_SNORM,
        VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_UNDEFINED,
        VK_FORMAT_R8G8B8A8_USCALED,
        VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_A2B10G10R10_SNORM_PACK32,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SNORM,
        VK_FORMAT_R16G16B16A16_SNORM };

static VkFormat GetVertexAttributeVkFormat( eVertexAttributeFormat type ) {
    if ( type == eVertexAttributeFormat::VAF_INSTANCE_R32G32B32A32_SFLOAT ) {
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return ( type < eVertexAttributeFormat::VAF_Count )
               ? kVertexAttributeFormats[type]
               : VK_FORMAT_UNDEFINED;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
VulkanDebugCallback( VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                     VkDebugUtilsMessageTypeFlagsEXT,
                     const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
                     void * ) {
    if ( 1268585879 == callbackData->messageIdNumber ||
         1397510317 == callbackData->messageIdNumber ) {
        return VK_TRUE;
    }

    // OTDU_ADDITION: each distinct message (handles and numbers aside) at
    // most twice and each message id at most 40 times (a sweep stays
    // readable), and break only under a debugger (else the first error ends
    // the run)
    const char *pMessage =
        ( callbackData->pMessage != nullptr ) ? callbackData->pMessage : "";
    std::string normalized;
    for ( const char *pChar = pMessage; *pChar != '\0'; pChar++ ) {
        if ( pChar[0] == '0' && pChar[1] == 'x' ) {
            pChar += 2;
            while ( isxdigit( static_cast<unsigned char>( *pChar ) ) ) {
                pChar++;
            }
            pChar--;
            normalized += '#';
        } else if ( isdigit( static_cast<unsigned char>( *pChar ) ) ) {
            if ( normalized.empty() || normalized.back() != '#' ) {
                normalized += '#';
            }
        } else {
            normalized += *pChar;
        }
    }
    static std::mutex sLock;
    static std::unordered_map<std::string, uint32_t> sTextCounts;
    static std::unordered_map<int32_t, uint32_t> sIdCounts;
    {
        std::lock_guard<std::mutex> lock( sLock );
        if ( ++sTextCounts[normalized] > 2 ||
             ++sIdCounts[callbackData->messageIdNumber] > 40 ) {
            return VK_FALSE;
        }
    }

    OTDU_LOG_ERROR(
        "%s%s\n",
        ( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) != 0
            ? ""
            : "(warning) ",
        pMessage );
    // The named objects (render targets carry their names)
    for ( uint32_t i = 0; i < callbackData->objectCount; i++ ) {
        const VkDebugUtilsObjectNameInfoEXT &object =
            callbackData->pObjects[i];
        if ( object.pObjectName != nullptr ) {
            OTDU_LOG_ERROR( "    object %u: type %d '%s'\n", i,
                            static_cast<int32_t>( object.objectType ),
                            object.pObjectName );
        }
    }
    if ( ( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) != 0 &&
         IsDebuggerPresent() ) {
        OTDU_TRIGGER_BREAKPOINT;
    }

    return VK_FALSE;
}

// Validation settings: to fine tune what is checked
struct ValidationSettings {
    VkBool32 fine_grained_locking{ VK_TRUE };
    VkBool32 validate_core{ VK_TRUE };
    VkBool32 check_image_layout{ VK_TRUE };
    VkBool32 check_command_buffer{ VK_TRUE };
    VkBool32 check_object_in_use{ VK_TRUE };
    VkBool32 check_query{ VK_TRUE };
    VkBool32 check_shaders{ VK_TRUE };
    VkBool32 check_shaders_caching{ VK_TRUE };
    VkBool32 unique_handles{ VK_TRUE };
    VkBool32 object_lifetime{ VK_TRUE };
    VkBool32 stateless_param{ VK_TRUE };
    VkBool32 validate_sync{ VK_FALSE };
    std::vector<const char *> debug_action{
        "VK_DBG_LAYER_ACTION_LOG_MSG" }; // "VK_DBG_LAYER_ACTION_DEBUG_OUTPUT",
                                         // "VK_DBG_LAYER_ACTION_BREAK"
    // OTDU_ADDITION: warnings too (e.g. a fragment shader input no vertex
    // shader output writes)
    std::vector<const char *> report_flags{ "error", "warn" };

    VkBaseInStructure *buildPNextChain() {
        layerSettings = std::vector<VkLayerSettingEXT>{
            {layerName, "fine_grained_locking",
             VK_LAYER_SETTING_TYPE_BOOL32_EXT,                                     1, &fine_grained_locking },
            {layerName, "validate_core",         VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &validate_core                                                                                 },
            {layerName, "check_image_layout",    VK_LAYER_SETTING_TYPE_BOOL32_EXT,
             1,                                                                       &check_image_layout   },
            {layerName, "check_command_buffer",
             VK_LAYER_SETTING_TYPE_BOOL32_EXT,                                     1, &check_command_buffer },
            {layerName, "check_object_in_use",
             VK_LAYER_SETTING_TYPE_BOOL32_EXT,                                     1, &check_object_in_use  },
            {layerName, "check_query",           VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &check_query                                                                                   },
            {layerName, "check_shaders",         VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &check_shaders                                                                                 },
            {layerName, "check_shaders_caching",
             VK_LAYER_SETTING_TYPE_BOOL32_EXT,                                     1, &check_shaders_caching},
            {layerName, "unique_handles",        VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &unique_handles                                                                                },
            {layerName, "object_lifetime",       VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &object_lifetime                                                                               },
            {layerName, "stateless_param",       VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &stateless_param                                                                               },
            {layerName, "debug_action",          VK_LAYER_SETTING_TYPE_STRING_EXT,
             uint32_t( debug_action.size() ),                                         debug_action.data()   },
            {layerName, "report_flags",          VK_LAYER_SETTING_TYPE_STRING_EXT,
             uint32_t( report_flags.size() ),                                         report_flags.data()   },
            {layerName, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &validate_sync},
        };
        layerSettingsCreateInfo.sType =
            VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
        layerSettingsCreateInfo.settingCount = uint32_t( layerSettings.size() );
        layerSettingsCreateInfo.pSettings = layerSettings.data();

        return reinterpret_cast<VkBaseInStructure *>(
            &layerSettingsCreateInfo );
    }

    static constexpr const char *layerName{ "VK_LAYER_KHRONOS_validation" };
    std::vector<VkLayerSettingEXT> layerSettings{};
    VkLayerSettingsCreateInfoEXT layerSettingsCreateInfo{};
};

// Work in conjunction with the above
bool extensionIsAvailable(
    const std::string &name,
    const std::vector<VkExtensionProperties> &extensions ) {
    for ( auto &ext : extensions ) {
        if ( name == ext.extensionName )
            return true;
    }
    return false;
}

inline uint32_t GetNumMipMaps( Texture *pTexture ) {
    return static_cast<uint32_t>(
        floor( log2( Max( pTexture->Width, pTexture->Height ) ) ) + 1 );
}

bool HasStencilBits( VkFormat fmt ) {
    return fmt == kDepth24Stencil8 || fmt == kDepth24Stencil8Float;
}

VkFormat GetPortableAttachmentFormat( const VkFormat format ) {
    return ( format == kDepth24Stencil8 ) ? VK_FORMAT_D24_UNORM_S8_UINT
                                          : format;
}

VkFormat GetNativeAttachmentFormat( const VkFormat format ) {
    return ( format == VK_FORMAT_D24_UNORM_S8_UINT ) ? kDepth24Stencil8
                                                     : format;
}

inline bool IsVulkanDepthStencilFormat( VkFormat fmt ) {
    return fmt == kDepth16 || fmt == kDepth24Stencil8 ||
           fmt == kDepth24Stencil8Float;
}

RenderDevice::RenderDevice()
    : instance( VK_NULL_HANDLE ),
      physicalDevice( VK_NULL_HANDLE ),
      physicalDeviceIndex( 0u ),
      device( VK_NULL_HANDLE )
#if OTDU_DEVBUILD
      ,
      debugCallback( VK_NULL_HANDLE )
#endif
      ,
      surface( VK_NULL_HANDLE ),
      frameIndex( 0 ),
      absoluteFrameIndex( 0 ),
      bufferFormatSupport( 0u ),
      framebufferInfos{},
      swapchainImageFormat( VK_FORMAT_UNDEFINED ),
      time( 0.0f ),
      activeColorMask( VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT ),
      pIndexBuffer( nullptr ),
      commandBuffer( VK_NULL_HANDLE ),
      pDepthStencilAttachment( nullptr ),
      bDepthStencilAttachmentValid( false ),
      bIsMainRTUsingMSAA( false ),
      bDepthStencilEnabled( true ),
      bRecordingCommands( false ),
      bNeedRecreateSwapchain( false ),
      bVSyncEnabled( false ),
      bNeedBackbufferClear( false ),
      bHDROutputEnabled( false ),
      bSwapchainColorSpaceSupported( false ),
      bHDRMetadataSupported( false ),
      bHDROutputSupported( false ),
      hdrPaperWhiteNits( kDefaultHDRPaperWhiteNits ),
      hdrPeakNits( kAutoHDRPeakNits ),
      detectedPeakNits( 0.0f ),
      hdrHighlightStrength( kDefaultHDRHighlightStrength ),
      activeOutputTransfer( eOutputTransfer::OT_SDR ),
      activeClearColor( gColorBlackNoAlpha ),
      lastPassColorAttachments{},
      pLastPassDepthStencil( nullptr ),
      boundPSO( VK_NULL_HANDLE ),
      activeRenderPass( VK_NULL_HANDLE ),
      activeFramebuffer( VK_NULL_HANDLE ) {
    deviceExtensions.push_back( VK_KHR_SWAPCHAIN_EXTENSION_NAME );
    instanceExtensions.push_back(
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME );

    memset( formatCaps, 0,
            sizeof( FormatCapabilities ) * eViewFormat::VF_Count );

    for ( uint32_t i = 0; i < 0x10; i++ ) {
        vertexBuffers[i] = VK_NULL_HANDLE;
        vertexBufferOffsets[i] = 0;
    }

    memset( pFramebufferAttachments, 0,
            sizeof( GPUTexture * ) * kMaxSimultaneousRT );
    memset( bFramebufferAttachmentValid, 0,
            sizeof( bool ) * kMaxSimultaneousRT );

    memset( boundSamplers, 0, sizeof( int32_t ) * 0x8 );

    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        framePools[i] = VK_NULL_HANDLE;
        commandBuffers[i] = VK_NULL_HANDLE;
        presentPools[i] = VK_NULL_HANDLE;
        presentCommandBuffers[i] = VK_NULL_HANDLE;
    }

    resetRecordedBindings();
    activeFramebufferKey = {};
}

RenderDevice::~RenderDevice() {
    waitForIdle();
    stopSubmitThread();

    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        vkDestroyCommandPool( device, framePools[i], nullptr );
        vkDestroyCommandPool( device, presentPools[i], nullptr );
    }
    for ( PreFrameCommands &commands : preFrameCommands ) {
        vkDestroyCommandPool( device, commands.Pool, nullptr );
    }
    destroyIndirectDrawBuffers();
    destroyTransientRings();
    for ( VkDescriptorPool &pool : computeDescriptorPools ) {
        if ( pool != VK_NULL_HANDLE ) {
            vkDestroyDescriptorPool( device, pool, nullptr );
            pool = VK_NULL_HANDLE;
        }
    }
    vkDestroyImageView( device, fallbackSRVView, nullptr );
    vmaDestroyImage( allocator, fallbackSRVImage, fallbackSRVAllocation );

    savePipelineCache();
    savePSOLibrary();
    releasePrebuildResources();
    psoFactory.releaseCachedPSOs( device );
    vkDestroyPipelineCache( device, psoCache, nullptr );
    renderPassManager.releaseCachedRenderPass( device );
    samplerHeap.destroy( device );
    resourceBinder.destroy( device, allocator );
    gammaRampPass.releaseSwapchainResources( device );
    gammaRampPass.destroy( device, allocator );

    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        vkDestroySemaphore( device, nextImageSemaphore[i], nullptr );
    }

    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        vkDestroyFence( device, inFlightFence[i], nullptr );
    }

    releaseSwapchain();
    releaseFramebufferCache();

#if defined( OTDU_MACOS )
    if ( pFanEmulationIndexBuffer != nullptr ) {
        destroyBuffer( pFanEmulationIndexBuffer );
        pFanEmulationIndexBuffer = nullptr;
    }
#endif
    if ( pNullVertexBuffer != nullptr ) {
        destroyBuffer( pNullVertexBuffer );
        pNullVertexBuffer = nullptr;
    }
    releaseRetireList( pendingRetire );
    releaseRetireList( nextFrameRetire );

    vmaDestroyAllocator( allocator );
    vkDestroyDevice( device, nullptr );

    physicalDevice = VK_NULL_HANDLE;
    physicalDevices.clear();
    deviceExtensions.clear();

    vkDestroySurfaceKHR( instance, surface, nullptr );

#if OTDU_DEVBUILD
    if ( gVulkanEnableValidationLayer && vkCreateDebugUtilsMessengerEXT ) {
        vkDestroyDebugUtilsMessengerEXT( instance, debugCallback, nullptr );
    }
#endif

    vkDestroyInstance( instance, nullptr );
}

QueueInfo RenderDevice::getQueue( VkQueueFlagBits flags ) const {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2( physicalDevice,
                                               &queueFamilyCount, nullptr );

    VkQueueFamilyProperties2 defaultProp = {};
    defaultProp.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    std::vector<VkQueueFamilyProperties2> queueFamilies( queueFamilyCount,
                                                         defaultProp );
    vkGetPhysicalDeviceQueueFamilyProperties2(
        physicalDevice, &queueFamilyCount, queueFamilies.data() );

    QueueInfo queueInfo;
    for ( uint32_t i = 0; i < queueFamilies.size(); i++ ) {
        if ( queueFamilies[i].queueFamilyProperties.queueFlags & flags ) {
            queueInfo.familyIndex = i;
            queueInfo.queueIndex = 0; // A second graphic queue could be index 1
                                      // (need logic to find the right one)
            // m_queueInfo.queue = After creating the logical device
            break;
        }
    }
    return queueInfo;
}

static std::vector<VkExtensionProperties>
EnumerateDeviceExtensions( VkPhysicalDevice physicalDevice ) {
    std::vector<VkExtensionProperties> availableDeviceExtensions;
    uint32_t count = 0u;
    vkEnumerateDeviceExtensionProperties( physicalDevice, nullptr, &count,
                                          nullptr );
    availableDeviceExtensions.resize( count );
    vkEnumerateDeviceExtensionProperties( physicalDevice, nullptr, &count,
                                          availableDeviceExtensions.data() );
    return availableDeviceExtensions;
}

static constexpr uint32_t kMinRequiredClipDistances = 6u; // d3d9_abi.glsl

static bool QueryDeviceCaps( VkPhysicalDevice physicalDevice,
                             VulkanDeviceCaps &outCaps,
                             std::string &outMissing ) {
    outCaps = {};

    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties( physicalDevice, &properties );
    outCaps.ApiVersion = properties.apiVersion;
    if ( properties.apiVersion < VK_API_VERSION_1_2 ) {
        outMissing = "Vulkan 1.2";
        return false;
    }

    const std::vector<VkExtensionProperties> extensions =
        EnumerateDeviceExtensions( physicalDevice );
    if ( !extensionIsAvailable( VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                extensions ) ) {
        outMissing = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        return false;
    }

    const bool bVulkan13 = properties.apiVersion >= VK_API_VERSION_1_3;

    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demoteFeatures =
        {};
    demoteFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT edsFeatures = {};
    edsFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 features = {};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features12;

    const bool bDemoteExtension = extensionIsAvailable(
        VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME, extensions );
    const bool bEDSExtension = extensionIsAvailable(
        VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME, extensions );
    if ( bVulkan13 ) {
        features12.pNext = &features13;
    } else {
        void **ppNext = &features12.pNext;
        if ( bDemoteExtension ) {
            *ppNext = &demoteFeatures;
            ppNext = &demoteFeatures.pNext;
        }
        if ( bEDSExtension ) {
            *ppNext = &edsFeatures;
        }
    }
    vkGetPhysicalDeviceFeatures2( physicalDevice, &features );

    VkPhysicalDeviceVulkan12Properties properties12 = {};
    properties12.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
    VkPhysicalDeviceProperties2 properties2 = {};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &properties12;
    vkGetPhysicalDeviceProperties2( physicalDevice, &properties2 );

    // Required
    if ( !features.features.shaderClipDistance ||
         properties.limits.maxClipDistances < kMinRequiredClipDistances ) {
        outMissing = "shaderClipDistance";
        return false;
    }
    if ( !features12.runtimeDescriptorArray ||
         !features12.descriptorBindingPartiallyBound ||
         !features12.descriptorBindingSampledImageUpdateAfterBind ||
         properties12.maxPerStageDescriptorUpdateAfterBindSamplers <
             kSamplerHeapCapacity ) {
        outMissing = "descriptor indexing (update after bind samplers)";
        return false;
    }
    const bool bDemote = bVulkan13
                             ? features13.shaderDemoteToHelperInvocation
                             : demoteFeatures.shaderDemoteToHelperInvocation;
    if ( !bDemote ) {
        outMissing = "shaderDemoteToHelperInvocation";
        return false;
    }
    outCaps.bDemoteExtension = !bVulkan13;

    // Optional
    outCaps.bExtendedDynamicState =
        bVulkan13 || ( bEDSExtension && edsFeatures.extendedDynamicState );
    outCaps.bExtendedDynamicStateExtension =
        !bVulkan13 && outCaps.bExtendedDynamicState;
    outCaps.bDepthClamp = features.features.depthClamp;
    outCaps.bFillModeNonSolid = features.features.fillModeNonSolid;
    outCaps.bSamplerAnisotropy = features.features.samplerAnisotropy;
    outCaps.MaxSamplerAnisotropy = outCaps.bSamplerAnisotropy
                                       ? properties.limits.maxSamplerAnisotropy
                                       : 1.0f;
    outCaps.bMaintenance4 = bVulkan13 && features13.maintenance4;
    outCaps.bHDRMetadata =
        extensionIsAvailable( VK_EXT_HDR_METADATA_EXTENSION_NAME, extensions );
    outCaps.bPortabilitySubset =
        extensionIsAvailable( "VK_KHR_portability_subset", extensions );
    outCaps.bUpdateUnusedWhilePending =
        features12.descriptorBindingUpdateUnusedWhilePending;
    outCaps.bMultiDrawIndirect = features.features.multiDrawIndirect &&
                                 features.features.drawIndirectFirstInstance;
    outCaps.MaxDrawIndirectCount = properties.limits.maxDrawIndirectCount;
    return true;
}

static int32_t GetDeviceTypeScore( const VkPhysicalDeviceType type ) {
    switch ( type ) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 4;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 3;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 1;
    default:
        return 0;
    }
}

#if defined( OTDU_MACOS ) && defined( OTDU_USE_SDL )
static void SetupBundledVulkanDriver() {
    if ( SDL_getenv( "VK_DRIVER_FILES" ) != nullptr ||
         SDL_getenv( "VK_ICD_FILENAMES" ) != nullptr ) {
        return;
    }

    const char *pBasePath = SDL_GetBasePath();
    if ( pBasePath == nullptr ) {
        return;
    }

    const std::filesystem::path manifestPath =
        std::filesystem::path( pBasePath ) / "vulkan" / "icd.d" /
        "MoltenVK_icd.json";
    if ( !std::filesystem::exists( manifestPath ) ) {
        return;
    }

    const std::string manifest = manifestPath.string();
    SDL_setenv_unsafe( "VK_DRIVER_FILES", manifest.c_str(), 0 );
    SDL_setenv_unsafe( "VK_ICD_FILENAMES", manifest.c_str(),
                       0 ); // Loaders older than 1.3.207
    OTDU_LOG_DEBUG( "Using bundled Vulkan driver manifest '%s'\n",
                    manifest.c_str() );
}
#endif

void RenderDevice::initialize() {
#if defined( OTDU_MACOS ) && defined( OTDU_USE_SDL )
    SetupBundledVulkanDriver();
#endif
    OTDU_ASSERT_FATAL( volkInitialize() == VK_SUCCESS );
    OTDU_ASSERT_FATAL( createVulkanInstance() );
    createDebugCallback();
    OTDU_ASSERT_FATAL( selectPhysicalDevice() );
    OTDU_ASSERT_FATAL( createDevice() );
    createVMAAllocator();
    createPipelineCache();
    OTDU_ASSERT_FATAL( createSwapchain() );

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        VkResult semaphoreCreation = vkCreateSemaphore(
            device, &semaphoreInfo, nullptr, &nextImageSemaphore[i] );
        OTDU_ASSERT( semaphoreCreation == VK_SUCCESS );
    }

    renderPassManager.initialize();
    samplerHeap.initialize( device, deviceCaps.MaxSamplerAnisotropy,
                            deviceCaps.bUpdateUnusedWhilePending );
    VkDeviceSize uboAlignment =
        deviceProperties.properties.limits.minUniformBufferOffsetAlignment;
    resourceBinder.initialize( device, allocator, uboAlignment );
    psoFactory.bExtendedDynamicState = deviceCaps.bExtendedDynamicState;
    psoFactory.initialize( device, samplerHeap, resourceBinder );
    gammaRampPass.initialize( device, allocator, psoCache );
    createOutputPassResources();

    VkCommandPoolCreateInfo poolInfos = {};
    poolInfos.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfos.pNext = nullptr;
    poolInfos.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfos.queueFamilyIndex = queues[0].familyIndex;

    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.pNext = nullptr;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        VkResult poolCreation =
            vkCreateCommandPool( device, &poolInfos, nullptr, &framePools[i] );
        OTDU_ASSERT( poolCreation == VK_SUCCESS );
        cmdAllocInfo.commandPool = framePools[i];
        vkAllocateCommandBuffers( device, &cmdAllocInfo, &commandBuffers[i] );

        poolCreation = vkCreateCommandPool( device, &poolInfos, nullptr,
                                            &presentPools[i] );
        OTDU_ASSERT( poolCreation == VK_SUCCESS );
        cmdAllocInfo.commandPool = presentPools[i];
        vkAllocateCommandBuffers( device, &cmdAllocInfo,
                                  &presentCommandBuffers[i] );
    }

    for ( PreFrameCommands &commands : preFrameCommands ) {
        VkResult poolCreation =
            vkCreateCommandPool( device, &poolInfos, nullptr, &commands.Pool );
        OTDU_ASSERT( poolCreation == VK_SUCCESS );
        cmdAllocInfo.commandPool = commands.Pool;
        cmdAllocInfo.commandBufferCount = PFCL_Count;
        vkAllocateCommandBuffers( device, &cmdAllocInfo,
                                  commands.CommandBuffers );
    }

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.pNext = nullptr;
    fenceInfo.flags = 0;
    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        VkResult fenceResult =
            vkCreateFence( device, &fenceInfo, nullptr, &inFlightFence[i] );
        OTDU_ASSERT( fenceResult == VK_SUCCESS );
    }

    framebufferInfos = {};
    framebufferInfos.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfos.pNext = nullptr;
    framebufferInfos.attachmentCount = 0;

    // TODO: We could technically enable this; but this would diverge from the
    // original p-code (and potentially introduce unecessary bugs/regressions)
#if 0
    // Vulkan always supports hardware instancing (vertex stream frequency) and
    // point sprites — set capability bits so billboard/instancing materials
    // take the hardware path instead of the CPU fallback.
    gHWInstancingSupport = 3;
#endif

    bDrawBatching = !gDisableDrawBatching;
    bMultiDrawBatching = bDrawBatching && deviceCaps.bMultiDrawIndirect &&
                         deviceCaps.MaxDrawIndirectCount > 1;
    if ( bMultiDrawBatching ) {
        createIndirectDrawBuffers();
    }
    createTransientRings();
    OTDU_LOG_DEBUG( "Draw batching: %s (multi draw indirect: %s)\n",
                    bDrawBatching ? "on" : "off",
                    bMultiDrawBatching ? "on" : "off" );

    createFallbackSRV();
    resourceBinder.FallbackSRV = fallbackSRVView;

    startSubmitThread();
}

GPUTexture *RenderDevice::createTexture( Texture *pTexture ) {
    // FUN_00512620
    uint16_t uVar1 = pTexture->Width;
    if ( pTexture->Width < pTexture->Height ) {
        uVar1 = pTexture->Height;
    }
    uint8_t iVar4 = 0;
    for ( ; uVar1 != 0; uVar1 = uVar1 >> 1 ) {
        iVar4 = iVar4 + 1;
    }
    if ( iVar4 < pTexture->NumMipmap ) {
        pTexture->NumMipmap = iVar4;
    }

    if ( ( ( pTexture->Flags >> 0xe & 1 ) != 0 ) && pTexture->NumMipmap <= 1 ) {
        pTexture->NumMipmap = GetNumMipMaps( pTexture );
    }

    bool bTexture2DOrCube = ( pTexture->Flags >> 0xb & 1 ) == 0;
    bool bTexture2D = ( pTexture->Flags >> 6 & 1 ) == 0;

    VkImageCreateInfo imgCreateInfo = {};
    imgCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCreateInfo.pNext = nullptr;
    imgCreateInfo.imageType =
        bTexture2DOrCube ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_3D;
    imgCreateInfo.extent.width = pTexture->Width;
    imgCreateInfo.extent.height = pTexture->Height;
    // TODO: Extend support for modern tex types (i.e. 3D array/2d array/etc.).
    // Current logic is rough mapping from d3d9 to save time)
    if ( bTexture2DOrCube ) {
        imgCreateInfo.extent.depth = 1;
        imgCreateInfo.arrayLayers = Max( 1, pTexture->Depth );
    } else {
        imgCreateInfo.extent.depth = Max( 1, pTexture->Depth );
        imgCreateInfo.arrayLayers = 1;
    }
    imgCreateInfo.mipLevels = pTexture->NumMipmap;
    imgCreateInfo.format = kViewFormatLUT[pTexture->Format];
    imgCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgCreateInfo.usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // Textures are assumed to be read only
    if ( IsRenderableSurface( pTexture, formatCaps[pTexture->Format] ) ) {
        imgCreateInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    imgCreateInfo.samples =
        VK_SAMPLE_COUNT_1_BIT; // Regular textures are always single-sampled

    imgCreateInfo.flags = 0;
    if ( bTexture2DOrCube && !bTexture2D ) {
        imgCreateInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocCreateInfo.priority = 1.0f;

    GPUTexture *renderTarget = new GPUTexture();

    VkResult imageCreationResult = vmaCreateImage(
        allocator, &imgCreateInfo, &allocCreateInfo, &renderTarget->Image,
        &renderTarget->Allocation, nullptr );
    OTDU_ASSERT( imageCreationResult == VK_SUCCESS );

#if OTDU_DEVBUILD
    std::string objName;

    TestDriveString8 hashStr;
    hashStr = pTexture->Hashcode;

    for ( uint32_t j = 0; j < sizeof( uint64_t ); j++ ) {
        if ( hashStr.AsChar[j] == '\0' ) {
            break;
        }

        if ( isprint( (unsigned char)hashStr.AsChar[j] ) ) {
            objName += hashStr.AsChar[j];
        }
    }

    VkDebugUtilsObjectNameInfoEXT nameInfo = {};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = VkObjectType::VK_OBJECT_TYPE_IMAGE;
    nameInfo.objectHandle = (uint64_t)renderTarget->Image;
    nameInfo.pObjectName = objName.data();
    vkSetDebugUtilsObjectNameEXT( device, &nameInfo );
#endif

    renderTarget->ViewFormat = imgCreateInfo.format;
    renderTarget->SampleCount = imgCreateInfo.samples;
    renderTarget->NumMips = imgCreateInfo.mipLevels;
    renderTarget->Depth = imgCreateInfo.arrayLayers;
    renderTarget->Width = pTexture->Width;
    renderTarget->Height = pTexture->Height;
    renderTarget->bNeedMSAAResolve = false;
    renderTarget->CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageViewCreateInfo viewInfos = {};
    viewInfos.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfos.pNext = nullptr;
    if ( bTexture2DOrCube ) {
        viewInfos.viewType =
            bTexture2D ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_CUBE;
    } else {
        viewInfos.viewType = VK_IMAGE_VIEW_TYPE_3D;
    }
    viewInfos.format = imgCreateInfo.format;
    viewInfos.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfos.subresourceRange.baseMipLevel = 0;
    viewInfos.subresourceRange.levelCount = pTexture->NumMipmap;
    viewInfos.subresourceRange.baseArrayLayer = 0;
    viewInfos.subresourceRange.layerCount =
        bTexture2DOrCube ? Max( 1, pTexture->Depth ) : 1;
    viewInfos.image = renderTarget->Image;

    VkResult imageViewCreationResult = vkCreateImageView(
        device, &viewInfos, nullptr, &renderTarget->ImageView );
    OTDU_ASSERT( imageViewCreationResult == VK_SUCCESS );
    renderTarget->ViewId = allocateViewId();

    pTexture->bUploaded = false;
    return renderTarget;
}

GPUTexture *RenderDevice::getSurfaceLevel( const Texture *pTexture ) {
    if ( pTexture == nullptr || pTexture->pTexture == nullptr ||
         !IsRenderableSurface( pTexture, formatCaps[pTexture->Format] ) ) {
        return nullptr;
    }

    return pTexture->pTexture;
}

GPUTexture *
RenderDevice::createRenderTarget( const uint32_t width,
                                  const uint32_t height,
                                  const eViewFormat format,
                                  const eAntiAliasingMethod msaaSampleCount ) {
    const bool bIsDepthStencil = IsDepthStencilFormat( format );

    VkImageCreateInfo imgCreateInfo = {};
    imgCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCreateInfo.pNext = nullptr;
    imgCreateInfo.imageType = VK_IMAGE_TYPE_2D; // Only 2D RT are used AFAIK
    imgCreateInfo.extent.width = width;
    imgCreateInfo.extent.height = height;
    imgCreateInfo.extent.depth = 1;
    imgCreateInfo.mipLevels = 1;
    imgCreateInfo.arrayLayers = 1;
    if ( bIsDepthStencil ) {
        switch ( format ) {
        case eViewFormat::VF_D16:
            imgCreateInfo.format = kDepth16;
            break;
        case eViewFormat::VF_D24S8:
            imgCreateInfo.format = kDepth24Stencil8;
            break;
        case eViewFormat::VF_D24S8F:
            imgCreateInfo.format = kDepth24Stencil8Float;
            break;
        default:
            OTDU_UNIMPLEMENTED;
            imgCreateInfo.format = VK_FORMAT_UNDEFINED;
            break;
        }
    } else {
        imgCreateInfo.format = kViewFormatLUT[format];
    }
    imgCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    if ( bIsDepthStencil ) {
        imgCreateInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    } else {
        imgCreateInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    switch ( msaaSampleCount ) {
    case eAntiAliasingMethod::AAM_MSAA_X2:
        imgCreateInfo.samples = VK_SAMPLE_COUNT_2_BIT;
        break;
    case eAntiAliasingMethod::AAM_MSAA_X4:
        imgCreateInfo.samples = VK_SAMPLE_COUNT_4_BIT;
        break;
    default:
        imgCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        break;
    }

    imgCreateInfo.samples = clampSampleCount( imgCreateInfo.samples );

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocCreateInfo.priority = 1.0f;

    GPUTexture *renderTarget = new GPUTexture();

    VkResult imageCreationResult = vmaCreateImage(
        allocator, &imgCreateInfo, &allocCreateInfo, &renderTarget->Image,
        &renderTarget->Allocation, nullptr );
    OTDU_ASSERT( imageCreationResult == VK_SUCCESS );

#if OTDU_DEVBUILD
    VkDebugUtilsObjectNameInfoEXT nameInfo = {};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = VkObjectType::VK_OBJECT_TYPE_IMAGE;
    nameInfo.objectHandle = (uint64_t)renderTarget->Image;
    nameInfo.pObjectName = "PooledRenderTarget";
    vkSetDebugUtilsObjectNameEXT( device, &nameInfo );
#endif

    renderTarget->ViewFormat = imgCreateInfo.format;
    renderTarget->SampleCount = imgCreateInfo.samples;
    renderTarget->NumMips = imgCreateInfo.mipLevels;
    renderTarget->Depth = imgCreateInfo.arrayLayers;
    renderTarget->Width = width;
    renderTarget->Height = height;
    renderTarget->bNeedMSAAResolve = false;
    renderTarget->CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageViewCreateInfo viewInfos = {};
    viewInfos.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfos.pNext = nullptr;
    viewInfos.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfos.format = imgCreateInfo.format;
    viewInfos.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.subresourceRange.aspectMask =
        bIsDepthStencil
            ? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT )
            : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfos.subresourceRange.baseMipLevel = 0;
    viewInfos.subresourceRange.levelCount = 1;
    viewInfos.subresourceRange.baseArrayLayer = 0;
    viewInfos.subresourceRange.layerCount = 1;

    viewInfos.image = renderTarget->Image;
    VkResult imageViewCreationResult = vkCreateImageView(
        device, &viewInfos, nullptr, &renderTarget->ImageView );
    OTDU_ASSERT( imageViewCreationResult == VK_SUCCESS );
    renderTarget->ViewId = allocateViewId();

    return renderTarget;
}

GPUBuffer *RenderDevice::createBuffer( const struct GPUBufferDesc *desc ) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = desc->Size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.flags = 0;

    // TODO: This is currently extremely simplistic (since TDU1 is a D3D9 game).
    // This could be extended to add new render passes/modernize the game
    // rendering pipeline.
    switch ( desc->BufferType ) {
    case GPUBufferDesc::Type::BT_VertexBuffer:
        bufferInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        break;
    case GPUBufferDesc::Type::BT_IndexBuffer:
        bufferInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        break;
    default:
        OTDU_UNIMPLEMENTED;
        break;
    };
    if ( desc->bStorage ) {
        bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if ( desc->bIndirect ) {
        bufferInfo.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }

    VmaAllocationCreateInfo allocInfo = {};
    // TODO: Static buffers should live in device local memory (uploaded
    // through a staging buffer on unlock)
    allocInfo.usage =
        desc->bDynamic ? GetPerFrameBufferMemoryUsage() : VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    if ( desc->bDeviceLocal ) {
        OTDU_ASSERT( !desc->bDynamic );
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.flags = 0;
    }

    VkBuffer buffer;
    VmaAllocation allocation;
    VkResult opResult = VK_SUCCESS;

    GPUBuffer *pBuffer = new GPUBuffer();

    if ( desc->bDynamic ) {
        for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
            opResult = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo,
                                        &buffer, &allocation, nullptr );
            OTDU_ASSERT_FATAL( opResult == VK_SUCCESS );

            pBuffer->NativeBuffer[i] = buffer;
            pBuffer->Allocation[i] = allocation;
        }
        LogBufferPlacement( allocator, allocation, "Dynamic VB/IB" );

        pBuffer->NumResources = PendingFrameCount;
        pBuffer->ShadowCopy.resize( desc->Size, 0u );
    } else {
        opResult = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &buffer,
                                    &allocation, nullptr );
        OTDU_ASSERT_FATAL( opResult == VK_SUCCESS );

        pBuffer->NativeBuffer[0] = buffer;
        pBuffer->Allocation[0] = allocation;
        pBuffer->NumResources = 1;
        LogBufferPlacement( allocator, allocation, "Static VB/IB" );
    }
    pBuffer->ByteSize = desc->Size;

    switch ( desc->BufferType ) {
    case GPUBufferDesc::Type::BT_VertexBuffer:
        pBuffer->Stride = desc->Stride;
        break;
    case GPUBufferDesc::Type::BT_IndexBuffer:
        pBuffer->IndexType =
            ( desc->Stride == 2 ) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        break;
    default:
        OTDU_UNIMPLEMENTED;
        break;
    };

    return pBuffer;
}

void *RenderDevice::lockBuffer( GPUBuffer *pBuffer,
                                const uint32_t offset,
                                const uint32_t size,
                                const bool bDiscardContent ) {
    OTDU_ASSERT( offset <= pBuffer->ByteSize );

    const uint32_t lockSize = ( size == 0 )
                                  ? ( pBuffer->ByteSize - offset )
                                  : Min( size, pBuffer->ByteSize - offset );

    if ( pBuffer->NumResources > 1 ) {
        pBuffer->LockStart = offset;
        pBuffer->LockEnd = offset + lockSize;
        pBuffer->bDiscardLock = bDiscardContent && bRecordingCommands;
        return pBuffer->ShadowCopy.data() + offset;
    }

    void *pMappedMemory = nullptr;
    VkResult opResult =
        vmaMapMemory( allocator, pBuffer->Allocation[0], &pMappedMemory );
    OTDU_ASSERT( opResult == VK_SUCCESS && pMappedMemory != nullptr );

    return static_cast<uint8_t *>( pMappedMemory ) + offset;
}

void RenderDevice::unlockBuffer( GPUBuffer *pBuffer ) {
    if ( pBuffer->NumResources == 1 ) {
        vmaUnmapMemory( allocator, pBuffer->Allocation[0] );
        return;
    }

    if ( pBuffer->LockEnd > pBuffer->LockStart ) {
        for ( uint32_t i = 0; i < pBuffer->NumResources; i++ ) {
            pBuffer->StaleStart[i] =
                Min( pBuffer->StaleStart[i], pBuffer->LockStart );
            pBuffer->StaleEnd[i] =
                Max( pBuffer->StaleEnd[i], pBuffer->LockEnd );
        }
    }

    const bool bRenamed = pBuffer->bDiscardLock &&
                          pBuffer->DiscardSerial == recordingSerial &&
                          renameDynamicBuffer( pBuffer );
    if ( pBuffer->bDiscardLock ) {
        pBuffer->DiscardSerial = recordingSerial;
    }
    pBuffer->bDiscardLock = false;
    pBuffer->LockStart = 0;
    pBuffer->LockEnd = 0;

    if ( bRecordingCommands && !bRenamed ) {
        pBuffer->TransientSerial = 0ull;
        syncDynamicBuffer( pBuffer, frameIndex );
    }

    for ( uint32_t i = 0; i < 0x10; i++ ) {
        if ( pBoundVertexBuffers[i] == pBuffer ) {
            VkDeviceSize baseOffset = 0ull;
            resolveBuffer( pBuffer, vertexBuffers[i], baseOffset );
            vertexBufferOffsets[i] = baseOffset + boundVertexBufferOffsets[i];
        }
    }
}

bool RenderDevice::renameDynamicBuffer( GPUBuffer *pBuffer ) {
    static constexpr VkDeviceSize kTransientAlignment = 256ull;

    TransientRing &ring = transientRings[frameIndex];
    const VkDeviceSize size = pBuffer->LockEnd;
    const VkDeviceSize start =
        ( ring.Head + kTransientAlignment - 1 ) & ~( kTransientAlignment - 1 );
    if ( ring.pMapped == nullptr || start + size > kTransientRingSize ) {
        OTDU_LOG_ERROR( "Transient ring full!\n" );
        OTDU_TRIGGER_BREAKPOINT;
        return false;
    }

    memcpy( ring.pMapped + start, pBuffer->ShadowCopy.data(),
            static_cast<size_t>( size ) );
    vmaFlushAllocation( allocator, ring.Allocation, start, size );
    ring.Head = start + size;

    pBuffer->TransientBuffer = ring.Buffer;
    pBuffer->TransientOffset = start;
    pBuffer->TransientSerial = recordingSerial;
    return true;
}

void RenderDevice::resolveBuffer( GPUBuffer *pBuffer,
                                  VkBuffer &outBuffer,
                                  VkDeviceSize &outBaseOffset ) {
    if ( pBuffer->NumResources > 1 && bRecordingCommands &&
         pBuffer->TransientSerial == recordingSerial ) {
        outBuffer = pBuffer->TransientBuffer;
        outBaseOffset = pBuffer->TransientOffset;
        return;
    }

    const uint32_t resourceIndex =
        ( pBuffer->NumResources == 1 ) ? 0 : frameIndex;
    if ( bRecordingCommands ) {
        syncDynamicBuffer( pBuffer, resourceIndex );
    }
    outBuffer = pBuffer->NativeBuffer[resourceIndex];
    outBaseOffset = 0ull;
}

void RenderDevice::syncDynamicBuffer( GPUBuffer *pBuffer,
                                      const uint32_t resourceIndex ) {
    if ( pBuffer->NumResources == 1 ||
         pBuffer->StaleEnd[resourceIndex] <=
             pBuffer->StaleStart[resourceIndex] ) {
        return;
    }

    const uint32_t start = pBuffer->StaleStart[resourceIndex];
    const uint32_t length = pBuffer->StaleEnd[resourceIndex] - start;

    void *pMappedMemory = nullptr;
    VkResult opResult = vmaMapMemory(
        allocator, pBuffer->Allocation[resourceIndex], &pMappedMemory );
    OTDU_ASSERT( opResult == VK_SUCCESS && pMappedMemory != nullptr );

    memcpy( static_cast<uint8_t *>( pMappedMemory ) + start,
            pBuffer->ShadowCopy.data() + start, length );

    vmaUnmapMemory( allocator, pBuffer->Allocation[resourceIndex] );
    vmaFlushAllocation( allocator, pBuffer->Allocation[resourceIndex], start,
                        length );

    pBuffer->StaleStart[resourceIndex] = ~0u;
    pBuffer->StaleEnd[resourceIndex] = 0u;
}

void RenderDevice::generateMipchain( Texture *pTexture ) {
    OTDU_ASSERT( pTexture->NumMipmap > 1 );

    recordMipchainGeneration( pTexture,
                              getPreFrameCommandBuffer( PFCL_MipchainGen ) );
}

void RenderDevice::generateMipchainInline( Texture *pTexture ) {
    if ( pTexture == nullptr || pTexture->pTexture == nullptr ||
         pTexture->NumMipmap <= 1 || !bRecordingCommands ) {
        return;
    }

    endActiveRenderPass();

    recordMipchainGeneration( pTexture, getCommandBuffer() );
}

void RenderDevice::recordMipchainGeneration( Texture *pTexture,
                                             VkCommandBuffer cmdBuffer ) {
    GPUTexture *pVulkanTex = pTexture->pTexture;

    uint32_t numTextures = Max( 1, pTexture->Depth );

    // Memory barrier for mip0 (for all layers)
    VkImageSubresourceRange range;
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = numTextures;

    // OTDU_ADDITION: real dependencies (sync validation: the barriers waited
    // on nothing, TOP_OF_PIPE / no access). Mip 0 waits for whatever wrote
    // it (a render pass for the ocean normal map, rebuilt every frame) and
    // each level for last frame's sampling; each blit then waits for the one
    // writing its source level, else the small mips were filtered from half
    // written data.
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = pVulkanTex->CurrentLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = pVulkanTex->Image;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                          nullptr, 1, &barrier );

    for ( uint32_t j = 0; j < numTextures; j++ ) {
        for ( uint32_t i = 1; i < pTexture->NumMipmap; i++ ) {
            VkImageBlit blitInfo = {};

            // Source
            blitInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitInfo.srcSubresource.layerCount = 1;
            blitInfo.srcSubresource.mipLevel = i - 1;
            blitInfo.srcOffsets[0] = { 0, 0, 0 };
            blitInfo.srcOffsets[1].x =
                static_cast<int32_t>( pTexture->Width >> ( i - 1 ) );
            blitInfo.srcOffsets[1].y =
                static_cast<int32_t>( pTexture->Height >> ( i - 1 ) );
            blitInfo.srcOffsets[1].z = 1;

            // Destination
            blitInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitInfo.dstSubresource.layerCount = 1;
            blitInfo.dstSubresource.mipLevel = i;
            blitInfo.dstOffsets[0] = { 0, 0, 0 };
            blitInfo.dstOffsets[1].x =
                static_cast<int32_t>( pTexture->Width >> i );
            blitInfo.dstOffsets[1].y =
                static_cast<int32_t>( pTexture->Height >> i );
            blitInfo.dstOffsets[1].z = 1;

            VkImageSubresourceRange range;
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = i;
            range.levelCount = 1;
            range.baseArrayLayer = j;
            range.layerCount = 1;

            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = pVulkanTex->CurrentLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.image = pVulkanTex->Image;
            barrier.subresourceRange = range;
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                                  0, nullptr, 1, &barrier );

            vkCmdBlitImage( cmdBuffer, pVulkanTex->Image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            pVulkanTex->Image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitInfo,
                            VK_FILTER_LINEAR );

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                                  0, nullptr, 1, &barrier );
        }
    }

    // Blit to SRV barrier
    VkImageSubresourceRange srvRange;
    srvRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    srvRange.baseMipLevel = 0;
    srvRange.levelCount = pTexture->NumMipmap;
    srvRange.baseArrayLayer = 0;
    srvRange.layerCount = numTextures;

    VkImageMemoryBarrier srvBarrier = {};
    srvBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    srvBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    srvBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    srvBarrier.image = pVulkanTex->Image;
    srvBarrier.subresourceRange = srvRange;
    srvBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srvBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          0, 0, nullptr, 0, nullptr, 1, &srvBarrier );

    pVulkanTex->CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void RenderDevice::destroyTexture( GPUTexture *pTexture ) {
    for ( GPUTexture *&pPrevious : lastPassColorAttachments ) {
        if ( pPrevious == pTexture ) {
            pPrevious = nullptr;
        }
    }
    if ( pLastPassDepthStencil == pTexture ) {
        pLastPassDepthStencil = nullptr;
    }

    RetireList &retireList = getRetireList();

    // OTDU_ADDITION: the depth-only view made for sampling (resource_binder.cpp),
    // retired before its image
    extern VkImageView TakeDepthSampleView( const GPUTexture *pTexture );
    VkImageView depthSampleView = TakeDepthSampleView( pTexture );
    if ( depthSampleView != VK_NULL_HANDLE ) {
        retireList.Textures.push_back(
            { VK_NULL_HANDLE, nullptr, depthSampleView } );
    }

    retireList.Textures.push_back(
        { pTexture->Image, pTexture->Allocation, pTexture->ImageView } );
    delete pTexture;
}

void RenderDevice::destroyBuffer( GPUBuffer *pBuffer ) {
    RetireList &retireList = getRetireList();
    for ( uint32_t i = 0; i < pBuffer->NumResources; i++ ) {
        retireList.Buffers.push_back(
            { pBuffer->NativeBuffer[i], pBuffer->Allocation[i] } );
    }
    delete pBuffer;
}

RenderDevice::RetireList &RenderDevice::getRetireList() {
    // e.g. mipchain generation requested while binding a texture mid-frame
    const PreFrameCommands &commands = preFrameCommands[activePreFrameCommands];
    const bool bPreFrameCommandsRecording =
        commands.bRecording[PFCL_TextureUpload] ||
        commands.bRecording[PFCL_MipchainGen];
    return ( bRecordingCommands && bPreFrameCommandsRecording )
               ? nextFrameRetire
               : pendingRetire;
}

void RenderDevice::releaseRetireList( RetireList &list ) {
    for ( const GPUBufferDeletion &entry : list.Buffers ) {
        vmaDestroyBuffer( allocator, entry.buffer, entry.allocation );
    }
    list.Buffers.clear();

    for ( const GPUTextureDeletion &entry : list.Textures ) {
        if ( entry.allocation == nullptr ) {
            vkDestroyImage( device, entry.image, nullptr );
        } else {
            vmaDestroyImage( allocator, entry.image, entry.allocation );
        }
        vkDestroyImageView( device, entry.imageView, nullptr );
    }
    list.Textures.clear();
}

bool RenderDevice::resize( uint32_t width, uint32_t height ) {
    if ( backbuffer.Width == width && backbuffer.Height == height ) {
        return true;
    }

    if ( bRecordingCommands ) {
        bNeedRecreateSwapchain = true;
    } else {
        recreateSwapchain();
    }
    return true;
}

void RenderDevice::setVSyncEnabled( const bool bEnabled ) {
    if ( bVSyncEnabled == bEnabled ) {
        return;
    }

    bVSyncEnabled = bEnabled;
    bNeedRecreateSwapchain = true;
}

struct HDRSurfaceFormat {
    VkFormat Format;
    VkColorSpaceKHR ColorSpace;
    eOutputTransfer Transfer;
};

// Ordered by preference. macOS favors EDR (extended linear) as it follows the
// system brightness.
static constexpr HDRSurfaceFormat kHDRSurfaceFormats[] = {
#if OTDU_MACOS
    {VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
                  eOutputTransfer::OT_ExtendedLinear},
#endif
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT,
                  eOutputTransfer::OT_PQ            },
    {VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT,
                  eOutputTransfer::OT_PQ            },
#if !OTDU_MACOS
    {VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
                  eOutputTransfer::OT_ExtendedLinear},
#endif
};

// Luminance of 1.0 in the extended linear color space (scRGB on Windows/Linux;
// on macOS 1.0 is the SDR white, which we map to the BT.2408 reference white)
#if OTDU_MACOS
static constexpr float kExtendedLinearWhiteNits = kDefaultHDRPaperWhiteNits;
#else
static constexpr float kExtendedLinearWhiteNits = 80.0f;
#endif
static constexpr float kPQMaxNits = 10000.0f;

static float GetOutputScale( const eOutputTransfer transfer,
                             const float paperWhiteNits ) {
    switch ( transfer ) {
    case eOutputTransfer::OT_PQ:
        return paperWhiteNits / kPQMaxNits;
    case eOutputTransfer::OT_ExtendedLinear:
        return paperWhiteNits / kExtendedLinearWhiteNits;
    default:
        return 1.0f;
    }
}

void RenderDevice::setHDROutput( const bool bEnabled,
                                 const float paperWhiteNits ) {
    hdrPaperWhiteNits =
        Clamp( paperWhiteNits, kMinHDRPaperWhiteNits, kMaxHDRPaperWhiteNits );
    updateOutputParameters();

    if ( bHDROutputEnabled != bEnabled ) {
        bHDROutputEnabled = bEnabled;
        bNeedRecreateSwapchain = true;
    }
}

void RenderDevice::setHDRPeakNits( const float peakNits ) {
    hdrPeakNits = ( peakNits <= kAutoHDRPeakNits )
                      ? kAutoHDRPeakNits
                      : Clamp( peakNits, kMinHDRPeakNits, kMaxHDRPeakNits );
    updateOutputParameters();
}

void RenderDevice::updateDisplayPeak() {
    float peakNits = 0.0f;
#if defined( OTDU_USE_SDL )
    // Headroom and SDR white are relative to 1.0 in the extended linear color
    // space (Windows: the display MaxLuminance; macOS: the current EDR range)
    SDL_PropertiesID properties =
        SDL_GetWindowProperties( (SDL_Window *)gEdSys.getWindow() );
    const float headroom = SDL_GetFloatProperty(
        properties, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f );
    const float sdrWhite = SDL_GetFloatProperty(
        properties, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f );
    if ( headroom > 1.0f ) {
        peakNits = Min( headroom * sdrWhite * kExtendedLinearWhiteNits,
                        kMaxHDRPeakNits );
    }
#endif

    if ( peakNits != detectedPeakNits ) {
        OTDU_LOG_INFO( "Display HDR peak: %.0f nits\n", peakNits );
        detectedPeakNits = peakNits;
        updateOutputParameters();
    }
}

void RenderDevice::setHDRHighlightStrength( const float strength ) {
    hdrHighlightStrength = Clamp( strength, 0.0f, kMaxHDRHighlightStrength );
    updateOutputParameters();
}

void RenderDevice::updateOutputParameters() {
    // Highlights above the paper white roll off towards the display peak
    gammaRampPass.setOutputParameters(
        GetOutputScale( activeOutputTransfer, hdrPaperWhiteNits ),
        Max( getEffectiveHDRPeakNits() / hdrPaperWhiteNits, 1.0f ),
        hdrHighlightStrength );

    if ( activeOutputTransfer != eOutputTransfer::OT_PQ ||
         !bHDRMetadataSupported || swapchain == VK_NULL_HANDLE ) {
        return;
    }

    // The swapchain belongs to the submit thread: applied with the next frame
    VkHdrMetadataEXT &metadata = pendingHDRMetadata;
    metadata = {};
    metadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
    metadata.displayPrimaryRed = { 0.640f, 0.330f };
    metadata.displayPrimaryGreen = { 0.300f, 0.600f };
    metadata.displayPrimaryBlue = { 0.150f, 0.060f };
    metadata.whitePoint = { 0.3127f, 0.3290f };
    metadata.maxLuminance = Max( getEffectiveHDRPeakNits(), hdrPaperWhiteNits );
    metadata.minLuminance = 0.0f;
    metadata.maxContentLightLevel = metadata.maxLuminance;
    metadata.maxFrameAverageLightLevel = hdrPaperWhiteNits;
    bHDRMetadataDirty = true;
}

void RenderDevice::waitForIdle() {
    waitForSubmissionsRetired();
    submitPreFrameCommandsNow();
    vkDeviceWaitIdle( device );

    // Resources destroyed while recording may still be referenced by the
    // frame commands
    if ( !bRecordingCommands ) {
        releaseRetireList( pendingRetire );
        releaseRetireList( nextFrameRetire );
    }
}

void RenderDevice::flushAndWaitForIdle() {
    if ( bRecordingCommands ) {
        present();
    }
    waitForIdle();
}

void RenderDevice::destroyVertexLayout( GPUVertexLayout *pVertexLayout ) {
    pVertexLayout->Semantics.clear();
    pVertexLayout->InputBindings.clear();
    delete pVertexLayout;
}

GPUBackbuffer *RenderDevice::getBackbuffer() { return &backbuffer; }

FormatCapabilities RenderDevice::getFormatCapabilities( eViewFormat format ) {
    return formatCaps[format];
}

void RenderDevice::blitBound2DBToRT( RenderTarget *pRenderTarget ) {
    // NOTE: This is slightly different from the original code (as the game
    // used to manually copy the bitmap to the RT with a fullscreen quad).
    // Since Vulkan offers an explicit cmd to blit textures we'll use that
    // instead to save time and reduce risk.
    Render2DB *pBound2DB = pRenderTarget->getBound2DB();
    if ( pBound2DB == nullptr ) {
        return;
    }

    blit( pBound2DB, pRenderTarget, false );
}

void RenderDevice::setViewport( Viewport &vp ) {
    VkViewport viewport;
    if ( vp.bNativeViewport ) {
        // Anything that does not come from the game/relies on translated
        // shaders goes through this
        viewport.x = vp.X;
        viewport.y = vp.Y;
        viewport.width = vp.Width;
        viewport.height = vp.Height;
    } else {
        // Correctness Factor for 1/2 texel offset
        constexpr float cf = 0.5f;

        viewport.x = vp.X + cf;
        viewport.y = vp.Y + vp.Height + cf;
        viewport.width = vp.Width;
        viewport.height = -vp.Height;
    }

    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport( getStateCommandBuffer(), 0, 1, &viewport );
#if OTDU_DEVBUILD
    activeViewport = viewport;
#endif

    // Set scissor region (required by Vulkan standard but never set by the game
    // so far)
    VkRect2D scissor;
    scissor.offset.x = (int32_t)vp.X;
    scissor.offset.y = (int32_t)vp.Y;
    scissor.extent.width = (int32_t)vp.Width;
    scissor.extent.height = (int32_t)vp.Height;

    vkCmdSetScissor( getStateCommandBuffer(), 0, 1, &scissor );
#if OTDU_DEVBUILD
    activeScissor = scissor;
#endif

    // Clear color will be set when the framebuffer is cleared
    if ( vp.Flags & 1 ) {
        activeClearColor = vp.ClearColor;
    }
}

void RenderDevice::setScissor( Scissor &scissor ) {
    VkRect2D vkScissor;
    vkScissor.offset.x = scissor.X;
    vkScissor.offset.y = scissor.Y;
    vkScissor.extent.width = scissor.Width;
    vkScissor.extent.height = scissor.Height;

    vkCmdSetScissor( getStateCommandBuffer(), 0, 1, &vkScissor );
#if OTDU_DEVBUILD
    activeScissor = vkScissor;
#endif
}

void RenderDevice::setFloatConstants( eShaderType stage,
                                      float *pFloats,
                                      const uint32_t offset,
                                      const uint32_t numVectors ) {
    resourceBinder.setFloatConstants( stage, pFloats, offset, numVectors );
}

void RenderDevice::bindTexture( Texture *pTexture, const uint32_t index ) {
    resourceBinder.bindTexture( pTexture, index );
}

void RenderDevice::clearFramebuffer( const ColorRGBA clearColor,
                                     const float clearDepth,
                                     const uint32_t clearStencil,
                                     const bool bClearColor,
                                     const bool bClearDepth,
                                     const bool bClearStencil ) {
    if ( !bClearColor && !bClearDepth && !bClearStencil ) {
        return;
    }

    PendingClear clear;
    clear.bActive = true;
    clear.bColor = bClearColor;
    clear.bDepth = bClearDepth;
    clear.bStencil = bClearStencil;
    clear.Color.float32[0] = clearColor.R / 255.0f;
    clear.Color.float32[1] = clearColor.G / 255.0f;
    clear.Color.float32[2] = clearColor.B / 255.0f;
    clear.Color.float32[3] = clearColor.A / 255.0f;
    clear.DepthStencil.depth = clearDepth;
    clear.DepthStencil.stencil = clearStencil;
    for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
        clear.pColorTargets[i] = bFramebufferAttachmentValid[i]
                                     ? pFramebufferAttachments[i]
                                     : nullptr;
    }
    clear.pDepthStencil =
        bDepthStencilAttachmentValid ? pDepthStencilAttachment : nullptr;

    if ( pendingClear.bActive && isPendingClearForBoundAttachments() ) {
        if ( clear.bColor ) {
            pendingClear.bColor = true;
            pendingClear.Color = clear.Color;
        }
        if ( clear.bDepth ) {
            pendingClear.bDepth = true;
            pendingClear.DepthStencil.depth = clear.DepthStencil.depth;
        }
        if ( clear.bStencil ) {
            pendingClear.bStencil = true;
            pendingClear.DepthStencil.stencil = clear.DepthStencil.stencil;
        }
        return;
    }

    if ( pendingClear.bActive ) {
        flushPendingClear();
    }
    pendingClear = clear;
}

bool RenderDevice::isPendingClearForBoundAttachments() const {
    for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
        GPUTexture *pBound = bFramebufferAttachmentValid[i]
                                 ? pFramebufferAttachments[i]
                                 : nullptr;
        if ( pBound != pendingClear.pColorTargets[i] ) {
            return false;
        }
    }
    GPUTexture *pBoundDepth =
        bDepthStencilAttachmentValid ? pDepthStencilAttachment : nullptr;
    return pBoundDepth == pendingClear.pDepthStencil;
}

void RenderDevice::flushPendingClear() {
    if ( isPendingClearForActivePass() ) {
        recordInPassClear();
        return;
    }

    const PendingClear clear = pendingClear;
    pendingClear.bActive = false;
    recordImageClear( clear );
}

void RenderDevice::recordImageClear( const PendingClear &clear ) {
    endActiveRenderPass();

    // Assume 2D non-array for now
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    if ( clear.bColor ) {
        for ( GPUTexture *pAttachment : clear.pColorTargets ) {
            if ( pAttachment == nullptr ) {
                continue;
            }
            insertMemoryBarrier( pAttachment, VK_IMAGE_LAYOUT_GENERAL );
            vkCmdClearColorImage( getCommandBuffer(), pAttachment->Image,
                                  pAttachment->CurrentLayout, &clear.Color, 1,
                                  &subresourceRange );
        }
    }

    subresourceRange.aspectMask = 0;
    if ( clear.bDepth ) {
        subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if ( clear.bStencil ) {
        subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    GPUTexture *pDepthStencil = clear.pDepthStencil;
    if ( subresourceRange.aspectMask != 0 && pDepthStencil != nullptr ) {
        insertMemoryBarrier( pDepthStencil, VK_IMAGE_LAYOUT_GENERAL );
        vkCmdClearDepthStencilImage( getCommandBuffer(), pDepthStencil->Image,
                                     pDepthStencil->CurrentLayout,
                                     &clear.DepthStencil, 1,
                                     &subresourceRange );
    }
}

bool RenderDevice::isPendingClearForActivePass() const {
    if ( !pendingClear.bActive || activeRenderPass == VK_NULL_HANDLE ) {
        return false;
    }
    for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
        if ( pendingClear.pColorTargets[i] != lastPassColorAttachments[i] ) {
            return false;
        }
    }
    return pendingClear.pDepthStencil == pLastPassDepthStencil;
}

void RenderDevice::recordInPassClear() {
    std::array<VkClearAttachment, kMaxSimultaneousRT + 1> clears;
    uint32_t numClears = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    uint32_t colorAttachmentIndex = 0;
    for ( GPUTexture *pTarget : pendingClear.pColorTargets ) {
        if ( pTarget == nullptr ) {
            continue;
        }
        width = pTarget->Width;
        height = pTarget->Height;
        if ( pendingClear.bColor ) {
            VkClearAttachment &clear = clears[numClears++];
            clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clear.colorAttachment = colorAttachmentIndex;
            clear.clearValue.color = pendingClear.Color;
        }
        colorAttachmentIndex++;
    }

    GPUTexture *pDepthStencil = pendingClear.pDepthStencil;
    if ( pDepthStencil != nullptr ) {
        if ( width == 0 ) {
            width = pDepthStencil->Width;
            height = pDepthStencil->Height;
        }

        VkImageAspectFlags aspectMask = 0;
        if ( pendingClear.bDepth ) {
            aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        if ( pendingClear.bStencil &&
             HasStencilBits( pDepthStencil->ViewFormat ) ) {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        if ( aspectMask != 0 ) {
            VkClearAttachment &clear = clears[numClears++];
            clear.aspectMask = aspectMask;
            clear.colorAttachment = 0;
            clear.clearValue.depthStencil = pendingClear.DepthStencil;
        }
    }

    pendingClear.bActive = false;
    if ( numClears == 0 ) {
        return;
    }

    VkClearRect clearRect = {};
    clearRect.rect.offset = { 0, 0 };
    clearRect.rect.extent = { width, height };
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;
    vkCmdClearAttachments( getCommandBuffer(), numClears, clears.data(), 1,
                           &clearRect );
}

bool RenderDevice::isBoundColorAttachment( const GPUTexture *pTexture ) const {
    for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
        if ( bFramebufferAttachmentValid[i] &&
             pFramebufferAttachments[i] == pTexture ) {
            return true;
        }
    }
    return false;
}

void RenderDevice::transitionPreviousPassAttachments() {
    for ( GPUTexture *&pPrevious : lastPassColorAttachments ) {
        GPUTexture *pTexture = pPrevious;
        pPrevious = nullptr;
        if ( pTexture == nullptr ||
             pTexture->SampleCount != VK_SAMPLE_COUNT_1_BIT ||
             pTexture->CurrentLayout ==
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
             isBoundColorAttachment( pTexture ) ) {
            continue;
        }

        insertImageBarrier( pTexture, pTexture->CurrentLayout,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT );
    }
}

void RenderDevice::clearColorRenderTarget( GPUTexture *pTexture,
                                           const ColorRGBA clearColor,
                                           const ClearRegion &clearRegion ) {

    if ( pTexture == nullptr ) {
        OTDU_LOG_WARN( "Tried to clear null texture...\n" );
        return;
    }

    if ( clearRegion.X != 0 || clearRegion.Y != 0 ) {
        // TODO: Revisit this function to properly support rect clear outside
        // render passes... (the game never uses custom region this is low
        // priority)
        OTDU_ASSERT_FATAL( false );
    }

    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    VkClearColorValue clearColorValue;
    clearColorValue.float32[0] = clearColor.R / 255.0f;
    clearColorValue.float32[1] = clearColor.G / 255.0f;
    clearColorValue.float32[2] = clearColor.B / 255.0f;
    clearColorValue.float32[3] = clearColor.A / 255.0f;

    insertMemoryBarrier( pTexture, VK_IMAGE_LAYOUT_GENERAL );

    vkCmdClearColorImage( getCommandBuffer(), pTexture->Image,
                          pTexture->CurrentLayout, &clearColorValue, 1,
                          &subresourceRange );
}

void RenderDevice::bindVertexLayout( GPUVertexLayout *pLayout ) {
    if ( psoFactory.pVertexLayout == pLayout ) {
        return;
    }
    psoFactory.pVertexLayout = pLayout;
    psoFactory.bDirty = true;
}

void RenderDevice::bindVertexBuffer( GPUBuffer *pBuffer,
                                     const uint32_t index,
                                     const uint32_t stride,
                                     const uint32_t offset ) {
    pBoundVertexBuffers[index] = pBuffer;
    boundVertexBufferOffsets[index] = offset;
    if ( pBuffer == nullptr ) {
        vertexBuffers[index] = VK_NULL_HANDLE;
    } else {
        VkDeviceSize baseOffset = 0ull;
        resolveBuffer( pBuffer, vertexBuffers[index], baseOffset );
        vertexBufferOffsets[index] = baseOffset + offset;
    }
}

void RenderDevice::recordIndexedDraw(
    const VkDrawIndexedIndirectCommand &command ) {
    if ( !bDrawBatching || command.instanceCount > 1 ||
         isInstanceRateLayoutBound() ) {
        vkCmdDrawIndexed( getCommandBuffer(), command.indexCount,
                          command.instanceCount, command.firstIndex,
                          command.vertexOffset, command.firstInstance );
        return;
    }

    if ( pendingDrawBatch.Count == 0u ) {
        pendingDrawBatch.First = command;
        pendingDrawBatch.Count = 1u;
        return;
    }

    IndirectDrawBuffer &indirect = indirectDrawBuffers[frameIndex];
    VkDrawIndexedIndirectCommand &last =
        ( pendingDrawBatch.Count == 1u )
            ? pendingDrawBatch.First
            : indirect.pCommands[indirect.NumCommands - 1u];
    if ( command.indexCount == last.indexCount &&
         command.firstIndex == last.firstIndex &&
         command.vertexOffset == last.vertexOffset &&
         command.firstInstance == last.firstInstance + last.instanceCount ) {
        // Instance i reads DrawVS block firstInstance + i
        last.instanceCount++;
        return;
    }

    const uint32_t numNewCommands = ( pendingDrawBatch.Count == 1u ) ? 2u : 1u;
    if ( !bMultiDrawBatching ||
         indirect.NumCommands + numNewCommands > kMaxIndirectDrawsPerFrame ||
         pendingDrawBatch.Count >= deviceCaps.MaxDrawIndirectCount ) {
        flushDrawBatch();
        pendingDrawBatch.First = command;
        pendingDrawBatch.Count = 1u;
        return;
    }

    if ( pendingDrawBatch.Count == 1u ) {
        pendingDrawBatch.FirstIndirectCommand = indirect.NumCommands;
        indirect.pCommands[indirect.NumCommands++] = pendingDrawBatch.First;
    }
    indirect.pCommands[indirect.NumCommands++] = command;
    pendingDrawBatch.Count++;
}

void RenderDevice::flushDrawBatch() {
    const uint32_t count = pendingDrawBatch.Count;
    pendingDrawBatch.Count = 0u;

    if ( count == 1u ) {
        const VkDrawIndexedIndirectCommand &command = pendingDrawBatch.First;
        vkCmdDrawIndexed( commandBuffer, command.indexCount,
                          command.instanceCount, command.firstIndex,
                          command.vertexOffset, command.firstInstance );
        return;
    }

    const IndirectDrawBuffer &indirect = indirectDrawBuffers[frameIndex];
    const VkDeviceSize offset =
        VkDeviceSize( pendingDrawBatch.FirstIndirectCommand ) *
        sizeof( VkDrawIndexedIndirectCommand );
    const VkDeviceSize size =
        VkDeviceSize( count ) * sizeof( VkDrawIndexedIndirectCommand );
    vmaFlushAllocation( allocator, indirect.Allocation, offset, size );
    vkCmdDrawIndexedIndirect( commandBuffer, indirect.Buffer, offset, count,
                              sizeof( VkDrawIndexedIndirectCommand ) );
}

void RenderDevice::endActiveRenderPass() {
    if ( activeRenderPass == VK_NULL_HANDLE ) {
        return;
    }

    if ( isPendingClearForActivePass() ) {
        recordInPassClear();
    }

    if ( pendingDrawBatch.Count != 0u ) {
        flushDrawBatch();
    }
    vkCmdEndRenderPass( commandBuffer );
    activeRenderPass = VK_NULL_HANDLE;
}

void RenderDevice::createFallbackSRV() {
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = { 1u, 1u, 1u };
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    VkResult result =
        vmaCreateImage( allocator, &imageInfo, &allocInfo, &fallbackSRVImage,
                        &fallbackSRVAllocation, nullptr );
    OTDU_ASSERT_FATAL( result == VK_SUCCESS );

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = fallbackSRVImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    result = vkCreateImageView( device, &viewInfo, nullptr, &fallbackSRVView );
    OTDU_ASSERT_FATAL( result == VK_SUCCESS );

    VkCommandBuffer cmdBuffer = getPreFrameCommandBuffer( PFCL_TextureUpload );

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = fallbackSRVImage;
    barrier.subresourceRange = viewInfo.subresourceRange;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                          nullptr, 1, &barrier );

    const VkClearColorValue black = {};
    vkCmdClearColorImage( cmdBuffer, fallbackSRVImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1u,
                          &viewInfo.subresourceRange );

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 0,
                          nullptr, 1, &barrier );
    submitPreFrameCommandsNow();
}

void RenderDevice::createIndirectDrawBuffers() {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = VkDeviceSize( kMaxIndirectDrawsPerFrame ) *
                      sizeof( VkDrawIndexedIndirectCommand );
    bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = GetPerFrameBufferMemoryUsage();
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for ( IndirectDrawBuffer &indirect : indirectDrawBuffers ) {
        VmaAllocationInfo info = {};
        VkResult result =
            vmaCreateBuffer( allocator, &bufferInfo, &allocInfo,
                             &indirect.Buffer, &indirect.Allocation, &info );
        OTDU_ASSERT_FATAL( result == VK_SUCCESS );
        LogBufferPlacement( allocator, indirect.Allocation, "Indirect draw" );
        indirect.pCommands =
            static_cast<VkDrawIndexedIndirectCommand *>( info.pMappedData );
        indirect.NumCommands = 0u;
    }
}

void RenderDevice::createTransientRings() {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = kTransientRingSize;
    bufferInfo.usage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = GetPerFrameBufferMemoryUsage();
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for ( TransientRing &ring : transientRings ) {
        VmaAllocationInfo info = {};
        VkResult result =
            vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &ring.Buffer,
                             &ring.Allocation, &info );
        OTDU_ASSERT_FATAL( result == VK_SUCCESS );
        LogBufferPlacement( allocator, ring.Allocation, "Transient VB/IB" );
        ring.pMapped = static_cast<uint8_t *>( info.pMappedData );
        ring.Head = 0ull;
    }
}

void RenderDevice::destroyTransientRings() {
    for ( TransientRing &ring : transientRings ) {
        if ( ring.Buffer != VK_NULL_HANDLE ) {
            vmaDestroyBuffer( allocator, ring.Buffer, ring.Allocation );
            ring = {};
        }
    }
}

void RenderDevice::destroyIndirectDrawBuffers() {
    for ( IndirectDrawBuffer &indirect : indirectDrawBuffers ) {
        if ( indirect.Buffer != VK_NULL_HANDLE ) {
            vmaDestroyBuffer( allocator, indirect.Buffer, indirect.Allocation );
            indirect = {};
        }
    }
}

uint32_t
RenderDevice::getDrawFirstInstance( const uint32_t instanceCount ) const {
    // See D3D9RenderStateInfo::instancedDrawIndex
    return isInstanceRateLayoutBound() ? 0u : resourceBinder.drawVSIndex;
}

void RenderDevice::draw( ePrimitiveType primitiveType,
                         uint32_t numVertex,
                         uint32_t numInstance,
                         uint32_t firstVertex ) {
    if ( !prepareDrawCall( primitiveType ) ) {
        return;
    }
    const uint32_t firstInstance = getDrawFirstInstance( numInstance );

    // Actual draw cmd
    vkCmdDraw( getCommandBuffer(), numVertex, numInstance, firstVertex,
               firstInstance );
}

// From dxvk
inline uint32_t GetVertexCount( ePrimitiveType type, uint32_t count ) {
    switch ( type ) {
    default:
    case ePrimitiveType::PT_Triangle:
        return count * 3;
    case ePrimitiveType::PT_Point:
        return count;
    case ePrimitiveType::PT_Line:
        return count * 2;
    case ePrimitiveType::PT_LineStrip:
        return count + 1;
    case ePrimitiveType::PT_TriangleStrip:
        return count + 2;
    case ePrimitiveType::PT_TriangleFan:
        return count + 2;
    }
}

void RenderDevice::drawIndexedPrimitive( ePrimitiveType primitiveType,
                                         int32_t baseVertexIndex,
                                         uint32_t minVertexIndex,
                                         uint32_t numVertices,
                                         uint32_t startIndex,
                                         uint32_t primitiveCount ) {
#if defined( OTDU_MACOS )
    // MoltenVK has no triangle-fan topology; emulate it as a triangle list over
    // the same (sequentially indexed) DrawList vertices.
    if ( primitiveType == ePrimitiveType::PT_TriangleFan ) {
        drawTriangleFanAsList( baseVertexIndex, primitiveCount );
        return;
    }
#endif

    if ( !prepareDrawCall( primitiveType ) ) {
        return;
    }

    OTDU_ASSERT( pIndexBuffer != nullptr );

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexBufferOffset = 0ull;
    resolveBuffer( pIndexBuffer, indexBuffer, indexBufferOffset );
    bindIndexBufferToCmdBuffer( indexBuffer, indexBufferOffset,
                                pIndexBuffer->IndexType );

    uint32_t indexCount = GetVertexCount( primitiveType, primitiveCount );
    uint32_t instanceCount = activeInstanceCount;
    activeInstanceCount = 1; // reset to 1 (non-instanced default)
    const uint32_t firstInstance = getDrawFirstInstance( instanceCount );

    // Actual draw cmd
    VkDrawIndexedIndirectCommand command;
    command.indexCount = indexCount;
    command.instanceCount = instanceCount;
    command.firstIndex = startIndex;
    command.vertexOffset = baseVertexIndex;
    command.firstInstance = firstInstance;
    recordIndexedDraw( command );
}

void RenderDevice::drawIndexedIndirectBuffer( ePrimitiveType primitiveType,
                                              GPUBuffer *pArguments,
                                              uint32_t offset,
                                              uint32_t numDraws ) {
    if ( numDraws == 0u || pArguments == nullptr ||
         !prepareDrawCall( primitiveType ) ) {
        return;
    }
    OTDU_ASSERT( pIndexBuffer != nullptr );

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexBufferOffset = 0ull;
    resolveBuffer( pIndexBuffer, indexBuffer, indexBufferOffset );
    bindIndexBufferToCmdBuffer( indexBuffer, indexBufferOffset,
                                pIndexBuffer->IndexType );
    activeInstanceCount = 1;

    VkBuffer argumentBuffer = VK_NULL_HANDLE;
    VkDeviceSize argumentOffset = 0ull;
    resolveBuffer( pArguments, argumentBuffer, argumentOffset );

    const uint32_t stride = sizeof( VkDrawIndexedIndirectCommand );
    const uint32_t maxDrawCount =
        deviceCaps.bMultiDrawIndirect ? deviceCaps.MaxDrawIndirectCount : 1u;
    VkCommandBuffer cmdBuffer = getCommandBuffer();
    for ( uint32_t i = 0u; i < numDraws; i += maxDrawCount ) {
        vkCmdDrawIndexedIndirect( cmdBuffer, argumentBuffer,
                                  argumentOffset + offset +
                                      VkDeviceSize( i ) * stride,
                                  Min( numDraws - i, maxDrawCount ), stride );
    }
}

GPUComputePipeline *
RenderDevice::createComputePipeline( const uint32_t *pSPIRV,
                                     size_t spirvSize,
                                     uint32_t numStorageBuffers,
                                     uint32_t pushConstantSize ) {
    OTDU_ASSERT( numStorageBuffers <= kMaxComputeBuffersPerSet );

    if ( computeDescriptorPools[0] == VK_NULL_HANDLE ) {
        VkDescriptorPoolSize poolSize = {};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount =
            kMaxComputeSetsPerFrame * kMaxComputeBuffersPerSet;
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = kMaxComputeSetsPerFrame;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        for ( VkDescriptorPool &pool : computeDescriptorPools ) {
            VkResult result =
                vkCreateDescriptorPool( device, &poolInfo, nullptr, &pool );
            OTDU_ASSERT_FATAL( result == VK_SUCCESS );
        }
    }

    GPUComputePipeline *pPipeline = new GPUComputePipeline();
    pPipeline->NumStorageBuffers = numStorageBuffers;
    pPipeline->PushConstantSize = pushConstantSize;

    VkDescriptorSetLayoutBinding bindings[kMaxComputeBuffersPerSet] = {};
    for ( uint32_t i = 0; i < numStorageBuffers; i++ ) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setLayoutInfo = {};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = numStorageBuffers;
    setLayoutInfo.pBindings = bindings;
    VkResult result = vkCreateDescriptorSetLayout(
        device, &setLayoutInfo, nullptr, &pPipeline->SetLayout );
    OTDU_ASSERT_FATAL( result == VK_SUCCESS );

    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.size = pushConstantSize;
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pPipeline->SetLayout;
    layoutInfo.pushConstantRangeCount = pushConstantSize != 0u ? 1 : 0;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    result = vkCreatePipelineLayout( device, &layoutInfo, nullptr,
                                     &pPipeline->Layout );
    OTDU_ASSERT_FATAL( result == VK_SUCCESS );

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spirvSize;
    moduleInfo.pCode = pSPIRV;
    VkShaderModule module = VK_NULL_HANDLE;
    result = vkCreateShaderModule( device, &moduleInfo, nullptr, &module );
    OTDU_ASSERT_FATAL( result == VK_SUCCESS );

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pPipeline->Layout;
    result = vkCreateComputePipelines( device, psoCache, 1, &pipelineInfo,
                                       nullptr, &pPipeline->Pipeline );
    vkDestroyShaderModule( device, module, nullptr );
    if ( result != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "Failed to create compute pipeline (%d)\n", result );
        destroyComputePipeline( pPipeline );
        return nullptr;
    }
    return pPipeline;
}

void RenderDevice::destroyComputePipeline( GPUComputePipeline *pPipeline ) {
    if ( pPipeline == nullptr ) {
        return;
    }
    waitForIdle();
    if ( pPipeline->Pipeline != VK_NULL_HANDLE ) {
        vkDestroyPipeline( device, pPipeline->Pipeline, nullptr );
    }
    if ( pPipeline->Layout != VK_NULL_HANDLE ) {
        vkDestroyPipelineLayout( device, pPipeline->Layout, nullptr );
    }
    if ( pPipeline->SetLayout != VK_NULL_HANDLE ) {
        vkDestroyDescriptorSetLayout( device, pPipeline->SetLayout, nullptr );
    }
    delete pPipeline;
}

void RenderDevice::dispatchCompute( GPUComputePipeline *pPipeline,
                                    GPUBuffer *const *ppStorageBuffers,
                                    const void *pPushConstants,
                                    uint32_t numGroupsX,
                                    uint32_t numGroupsY,
                                    uint32_t numGroupsZ ) {
    if ( !bRecordingCommands || pPipeline == nullptr ) {
        return;
    }
    endActiveRenderPass();

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = computeDescriptorPools[frameIndex];
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &pPipeline->SetLayout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if ( vkAllocateDescriptorSets( device, &allocInfo, &descriptorSet ) !=
         VK_SUCCESS ) {
        OTDU_LOG_ERROR( "Out of compute descriptor sets\n" );
        return;
    }

    VkDescriptorBufferInfo bufferInfos[kMaxComputeBuffersPerSet] = {};
    VkWriteDescriptorSet writes[kMaxComputeBuffersPerSet] = {};
    for ( uint32_t i = 0; i < pPipeline->NumStorageBuffers; i++ ) {
        GPUBuffer *pBuffer = ppStorageBuffers[i];
        resolveBuffer( pBuffer, bufferInfos[i].buffer, bufferInfos[i].offset );
        bufferInfos[i].range = pBuffer->ByteSize;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    vkUpdateDescriptorSets( device, pPipeline->NumStorageBuffers, writes, 0,
                            nullptr );

    VkCommandBuffer cmdBuffer = getCommandBuffer();
    vkCmdBindPipeline( cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                       pPipeline->Pipeline );
    vkCmdBindDescriptorSets( cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                             pPipeline->Layout, 0, 1, &descriptorSet, 0,
                             nullptr );
    if ( pPipeline->PushConstantSize != 0u && pPushConstants != nullptr ) {
        vkCmdPushConstants( cmdBuffer, pPipeline->Layout,
                            VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            pPipeline->PushConstantSize, pPushConstants );
    }
    vkCmdDispatch( cmdBuffer, numGroupsX, numGroupsY, numGroupsZ );

    resetRecordedBindings(); // TODO: Invalidate device state (this SUCKS big
                             // time and should be reworked)
}

void RenderDevice::updateBuffer( GPUBuffer *pBuffer,
                                 uint32_t offset,
                                 const void *pData,
                                 uint32_t size ) {
    if ( !bRecordingCommands || size == 0u ) {
        return;
    }
    OTDU_ASSERT( size <= 65536u && ( size & 3u ) == 0u );
    endActiveRenderPass();
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize bufferOffset = 0ull;
    resolveBuffer( pBuffer, buffer, bufferOffset );
    vkCmdUpdateBuffer( getCommandBuffer(), buffer, bufferOffset + offset, size,
                       pData );
}

void RenderDevice::insertMemoryBarrier( VkPipelineStageFlags srcStages,
                                        VkAccessFlags srcAccess,
                                        VkPipelineStageFlags dstStages,
                                        VkAccessFlags dstAccess ) {
    if ( !bRecordingCommands ) {
        return;
    }
    endActiveRenderPass();
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier( getCommandBuffer(), srcStages, dstStages, 0, 1,
                          &barrier, 0, nullptr, 0, nullptr );
}

void RenderDevice::deferMemoryBarrier( GPUTexture *pTarget,
                                       VkPipelineStageFlags srcStages,
                                       VkAccessFlags srcAccess,
                                       VkPipelineStageFlags dstStages,
                                       VkAccessFlags dstAccess ) {
    if ( !bRecordingCommands ) {
        return;
    }
    if ( pTarget == nullptr ) {
        insertMemoryBarrier( srcStages, srcAccess, dstStages, dstAccess );
        return;
    }
    deferredBarriers.push_back(
        { pTarget, srcStages, srcAccess, dstStages, dstAccess } );
}

void RenderDevice::flushDeferredBarriers( const GPUTexture *pTarget ) {
    // Outside of any render pass
    auto it = deferredBarriers.begin();
    while ( it != deferredBarriers.end() ) {
        if ( pTarget != nullptr && it->pTarget != pTarget ) {
            ++it;
            continue;
        }
        VkMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = it->SrcAccess;
        barrier.dstAccessMask = it->DstAccess;
        vkCmdPipelineBarrier( getCommandBuffer(), it->SrcStages, it->DstStages,
                              0, 1, &barrier, 0, nullptr, 0, nullptr );
        it = deferredBarriers.erase( it );
    }
}

uint32_t RenderDevice::addBeginSceneCallback( std::function<void()> callback ) {
    const uint32_t handle = nextBeginSceneCallback++;
    beginSceneCallbacks.emplace_back( handle, std::move( callback ) );
    return handle;
}

void RenderDevice::removeBeginSceneCallback( uint32_t handle ) {
    beginSceneCallbacks.erase( std::remove_if( beginSceneCallbacks.begin(),
                                               beginSceneCallbacks.end(),
                                               [handle]( const auto &entry ) {
                                                   return entry.first == handle;
                                               } ),
                               beginSceneCallbacks.end() );
}

#if defined( OTDU_MACOS )
void RenderDevice::drawTriangleFanAsList( int32_t baseVertexIndex,
                                          uint32_t primitiveCount ) {
    static constexpr uint32_t kMaxFanTriangles = 1024;
    if ( primitiveCount == 0 ) {
        return;
    }
    if ( primitiveCount > kMaxFanTriangles ) {
        OTDU_LOG_WARN( "Triangle fan with %u triangles exceeds the emulation "
                       "limit (%u); clamping\n",
                       primitiveCount, kMaxFanTriangles );
        primitiveCount = kMaxFanTriangles;
    }

    if ( pFanEmulationIndexBuffer == nullptr ) {
        GPUBufferDesc desc;
        desc.BufferType = GPUBufferDesc::Type::BT_IndexBuffer;
        desc.Size = kMaxFanTriangles * 3 * (uint32_t)sizeof( uint16_t );
        desc.Stride = sizeof( uint16_t );
        desc.bDynamic = false;
        pFanEmulationIndexBuffer = createBuffer( &desc );

        uint16_t *pIndices =
            (uint16_t *)lockBuffer( pFanEmulationIndexBuffer, 0, 0, true );
        for ( uint32_t t = 0; t < kMaxFanTriangles; t++ ) {
            pIndices[t * 3 + 0] = 0;
            pIndices[t * 3 + 1] = (uint16_t)( t + 1 );
            pIndices[t * 3 + 2] = (uint16_t)( t + 2 );
        }
        unlockBuffer( pFanEmulationIndexBuffer );
    }

    if ( !prepareDrawCall( ePrimitiveType::PT_Triangle ) ) {
        return;
    }

    bindIndexBufferToCmdBuffer( pFanEmulationIndexBuffer->NativeBuffer[0], 0ull,
                                pFanEmulationIndexBuffer->IndexType );

    uint32_t instanceCount = activeInstanceCount;
    activeInstanceCount = 1; // reset to 1 (non-instanced default)
    vkCmdDrawIndexed( getCommandBuffer(), primitiveCount * 3, instanceCount, 0,
                      baseVertexIndex, getDrawFirstInstance( instanceCount ) );
}
#endif

void RenderDevice::blit( Render2DB *pBound2DB,
                         RenderTarget *pDst,
                         const bool bLinearFiltering ) {
    Texture *pSrcTex = pBound2DB->getFirstBitmap();
    OTDU_ASSERT( pSrcTex );

    blit( pSrcTex->pTexture, pDst, bLinearFiltering );
}

void RenderDevice::blit( GPUTexture *pSrc,
                         RenderTarget *pDst,
                         const bool bLinearFiltering ) {
    OTDU_ASSERT( pSrc != nullptr );
    OTDU_ASSERT( pDst != nullptr );

    GPUTexture *pTextureRT = pDst->getTextureColor();
    if ( pSrc == pTextureRT ) {
        return;
    }

    endActiveRenderPass();

    VkImage srcImage = pSrc->Image;
    VkImage dstImage = pTextureRT->Image;

    insertMemoryBarrier( pSrc, VK_IMAGE_LAYOUT_GENERAL );
    insertMemoryBarrier( pTextureRT, VK_IMAGE_LAYOUT_GENERAL );

    const bool bSrcMSAA = ( pSrc->SampleCount != VK_SAMPLE_COUNT_1_BIT );
    const bool bDstMSAA = ( pTextureRT->SampleCount != VK_SAMPLE_COUNT_1_BIT );

    const uint32_t copyWidth = Min( pSrc->Width, (uint32_t)pDst->getWidth() );
    const uint32_t copyHeight =
        Min( pSrc->Height, (uint32_t)pDst->getHeight() );

    if ( !bSrcMSAA && !bDstMSAA ) {
        VkImageBlit region = {};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcOffsets[0] = { 0, 0, 0 };
        region.srcOffsets[1].x = (int32_t)pSrc->Width;
        region.srcOffsets[1].y = (int32_t)pSrc->Height;
        region.srcOffsets[1].z = 1;

        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = 0;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.dstOffsets[0] = { 0, 0, 0 };
        region.dstOffsets[1].x = pDst->getWidth();
        region.dstOffsets[1].y = pDst->getHeight();
        region.dstOffsets[1].z = 1;

        vkCmdBlitImage( getCommandBuffer(), srcImage, pSrc->CurrentLayout,
                        dstImage, pTextureRT->CurrentLayout, 1, &region,
                        VK_FILTER_NEAREST );
    } else if ( bSrcMSAA && !bDstMSAA ) {
        // Multisampled source into a single-sampled destination: resolve.
        VkImageResolve region = {};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1;
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.layerCount = 1;
        region.extent = { copyWidth, copyHeight, 1 };

        vkCmdResolveImage( getCommandBuffer(), srcImage, pSrc->CurrentLayout,
                           dstImage, pTextureRT->CurrentLayout, 1, &region );
    } else if ( pSrc->SampleCount == pTextureRT->SampleCount ) {
        // Matching multisampled source and destination: blit/resolve can't
        // target a multisampled image, but a straight copy can when the sample
        // counts match.
        VkImageCopy region = {};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1;
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.layerCount = 1;
        region.extent = { copyWidth, copyHeight, 1 };

        vkCmdCopyImage( getCommandBuffer(), srcImage, pSrc->CurrentLayout,
                        dstImage, pTextureRT->CurrentLayout, 1, &region );
    } else {
        OTDU_FATAL_ERROR( "Cannot transfer a single-sampled source into a "
                          "multisampled render target! (bad logic)\n" );
    }
}

void RenderDevice::resetCachedStates() {
    // FUN_00513cd0
    boundPSO = VK_NULL_HANDLE;
    gStateCache.resetBoundShaders();
}

void RenderDevice::bindStencilDynamicState() {
    const VkStencilOpState &front = psoFactory.DepthStencil.front;
    const VkStencilOpState &back = psoFactory.DepthStencil.back;
    VkStencilOpState &recordedFront = recordedBindings.StencilFront;
    VkStencilOpState &recordedBack = recordedBindings.StencilBack;
    const bool bValid = recordedBindings.bStencilDynamicStateValid;

    if ( !bValid || front.reference != recordedFront.reference ) {
        vkCmdSetStencilReference( getStateCommandBuffer(),
                                  VK_STENCIL_FACE_FRONT_BIT, front.reference );
    }
    if ( !bValid || back.reference != recordedBack.reference ) {
        vkCmdSetStencilReference( getStateCommandBuffer(),
                                  VK_STENCIL_FACE_BACK_BIT, back.reference );
    }
    if ( !bValid || front.compareMask != recordedFront.compareMask ) {
        vkCmdSetStencilCompareMask( getStateCommandBuffer(),
                                    VK_STENCIL_FACE_FRONT_BIT,
                                    front.compareMask );
    }
    if ( !bValid || back.compareMask != recordedBack.compareMask ) {
        vkCmdSetStencilCompareMask( getStateCommandBuffer(),
                                    VK_STENCIL_FACE_BACK_BIT,
                                    back.compareMask );
    }
    if ( !bValid || front.writeMask != recordedFront.writeMask ) {
        vkCmdSetStencilWriteMask( getStateCommandBuffer(),
                                  VK_STENCIL_FACE_FRONT_BIT, front.writeMask );
    }
    if ( !bValid || back.writeMask != recordedBack.writeMask ) {
        vkCmdSetStencilWriteMask( getStateCommandBuffer(),
                                  VK_STENCIL_FACE_BACK_BIT, back.writeMask );
    }

    recordedFront.reference = front.reference;
    recordedFront.compareMask = front.compareMask;
    recordedFront.writeMask = front.writeMask;
    recordedBack.reference = back.reference;
    recordedBack.compareMask = back.compareMask;
    recordedBack.writeMask = back.writeMask;
    recordedBindings.bStencilDynamicStateValid = true;
    psoFactory.bDirtyStencilDynamicState = false;
}

static bool IsSameStencilOp( const VkStencilOpState &a,
                             const VkStencilOpState &b ) {
    return a.failOp == b.failOp && a.passOp == b.passOp &&
           a.depthFailOp == b.depthFailOp && a.compareOp == b.compareOp;
}

void RenderDevice::bindExtendedDynamicState() {
    const VkPipelineDepthStencilStateCreateInfo &depthStencil =
        psoFactory.DepthStencil;
    const VkCullModeFlags cullMode = psoFactory.RasterState.cullMode;
    CommandBufferBindings &recorded = recordedBindings;
    const bool bValid = recorded.bExtendedDynamicStateValid;

    if ( !bValid || cullMode != recorded.CullMode ) {
        cmdSetCullMode( getStateCommandBuffer(), cullMode );
        recorded.CullMode = cullMode;
    }
    if ( !bValid || depthStencil.depthTestEnable != recorded.DepthTestEnable ) {
        cmdSetDepthTestEnable( getStateCommandBuffer(),
                               depthStencil.depthTestEnable );
        recorded.DepthTestEnable = depthStencil.depthTestEnable;
    }
    if ( !bValid ||
         depthStencil.depthWriteEnable != recorded.DepthWriteEnable ) {
        cmdSetDepthWriteEnable( getStateCommandBuffer(),
                                depthStencil.depthWriteEnable );
        recorded.DepthWriteEnable = depthStencil.depthWriteEnable;
    }
    if ( !bValid || depthStencil.depthCompareOp != recorded.DepthCompareOp ) {
        cmdSetDepthCompareOp( getStateCommandBuffer(),
                              depthStencil.depthCompareOp );
        recorded.DepthCompareOp = depthStencil.depthCompareOp;
    }
    if ( !bValid ||
         depthStencil.stencilTestEnable != recorded.StencilTestEnable ) {
        cmdSetStencilTestEnable( getStateCommandBuffer(),
                                 depthStencil.stencilTestEnable );
        recorded.StencilTestEnable = depthStencil.stencilTestEnable;
    }

    if ( !bValid ||
         !IsSameStencilOp( depthStencil.front, recorded.StencilFront ) ) {
        cmdSetStencilOp( getStateCommandBuffer(), VK_STENCIL_FACE_FRONT_BIT,
                         depthStencil.front.failOp, depthStencil.front.passOp,
                         depthStencil.front.depthFailOp,
                         depthStencil.front.compareOp );
    }
    if ( !bValid ||
         !IsSameStencilOp( depthStencil.back, recorded.StencilBack ) ) {
        cmdSetStencilOp( getStateCommandBuffer(), VK_STENCIL_FACE_BACK_BIT,
                         depthStencil.back.failOp, depthStencil.back.passOp,
                         depthStencil.back.depthFailOp,
                         depthStencil.back.compareOp );
    }

    VkStencilOpState &recordedFront = recorded.StencilFront;
    VkStencilOpState &recordedBack = recorded.StencilBack;
    recordedFront.failOp = depthStencil.front.failOp;
    recordedFront.passOp = depthStencil.front.passOp;
    recordedFront.depthFailOp = depthStencil.front.depthFailOp;
    recordedFront.compareOp = depthStencil.front.compareOp;
    recordedBack.failOp = depthStencil.back.failOp;
    recordedBack.passOp = depthStencil.back.passOp;
    recordedBack.depthFailOp = depthStencil.back.depthFailOp;
    recordedBack.compareOp = depthStencil.back.compareOp;
    recorded.bExtendedDynamicStateValid = true;
    psoFactory.bDirtyExtendedDynamicState = false;
}

void RenderDevice::markExtendedDynamicStateDirty() {
    if ( deviceCaps.bExtendedDynamicState ) {
        psoFactory.bDirtyExtendedDynamicState = true;
    } else {
        psoFactory.bDirty = true;
    }
}

void RenderDevice::present() {
    // FUN_005f16b0
    if ( bRecordingCommands ) {
        endActiveRenderPass();

        FrameSubmission submission;
        submission.Serial = recordingSerial;
        submission.Slot = frameIndex;
        for ( uint32_t i = 0; i < numRecordingPreFrameCommands; i++ ) {
            submission.CommandBuffers[submission.NumCommandBuffers++] =
                recordingPreFrameCommands[i];
        }
        numRecordingPreFrameCommands = 0u;

        presentBackbuffer( submission );

        VkResult opResult = vkEndCommandBuffer( getCommandBuffer() );
        OTDU_ASSERT( opResult == VK_SUCCESS );
        submission.CommandBuffers[submission.NumCommandBuffers++] =
            commandBuffer;
        commandBuffer = VK_NULL_HANDLE;

        submission.bUpdateHDRMetadata = bHDRMetadataDirty;
        submission.HDRMetadata = pendingHDRMetadata;
        bHDRMetadataDirty = false;

        std::swap( submission.Retire, pendingRetire );
        std::swap( pendingRetire, nextFrameRetire );

        queueSubmission( std::move( submission ) );

        recordingSerial = 0ull;
        bRecordingCommands = false;
    }

#if OTDU_DEVBUILD
    processPendingRenderTargetDumps();
#endif

    if ( bSwapchainOutOfDate.load( std::memory_order_acquire ) ||
         bNeedRecreateSwapchain ) {
        bNeedRecreateSwapchain = false;
        recreateSwapchain();
    }
}

void RenderDevice::setGammaRamp( const GammaRamp &ramp ) {
    gammaRampPass.setRamp( ramp );
}

bool RenderDevice::isFramebufferUsingMSAA() const { return bIsMainRTUsingMSAA; }

void RenderDevice::setMSAAState( bool param_1 ) {
    bIsMainRTUsingMSAA = param_1;
}

// OTDU_ADDITION: wheel spin blur (render/wheel_blur.cpp)
void RenderDevice::setAccumulationWeight( float weight, uint32_t flags ) {
    renderStates.otduAccumWeight = weight;
    renderStates.otduAccumFlags = flags;
}

void RenderDevice::copyDepthStencilRegion( GPUTexture *pSrc,
                                           GPUTexture *pDst,
                                           int32_t x,
                                           int32_t y,
                                           uint32_t width,
                                           uint32_t height ) {
    if ( !bRecordingCommands || pSrc == nullptr || pDst == nullptr ||
         pSrc == pDst || width == 0u || height == 0u ) {
        return;
    }
    OTDU_ASSERT( pSrc->ViewFormat == pDst->ViewFormat &&
                 pSrc->SampleCount == pDst->SampleCount );

    endActiveRenderPass();
    insertMemoryBarrier( pSrc, VK_IMAGE_LAYOUT_GENERAL );
    insertMemoryBarrier( pDst, VK_IMAGE_LAYOUT_GENERAL );

    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if ( HasStencilBits( pSrc->ViewFormat ) ) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkImageCopy region = {};
    region.srcSubresource.aspectMask = aspect;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource = region.srcSubresource;
    region.srcOffset = { x, y, 0 };
    region.dstOffset = { x, y, 0 };
    region.extent = { width, height, 1u };

    vkCmdCopyImage( getCommandBuffer(), pSrc->Image, pSrc->CurrentLayout,
                    pDst->Image, pDst->CurrentLayout, 1, &region );
}

void RenderDevice::bumpInternalFrameIndex() {
    if ( !bRecordingCommands ) {
        absoluteFrameIndex++;
        frameIndex = ( absoluteFrameIndex % PendingFrameCount );

        savePipelineCacheWhenIdle();
    }
}

void RenderDevice::setCullMode( eCullMode param_1 ) {
    // FUN_0051335
    static constexpr VkCullModeFlags kCullModeLUT[eCullMode::CM_Count] = {
        VK_CULL_MODE_BACK_BIT,
        VK_CULL_MODE_FRONT_BIT,
        VK_CULL_MODE_BACK_BIT,
        VK_CULL_MODE_NONE,
    };

    psoFactory.RasterState.cullMode = kCullModeLUT[param_1];
    markExtendedDynamicStateDirty();
}

bool RenderDevice::isVertexAttributeFormatSupported(
    eVertexAttributeFormat format ) const {
    // FUN_00512e90
    return ( bufferFormatSupport >> format ) & 1;
}

void RenderDevice::setStreamFrequency( int32_t streamIndex,
                                       int32_t frequency ) {
    // D3DSTREAMSOURCE_INDEXEDDATA (0x40000000): vertex stream; the lower bits
    // are the number of instances to draw.  Store it so drawIndexedPrimitive
    // can forward it as the Vulkan instanceCount.
    // D3DSTREAMSOURCE_INSTANCEDATA (0x80000000): instance stream; its input
    // rate is handled by VK_VERTEX_INPUT_RATE_INSTANCE in the PSO, so no
    // action is needed here.
    constexpr uint32_t D3DSTREAMSOURCE_INDEXEDDATA = 0x40000000u;
    if ( (uint32_t)frequency & D3DSTREAMSOURCE_INDEXEDDATA ) {
        activeInstanceCount =
            Max( 1u, (uint32_t)frequency & ~D3DSTREAMSOURCE_INDEXEDDATA );
    }
}

void RenderDevice::setColorWriteChannels( bool bWriteRed,
                                          bool bWriteGreen,
                                          bool bWriteBlue,
                                          bool bWriteAlpha ) {
    activeColorMask = 0;

    if ( bWriteRed )
        activeColorMask |= VK_COLOR_COMPONENT_R_BIT;

    if ( bWriteGreen )
        activeColorMask |= VK_COLOR_COMPONENT_G_BIT;

    if ( bWriteBlue )
        activeColorMask |= VK_COLOR_COMPONENT_B_BIT;

    if ( bWriteAlpha )
        activeColorMask |= VK_COLOR_COMPONENT_A_BIT;
}

void RenderDevice::setClipPlaneEnabled( uint32_t param_1 ) {
    const uint32_t clipPlaneCount = resourceBinder.setClipPlaneMask( param_1 );
    const VkBool32 bDepthClamp = param_1 != 0 && deviceCaps.bDepthClamp;
    resourceBinder.setShaderSpecBits( 5, 21, 3, clipPlaneCount );
    if ( psoFactory.RasterState.depthClampEnable == bDepthClamp &&
         psoFactory.VSClipPlaneCount == clipPlaneCount ) {
        return;
    }
    psoFactory.RasterState.depthClampEnable = bDepthClamp;
    psoFactory.VSClipPlaneCount = clipPlaneCount;
    psoFactory.bDirty = true;
}

void RenderDevice::bindIndexBuffer( GPUBuffer *param_1 ) {
    pIndexBuffer = param_1;
}

void RenderDevice::setPointSize( uint32_t param_1 ) {
    renderStates.pointSizeInt = param_1;
}

bool RenderDevice::retrieveDeviceCapabilities() {
    // FUN_00512b20
    // Retrieve format capabilities
    for ( uint32_t iVar7 = 0; iVar7 < eViewFormat::VF_Count; iVar7++ ) {
        if ( kViewFormatLUT[iVar7] == VK_FORMAT_UNDEFINED ) {
            continue;
        }

        VkImageFormatProperties properties = {};
        VkResult opResult = vkGetPhysicalDeviceImageFormatProperties(
            physicalDevice, kViewFormatLUT[iVar7], VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0,
            &properties );

        if ( opResult == VK_SUCCESS ) {
            VkFormatProperties fmtProps = {};
            vkGetPhysicalDeviceFormatProperties(
                physicalDevice, kViewFormatLUT[iVar7], &fmtProps );
            constexpr VkFormatFeatureFlags kBlendAndFilter =
                VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

            formatCaps[iVar7].bSupported = true;
            formatCaps[iVar7].bSupportBlending =
                ( fmtProps.optimalTilingFeatures & kBlendAndFilter ) ==
                kBlendAndFilter;
            formatCaps[iVar7].bSupportMSAAx2 =
                ( properties.sampleCounts & VK_SAMPLE_COUNT_2_BIT ) != 0;
            formatCaps[iVar7].bSupportMSAAx4 =
                ( properties.sampleCounts & VK_SAMPLE_COUNT_4_BIT ) != 0;
        } else {
            OTDU_LOG_WARN(
                "Color attachment format %s is unsupported by this device!\n",
                ViewFormatToString( (eViewFormat)iVar7 ) );
        }
    }

    for ( uint32_t iVar7 = 0; iVar7 < eVertexAttributeFormat::VAF_Count;
          iVar7++ ) {
        if ( kVertexAttributeFormats[iVar7] == VK_FORMAT_UNDEFINED ) {
            continue;
        }
        VkFormatProperties props = {};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, kVertexAttributeFormats[iVar7], &props );

        if ( props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT ) {
            bufferFormatSupport |= ( 1 << iVar7 );
        } else {
            OTDU_LOG_WARN(
                "Vertex attribute format %u is unsupported by this device!\n",
                iVar7 );
        }
    }

    return true;
}

void RenderDevice::bindVertexShader( GPUShader *param_1 ) {
    psoFactory.setShader( eShaderType::ST_Vertex, param_1 );
}

void RenderDevice::bindPixelShader( GPUShader *param_1 ) {
    psoFactory.setShader( eShaderType::ST_Pixel, param_1 );
}

void RenderDevice::setRasterState( uint32_t param_1, uint32_t param_2 ) {
    // NOP
}

void RenderDevice::setSamplerState( const SamplerDesc &samplerDesc,
                                    uint32_t index ) {
    OTDU_ASSERT( index < 21 ); // Limit from dxvk's dxso compiler

    // dxvk packs two 16-bit sampler heap indices per push-constant
    // dword: even stages → bits [0:15], odd stages → bits [16:31]
    uint32_t heapIndex = samplerHeap.getSamplerHeapIndex( samplerDesc );
    uint32_t &dword = boundSamplers[index / 2];
    uint32_t shift = ( index & 1u ) * 16u;
    dword = ( dword & ~( 0xFFFFu << shift ) ) |
            ( ( heapIndex & 0xFFFFu ) << shift );

    // TODO: Sampler w/ depth comparison support
    resourceBinder.setShaderSpecBits( 1, index, 1, 0 );
}

void RenderDevice::setFillMode( eFillMode param_1 ) {
    static constexpr VkPolygonMode kFillModeLUT[eFillMode::FM_Count] = {
        VK_POLYGON_MODE_FILL, VK_POLYGON_MODE_POINT, VK_POLYGON_MODE_LINE,
        VK_POLYGON_MODE_FILL };

    const VkPolygonMode polygonMode = deviceCaps.bFillModeNonSolid
                                          ? kFillModeLUT[param_1]
                                          : VK_POLYGON_MODE_FILL;
    if ( psoFactory.RasterState.polygonMode == polygonMode ) {
        return;
    }
    psoFactory.RasterState.polygonMode = polygonMode;
    psoFactory.bDirty = true;
}

void RenderDevice::setAlphaBlendingEnabled( bool param_1 ) {
    if ( psoFactory.AlphaBlending.blendEnable == (VkBool32)param_1 ) {
        return;
    }
    psoFactory.AlphaBlending.blendEnable = param_1;
    psoFactory.bDirty = true;
}

void RenderDevice::setAlphaRef( uint8_t param_1 ) {
    renderStates.alphaRef = param_1;
}

void RenderDevice::setAlphaTestEnabled( bool param_1 ) {
    if ( !param_1 ) {
        resourceBinder.setShaderSpecBits( 1, 21, 3, 7 );
        resourceBinder.setShaderSpecBits( 2, 27, 8, 0 );
    } else {
        resourceBinder.setShaderSpecBits( 2, 27, 8, 0xf );
    }
}

void RenderDevice::setLampsEnabled( const bool bEnabled ) {
    // TODO: Make this generic to support new effects (strongly typed enum that
    // toggle a bit per effect)
    const uint32_t lampsEnabled = bEnabled ? 1u : 0u;
    if ( psoFactory.LampsEnabled == lampsEnabled ) {
        return;
    }
    psoFactory.LampsEnabled = lampsEnabled;
    psoFactory.bDirty = true;
}

void RenderDevice::setAlphaTestFunc( eComparisonFunction param_1 ) {
    resourceBinder.setShaderSpecBits( 1, 21, 3, param_1 );
    resourceBinder.setShaderSpecBits( 2, 27, 8, 0xf );
}

void RenderDevice::setStencilEnabled( bool param_1 ) {
    psoFactory.DepthStencil.stencilTestEnable = param_1;
    markExtendedDynamicStateDirty();
}

void RenderDevice::setTwoSidedStencilEnabled( bool param_1 ) {
    psoFactory.bTwoSidedStencil = param_1;
    if ( !param_1 ) {
        // D3D9 single-sided mode: back face uses the same ops as front.
        psoFactory.DepthStencil.back = psoFactory.DepthStencil.front;
        markExtendedDynamicStateDirty();
        psoFactory.bDirtyStencilDynamicState = true;
    }
}

static constexpr VkStencilOp kStencilFunctionLUT[eStencilFunction::SF_Count] = {
    VK_STENCIL_OP_KEEP,
    VK_STENCIL_OP_ZERO,
    VK_STENCIL_OP_REPLACE,
    VK_STENCIL_OP_INCREMENT_AND_CLAMP,
    VK_STENCIL_OP_DECREMENT_AND_CLAMP,
    VK_STENCIL_OP_INVERT,
    VK_STENCIL_OP_INCREMENT_AND_WRAP,
    VK_STENCIL_OP_DECREMENT_AND_WRAP };

static constexpr VkCompareOp kComparisonFunLUT[eComparisonFunction::CF_Count] =
    {
        VK_COMPARE_OP_NEVER,
        VK_COMPARE_OP_LESS,
        VK_COMPARE_OP_EQUAL,
        VK_COMPARE_OP_LESS_OR_EQUAL,
        VK_COMPARE_OP_GREATER,
        VK_COMPARE_OP_NOT_EQUAL,
        VK_COMPARE_OP_GREATER_OR_EQUAL,
        VK_COMPARE_OP_ALWAYS,
};

void RenderDevice::setStencilFail( eStencilFunction param_1 ) {
    psoFactory.DepthStencil.front.failOp = kStencilFunctionLUT[param_1];
    if ( !psoFactory.bTwoSidedStencil )
        psoFactory.DepthStencil.back.failOp =
            psoFactory.DepthStencil.front.failOp;
    markExtendedDynamicStateDirty();
}

void RenderDevice::setStencilZFail( eStencilFunction param_1 ) {
    psoFactory.DepthStencil.front.depthFailOp = kStencilFunctionLUT[param_1];
    if ( !psoFactory.bTwoSidedStencil )
        psoFactory.DepthStencil.back.depthFailOp =
            psoFactory.DepthStencil.front.depthFailOp;
    markExtendedDynamicStateDirty();
}

void RenderDevice::setStencilPass( eStencilFunction param_1 ) {
    psoFactory.DepthStencil.front.passOp = kStencilFunctionLUT[param_1];
    if ( !psoFactory.bTwoSidedStencil )
        psoFactory.DepthStencil.back.passOp =
            psoFactory.DepthStencil.front.passOp;
    markExtendedDynamicStateDirty();
}

void RenderDevice::setStencilFunc( eComparisonFunction param_1 ) {
    psoFactory.DepthStencil.front.compareOp = kComparisonFunLUT[param_1];
    if ( !psoFactory.bTwoSidedStencil )
        psoFactory.DepthStencil.back.compareOp =
            psoFactory.DepthStencil.front.compareOp;
    markExtendedDynamicStateDirty();
}

void RenderDevice::setStencilRef( uint32_t param_1 ) {
    psoFactory.DepthStencil.front.reference = param_1;
    if ( !psoFactory.bTwoSidedStencil )
        psoFactory.DepthStencil.back.reference =
            psoFactory.DepthStencil.front.reference;
    psoFactory.bDirtyStencilDynamicState = true;
}

void RenderDevice::setStencilMask( uint32_t param_1 ) {
    psoFactory.DepthStencil.front.compareMask = param_1;
    if ( !psoFactory.bTwoSidedStencil )
        psoFactory.DepthStencil.back.compareMask =
            psoFactory.DepthStencil.front.compareMask;
    psoFactory.bDirtyStencilDynamicState = true;
}

void RenderDevice::setStencilWriteMask( uint32_t param_1 ) {
    psoFactory.DepthStencil.front.writeMask = param_1;
    if ( !psoFactory.bTwoSidedStencil )
        psoFactory.DepthStencil.back.writeMask =
            psoFactory.DepthStencil.front.writeMask;
    psoFactory.bDirtyStencilDynamicState = true;
}

void RenderDevice::setCCWStencilFail( eStencilFunction param_1 ) {
    psoFactory.DepthStencil.back.failOp = kStencilFunctionLUT[param_1];
    markExtendedDynamicStateDirty();
}

void RenderDevice::setCCWStencilZFail( eStencilFunction param_1 ) {
    psoFactory.DepthStencil.back.depthFailOp = kStencilFunctionLUT[param_1];
    markExtendedDynamicStateDirty();
}

void RenderDevice::setCCWStencilPass( eStencilFunction param_1 ) {
    psoFactory.DepthStencil.back.passOp = kStencilFunctionLUT[param_1];
    markExtendedDynamicStateDirty();
}

void RenderDevice::setCCWStencilFunc( eComparisonFunction param_1 ) {
    psoFactory.DepthStencil.back.compareOp = kComparisonFunLUT[param_1];
    markExtendedDynamicStateDirty();
}

void RenderDevice::setDepthWrite( bool param_1 ) {
    // D3D9 silently ignores ZWRITEENABLE when no depth surface is set.
    // In Vulkan, depthWriteEnable=TRUE without a depth attachment is invalid
    // (VUID-vkCmdDraw-None-09552), so gate writes on attachment presence.
    psoFactory.DepthStencil.depthWriteEnable =
        param_1 && bDepthStencilAttachmentValid;
    markExtendedDynamicStateDirty();
}

bool RenderDevice::uploadTexture( Texture *pTexture ) {
    uint32_t textureSize = Render2DB::CalcRenderTargetSize( pTexture );

    // Allocate staging buffer for all mips/slices
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = textureSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.flags = 0;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    GPUBuffer *pBuffer = new GPUBuffer();
    pBuffer->NumResources = 1;
    VkResult opResult = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo,
                                         &pBuffer->NativeBuffer[0],
                                         &pBuffer->Allocation[0], nullptr );
    OTDU_ASSERT( opResult == VK_SUCCESS );

    // Upload mips to staging buffer
    void *pMappedMemory = nullptr;
    opResult =
        vmaMapMemory( allocator, pBuffer->Allocation[0], &pMappedMemory );
    OTDU_ASSERT( opResult == VK_SUCCESS && pMappedMemory != nullptr );

    eViewFormat eVar2 = pTexture->Format;
    uint32_t local_1c = 0;
    uint32_t local_18 = 0;
    std::queue<size_t> mipOffsets; // Offsets in staging buffer
    size_t offset = 0;
    for ( int32_t j = 0; j < pTexture->Depth; j++ ) {
        local_1c = pTexture->Width;
        local_18 = pTexture->Height;

        for ( int32_t i = 0; i < pTexture->NumMipmap; i++ ) {
            uint32_t mipSize =
                Render2DB::CalcMipSize( local_1c, local_18, eVar2 );

            void *pTexels = Render2DB::GetTexelsPointer( pTexture, i, j );
            memcpy( (uint8_t *)pMappedMemory + offset, pTexels, mipSize );
            mipOffsets.push( offset );
            offset += mipSize;

            local_1c = local_1c >> 1;
            local_18 = local_18 >> 1;
        }
    }
    vmaUnmapMemory( allocator, pBuffer->Allocation[0] );

    // FIXME: Implement proper async texture upload (so that we do not have to
    // insert upload inside main pending cmdbuffer).
    if ( bRecordingCommands ) {
        endActiveRenderPass();
    }

    VkCommandBuffer cmdBuffer =
        ( bRecordingCommands ) ? getCommandBuffer()
                               : getPreFrameCommandBuffer( PFCL_TextureUpload );

    GPUTexture *pVulkanTex = pTexture->pTexture;

    const bool bIs3DTexture = ( ( pTexture->Flags >> 0xb ) & 1 ) != 0;

    VkImageSubresourceRange range;
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = pTexture->NumMipmap;
    range.baseArrayLayer = 0;
    range.layerCount = bIs3DTexture ? 1 : Max( 1, pTexture->Depth );

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = pVulkanTex->Image;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                          nullptr, 1, &barrier );

    std::vector<VkBufferImageCopy> copyRegions;
    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent.depth = 1; // pTexture->Depth;

    if ( bIs3DTexture ) {
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageOffset.z = 0;
        copyRegion.imageExtent.width = pTexture->Width;
        copyRegion.imageExtent.height = pTexture->Height;
        copyRegion.imageExtent.depth = pTexture->Depth;
        copyRegion.bufferOffset = mipOffsets.front();
        copyRegions.push_back( copyRegion );
    } else {
        for ( int32_t j = 0; j < pTexture->Depth; j++ ) {
            local_1c = pTexture->Width;
            local_18 = pTexture->Height;

            copyRegion.imageSubresource.baseArrayLayer = j;
            copyRegion.imageOffset.z = 0;
            for ( int32_t i = 0; i < pTexture->NumMipmap; i++ ) {
                copyRegion.imageSubresource.mipLevel = i;
                copyRegion.imageExtent.width = local_1c;
                copyRegion.imageExtent.height = local_18;

                size_t offset = mipOffsets.front();
                mipOffsets.pop();
                copyRegion.bufferOffset = offset;

                copyRegions.push_back( copyRegion );
                local_1c = local_1c >> 1;
                local_18 = local_18 >> 1;
            }
        }
    }

    vkCmdCopyBufferToImage( cmdBuffer, pBuffer->NativeBuffer[0],
                            pVulkanTex->Image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            (uint32_t)copyRegions.size(), copyRegions.data() );

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                          0, nullptr, 1, &barrier );

    pVulkanTex->CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Released once the frame submitting the copy completed
    destroyBuffer( pBuffer );

    return true;
}

void RenderDevice::uploadTexels( Texture *pTexture,
                                 const void *pTexels,
                                 const size_t texelsSize,
                                 const uint32_t mipIndex ) {
    OTDU_ASSERT( pTexels );
    OTDU_ASSERT_FATAL( !bRecordingCommands );

    // Allocate staging buffer for texels copy
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = texelsSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.flags = 0;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    GPUBuffer *pBuffer = new GPUBuffer();
    pBuffer->NumResources = 1;
    VkResult opResult = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo,
                                         &pBuffer->NativeBuffer[0],
                                         &pBuffer->Allocation[0], nullptr );
    OTDU_ASSERT( opResult == VK_SUCCESS );

    void *pMappedMemory = nullptr;
    opResult =
        vmaMapMemory( allocator, pBuffer->Allocation[0], &pMappedMemory );
    OTDU_ASSERT( opResult == VK_SUCCESS && pMappedMemory != nullptr );
    memcpy( pMappedMemory, pTexels, texelsSize );
    vmaUnmapMemory( allocator, pBuffer->Allocation[0] );

    VkCommandBuffer cmdBuffer = getPreFrameCommandBuffer( PFCL_TextureUpload );

    GPUTexture *pVulkanTex = pTexture->pTexture;

    VkImageSubresourceRange range;
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = mipIndex;
    range.levelCount = 1;
    range.baseArrayLayer = 0; // TODO: Support target array index
    range.layerCount = 1;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = pVulkanTex->Image;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                          nullptr, 1, &barrier );

    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = mipIndex;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent.width = pTexture->Width;
    copyRegion.imageExtent.height = pTexture->Height;
    copyRegion.imageExtent.depth = pTexture->Depth;
    vkCmdCopyBufferToImage(
        cmdBuffer, pBuffer->NativeBuffer[0], pVulkanTex->Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                          0, nullptr, 1, &barrier );

    pVulkanTex->CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    destroyBuffer( pBuffer );
}

void RenderDevice::beginScene() {
    // TODO: Split cmdbuffer to avoid submitting a big ass one (e.g. after
    // loading screen)
    if ( !bRecordingCommands ) {
        if ( bSwapchainOutOfDate.load( std::memory_order_acquire ) ) {
            recreateSwapchain();
        }

        recordingSerial = ++lastSerial;
        collectPreFrameCommands( recordingSerial );

        // The submit thread resets the slot pool and fence once the frame
        // previously recorded in this slot retired
        waitForRetiredSubmission( slotSerials[frameIndex] );
        slotSerials[frameIndex] = recordingSerial;
        commandBuffer = commandBuffers[frameIndex];

        psoFactory.reset();
        resourceBinder.onFrameBegin( device, frameIndex );
        OTDU_ASSERT( pendingDrawBatch.Count == 0u );
        indirectDrawBuffers[frameIndex].NumCommands = 0u;
        transientRings[frameIndex].Head = 0ull;

        vmaSetCurrentFrameIndex( allocator, absoluteFrameIndex );

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult beginResult =
            vkBeginCommandBuffer( commandBuffer, &beginInfo );
        OTDU_ASSERT( beginResult == VK_SUCCESS );
        bRecordingCommands = true;
        resetRecordedBindings();
        lastPassColorAttachments.fill( nullptr );
        pLastPassDepthStencil = nullptr;

        if ( bNeedBackbufferClear ) {
            clearColorRenderTarget( backbuffer.pTexture, gColorBlack,
                                    ClearRegion{} );
            bNeedBackbufferClear = false;
        }

        resourceBinder.dirtyStates = 0xffffffff;

        if ( computeDescriptorPools[frameIndex] != VK_NULL_HANDLE ) {
            vkResetDescriptorPool( device, computeDescriptorPools[frameIndex],
                                   0 );
        }

        flushDeferredBarriers( nullptr );
        for ( const auto &callback : beginSceneCallbacks ) {
            callback.second();
        }
    }
}

void RenderDevice::endScene() {
    // NOP
}

void RenderDevice::bindRenderTarget( GPUTexture *param_1, uint32_t param_2 ) {
    bFramebufferAttachmentValid[param_2] = ( param_1 != nullptr );
    pFramebufferAttachments[param_2] = param_1;

    renderPassManager.setAttachment( param_1, param_2 );
}

void RenderDevice::bindDepthStencil( GPUTexture *param_1 ) {
    bDepthStencilAttachmentValid = ( param_1 != nullptr );
    pDepthStencilAttachment = param_1;

#if OTDU_DEVBUILD
    if ( param_1 != nullptr && psoFactory.DepthStencil.stencilTestEnable ) {
        OTDU_ASSERT_FATAL( HasStencilBits( param_1->ViewFormat ) );
    }
#endif

    // When the depth attachment is removed, immediately clear PSO depth state
    // so stale test/write enables don't linger into the next depthless pass.
    if ( !bDepthStencilAttachmentValid ) {
        psoFactory.DepthStencil.depthTestEnable = VK_FALSE;
        psoFactory.DepthStencil.depthWriteEnable = VK_FALSE;
        markExtendedDynamicStateDirty();
    }

    renderPassManager.setDepthStencilAttachment( param_1 );
}

void RenderDevice::setClipPlane( int32_t param_1, float *param_2 ) {
    resourceBinder.setClipPlane( param_1, param_2 );
}

void RenderDevice::resolveMSAA( RenderTarget *param_1, uint32_t param_2 ) {
    // FUN_005fd160
    // In the original (D3D9) the render target surface is StretchRect'ed into
    // the bound 2DB texture (so that the pass output can be sampled by
    // later passes/postfx). Since RTs and 2DBs are distinct images in the
    // Vulkan backend, this is translated to an explicit blit (or a resolve
    // when the source is multisampled).
    Render2DB *pBound2DB = param_1->getBound2DB();
    if ( pBound2DB == nullptr ) {
        return;
    }

    Texture *pBound2DBBitmap = pBound2DB->getFirstBitmap();
    if ( pBound2DBBitmap == nullptr || pBound2DBBitmap->pTexture == nullptr ) {
        return;
    }

    GPUTexture *pSrc = param_1->getTextureColor();
    GPUTexture *pDst = pBound2DBBitmap->pTexture;
    if ( pSrc == nullptr || pSrc == pDst ) {
        return;
    }

    endActiveRenderPass();

    insertImageBarrier(
        pSrc, pSrc->CurrentLayout, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT );
    insertImageBarrier(
        pDst, pDst->CurrentLayout, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT );

    VkImage srcImage = pSrc->Image;
    VkImage dstImage = pDst->Image;

    uint32_t dstWidth = Max( 1u, pDst->Width >> param_2 );
    uint32_t dstHeight = Max( 1u, pDst->Height >> param_2 );

    if ( pSrc->SampleCount != VK_SAMPLE_COUNT_1_BIT ) {
        VkImageResolve region = {};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = param_2;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.extent.width = Min( pSrc->Width, dstWidth );
        region.extent.height = Min( pSrc->Height, dstHeight );
        region.extent.depth = 1;

        vkCmdResolveImage( getCommandBuffer(), srcImage, pSrc->CurrentLayout,
                           dstImage, pDst->CurrentLayout, 1, &region );
    } else {
        VkImageBlit region = {};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcOffsets[0] = { 0, 0, 0 };
        region.srcOffsets[1].x = (int32_t)pSrc->Width;
        region.srcOffsets[1].y = (int32_t)pSrc->Height;
        region.srcOffsets[1].z = 1;

        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = param_2;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.dstOffsets[0] = { 0, 0, 0 };
        region.dstOffsets[1].x = (int32_t)dstWidth;
        region.dstOffsets[1].y = (int32_t)dstHeight;
        region.dstOffsets[1].z = 1;

        // D3DTEXF_POINT in the original StretchRect
        vkCmdBlitImage( getCommandBuffer(), srcImage, pSrc->CurrentLayout,
                        dstImage, pDst->CurrentLayout, 1, &region,
                        VK_FILTER_NEAREST );
    }

    insertImageBarrier(
        pDst, pDst->CurrentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT );
}

void RenderDevice::setDepthCompareFunc( eComparisonFunction param_1 ) {
    psoFactory.DepthStencil.depthCompareOp = kComparisonFunLUT[param_1];
    markExtendedDynamicStateDirty();
}

void RenderDevice::setDepthStencilEnabled( bool param_1 ) {
    psoFactory.DepthStencil.depthTestEnable =
        param_1 && bDepthStencilAttachmentValid;
    markExtendedDynamicStateDirty();

    bDepthStencilEnabled = param_1;
}

// FUN_00601c60
static constexpr VkBlendFactor kBlendModes[eBlendMode::BM_Count] = {
    VkBlendFactor::VK_BLEND_FACTOR_ZERO,
    VkBlendFactor::VK_BLEND_FACTOR_ZERO,
    VkBlendFactor::VK_BLEND_FACTOR_ONE,
    VkBlendFactor::VK_BLEND_FACTOR_SRC_COLOR,
    VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    VkBlendFactor::VK_BLEND_FACTOR_SRC_ALPHA,
    VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    VkBlendFactor::VK_BLEND_FACTOR_DST_ALPHA,
    VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    VkBlendFactor::VK_BLEND_FACTOR_DST_COLOR,
    VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    VkBlendFactor::VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
    VkBlendFactor::VK_BLEND_FACTOR_SRC_ALPHA,
    VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA };

static constexpr VkBlendOp kBlendOP[eBlendOP::BO_Count] = {
    VkBlendOp::VK_BLEND_OP_ADD,      VkBlendOp::VK_BLEND_OP_ADD,
    VkBlendOp::VK_BLEND_OP_SUBTRACT, VkBlendOp::VK_BLEND_OP_REVERSE_SUBTRACT,
    VkBlendOp::VK_BLEND_OP_MIN,      VkBlendOp::VK_BLEND_OP_MAX,
    VkBlendOp::VK_BLEND_OP_MAX_ENUM };

void RenderDevice::setBlendSrc( eBlendMode param_1 ) {
    if ( psoFactory.srcBlendFactor == kBlendModes[param_1] ) {
        return;
    }
    psoFactory.srcBlendFactor = kBlendModes[param_1];
    psoFactory.bDirty = true;
}

void RenderDevice::setBlendDst( eBlendMode param_1 ) {
    if ( psoFactory.dstBlendFactor == kBlendModes[param_1] ) {
        return;
    }
    psoFactory.dstBlendFactor = kBlendModes[param_1];
    psoFactory.bDirty = true;
}

void RenderDevice::setBlendOP( eBlendOP param_1 ) {
    if ( psoFactory.blendOP == kBlendOP[param_1] ) {
        return;
    }
    psoFactory.blendOP = kBlendOP[param_1];
    psoFactory.bDirty = true;
}

void RenderDevice::pushDebugLabel( const char *pLabel ) {
#if OTDU_DEVBUILD
    VkDebugUtilsLabelEXT debugLabel = {};
    debugLabel.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    debugLabel.pNext = nullptr;
    debugLabel.pLabelName = pLabel;
    debugLabel.color[0] = 0.0f;
    debugLabel.color[1] = 0.0f;
    debugLabel.color[2] = 0.0f;
    debugLabel.color[3] = 0.0f;

    vkCmdBeginDebugUtilsLabelEXT( getStateCommandBuffer(), &debugLabel );
#endif
}

void RenderDevice::popDebugLabel() {
#if OTDU_DEVBUILD
    vkCmdEndDebugUtilsLabelEXT( getStateCommandBuffer() );
#endif
}

bool RenderDevice::doesFormatSupportsMSAA( eAntiAliasingMethod msaaMode,
                                           eViewFormat format ) {
    // FUN_00509e10
    if ( formatCaps[format].bSupported ) {
        if ( msaaMode == eAntiAliasingMethod::AAM_MSAA_X2 ) {
            return formatCaps[format].bSupportMSAAx2;
        } else if ( msaaMode == eAntiAliasingMethod::AAM_MSAA_X4 ) {
            return formatCaps[format].bSupportMSAAx4;
        } else {
            return true;
        }
    }

    return false;
}

bool RenderDevice::doesFormatSupportsBlending( eViewFormat format ) {
    // FUN_00506500
    return formatCaps[format].bSupportBlending;
}

bool RenderDevice::isMultisampled( const GPUTexture *pTexture ) const {
    return pTexture != nullptr &&
           pTexture->SampleCount != VK_SAMPLE_COUNT_1_BIT;
}

VkSampleCountFlagBits
RenderDevice::clampSampleCount( VkSampleCountFlagBits requested ) const {
    if ( requested == VK_SAMPLE_COUNT_1_BIT ) {
        return VK_SAMPLE_COUNT_1_BIT;
    }

    const VkSampleCountFlags supported =
        deviceProperties.properties.limits.framebufferColorSampleCounts &
        deviceProperties.properties.limits.framebufferDepthSampleCounts;

    if ( ( supported & requested ) != 0 ) {
        return requested;
    }

    OTDU_LOG_WARN(
        "Requested MSAA sample count (0x%x) is not supported by this "
        "device; falling back to single-sample.\n",
        requested );
    return VK_SAMPLE_COUNT_1_BIT;
}

GPUShader *
RenderDevice::createShader( eShaderType type,
                            const void *pBytecode,
                            const size_t bytecodeSize,
                            const ShaderMetadataSPIRVRuntime *pMetadata,
                            const uint64_t hashcode ) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.codeSize = bytecodeSize;
    createInfo.pCode = (const uint32_t *)pBytecode;

    VkShaderModule shaderModule;
    if ( vkCreateShaderModule( device, &createInfo, nullptr, &shaderModule ) !=
         VK_SUCCESS ) {
        return nullptr;
    }

    GPUShader *pShader = new GPUShader();
    pShader->ShaderModule = shaderModule;
    pShader->pMetadata = pMetadata;
    pShader->Hashcode = hashcode;

    // Build bitmask to avoid wasteful descriptor update(s)
    pShader->SRVMask = 0ull;
    static constexpr uint32_t kVulkanMaxSRVRegister =
        kVulkanSRVStartBindingSlot + kVulkanMaxNumSRV;
    for ( const SPIRVBindingInfo &binding : pMetadata->Bindings ) {
        if ( binding.binding >= kVulkanSRVStartBindingSlot &&
             binding.binding <= kVulkanMaxSRVRegister ) {
            uint32_t registerIndex = kVulkanMaxSRVRegister - binding.binding;
            pShader->SRVMask |= ( 1ull << registerIndex );
        }
    }

    return pShader;
}

GPUVertexLayout *
RenderDevice::createVertexLayout( VertexLayoutAttribute *pAttributes,
                                  const bool bInterleavedAttributes ) {
    OTDU_ASSERT( pAttributes );

    GPUVertexLayout *pLayout =
        new ( TestDrive::Alloc( sizeof( GPUVertexLayout ) ) ) GPUVertexLayout();

    // For interleaved layout, total stride is computed up front.
    uint32_t vertexStride = 0;
    if ( bInterleavedAttributes ) {
        VertexLayoutAttribute *pAttr = pAttributes;
        while ( pAttr->Type != kVertexLayoutSentinel.Type &&
                pAttr->Type != eVertexAttributeFormat::VAF_Count ) {
            vertexStride += GetVertexAttributeSize( pAttr->Type );
            pAttr++;
        }
    }

    // Pre-pass: for each D3D9 stream (= Vulkan binding), compute the stride
    // and whether it carries instance-rate data.
    //
    // In D3D9 a single stream can hold multiple vertex elements at different
    // byte offsets (e.g. the instancing buffer packs 3 matrix rows + a colour
    // into one stream with stride 0x40).  Vulkan requires one
    // VkVertexInputBindingDescription per binding, so we must de-duplicate.
    //
    // Stride rule:
    //   - single-attribute stream  → GetVertexAttributeSize(format)  (no
    //     rounding; avoids inflating tightly-packed streams like Position xyz)
    //   - multi-attribute stream   → align(max(Offset + attrSize), 16)
    //     (matches the D3D9 SetStreamSource stride, e.g. 0x34 → 0x40 for the
    //     instance buffer)
    struct StreamInfo {
        uint32_t maxEnd = 0;
        uint32_t attrCount = 0;
        bool hasInstanceData = false;
    };
    std::unordered_map<uint16_t, StreamInfo> streamInfos;

    {
        VertexLayoutAttribute *pAttr = pAttributes;
        while ( pAttr->Type != kVertexLayoutSentinel.Type &&
                pAttr->Type != eVertexAttributeFormat::VAF_Count ) {
            StreamInfo &info = streamInfos[pAttr->Stream];
            uint32_t aSize = GetVertexAttributeSize( pAttr->Type );
            info.maxEnd = Max( info.maxEnd, pAttr->Offset + aSize );
            info.attrCount++;
            if ( pAttr->Type ==
                 eVertexAttributeFormat::VAF_INSTANCE_R32G32B32A32_SFLOAT ) {
                info.hasInstanceData = true;
            }
            pAttr++;
        }
    }

    // Main pass: emit one semantic per attribute and one binding per stream.
    std::unordered_set<uint16_t> addedBindings;
    VertexLayoutAttribute *pAttributeIt = pAttributes;
    while ( pAttributeIt->Type != kVertexLayoutSentinel.Type &&
            pAttributeIt->Type != eVertexAttributeFormat::VAF_Count ) {
        const VertexLayoutAttribute &layoutAttribute = *pAttributeIt;
        const uint16_t streamIdx = layoutAttribute.Stream;
        OTDU_ASSERT( streamIdx < kNullVertexBinding );

        // Semantic — always added, one per attribute.
        // SPIRV location is resolved at PSO-build time by matching
        // Usage+UsageIndex against the active VS's ISGN.
        GPUVertexLayoutSemantic semantic = {};
        semantic.Binding = streamIdx;
        semantic.Offset = layoutAttribute.Offset;
        semantic.Format = GetVertexAttributeVkFormat( layoutAttribute.Type );
        semantic.Usage = layoutAttribute.Usage;
        semantic.UsageIndex = layoutAttribute.UsageIndex;
        pLayout->Semantics.push_back( semantic );

        // Binding description — one entry per unique D3D9 stream.
        if ( addedBindings.find( streamIdx ) == addedBindings.end() ) {
            addedBindings.insert( streamIdx );

            const StreamInfo &info = streamInfos[streamIdx];

            uint32_t stride;
            if ( bInterleavedAttributes ) {
                stride = vertexStride;
            } else if ( info.attrCount == 1 ) {
                stride = GetVertexAttributeSize( layoutAttribute.Type );
            } else {
                // Align to 16 bytes to match D3D9 SetStreamSource strides for
                // packed multi-attribute streams (e.g. instance buffer
                // 0x34→0x40).
                stride = ( info.maxEnd + 15u ) & ~15u;
            }

            VkVertexInputBindingDescription bindingDesc = {};
            bindingDesc.binding = streamIdx;
            bindingDesc.stride = stride;
            bindingDesc.inputRate = info.hasInstanceData
                                        ? VK_VERTEX_INPUT_RATE_INSTANCE
                                        : VK_VERTEX_INPUT_RATE_VERTEX;
            pLayout->InputBindings.push_back( bindingDesc );
            pLayout->bHasInstanceRateBinding |= info.hasInstanceData;
        }

        pAttributeIt++;
    }

    pLayout->updateHashcode();
    return pLayout;
}

void GPUVertexLayout::updateHashcode() {
    std::vector<uint32_t> words;
    words.reserve( Semantics.size() * 5 + InputBindings.size() * 3 );
    for ( const GPUVertexLayoutSemantic &semantic : Semantics ) {
        words.push_back( semantic.Binding );
        words.push_back( semantic.Offset );
        words.push_back( semantic.Format );
        words.push_back( semantic.Usage );
        words.push_back( semantic.UsageIndex );
    }
    for ( const VkVertexInputBindingDescription &binding : InputBindings ) {
        words.push_back( binding.binding );
        words.push_back( binding.stride );
        words.push_back( binding.inputRate );
    }
    Hashcode = CityHash64( (const char *)words.data(),
                           words.size() * sizeof( uint32_t ) );
}

void RenderDevice::destroyShader( GPUShader *pShader ) {
    vkDestroyShaderModule( device, pShader->ShaderModule, nullptr );
    delete pShader;
}

bool RenderDevice::retrieveAdapterInfos( const uint32_t adapterIndex,
                                         struct GPUAdapterDesc *pOutDesc ) {
    if ( adapterIndex >= physicalDevices.size() ) {
        OTDU_LOG_WARN(
            "Failed to retrieve adapter %u infos (index out of bounds)\n",
            adapterIndex );
        return false;
    }

    VkPhysicalDeviceProperties2 properties2 = {};
    memset( &properties2, 0, sizeof( VkPhysicalDeviceProperties2 ) );
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vkGetPhysicalDeviceProperties2( physicalDevice, &properties2 );

    pOutDesc->AdapterName = properties2.properties.deviceName;
    pOutDesc->DeviceDesc = properties2.properties.deviceName;
    pOutDesc->DeviceID = properties2.properties.deviceID;
    pOutDesc->VendorId = properties2.properties.vendorID;
    pOutDesc->Revision = properties2.properties.driverVersion;
    pOutDesc->SubSysId = 0;

    return true;
}

bool RenderDevice::createVulkanInstance() {
    // This finds the KHR surface extensions needed to display on the right
    // platform
    uint32_t sdlExtensionCount = 0;
#if defined( OTDU_USE_SDL )
    const char *const *sdlExtensions =
        SDL_Vulkan_GetInstanceExtensions( &sdlExtensionCount );
#endif

    uint32_t count = 0u;
    vkEnumerateInstanceExtensionProperties( nullptr, &count, nullptr );
    instanceExtensionsAvailable.resize( count );
    vkEnumerateInstanceExtensionProperties(
        nullptr, &count, instanceExtensionsAvailable.data() );

    VkApplicationInfo applicationInfo = {};
    memset( &applicationInfo, 0, sizeof( VkApplicationInfo ) );
    applicationInfo.pApplicationName = "OpenTDU";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "OpenTDU";
    applicationInfo.engineVersion = 1;
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    // Add extensions requested by sdl
    instanceExtensions.insert( instanceExtensions.end(), sdlExtensions,
                               sdlExtensions + sdlExtensionCount );
    if ( extensionIsAvailable( VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                               instanceExtensionsAvailable ) ) {
        instanceExtensions.push_back(
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME ); // Allow debug utils (naming
                                                 // objects)
    }

    bSwapchainColorSpaceSupported =
        extensionIsAvailable( VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
                              instanceExtensionsAvailable );
    if ( bSwapchainColorSpaceSupported ) {
        instanceExtensions.push_back(
            VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME );
    }

#if OTDU_MACOS
    if ( extensionIsAvailable( VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
                               instanceExtensionsAvailable ) ) {
        instanceExtensions.push_back(
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME );
    }
#endif

    // Setting for the validation layer
    ValidationSettings validationSettings = {};

    // Adding the validation layer
    if ( gVulkanEnableValidationLayer ) {
        instanceLayers.push_back( "VK_LAYER_KHRONOS_validation" );
        validationSettings.validate_core = VK_TRUE;
        validationSettings.validate_sync =
            gVulkanEnableSyncValidation ? VK_TRUE : VK_FALSE;
    }

    VkInstanceCreateInfo instanceCreateInfo;
    memset( &instanceCreateInfo, 0, sizeof( VkInstanceCreateInfo ) );
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pNext = validationSettings.buildPNextChain();
    instanceCreateInfo.pApplicationInfo = &applicationInfo;
    instanceCreateInfo.enabledLayerCount = uint32_t( instanceLayers.size() );
    instanceCreateInfo.ppEnabledLayerNames = instanceLayers.data();
    instanceCreateInfo.enabledExtensionCount =
        uint32_t( instanceExtensions.size() );
    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
#ifdef OTDU_MACOS
    instanceCreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    OTDU_LOG_DEBUG( "Creating Vulkan instance...\n" );

    VkResult opResult =
        vkCreateInstance( &instanceCreateInfo, nullptr, &instance );

    if ( opResult != VK_SUCCESS || instance == nullptr ) {
        OTDU_LOG_ERROR( "Failed to create Vulkan Instance (error code: %x)\n",
                        opResult );
        return false;
    }

    // Load all Vulkan functions
    volkLoadInstance( instance );

    return true;
}

bool RenderDevice::selectPhysicalDevice() {
    OTDU_LOG_DEBUG( "Enumerating Vulkan devices...\n" );
    physicalDeviceIndex = std::numeric_limits<uint32_t>::max();

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices( instance, &deviceCount, nullptr );
    OTDU_LOG_DEBUG( "Found %u devices\n", deviceCount );

    physicalDevices.resize( deviceCount );
    vkEnumeratePhysicalDevices( instance, &deviceCount,
                                physicalDevices.data() );

    // Prefer discrete GPUs, then the most recent Vulkan version
    int32_t bestScore = -1;
    uint32_t bestApiVersion = 0;
    for ( uint32_t i = 0; i < physicalDevices.size(); i++ ) {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties( physicalDevices[i], &properties );

        VulkanDeviceCaps caps;
        std::string missing;
        if ( !QueryDeviceCaps( physicalDevices[i], caps, missing ) ) {
            OTDU_LOG_WARN( "Skipping '%s': %s is not supported\n",
                           properties.deviceName, missing.c_str() );
            continue;
        }

        const int32_t score = GetDeviceTypeScore( properties.deviceType );
        if ( score > bestScore ||
             ( score == bestScore && caps.ApiVersion > bestApiVersion ) ) {
            bestScore = score;
            bestApiVersion = caps.ApiVersion;
            physicalDeviceIndex = i;
            deviceCaps = caps;
        }
    }

    if ( physicalDeviceIndex == std::numeric_limits<uint32_t>::max() ) {
        OTDU_LOG_ERROR( "Could not find a GPU supporting the required Vulkan "
                        "1.2 features (see above)!\n" );
        return false;
    }

    physicalDevice = physicalDevices[physicalDeviceIndex];

    deviceProperties = {};
    memset( &deviceProperties, 0, sizeof( VkPhysicalDeviceProperties2 ) );
    deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vkGetPhysicalDeviceProperties2( physicalDevice, &deviceProperties );

    OTDU_LOG_DEBUG( "Selected GPU: %s\n",
                    deviceProperties.properties.deviceName );
    OTDU_LOG_DEBUG(
        "Driver: %d.%d.%d\n",
        VK_VERSION_MAJOR( deviceProperties.properties.driverVersion ),
        VK_VERSION_MINOR( deviceProperties.properties.driverVersion ),
        VK_VERSION_PATCH( deviceProperties.properties.driverVersion ) );
    OTDU_LOG_DEBUG(
        "Vulkan API: %d.%d.%d\n",
        VK_VERSION_MAJOR( deviceProperties.properties.apiVersion ),
        VK_VERSION_MINOR( deviceProperties.properties.apiVersion ),
        VK_VERSION_PATCH( deviceProperties.properties.apiVersion ) );
    OTDU_LOG_INFO( "Device capabilities: extended dynamic state %d, depth "
                   "clamp %d, non solid fill %d, anisotropy %d (max %.0f), "
                   "maintenance4 %d\n",
                   deviceCaps.bExtendedDynamicState, deviceCaps.bDepthClamp,
                   deviceCaps.bFillModeNonSolid, deviceCaps.bSamplerAnisotropy,
                   deviceCaps.MaxSamplerAnisotropy, deviceCaps.bMaintenance4 );

    return true;
}

bool RenderDevice::createDevice() {
    const float queuePriority = 1.0F;
    queues.clear();
    queues.emplace_back( getQueue( VK_QUEUE_GRAPHICS_BIT ) );

    // Request only one queue : graphic
    // User could request more specific queues: compute, transfer
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    memset( &queueCreateInfo, 0, sizeof( VkDeviceQueueCreateInfo ) );
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queues[0].familyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    if ( deviceCaps.bDemoteExtension ) {
        deviceExtensions.push_back(
            VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME );
    }
    if ( deviceCaps.bExtendedDynamicStateExtension ) {
        deviceExtensions.push_back(
            VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME );
    }
    bHDRMetadataSupported = deviceCaps.bHDRMetadata;
    if ( bHDRMetadataSupported ) {
        deviceExtensions.push_back( VK_EXT_HDR_METADATA_EXTENSION_NAME );
    }
    if ( deviceCaps.bPortabilitySubset ) {
        deviceExtensions.push_back( "VK_KHR_portability_subset" );
    }

    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demoteFeatures =
        {};
    demoteFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
    demoteFeatures.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT edsFeatures = {};
    edsFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    edsFeatures.extendedDynamicState = VK_TRUE;

    VkPhysicalDeviceVulkan13Features deviceFeatures13 = {};
    deviceFeatures13.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    deviceFeatures13.shaderDemoteToHelperInvocation = VK_TRUE;
    deviceFeatures13.maintenance4 = deviceCaps.bMaintenance4;

    VkPhysicalDeviceVulkan12Features deviceFeatures12 = {};
    deviceFeatures12.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    deviceFeatures12.runtimeDescriptorArray = VK_TRUE;
    deviceFeatures12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    deviceFeatures12.descriptorBindingPartiallyBound = VK_TRUE;
    deviceFeatures12.descriptorBindingUpdateUnusedWhilePending =
        deviceCaps.bUpdateUnusedWhilePending;
    if ( deviceCaps.ApiVersion >= VK_API_VERSION_1_3 ) {
        deviceFeatures12.pNext = &deviceFeatures13;
    } else {
        deviceFeatures12.pNext = &demoteFeatures;
        if ( deviceCaps.bExtendedDynamicStateExtension ) {
            demoteFeatures.pNext = &edsFeatures;
        }
    }

    VkPhysicalDeviceFeatures2 deviceFeatures = {};
    deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures.pNext = &deviceFeatures12;
    deviceFeatures.features.shaderClipDistance = VK_TRUE;
    deviceFeatures.features.depthClamp = deviceCaps.bDepthClamp;
    deviceFeatures.features.fillModeNonSolid = deviceCaps.bFillModeNonSolid;
    deviceFeatures.features.samplerAnisotropy = deviceCaps.bSamplerAnisotropy;
    deviceFeatures.features.multiDrawIndirect = deviceCaps.bMultiDrawIndirect;
    deviceFeatures.features.drawIndirectFirstInstance =
        deviceCaps.bMultiDrawIndirect;

    // Create the logical device
    VkDeviceCreateInfo deviceCreateInfo = {};
    memset( &deviceCreateInfo, 0, sizeof( VkDeviceCreateInfo ) );
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = &deviceFeatures;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount =
        uint32_t( deviceExtensions.size() );
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkResult op =
        vkCreateDevice( physicalDevice, &deviceCreateInfo, nullptr, &device );
    if ( op != VK_SUCCESS ) {
        return false;
    }

    volkLoadDevice( device ); // Load all Vulkan device functions

    if ( deviceCaps.bExtendedDynamicStateExtension ) {
        cmdSetCullMode = vkCmdSetCullModeEXT;
        cmdSetDepthTestEnable = vkCmdSetDepthTestEnableEXT;
        cmdSetDepthWriteEnable = vkCmdSetDepthWriteEnableEXT;
        cmdSetDepthCompareOp = vkCmdSetDepthCompareOpEXT;
        cmdSetStencilTestEnable = vkCmdSetStencilTestEnableEXT;
        cmdSetStencilOp = vkCmdSetStencilOpEXT;
    } else if ( deviceCaps.bExtendedDynamicState ) {
        cmdSetCullMode = vkCmdSetCullMode;
        cmdSetDepthTestEnable = vkCmdSetDepthTestEnable;
        cmdSetDepthWriteEnable = vkCmdSetDepthWriteEnable;
        cmdSetDepthCompareOp = vkCmdSetDepthCompareOp;
        cmdSetStencilTestEnable = vkCmdSetStencilTestEnable;
        cmdSetStencilOp = vkCmdSetStencilOp;
    }

    // Get the requested queues
    vkGetDeviceQueue( device, queues[0].familyIndex, queues[0].queueIndex,
                      &queues[0].queue );

    return true;
}

void RenderDevice::createDebugCallback() {
#if OTDU_DEVBUILD
    // Add the debug callback
    if ( gVulkanEnableValidationLayer && vkCreateDebugUtilsMessengerEXT ) {
        VkDebugUtilsMessengerCreateInfoEXT createInfos = {};
        createInfos.sType =
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfos.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfos.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        createInfos.pfnUserCallback = VulkanDebugCallback;
        vkCreateDebugUtilsMessengerEXT( instance, &createInfos, nullptr,
                                        &debugCallback );
    }
#endif
}

void RenderDevice::createVMAAllocator() {
    // Initialize the VMA allocator
    VmaAllocatorCreateInfo infos = {};
    memset( &infos, 0, sizeof( VmaAllocatorCreateInfo ) );
    infos.physicalDevice = physicalDevice;
    infos.device = device;
    infos.instance = instance;
    // infos.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;  // allow
    // querying for the GPU address of a buffer
    infos.vulkanApiVersion = ( deviceCaps.ApiVersion >= VK_API_VERSION_1_3 )
                                 ? VK_API_VERSION_1_3
                                 : VK_API_VERSION_1_2;
    if ( deviceCaps.bMaintenance4 ) {
        infos.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;
    }

    VmaVulkanFunctions functions = {};
    memset( &functions, 0, sizeof( VmaVulkanFunctions ) );
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    infos.pVulkanFunctions = &functions;

    vmaCreateAllocator( &infos, &allocator );

    resourceBinder.allocator = allocator;
}

static bool
IsPipelineCacheCompatible( const std::vector<char> &blob,
                           const VkPhysicalDeviceProperties &properties ) {
    VkPipelineCacheHeaderVersionOne header = {};
    if ( blob.size() < sizeof( VkPipelineCacheHeaderVersionOne ) ) {
        return false;
    }
    memcpy( &header, blob.data(), sizeof( VkPipelineCacheHeaderVersionOne ) );

    return header.headerSize >= sizeof( VkPipelineCacheHeaderVersionOne ) &&
           header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header.vendorID == properties.vendorID &&
           header.deviceID == properties.deviceID &&
           memcmp( header.pipelineCacheUUID, properties.pipelineCacheUUID,
                   VK_UUID_SIZE ) == 0;
}

void RenderDevice::createPipelineCache() {
    std::vector<char> blob;

    if ( gpFile != nullptr ) {
        const std::filesystem::path cacheFolder =
            std::filesystem::path( gpFile->getSaveFolder() ) / "cache";
        psoCachePath = cacheFolder / "pso_cache.bin";
        psoLibraryPath = cacheFolder / "pso_library.bin";
        psoLibrary.load( psoLibraryPath );

        char pWorkingDir[OTDU_MAX_PATH];
        TestDrive::GetWorkingDir( pWorkingDir, OTDU_MAX_PATH );
        psoSeed.load( std::filesystem::path( pWorkingDir ) / "opentdu_data" /
                      "pso_library.bin" );

        std::ifstream file( psoCachePath, std::ios::binary | std::ios::ate );
        if ( file.is_open() ) {
            blob.resize( static_cast<size_t>( file.tellg() ) );
            file.seekg( 0 );
            file.read( blob.data(), blob.size() );
            if ( !file ) {
                blob.clear();
            }
        }
    }

    if ( !blob.empty() &&
         !IsPipelineCacheCompatible( blob, deviceProperties.properties ) ) {
        OTDU_LOG_INFO( "Pipeline cache '%s' was created for another "
                       "device/driver; discarding it\n",
                       psoCachePath.string().c_str() );
        blob.clear();
    }

    VkPipelineCacheCreateInfo cacheInfos = {};
    cacheInfos.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfos.initialDataSize = blob.size();
    cacheInfos.pInitialData = blob.empty() ? nullptr : blob.data();

    VkResult result =
        vkCreatePipelineCache( device, &cacheInfos, nullptr, &psoCache );
    if ( result != VK_SUCCESS && !blob.empty() ) {
        OTDU_LOG_WARN( "Failed to load pipeline cache '%s' (error %d); "
                       "starting from an empty cache\n",
                       psoCachePath.string().c_str(), result );
        cacheInfos.initialDataSize = 0;
        cacheInfos.pInitialData = nullptr;
        result =
            vkCreatePipelineCache( device, &cacheInfos, nullptr, &psoCache );
    }

    if ( result != VK_SUCCESS ) {
        psoCache = VK_NULL_HANDLE;
        OTDU_LOG_WARN( "Failed to create pipeline cache (error %d)\n", result );
        return;
    }

    psoCacheSizeOnDisk = blob.size();
    OTDU_LOG_INFO( "Pipeline cache ready (%zu bytes loaded)\n", blob.size() );
}

void RenderDevice::savePipelineCache() {
    if ( psoCache == VK_NULL_HANDLE || psoCachePath.empty() ) {
        return;
    }

    const std::chrono::steady_clock::time_point saveStart =
        std::chrono::steady_clock::now();

    size_t dataSize = 0;
    if ( vkGetPipelineCacheData( device, psoCache, &dataSize, nullptr ) !=
             VK_SUCCESS ||
         dataSize == 0 || dataSize == psoCacheSizeOnDisk ) {
        return;
    }

    std::vector<char> blob( dataSize );
    if ( vkGetPipelineCacheData( device, psoCache, &dataSize, blob.data() ) !=
         VK_SUCCESS ) {
        return;
    }

    std::error_code errorCode;
    std::filesystem::create_directories( psoCachePath.parent_path(),
                                         errorCode );

    // Write to a temporary file first so an interrupted save never leaves a
    // truncated cache behind
    std::filesystem::path tmpPath = psoCachePath;
    tmpPath += ".tmp";
    {
        std::ofstream file( tmpPath, std::ios::binary | std::ios::trunc );
        file.write( blob.data(), dataSize );
        if ( !file ) {
            OTDU_LOG_WARN( "Failed to write pipeline cache '%s'\n",
                           tmpPath.string().c_str() );
            return;
        }
    }

    std::filesystem::rename( tmpPath, psoCachePath, errorCode );
    if ( errorCode ) {
        OTDU_LOG_WARN( "Failed to save pipeline cache '%s': %s\n",
                       psoCachePath.string().c_str(),
                       errorCode.message().c_str() );
        std::filesystem::remove( tmpPath, errorCode );
        return;
    }

    psoCacheSizeOnDisk = dataSize;

    const std::chrono::duration<double, std::milli> saveDuration =
        std::chrono::steady_clock::now() - saveStart;
    OTDU_LOG_INFO( "Saved pipeline cache (%zu bytes, %zu PSOs, %.2f ms)\n",
                   dataSize, psoFactory.getNumCachedPSOs(),
                   saveDuration.count() );
}

void RenderDevice::savePipelineCacheWhenIdle() {
    static constexpr std::chrono::seconds kPSOIdleDelay( 10 );

    const size_t numPSOs = psoFactory.getNumCachedPSOs();
    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    if ( numPSOs != numPSOsAtLastCheck ) {
        numPSOsAtLastCheck = numPSOs;
        lastPSOCreationTime = now;
        return;
    }

    if ( numPSOs != numPSOsAtLastCacheSave &&
         now - lastPSOCreationTime >= kPSOIdleDelay ) {
        numPSOsAtLastCacheSave = numPSOs;
        savePipelineCache();
        savePSOLibrary();
    }
}

void RenderDevice::savePSOLibrary() {
    if ( psoLibrary.isDirty() && !psoLibraryPath.empty() ) {
        psoLibrary.save( psoLibraryPath );
    }
}

bool RenderDevice::isPSORecordSupported( const PSORecord &record ) const {
    const VkPhysicalDeviceLimits &limits = deviceProperties.properties.limits;

    for ( const PSORecord::Semantic &semantic : record.Semantics ) {
        VkFormatProperties properties = {};
        vkGetPhysicalDeviceFormatProperties( physicalDevice, semantic.Format,
                                             &properties );
        if ( ( properties.bufferFeatures &
               VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT ) == 0 ) {
            return false;
        }
    }

    const RenderPassDesc &renderPass = record.RenderPass;
    for ( uint32_t i = 0; i < renderPass.getNumAttachments(); i++ ) {
        const VkAttachmentDescription &attachment = renderPass.Attachments[i];
        const bool bIsDepthStencil = ( i == renderPass.NumColorAttachments );

        VkFormatProperties properties = {};
        vkGetPhysicalDeviceFormatProperties( physicalDevice, attachment.format,
                                             &properties );
        const VkFormatFeatureFlags requiredFeature =
            bIsDepthStencil ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                            : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        const VkSampleCountFlags supportedSampleCounts =
            bIsDepthStencil ? limits.framebufferDepthSampleCounts
                            : limits.framebufferColorSampleCounts;
        if ( ( properties.optimalTilingFeatures & requiredFeature ) == 0 ||
             ( supportedSampleCounts & attachment.samples ) == 0 ) {
            return false;
        }
    }

    return true;
}

GPUShader *RenderDevice::getOrCreatePrebuildShader( const uint64_t hashcode,
                                                    const eShaderType stage ) {
    auto it = prebuildShaders.find( hashcode );
    if ( it != prebuildShaders.end() ) {
        return it->second;
    }

    ShaderTableEntry entry;
    entry.Hashcode = hashcode;
    entry.ShaderStage = stage;
    entry.pShaderCategory = "PSO library";

    GPUShader *pShader = nullptr;
    const ShaderBytecode_t bytecode =
        gShaderBinaryCache.getOrLoadBytecode( &entry );
    const ShaderMetadataSPIRVRuntime *pMetadata =
        gShaderBinaryCache.getMetadataSPIRV( &entry );
    if ( std::get<0>( bytecode ) != nullptr && pMetadata != nullptr ) {
        pShader = createShader( stage, std::get<0>( bytecode ),
                                static_cast<size_t>( std::get<1>( bytecode ) ),
                                pMetadata, hashcode );
    }

    prebuildShaders.insert( std::make_pair( hashcode, pShader ) );
    return pShader;
}

void RenderDevice::releasePrebuildResources() {
    for ( auto &entry : prebuildShaders ) {
        if ( entry.second != nullptr ) {
            destroyShader( entry.second );
        }
    }
    prebuildShaders.clear();
}

size_t RenderDevice::prebuildRecordedPSOs( const size_t maxCount ) {
    if ( !bPrebuildFactoryInitialized ) {
        prebuildFactory.bExtendedDynamicState =
            psoFactory.bExtendedDynamicState;
        prebuildFactory.reset();
        prebuildFactory.PipelineLayout = psoFactory.PipelineLayout;
        prebuildFactory.pParentFactory = &psoFactory;
        bPrebuildFactoryInitialized = true;
    }

    const std::vector<PSORecord> &records = psoLibrary.getRecords();
    const std::vector<PSORecord> &seedRecords = psoSeed.getRecords();
    size_t numProcessed = 0;
    while ( numProcessed < maxCount ) {
        const bool bIsSeedRecord = nextPrebuildRecord >= records.size();
        if ( bIsSeedRecord && nextPrebuildSeedRecord >= seedRecords.size() ) {
            break;
        }

        const PSORecord &record = bIsSeedRecord
                                      ? seedRecords[nextPrebuildSeedRecord++]
                                      : records[nextPrebuildRecord++];
        if ( psoFactory.hasCachedPSO( record.Hashcode ) ) {
            continue;
        }
        numProcessed++;

        uint64_t hashcode = 0ull;
        if ( !prebuildPSO( record, hashcode ) ) {
            if ( bIsSeedRecord ) {
                numSkippedSeedRecords++;
            } else {
                stalePSORecords.push_back( record.Hashcode );
            }
        } else if ( hashcode != record.Hashcode && !bIsSeedRecord ) {
            rehashedPSORecords.emplace_back( record.Hashcode, hashcode );
        }
    }

    const size_t numRemaining = ( records.size() - nextPrebuildRecord ) +
                                ( seedRecords.size() - nextPrebuildSeedRecord );
    if ( numRemaining == 0 ) {
        releasePrebuildResources();

        if ( numSkippedSeedRecords != 0 ) {
            OTDU_LOG_INFO( "Skipped %zu stale/unsupported PSO seed record(s)\n",
                           numSkippedSeedRecords );
            numSkippedSeedRecords = 0;
        }

        if ( !rehashedPSORecords.empty() ) {
            OTDU_LOG_INFO( "Rehashed %zu PSO record(s)\n",
                           rehashedPSORecords.size() );
            for ( const auto &hashcodes : rehashedPSORecords ) {
                psoLibrary.rehash( hashcodes.first, hashcodes.second );
            }
            rehashedPSORecords.clear();
            nextPrebuildRecord = psoLibrary.getRecords().size();
        }

        if ( !stalePSORecords.empty() ) {
            OTDU_LOG_INFO( "Discarded %zu stale/unsupported PSO record(s)\n",
                           stalePSORecords.size() );
            for ( const uint64_t hashcode : stalePSORecords ) {
                psoLibrary.discard( hashcode );
            }
            stalePSORecords.clear();
            nextPrebuildRecord = psoLibrary.getRecords().size();
        }
    }

    return numRemaining;
}

bool RenderDevice::prebuildPSO( const PSORecord &record,
                                uint64_t &outHashcode ) {
    if ( !isPSORecordSupported( record ) ) {
        return false;
    }

    GPUShader *pVertexShader =
        getOrCreatePrebuildShader( record.VSHashcode, eShaderType::ST_Vertex );
    GPUShader *pPixelShader = nullptr;
    if ( record.PSHashcode != 0ull ) {
        pPixelShader = getOrCreatePrebuildShader( record.PSHashcode,
                                                  eShaderType::ST_Pixel );
    }

    if ( pVertexShader == nullptr ||
         ( record.PSHashcode != 0ull && pPixelShader == nullptr ) ) {
        return false;
    }

    GPUVertexLayout vertexLayout;
    vertexLayout.InputBindings = record.InputBindings;
    for ( const PSORecord::Semantic &semantic : record.Semantics ) {
        vertexLayout.Semantics.push_back(
            { semantic.Binding, semantic.Offset, semantic.Format,
              static_cast<uint8_t>( semantic.Usage ),
              static_cast<uint8_t>( semantic.UsageIndex ) } );
    }
    vertexLayout.updateHashcode();

    const CachedRenderPass renderPass =
        renderPassManager.getOrCreateRenderPass( device, record.RenderPass );
    PSOLibrary::ApplyRecord( record, prebuildFactory, pVertexShader,
                             pPixelShader, &vertexLayout, renderPass );
    const CachedPSO &pso = prebuildFactory.getOrBuildPSO( device, psoCache );
    outHashcode = pso.Hashcode;
    psoFactory.adoptCachedPSOs( device, prebuildFactory );
    return true;
}

// DAT_00f471d0
static constexpr VkFormat kBackbufferFormats[4] = {
    VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_UNDEFINED, VK_FORMAT_R5G6B5_UNORM_PACK16 };

bool RenderDevice::createSwapchain() {
    // Create window surface
#if defined( OTDU_USE_SDL )
    if ( surface == VK_NULL_HANDLE ) {
        bool surfCreation = SDL_Vulkan_CreateSurface(
            (SDL_Window *)gEdSys.getWindow(), instance, nullptr, &surface );
        if ( !surfCreation ) {
            return false;
        }
    }
#endif

    VkBool32 bSupportSwapchain = VK_FALSE;
    VkResult swapchainSupported = vkGetPhysicalDeviceSurfaceSupportKHR(
        physicalDevice, queues[0].queueIndex, surface, &bSupportSwapchain );
    if ( swapchainSupported != VK_SUCCESS ||
         ( swapchainSupported == VK_SUCCESS && !bSupportSwapchain ) ) {
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physicalDevice, surface,
                                               &capabilities );

    uint32_t formatCount = 0u;
    std::vector<VkSurfaceFormatKHR> formats;
    vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, surface, &formatCount,
                                          nullptr );

    if ( formatCount == 0 ) {
        return false;
    }
    formats.resize( formatCount );
    vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, surface, &formatCount,
                                          formats.data() );

    uint32_t presentModeCount = 0u;
    std::vector<VkPresentModeKHR> presentModes;
    vkGetPhysicalDeviceSurfacePresentModesKHR( physicalDevice, surface,
                                               &presentModeCount, nullptr );

    if ( presentModeCount == 0 ) {
        return false;
    }
    presentModes.resize( presentModeCount );
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        physicalDevice, surface, &presentModeCount, presentModes.data() );

    // Select backbuffer format
    uint32_t pickedFormatIndex = std::numeric_limits<uint32_t>::max();
    for ( uint32_t j = 0; j < formats.size(); j++ ) {
        VkSurfaceFormatKHR &surfaceFormat = formats[j];

        bool bMatchingFormat = false;
        for ( uint32_t i = 0; i < 4; i++ ) {
            if ( surfaceFormat.format == kBackbufferFormats[i] &&
                 surfaceFormat.colorSpace ==
                     VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
                bMatchingFormat = true;
                break;
            }
        }

        if ( bMatchingFormat ) {
            pickedFormatIndex = j;
            break;
        }
    }
    OTDU_ASSERT_FATAL( pickedFormatIndex !=
                       std::numeric_limits<uint32_t>::max() );

    VkSurfaceFormatKHR swapchainFormat = formats[pickedFormatIndex];
    VkFormat backbufferFormat = swapchainFormat.format;
    activeOutputTransfer = eOutputTransfer::OT_SDR;

    const HDRSurfaceFormat *pHDRFormat = nullptr;
    for ( const HDRSurfaceFormat &hdrFormat : kHDRSurfaceFormats ) {
        for ( const VkSurfaceFormatKHR &surfaceFormat : formats ) {
            if ( surfaceFormat.format == hdrFormat.Format &&
                 surfaceFormat.colorSpace == hdrFormat.ColorSpace ) {
                pHDRFormat = &hdrFormat;
                break;
            }
        }
        if ( pHDRFormat != nullptr ) {
            break;
        }
    }
    bHDROutputSupported = ( pHDRFormat != nullptr );

    if ( bHDROutputEnabled ) {
        if ( pHDRFormat != nullptr ) {
            swapchainFormat = { pHDRFormat->Format, pHDRFormat->ColorSpace };
            activeOutputTransfer = pHDRFormat->Transfer;
            // Keeps what the composition writes above 1.0 (highlights)
            backbufferFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
            OTDU_LOG_INFO( "HDR output: format %u, color space %u\n",
                           swapchainFormat.format, swapchainFormat.colorSpace );
        } else {
            OTDU_LOG_WARN( "HDR output requested but the display surface does "
                           "not expose an HDR color space%s; using SDR\n",
                           bSwapchainColorSpaceSupported
                               ? ""
                               : " (VK_EXT_swapchain_colorspace missing)" );
        }
    }

    // Select Present Mode (FIFO is the only mode the spec guarantees)
    auto isPresentModeSupported = [&]( const VkPresentModeKHR mode ) {
        return std::find( presentModes.begin(), presentModes.end(), mode ) !=
               presentModes.end();
    };

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if ( !bVSyncEnabled ) {
        if ( isPresentModeSupported( VK_PRESENT_MODE_MAILBOX_KHR ) ) {
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        } else if ( isPresentModeSupported( VK_PRESENT_MODE_IMMEDIATE_KHR ) ) {
            presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    VkExtent2D swapchainDimension = capabilities.currentExtent;

    uint32_t imageCount = PendingFrameCount;
    if ( capabilities.maxImageCount > 0 &&
         imageCount > capabilities.maxImageCount ) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    swapchainImageFormat = swapchainFormat.format;
    createInfo.imageFormat = swapchainFormat.format;
    createInfo.imageColorSpace = swapchainFormat.colorSpace;
    createInfo.imageExtent = swapchainDimension;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ( capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) {
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult opResult =
        vkCreateSwapchainKHR( device, &createInfo, nullptr, &swapchain );
    if ( opResult != VK_SUCCESS ) {
        return false;
    }

    vkGetSwapchainImagesKHR( device, swapchain, &imageCount, nullptr );
    swapchainImages.resize( imageCount );
    vkGetSwapchainImagesKHR( device, swapchain, &imageCount,
                             swapchainImages.data() );

    VkImageViewCreateInfo viewInfos = {};
    viewInfos.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfos.pNext = nullptr;
    viewInfos.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfos.format = swapchainFormat.format;
    viewInfos.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfos.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfos.subresourceRange.baseMipLevel = 0;
    viewInfos.subresourceRange.levelCount = 1;
    viewInfos.subresourceRange.baseArrayLayer = 0;
    viewInfos.subresourceRange.layerCount = 1;

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    presentSemaphores.resize( swapchainImages.size(), VK_NULL_HANDLE );
    for ( VkSemaphore &semaphore : presentSemaphores ) {
        VkResult semaphoreCreation =
            vkCreateSemaphore( device, &semaphoreInfo, nullptr, &semaphore );
        OTDU_ASSERT( semaphoreCreation == VK_SUCCESS );
    }

    swapchainImageViews.resize( swapchainImages.size(), VK_NULL_HANDLE );
    for ( size_t i = 0; i < swapchainImages.size(); i++ ) {
        viewInfos.image = swapchainImages[i];
        VkResult imageCreationResult = vkCreateImageView(
            device, &viewInfos, nullptr, &swapchainImageViews[i] );
        OTDU_ASSERT( imageCreationResult == VK_SUCCESS );
    }

    if ( !createBackbufferTexture( backbufferFormat, swapchainDimension ) ) {
        return false;
    }

    backbuffer.Width = swapchainDimension.width;
    backbuffer.Height = swapchainDimension.height;

    // NOTE: Only maps to what the original game support for now (in
    // the future this could be extended to support HW HDR)
    switch ( backbufferFormat ) {
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        backbuffer.Format = eViewFormat::VF_A2R10G10B10;
        break;

    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
        backbuffer.Format = eViewFormat::VF_X8R8G8B8;
        break;

    case VK_FORMAT_R5G6B5_UNORM_PACK16:
        backbuffer.Format = eViewFormat::VF_R5G6B5;
        break;

    case VK_FORMAT_R16G16B16A16_SFLOAT:
        backbuffer.Format = eViewFormat::VF_A16B16G16R16F;
        break;

    default:
        OTDU_FATAL_ERROR( "Unsupported/invalid swapchain format %x",
                          backbufferFormat );
        backbuffer.Format = eViewFormat::VF_Invalid;
        break;
    }

    return true;
}

bool RenderDevice::createBackbufferTexture( const VkFormat format,
                                            const VkExtent2D extent ) {
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { extent.width, extent.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocCreateInfo.priority = 1.0f;

    if ( backbuffer.pTexture == nullptr ) {
        backbuffer.pTexture = new GPUTexture();
    }

    backbuffer.pTexture->ViewFormat = format;
    backbuffer.pTexture->SampleCount = VK_SAMPLE_COUNT_1_BIT;
    backbuffer.pTexture->bNeedMSAAResolve = false;
    backbuffer.pTexture->Width = extent.width;
    backbuffer.pTexture->Height = extent.height;
    backbuffer.pTexture->NumMips = 1;
    backbuffer.pTexture->Depth = 1;
    bNeedBackbufferClear = true;

    VkImageViewCreateInfo viewInfos = {};
    viewInfos.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfos.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfos.format = format;
    viewInfos.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfos.subresourceRange.levelCount = 1;
    viewInfos.subresourceRange.layerCount = 1;

    VkResult imageCreationResult = vmaCreateImage(
        allocator, &imageInfo, &allocCreateInfo, &backbuffer.pTexture->Image,
        &backbuffer.pTexture->Allocation, nullptr );
    if ( imageCreationResult != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "Failed to allocate the backbuffer (format %u)\n",
                        format );
        return false;
    }

    backbuffer.pTexture->CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    viewInfos.image = backbuffer.pTexture->Image;
    imageCreationResult = vkCreateImageView( device, &viewInfos, nullptr,
                                             &backbuffer.pTexture->ImageView );
    OTDU_ASSERT( imageCreationResult == VK_SUCCESS );
    backbuffer.pTexture->ViewId = allocateViewId();

#if OTDU_DEVBUILD
    VkDebugUtilsObjectNameInfoEXT nameInfo = {};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = VkObjectType::VK_OBJECT_TYPE_IMAGE;
    nameInfo.objectHandle = (uint64_t)backbuffer.pTexture->Image;
    nameInfo.pObjectName = "Backbuffer";
    vkSetDebugUtilsObjectNameEXT( device, &nameInfo );
#endif

    return true;
}

void RenderDevice::presentBackbuffer( FrameSubmission &submission ) {
    GPUTexture *pBackbuffer = backbuffer.pTexture;

    submission.bGammaRampPass = gammaRampPass.isActive();
    submission.Backbuffer = pBackbuffer->Image;
    submission.BackbufferExtent = { backbuffer.Width, backbuffer.Height };
    gammaRampPass.captureFrameParameters( submission.GammaRamp );

    if ( submission.bGammaRampPass ) {
        insertImageBarrier(
            pBackbuffer, pBackbuffer->CurrentLayout,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
    } else {
        insertImageBarrier(
            pBackbuffer, pBackbuffer->CurrentLayout,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT );
    }
}

void RenderDevice::recordPresentPass( VkCommandBuffer cmdBuffer,
                                      const FrameSubmission &submission,
                                      const uint32_t imageIndex ) {
    gammaRampPass.uploadRamp( cmdBuffer, submission.GammaRamp );

    if ( imageIndex == ~0u ) {
        return;
    }

    if ( submission.bGammaRampPass ) {
        gammaRampPass.draw( cmdBuffer, submission.Slot, imageIndex,
                            submission.GammaRamp );
        return;
    }

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchainImages[imageIndex];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier( cmdBuffer,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                          nullptr, 1, &barrier );

    VkImageCopy region = {};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource = region.srcSubresource;
    region.extent = { submission.BackbufferExtent.width,
                      submission.BackbufferExtent.height, 1 };
    vkCmdCopyImage( cmdBuffer, submission.Backbuffer,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    swapchainImages[imageIndex],
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                          0, nullptr, 1, &barrier );
}

void RenderDevice::startSubmitThread() {
    bStopSubmitThread = false;
    submitThread = std::thread( [this]() { submitThreadMain(); } );
}

void RenderDevice::stopSubmitThread() {
    if ( !submitThread.joinable() ) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock( submitMutex );
        bStopSubmitThread = true;
    }
    submitCondition.notify_all();
    submitThread.join();
}

void RenderDevice::submitThreadMain() {
#if defined( OTDU_MACOS )
    pthread_setname_np( "OpenTDU Submit" );
#elif defined( OTDU_UNIX )
    pthread_setname_np( pthread_self(), "OpenTDU Submit" );
#endif

    std::unique_lock<std::mutex> lock( submitMutex );
    while ( true ) {
        submitCondition.wait( lock, [this]() {
            return bStopSubmitThread || !queuedSubmissions.empty() ||
                   ( retireRequestSerial > retiredSerial &&
                     !inFlightSubmissions.empty() );
        } );

        if ( !queuedSubmissions.empty() ) {
            FrameSubmission submission = std::move( queuedSubmissions.front() );
            queuedSubmissions.pop_front();
            bProcessingSubmission = true;
            lock.unlock();
            submitCondition.notify_all();

            processSubmission( submission );

            lock.lock();
            inFlightSubmissions.push_back( std::move( submission ) );
            bProcessingSubmission = false;

            // Keep only the latest frame in flight: the main thread can then
            // reuse the slot of the previous one without waiting
            while ( inFlightSubmissions.size() > 1 ) {
                retireOldestSubmission( lock );
            }
            submitCondition.notify_all();
        } else if ( retireRequestSerial > retiredSerial &&
                    !inFlightSubmissions.empty() ) {
            retireOldestSubmission( lock );
            submitCondition.notify_all();
        } else if ( bStopSubmitThread ) {
            break;
        }
    }
}

void RenderDevice::queueSubmission( FrameSubmission &&submission ) {
    std::unique_lock<std::mutex> lock( submitMutex );

    submitCondition.wait( lock,
                          [this]() { return queuedSubmissions.empty(); } );
    lastQueuedSerial = submission.Serial;
    queuedSubmissions.push_back( std::move( submission ) );
    lock.unlock();
    submitCondition.notify_all();
}

void RenderDevice::processSubmission( FrameSubmission &submission ) {
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = submission.NumCommandBuffers;
    submitInfo.pCommandBuffers = submission.CommandBuffers;
    VkResult result =
        vkQueueSubmit( queues[0].queue, 1, &submitInfo, VK_NULL_HANDLE );
    if ( result == VK_ERROR_DEVICE_LOST ) {
        bDeviceLost.store( true, std::memory_order_release );
    }
    OTDU_ASSERT( result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST );

    const uint32_t slot = submission.Slot;
    uint32_t imageIndex = ~0u;
    if ( swapchain != VK_NULL_HANDLE ) {
        result = vkAcquireNextImageKHR( device, swapchain, UINT64_MAX,
                                        nextImageSemaphore[slot],
                                        VK_NULL_HANDLE, &imageIndex );
        if ( result == VK_SUBOPTIMAL_KHR ) {
            bSwapchainOutOfDate.store( true, std::memory_order_release );
        } else if ( result != VK_SUCCESS ) {
            // Frame dropped; the main thread recreates the swapchain
            bSwapchainOutOfDate.store( true, std::memory_order_release );
            imageIndex = ~0u;
        }
    }

    if ( submission.bUpdateHDRMetadata && bHDRMetadataSupported &&
         swapchain != VK_NULL_HANDLE ) {
        vkSetHdrMetadataEXT( device, 1, &swapchain, &submission.HDRMetadata );
    }

    VkCommandBuffer presentCmdBuffer = presentCommandBuffers[slot];
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer( presentCmdBuffer, &beginInfo );
    recordPresentPass( presentCmdBuffer, submission, imageIndex );
    vkEndCommandBuffer( presentCmdBuffer );

    const bool bImageAcquired = ( imageIndex != ~0u );
    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = bImageAcquired ? 1u : 0u;
    submitInfo.pWaitSemaphores = &nextImageSemaphore[slot];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &presentCmdBuffer;
    submitInfo.signalSemaphoreCount = bImageAcquired ? 1u : 0u;
    submitInfo.pSignalSemaphores =
        bImageAcquired ? &presentSemaphores[imageIndex] : nullptr;
    result =
        vkQueueSubmit( queues[0].queue, 1, &submitInfo, inFlightFence[slot] );
    if ( result == VK_ERROR_DEVICE_LOST ) {
        bDeviceLost.store( true, std::memory_order_release );
    }
    OTDU_ASSERT( result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST );

    if ( !bImageAcquired ) {
        return;
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &presentSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR( queues[0].queue, &presentInfo );
    if ( result == VK_ERROR_DEVICE_LOST ) {
        bDeviceLost.store( true, std::memory_order_release );
    } else if ( result == VK_ERROR_OUT_OF_DATE_KHR ||
                result == VK_SUBOPTIMAL_KHR ) {
        bSwapchainOutOfDate.store( true, std::memory_order_release );
    }
}

void RenderDevice::retireOldestSubmission(
    std::unique_lock<std::mutex> &lock ) {
    // Only the submit thread mutates inFlightSubmissions
    FrameSubmission &submission = inFlightSubmissions.front();
    const uint32_t slot = submission.Slot;

    lock.unlock();
    vkWaitForFences( device, 1, &inFlightFence[slot], VK_TRUE, UINT64_MAX );
    vkResetFences( device, 1, &inFlightFence[slot] );
    vkResetCommandPool( device, framePools[slot], 0 );
    vkResetCommandPool( device, presentPools[slot], 0 );
    releaseRetireList( submission.Retire );
    lock.lock();

    retiredSerial = submission.Serial;
    inFlightSubmissions.pop_front();
}

void RenderDevice::waitForRetiredSubmission( const uint64_t serial ) {
    std::unique_lock<std::mutex> lock( submitMutex );
    OTDU_ASSERT( serial <= lastQueuedSerial );
    if ( retiredSerial >= serial ) {
        return;
    }

    retireRequestSerial = Max( retireRequestSerial, serial );
    submitCondition.notify_all();
    submitCondition.wait(
        lock, [this, serial]() { return retiredSerial >= serial; } );
}

void RenderDevice::waitForSubmissionsRetired() {
    std::unique_lock<std::mutex> lock( submitMutex );
    retireRequestSerial = Max( retireRequestSerial, lastQueuedSerial );
    submitCondition.notify_all();
    submitCondition.wait( lock, [this]() {
        return queuedSubmissions.empty() && !bProcessingSubmission &&
               retiredSerial >= lastQueuedSerial;
    } );
}

VkCommandBuffer RenderDevice::getPreFrameCommandBuffer(
    const ePreFrameCommandList commandList ) {
    PreFrameCommands &commands = preFrameCommands[activePreFrameCommands];
    if ( commands.bRecording[commandList] ) {
        return commands.CommandBuffers[commandList];
    }

    if ( commands.Serial != 0ull ) {
        // First list recorded since the pool was last submitted
        waitForRetiredSubmission( commands.Serial );
        vkResetCommandPool( device, commands.Pool, 0 );
        commands.Serial = 0ull;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer( commands.CommandBuffers[commandList], &beginInfo );
    commands.bRecording[commandList] = true;

    return commands.CommandBuffers[commandList];
}

void RenderDevice::collectPreFrameCommands( const uint64_t serial ) {
    // Same submission order as the former immediate flush (uploads, then mip
    // generation)
    PreFrameCommands &commands = preFrameCommands[activePreFrameCommands];
    numRecordingPreFrameCommands = 0u;
    for ( uint32_t i = 0; i < PFCL_Count; i++ ) {
        if ( commands.bRecording[i] ) {
            vkEndCommandBuffer( commands.CommandBuffers[i] );
            commands.bRecording[i] = false;
            recordingPreFrameCommands[numRecordingPreFrameCommands++] =
                commands.CommandBuffers[i];
        }
    }

    if ( numRecordingPreFrameCommands != 0u ) {
        commands.Serial = serial;
        activePreFrameCommands =
            ( activePreFrameCommands + 1 ) % kNumPreFrameCommands;
    }
}

void RenderDevice::submitPreFrameCommandsNow() {
    // Only called once the submit thread is idle (owns the queue otherwise).
    // Commands collected for the frame being recorded go first to keep the
    // recording order.
    PreFrameCommands &commands = preFrameCommands[activePreFrameCommands];
    VkCommandBuffer cmdBuffers[PFCL_Count * 2];
    uint32_t numCmdBuffers = 0u;
    for ( uint32_t i = 0; i < numRecordingPreFrameCommands; i++ ) {
        cmdBuffers[numCmdBuffers++] = recordingPreFrameCommands[i];
    }
    numRecordingPreFrameCommands = 0u;

    for ( uint32_t i = 0; i < PFCL_Count; i++ ) {
        if ( commands.bRecording[i] ) {
            vkEndCommandBuffer( commands.CommandBuffers[i] );
            commands.bRecording[i] = false;
            cmdBuffers[numCmdBuffers++] = commands.CommandBuffers[i];
        }
    }

    if ( numCmdBuffers == 0u ) {
        return;
    }

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = numCmdBuffers;
    submitInfo.pCommandBuffers = cmdBuffers;
    vkQueueSubmit( queues[0].queue, 1, &submitInfo, VK_NULL_HANDLE );
    vkQueueWaitIdle( queues[0].queue );
    vkResetCommandPool( device, commands.Pool, 0 );
}

VkFramebuffer
RenderDevice::getOrCreateFramebuffer( const FramebufferKey &key ) {
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = key.RenderPass;
    framebufferInfo.attachmentCount = key.NumAttachments;
    framebufferInfo.pAttachments = key.Attachments.data();
    framebufferInfo.width = key.Width;
    framebufferInfo.height = key.Height;
    framebufferInfo.layers = 1;

    uint64_t framebufferHash = 0ull;
    for ( uint32_t i = 0; i < key.NumAttachments; i++ ) {
        framebufferHash = CombineCityHash64( framebufferHash,
                                             (const char *)&key.Attachments[i],
                                             sizeof( VkImageView ) );
    }
    framebufferHash = CombineCityHash64(
        framebufferHash, (const char *)&framebufferInfo.renderPass,
        sizeof( VkRenderPass ) );
    framebufferHash = CombineCityHash64( framebufferHash,
                                         (const char *)&framebufferInfo.width,
                                         sizeof( uint32_t ) );
    framebufferHash = CombineCityHash64( framebufferHash,
                                         (const char *)&framebufferInfo.height,
                                         sizeof( uint32_t ) );

    auto fbCacheIt = framebufferCache.find( framebufferHash );
    if ( fbCacheIt != framebufferCache.end() ) {
        return fbCacheIt->second;
    }

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkResult fbCreationResult =
        vkCreateFramebuffer( device, &framebufferInfo, nullptr, &framebuffer );
    OTDU_ASSERT( fbCreationResult == VK_SUCCESS );

    framebufferCache.insert( std::make_pair( framebufferHash, framebuffer ) );
    return framebuffer;
}

void RenderDevice::resetRecordedBindings() {
    for ( uint32_t i = 0; i < 0x10; i++ ) {
        recordedBindings.VertexBuffers[i] = VK_NULL_HANDLE;
        recordedBindings.VertexBufferOffsets[i] = 0;
    }
    recordedBindings.NumVertexBuffers = -1;
    recordedBindings.bNullVertexBuffer = false;

    recordedBindings.IndexBuffer = VK_NULL_HANDLE;
    recordedBindings.IndexBufferOffset = 0ull;
    recordedBindings.IndexType = VK_INDEX_TYPE_UINT16;

    recordedBindings.bRenderStatesValid = false;
    recordedBindings.bStencilDynamicStateValid = false;
    recordedBindings.bExtendedDynamicStateValid = false;
    recordedBindings.SamplersSize = 0;
}

void RenderDevice::bindIndexBufferToCmdBuffer( VkBuffer buffer,
                                               VkDeviceSize offset,
                                               VkIndexType indexType ) {
    if ( recordedBindings.IndexBuffer == buffer &&
         recordedBindings.IndexBufferOffset == offset &&
         recordedBindings.IndexType == indexType ) {
        return;
    }

    vkCmdBindIndexBuffer( getCommandBuffer(), buffer, offset, indexType );
    recordedBindings.IndexBuffer = buffer;
    recordedBindings.IndexBufferOffset = offset;
    recordedBindings.IndexType = indexType;
}

bool RenderDevice::prepareDrawCall( ePrimitiveType primitiveType ) {
    if ( !bRecordingCommands ) {
        return false;
    }

#if 0
    uint32_t vbMask = psoFactory.pGraphicsShaderStages[0]->pMetadata->InputMask;
    for ( int32_t vbIndex = 0; vbIndex < 0x10; vbIndex++ ) {
        if ( ( vbMask >> vbIndex ) & 1 ) {
            if ( vertexBuffers[vbIndex] == nullptr ) {
                OTDU_TRIGGER_BREAKPOINT;
            }
        }
    }
#endif

    // No geometry bound at all — skip draw.  Matches the original null-scan
    // early exit: if slot 0 is empty there is nothing to render.
    if ( vertexBuffers[0] == VK_NULL_HANDLE ) {
        return false;
    }

    // Use the vertex layout's binding count as the authoritative slot count.
    // The old scan (stop at first VK_NULL_HANDLE) breaks when UnstreamGeometry
    // resets all slots mid-frame (e.g. lazy unstream of old car geometry) and
    // a subsequent shadow pass only rebinds a subset of the required slots.
    int32_t numVertexBuffers =
        ( psoFactory.pVertexLayout != nullptr )
            ? static_cast<int32_t>(
                  psoFactory.pVertexLayout->InputBindings.size() )
            : 0;

    // TODO: Check if we actually need to bind geometry (eg. for compute)
    if ( numVertexBuffers == 0 ) {
        return false;
    }

#if OTDU_DEVBUILD
    for ( int32_t i = 0; i < numVertexBuffers; i++ ) {
        if ( vertexBuffers[i] == VK_NULL_HANDLE ) {
            OTDU_LOG_ERROR(
                "prepareDrawCall: vertex buffer slot %d is unbound but "
                "required by the current PSO (%d total bindings)\n",
                i, numVertexBuffers );
            OTDU_TRIGGER_BREAKPOINT;
        }
    }
#endif

    if ( psoFactory.PrimitiveType != primitiveType ) {
        psoFactory.PrimitiveType = primitiveType;
        psoFactory.bDirty = true;
    }

    for ( uint32_t i = 0; i < 2; i++ ) {
        const uint32_t psSpecDword =
            resourceBinder.shaderSpecs[1 + i] & kPSSpecDwordMasks[i];
        if ( psoFactory.PSSpecDwords[i] != psSpecDword ) {
            psoFactory.PSSpecDwords[i] = psSpecDword;
            psoFactory.bDirty = true;
        }
    }

    // Commands recorded until the render pass begins keep the pending clear
    struct PreparingDrawCallScope {
        bool &bPreparing;
        explicit PreparingDrawCallScope( bool &bValue ) : bPreparing( bValue ) {
            bPreparing = true;
        }
        ~PreparingDrawCallScope() { bPreparing = false; }
    } preparingScope( bPreparingDrawCall );

    bool bClearOnLoad = false;
    if ( pendingClear.bActive ) {
        if ( isPendingClearForBoundAttachments() ) {
            bClearOnLoad = true;
        } else {
            flushPendingClear();
        }
    }

    // Create or retrieve cached renderpass for incoming drawcall (do this as
    // late as possible to make sure to capture all framebuffer cmds)
    bool bNeedRestartRenderPass = ( activeRenderPass == VK_NULL_HANDLE );
    if ( bDepthStencilAttachmentValid ) {
        if ( pDepthStencilAttachment->CurrentLayout !=
             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ) {
            insertImageBarrier(
                pDepthStencilAttachment, pDepthStencilAttachment->CurrentLayout,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT );
            bNeedRestartRenderPass = true; // Can't insert memory barriers
                                           // inside active renderpass
        }
    }
    CachedRenderPass renderPass =
        renderPassManager.getOrCreateRenderPass( device );
    if ( psoFactory.renderPassHashcode != renderPass.Hashcode ) {
        psoFactory.renderPass = renderPass.Instance;
        psoFactory.renderPassHashcode = renderPass.Hashcode;
        psoFactory.bDirty = true;

        bNeedRestartRenderPass = true;
    }

    // TODO: MOVE FB LOGIC TO A SEPARATE FUNC/CLASS!!!!!!
    // Create or retrieve framebuffer object
    std::array<VkImageView, kMaxSimultaneousRT> attachments;
    attachments.fill( VK_NULL_HANDLE );

    uint32_t numAttachments = 0;
    for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
        if ( bFramebufferAttachmentValid[i] ) {
            OTDU_ASSERT( pFramebufferAttachments[i] );
            OTDU_ASSERT( pFramebufferAttachments[i]->ImageView );
            attachments[numAttachments] = pFramebufferAttachments[i]->ImageView;
            numAttachments++;
        }
    }

    // Mem barrier RTVs (if required)
    for ( uint32_t i = 0; i < numAttachments; i++ ) {
        if ( bFramebufferAttachmentValid[i] ) {
            OTDU_ASSERT( pFramebufferAttachments[i] );
            OTDU_ASSERT( pFramebufferAttachments[i]->ImageView );
            if ( pFramebufferAttachments[i]->CurrentLayout !=
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ) {
                insertImageBarrier(
                    pFramebufferAttachments[i],
                    pFramebufferAttachments[i]->CurrentLayout,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT );
                bNeedRestartRenderPass = true; // Can't insert memory barriers
                                               // inside active renderpass
            }
        }
    }
    if ( psoFactory.BlendState.attachmentCount != numAttachments ) {
        psoFactory.BlendState.attachmentCount = numAttachments;
        psoFactory.bDirty = true;
    }

    std::array<VkPipelineColorBlendAttachmentState, kMaxSimultaneousRT>
        blendAttachments;
    blendAttachments.fill( {} );

    for ( VkPipelineColorBlendAttachmentState &attachment : blendAttachments ) {
        attachment.blendEnable = psoFactory.AlphaBlending.blendEnable;

        attachment.srcColorBlendFactor = psoFactory.srcBlendFactor;
        attachment.dstColorBlendFactor = psoFactory.dstBlendFactor;
        attachment.colorBlendOp =
            attachment.blendEnable ? psoFactory.blendOP : VK_BLEND_OP_ADD;

        attachment.srcAlphaBlendFactor = psoFactory.srcBlendFactor;
        attachment.dstAlphaBlendFactor = psoFactory.dstBlendFactor;
        attachment.alphaBlendOp =
            attachment.blendEnable ? psoFactory.blendOP : VK_BLEND_OP_ADD;

        attachment.colorWriteMask = activeColorMask;
    }

    if ( memcmp( blendAttachments.data(), blendOpAttachments.data(),
                 sizeof( VkPipelineColorBlendAttachmentState ) *
                     kMaxSimultaneousRT ) != 0 ) {
        blendOpAttachments = blendAttachments;
        psoFactory.bDirty = true;
    }

    if ( bDepthStencilAttachmentValid && numAttachments < kMaxSimultaneousRT ) {
        attachments[numAttachments] = pDepthStencilAttachment->ImageView;
        numAttachments++;
    }
    psoFactory.BlendState.pAttachments = blendOpAttachments.data();

    // Size the framebuffer from the attachment dimensions, not the render
    // region extent. The render region may have a non-zero offset (e.g. shadow
    // passes apply a 1-texel border), and Vulkan requires offset + extent <=
    // framebuffer size. Using the full attachment size satisfies this for any
    // offset.
    uint32_t fbWidth = 0;
    uint32_t fbHeight = 0;
    for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
        if ( bFramebufferAttachmentValid[i] && pFramebufferAttachments[i] ) {
            fbWidth = pFramebufferAttachments[i]->Width;
            fbHeight = pFramebufferAttachments[i]->Height;
            break;
        }
    }
    if ( fbWidth == 0 && bDepthStencilAttachmentValid &&
         pDepthStencilAttachment ) {
        fbWidth = pDepthStencilAttachment->Width;
        fbHeight = pDepthStencilAttachment->Height;
    }

    // OTDU_ADDITION: never larger than any attachment (D3D9 required the depth
    // buffer to cover the render target). A swapchain recreated at a new
    // window size meets the game's old size depth buffer until the resize
    // settles (edRender::flushDrawCommands); sized from the color alone, the
    // framebuffer let the GPU write past the depth image (device lost seen in
    // the login menu).
    for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
        if ( bFramebufferAttachmentValid[i] && pFramebufferAttachments[i] ) {
            fbWidth = Min( fbWidth, pFramebufferAttachments[i]->Width );
            fbHeight = Min( fbHeight, pFramebufferAttachments[i]->Height );
        }
    }
    if ( bDepthStencilAttachmentValid && pDepthStencilAttachment ) {
        fbWidth = Min( fbWidth, pDepthStencilAttachment->Width );
        fbHeight = Min( fbHeight, pDepthStencilAttachment->Height );
    }

    VkSampleCountFlagBits activeSampleCount = VK_SAMPLE_COUNT_1_BIT;
    bool bFoundColorAttachment = false;
    for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
        if ( bFramebufferAttachmentValid[i] && pFramebufferAttachments[i] ) {
            activeSampleCount = pFramebufferAttachments[i]->SampleCount;
            bFoundColorAttachment = true;
            break;
        }
    }
    if ( !bFoundColorAttachment && bDepthStencilAttachmentValid &&
         pDepthStencilAttachment ) {
        activeSampleCount = pDepthStencilAttachment->SampleCount;
    }

#if OTDU_DEVBUILD
    // Sanity check
    if ( bFoundColorAttachment && bDepthStencilAttachmentValid &&
         pDepthStencilAttachment ) {
        OTDU_ASSERT( pDepthStencilAttachment->SampleCount ==
                     activeSampleCount );
    }
#endif

    if ( psoFactory.MultisampleState.rasterizationSamples !=
         activeSampleCount ) {
        psoFactory.MultisampleState.rasterizationSamples = activeSampleCount;
        psoFactory.bDirty = true;
    }

    FramebufferKey framebufferKey;
    framebufferKey.Attachments = attachments;
    framebufferKey.NumAttachments = numAttachments;
    framebufferKey.RenderPass = renderPass.Instance;
    framebufferKey.Width = fbWidth;
    framebufferKey.Height = fbHeight;
    if ( !( framebufferKey == activeFramebufferKey ) ) {
        activeFramebuffer = getOrCreateFramebuffer( framebufferKey );
        activeFramebufferKey = framebufferKey;
        bNeedRestartRenderPass = true;
    }

    // Mem barrier SRVs (if required)
    for ( uint32_t i = 0; i < 0x10; i++ ) {
        // TODO: Assumes fragment sampling (because of SM2 restrictions). Might
        // want to extend that to support SRVs for VS/RT in the future
        if ( resourceBinder.pBoundSRVs[i] == nullptr ) {
            continue;
        }

        if ( resourceBinder.pBoundSRVs[i]->CurrentLayout !=
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) {
            insertImageBarrier(
                resourceBinder.pBoundSRVs[i],
                resourceBinder.pBoundSRVs[i]->CurrentLayout,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT );
            bNeedRestartRenderPass =
                true; // Can't insert memory barriers inside active renderpass
        }
    }

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass.Instance;
    renderPassInfo.framebuffer = activeFramebuffer;
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = { fbWidth, fbHeight };
    renderPassInfo.clearValueCount = 0;
    renderPassInfo.pClearValues = nullptr;

    std::array<VkClearValue, kMaxSimultaneousRT + 1> clearValues;
    if ( bClearOnLoad && bNeedRestartRenderPass ) {
        RenderPassDesc clearDesc;
        renderPassManager.captureDesc( clearDesc );
        for ( uint32_t i = 0; i < clearDesc.NumColorAttachments; i++ ) {
            if ( pendingClear.bColor ) {
                clearDesc.Attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
            clearValues[i].color = pendingClear.Color;
        }
        if ( clearDesc.bUseDepthStencil ) {
            VkAttachmentDescription &depthStencil =
                clearDesc.Attachments[clearDesc.NumColorAttachments];
            if ( pendingClear.bDepth ) {
                depthStencil.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
            if ( pendingClear.bStencil &&
                 depthStencil.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD ) {
                depthStencil.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
            clearValues[clearDesc.NumColorAttachments].depthStencil =
                pendingClear.DepthStencil;
        }

        renderPassInfo.renderPass =
            renderPassManager.getOrCreateRenderPass( device, clearDesc )
                .Instance;
        renderPassInfo.clearValueCount = clearDesc.getNumAttachments();
        renderPassInfo.pClearValues = clearValues.data();
        pendingClear.bActive = false;
    }

    if ( bNeedRestartRenderPass ) {
        transitionPreviousPassAttachments();
        endActiveRenderPass();

        if ( !deferredBarriers.empty() ) {
            for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
                if ( bFramebufferAttachmentValid[i] &&
                     pFramebufferAttachments[i] != nullptr ) {
                    flushDeferredBarriers( pFramebufferAttachments[i] );
                }
            }
        }

        vkCmdBeginRenderPass( getCommandBuffer(), &renderPassInfo,
                              VK_SUBPASS_CONTENTS_INLINE );
        activeRenderPass = renderPass.Instance;

        for ( uint32_t i = 0; i < kMaxSimultaneousRT; i++ ) {
            lastPassColorAttachments[i] = bFramebufferAttachmentValid[i]
                                              ? pFramebufferAttachments[i]
                                              : nullptr;
        }
        pLastPassDepthStencil =
            bDepthStencilAttachmentValid ? pDepthStencilAttachment : nullptr;
    } else if ( bClearOnLoad ) {
        recordInPassClear();
    }

    bool bDirtyDescriptorSets = bNeedRestartRenderPass;

    const size_t numCachedPSOs = psoFactory.getNumCachedPSOs();
    CachedPSO &pso = psoFactory.getOrBuildPSO( device, psoCache );
    if ( psoFactory.getNumCachedPSOs() != numCachedPSOs ) {
        RenderPassDesc renderPassDesc;
        renderPassManager.captureDesc( renderPassDesc );
        psoLibrary.record( psoFactory, pso.Hashcode, renderPassDesc );
    }

    if ( boundPSO != pso.Instance || bNeedRestartRenderPass ) {
        vkCmdBindPipeline( getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pso.Instance );
        boundPSO = pso.Instance;
        bDirtyDescriptorSets = true;
        psoFactory.bDirtyStencilDynamicState = true;
        psoFactory.bDirtyExtendedDynamicState = true;
    }

    if ( psoFactory.bDirtyStencilDynamicState ) {
        bindStencilDynamicState();
    }
    if ( psoFactory.bDirtyExtendedDynamicState &&
         deviceCaps.bExtendedDynamicState ) {
        bindExtendedDynamicState();
    }

    if ( recordedBindings.NumVertexBuffers != numVertexBuffers ||
         memcmp( recordedBindings.VertexBuffers, vertexBuffers,
                 sizeof( VkBuffer ) * numVertexBuffers ) != 0 ||
         memcmp( recordedBindings.VertexBufferOffsets, vertexBufferOffsets,
                 sizeof( VkDeviceSize ) * numVertexBuffers ) != 0 ) {
        vkCmdBindVertexBuffers( getCommandBuffer(), 0, numVertexBuffers,
                                vertexBuffers, vertexBufferOffsets );
        memcpy( recordedBindings.VertexBuffers, vertexBuffers,
                sizeof( VkBuffer ) * numVertexBuffers );
        memcpy( recordedBindings.VertexBufferOffsets, vertexBufferOffsets,
                sizeof( VkDeviceSize ) * numVertexBuffers );
        recordedBindings.NumVertexBuffers = numVertexBuffers;
    }

#ifndef OTDU_MACOS
    // OTDU_ADDITION: zero stream for vertex shader inputs the layout lacks
    // (PSOFactory::getOrBuildPSO)
    if ( !recordedBindings.bNullVertexBuffer ) {
        if ( pNullVertexBuffer == nullptr ) {
            GPUBufferDesc desc;
            desc.BufferType = GPUBufferDesc::Type::BT_VertexBuffer;
            desc.Size = 4 * sizeof( float );
            desc.Stride = 4 * sizeof( float );
            desc.bDynamic = false;
            pNullVertexBuffer = createBuffer( &desc );

            void *pZeros = lockBuffer( pNullVertexBuffer, 0, 0, true );
            memset( pZeros, 0, desc.Size );
            unlockBuffer( pNullVertexBuffer );
        }

        const VkDeviceSize nullOffset = 0ull;
        vkCmdBindVertexBuffers( getCommandBuffer(), kNullVertexBinding, 1,
                                &pNullVertexBuffer->NativeBuffer[0],
                                &nullOffset );
        recordedBindings.bNullVertexBuffer = true;
    }
#endif

    VkDescriptorSet activeSet = resourceBinder.DescriptorSet;
#ifdef OTDU_DEVBUILD
    if ( psoFactory.pGraphicsShaderStages[1] != nullptr ) {
        resourceBinder.validateSRVBindings(
            psoFactory.pGraphicsShaderStages[1] );
    }
#endif
    // Invalidate if UBOs offsets are dirty
    bDirtyDescriptorSets |= resourceBinder.isDescriptorSetDirty();

    VkDescriptorSet resourceSet = resourceBinder.getOrAllocateSet(
        device, pso.TextureBindingMask, frameIndex );

    // Bind Descriptors
    if ( bDirtyDescriptorSets ) {
        VkDescriptorSet pBoundSet[2] = { resourceSet,
                                         samplerHeap.DescriptorSet };

        vkCmdBindDescriptorSets( getCommandBuffer(),
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 psoFactory.PipelineLayout, 0, 2, pBoundSet,
                                 (uint32_t)resourceBinder.uboOffsets.size(),
                                 resourceBinder.uboOffsets.data() );
    }

    renderStates.instancedDrawIndex =
        isInstanceRateLayoutBound()
            ? resourceBinder.drawVSIndex
            : D3D9RenderStateInfo::kNoInstancedDrawIndex;
    if ( !recordedBindings.bRenderStatesValid ||
         memcmp( &recordedBindings.RenderStates, &renderStates,
                 sizeof( D3D9RenderStateInfo ) ) != 0 ) {
        vkCmdPushConstants( getCommandBuffer(), psoFactory.PipelineLayout,
                            VK_SHADER_STAGE_VERTEX_BIT |
                                VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof( D3D9RenderStateInfo ), &renderStates );
        recordedBindings.RenderStates = renderStates;
        recordedBindings.bRenderStatesValid = true;
    }

    uint32_t samplerIndexes = psoFactory.pGraphicsShaderStages[0]
                                  ->pMetadata->SamplerPushConstantsSize;
    if ( psoFactory.pGraphicsShaderStages[1] != nullptr ) {
        samplerIndexes =
            Max( samplerIndexes, psoFactory.pGraphicsShaderStages[1]
                                     ->pMetadata->SamplerPushConstantsSize );
    }
    if ( samplerIndexes > 0 &&
         ( samplerIndexes > recordedBindings.SamplersSize ||
           memcmp( recordedBindings.Samplers, boundSamplers, samplerIndexes ) !=
               0 ) ) {
        // Samplers start at GetPushSamplerOffset(0) = MaxSharedPushDataSize =
        // 64, NOT at sizeof(D3D9RenderStateInfo) = 52. The 12-byte gap is
        // intentional padding in DXVK's push constant layout.
        vkCmdPushConstants(
            getCommandBuffer(), psoFactory.PipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            kDxvkMaxSharedPushDataSize, samplerIndexes, boundSamplers );
        OTDU_ASSERT( samplerIndexes <= sizeof( recordedBindings.Samplers ) );
        memcpy( recordedBindings.Samplers, boundSamplers, samplerIndexes );
        recordedBindings.SamplersSize =
            Max( recordedBindings.SamplersSize, samplerIndexes );
    }

    return true;
}

struct ImageSyncScope {
    VkPipelineStageFlags Stages;
    VkAccessFlags Access;
};

static ImageSyncScope GetImageSyncScope( const VkImageLayout layout ) {
    switch ( layout ) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0 };
    case VK_IMAGE_LAYOUT_GENERAL:
        // Transfer ops; resolveMSAA also leaves its target in GENERAL for
        // shader sampling
        return { VK_PIPELINE_STAGE_TRANSFER_BIT |
                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                     VK_ACCESS_SHADER_READ_BIT };
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT };
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT };
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return { VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_SHADER_READ_BIT };
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
        return { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
    default:
        return { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                 VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT };
    }
}

static inline VkAccessFlags GetWriteAccess( const VkAccessFlags access ) {
    static constexpr VkAccessFlags kWriteAccessMask =
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT |
        VK_ACCESS_MEMORY_WRITE_BIT;
    return access & kWriteAccessMask;
}

static VkImageSubresourceRange
GetFullSubresourceRange( const GPUTexture *pTexture ) {
    const bool bIsDepthStencil =
        IsVulkanDepthStencilFormat( pTexture->ViewFormat );

    VkImageSubresourceRange range;
    range.aspectMask =
        bIsDepthStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if ( bIsDepthStencil && HasStencilBits( pTexture->ViewFormat ) ) {
        range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    range.baseMipLevel = 0;
    range.levelCount = pTexture->NumMips;
    range.baseArrayLayer = 0;
    range.layerCount = pTexture->Depth;
    return range;
}

void RenderDevice::insertMemoryBarrier( GPUTexture *pTexture,
                                        const VkImageLayout nextLayout ) {
    OTDU_ASSERT( bRecordingCommands );

    if ( pendingClear.bActive && !bPreparingDrawCall ) {
        flushPendingClear();
    }

    const VkImageLayout prevLayout = pTexture->CurrentLayout;
    if ( prevLayout == nextLayout && nextLayout != VK_IMAGE_LAYOUT_GENERAL ) {
        return;
    }

    endActiveRenderPass();

    const ImageSyncScope srcScope = GetImageSyncScope( prevLayout );
    const ImageSyncScope dstScope = GetImageSyncScope( nextLayout );

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = prevLayout;
    barrier.newLayout = nextLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pTexture->Image;
    barrier.subresourceRange = GetFullSubresourceRange( pTexture );
    barrier.srcAccessMask = GetWriteAccess( srcScope.Access );
    barrier.dstAccessMask = dstScope.Access;

    vkCmdPipelineBarrier( getCommandBuffer(), srcScope.Stages, dstScope.Stages,
                          0, 0, nullptr, 0, nullptr, 1, &barrier );

    pTexture->CurrentLayout = nextLayout;
}

void RenderDevice::insertImageBarrier( GPUTexture *pTexture,
                                       const VkImageLayout callerPrevLayout,
                                       const VkImageLayout nextLayout,
                                       const VkPipelineStageFlags srcStage,
                                       const VkAccessFlags srcAccess,
                                       const VkPipelineStageFlags dstStage,
                                       const VkAccessFlags dstAccess ) {
    OTDU_ASSERT( bRecordingCommands );

    VkImageLayout prevLayout = callerPrevLayout;
    if ( pendingClear.bActive && !bPreparingDrawCall ) {
        flushPendingClear();
        prevLayout = pTexture->CurrentLayout;
    }

    endActiveRenderPass();

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = prevLayout;
    barrier.newLayout = nextLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pTexture->Image;
    barrier.subresourceRange = GetFullSubresourceRange( pTexture );
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier( getCommandBuffer(), srcStage, dstStage, 0, 0, nullptr,
                          0, nullptr, 1, &barrier );

    pTexture->CurrentLayout = nextLayout;
}

void RenderDevice::recreateSwapchain() {
    int32_t width = 0;
    int32_t height = 0;

    bool bMinimized = false;
#if defined( OTDU_USE_SDL )
    SDL_Window *pWindow = (SDL_Window *)gEdSys.getWindow();
    SDL_GetWindowSize( pWindow, &width, &height );
    bMinimized =
        ( SDL_GetWindowFlags( pWindow ) & SDL_WINDOW_MINIMIZED ) != 0 ||
        width <= 0 || height <= 0;
#endif

    // OTDU_ADDITION: a minimized window (e.g. alt-tab out of fullscreen) has a
    // 0x0 surface. Recreating at that size fails after the current swapchain
    // and backbuffer have already been released, and the next frame then
    // renders to a destroyed image (driver crash). Keep the current swapchain
    // instead: acquire/present fail gracefully (frames are dropped) and the
    // recreation is retried once the window is restored.
    if ( surface != VK_NULL_HANDLE ) {
        VkSurfaceCapabilitiesKHR capabilities = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physicalDevice, surface,
                                                   &capabilities );
        if ( capabilities.currentExtent.width == 0 ||
             capabilities.currentExtent.height == 0 ) {
            bMinimized = true;
        }
    }
    if ( bMinimized ) {
        bSwapchainOutOfDate.store( true, std::memory_order_release );
        // Do not spin while nothing can be presented
        std::this_thread::sleep_for( std::chrono::milliseconds( 16 ) );
        return;
    }

    waitForIdle();
    bSwapchainOutOfDate.store( false, std::memory_order_release );

    gammaRampPass.releaseSwapchainResources( device );
    releaseSwapchain();
    releaseFramebufferCache();
    if ( createSwapchain() ) {
        createOutputPassResources();
    }
}

void RenderDevice::createOutputPassResources() {
    gammaRampPass.createSwapchainResources(
        device, backbuffer.pTexture, swapchainImageFormat, activeOutputTransfer,
        swapchainImageViews );
    updateDisplayPeak();
    updateOutputParameters();
}

void RenderDevice::releaseSwapchain() {
    if ( backbuffer.pTexture != nullptr ) {
        if ( backbuffer.pTexture->ImageView != VK_NULL_HANDLE ) {
            vkDestroyImageView( device, backbuffer.pTexture->ImageView,
                                nullptr );
            backbuffer.pTexture->ImageView = VK_NULL_HANDLE;
        }
        if ( backbuffer.pTexture->Image != VK_NULL_HANDLE ) {
            vmaDestroyImage( allocator, backbuffer.pTexture->Image,
                             backbuffer.pTexture->Allocation );
            backbuffer.pTexture->Image = VK_NULL_HANDLE;
            backbuffer.pTexture->Allocation = nullptr;
        }
    }

    for ( VkImageView &imageView : swapchainImageViews ) {
        vkDestroyImageView( device, imageView, nullptr );
    }
    swapchainImageViews.clear();

    vkDestroySwapchainKHR( device, swapchain, nullptr );
    swapchain = VK_NULL_HANDLE;

    for ( VkSemaphore &semaphore : presentSemaphores ) {
        vkDestroySemaphore( device, semaphore, nullptr );
    }
    presentSemaphores.clear();
}

void RenderDevice::releaseFramebufferCache() {
    for ( auto &entry : framebufferCache ) {
        vkDestroyFramebuffer( device, entry.second, nullptr );
    }
    framebufferCache.clear();

    activeFramebuffer = VK_NULL_HANDLE;
    activeFramebufferKey = {};
}

#if OTDU_DEVBUILD
void RenderDevice::requestRenderTargetDump( GPUTexture *pTexture,
                                            const char *pFilePath ) {
    if ( pTexture == nullptr || pFilePath == nullptr ) {
        return;
    }

    pendingRenderTargetDumps.push_back(
        { pTexture, std::string( pFilePath ) } );
}
#endif

bool RenderDevice::copyBackbufferToHost( std::vector<uint8_t> &texels,
                                         uint32_t &outWidth,
                                         uint32_t &outHeight,
                                         eViewFormat &outFormat ) {
    if ( backbuffer.pTexture == nullptr ) {
        return false;
    }

    waitForIdle();

    if ( !copyRenderTargetToHost( backbuffer.pTexture, texels ) ) {
        return false;
    }

    outWidth = backbuffer.Width;
    outHeight = backbuffer.Height;
    outFormat = backbuffer.Format;

    if ( backbuffer.Format == eViewFormat::VF_A16B16G16R16F ) {
        // HDR output: hand out the SDR rendition (clipped, BGRA8)
        const size_t texelCount = size_t( outWidth ) * outHeight;
        std::vector<uint8_t> converted( texelCount * 4 );
        const uint16_t *pHalves = (const uint16_t *)texels.data();
        for ( size_t i = 0; i < texelCount; i++ ) {
            for ( uint32_t channel = 0; channel < 4; channel++ ) {
                const float value = HalfToFloat( pHalves[i * 4 + channel] );
                // RGBA -> BGRA
                const uint32_t target =
                    ( channel < 3 ) ? ( 2 - channel ) : channel;
                converted[i * 4 + target] =
                    uint8_t( Clamp( value, 0.0f, 1.0f ) * 255.0f + 0.5f );
            }
        }
        texels = std::move( converted );
        outFormat = eViewFormat::VF_A8R8G8B8;
    }
    return true;
}

bool RenderDevice::copyRenderTargetToHost( GPUTexture *pTexture,
                                           std::vector<uint8_t> &texels ) {
    const uint32_t bytesPerTexel =
        GetVulkanFormatTexelSize( pTexture->ViewFormat );
    if ( bytesPerTexel == 0 ) {
        OTDU_LOG_ERROR( "Unsupported render target format 0x%x\n",
                        (uint32_t)pTexture->ViewFormat );
        return false;
    }

    const VkDeviceSize bufferSize =
        VkDeviceSize( pTexture->Width ) * pTexture->Height * bytesPerTexel;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    if ( vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &stagingBuffer,
                          &stagingAllocation, nullptr ) != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "Failed to allocate a %llu bytes readback buffer\n",
                        (unsigned long long)bufferSize );
        return false;
    }

    // The dump is a one-shot debug operation, so it gets its own pool/fence
    // rather than sharing the frame or upload command buffers.
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queues[0].familyIndex;

    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool( device, &poolInfo, nullptr, &pool );

    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = pool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers( device, &cmdAllocInfo, &cmdBuffer );

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer( cmdBuffer, &beginInfo );

    const VkImageLayout previousLayout = pTexture->CurrentLayout;

    VkImageSubresourceRange range;
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = previousLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = pTexture->Image;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                          nullptr, 1, &barrier );

    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;   // tightly packed
    copyRegion.bufferImageHeight = 0; // tightly packed
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = { 0, 0, 0 };
    copyRegion.imageExtent = { pTexture->Width, pTexture->Height, 1 };
    vkCmdCopyImageToBuffer( cmdBuffer, pTexture->Image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer,
                            1, &copyRegion );

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = previousLayout;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                          0, nullptr, 1, &barrier );

    vkEndCommandBuffer( cmdBuffer );

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence( device, &fenceInfo, nullptr, &fence );

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    vkQueueSubmit( queues[0].queue, 1, &submitInfo, fence );
    vkWaitForFences( device, 1, &fence, VK_TRUE, 9999999999 );

    bool bSuccess = false;
    void *pMappedMemory = nullptr;
    if ( vmaMapMemory( allocator, stagingAllocation, &pMappedMemory ) ==
             VK_SUCCESS &&
         pMappedMemory != nullptr ) {
        vmaInvalidateAllocation( allocator, stagingAllocation, 0, bufferSize );

        texels.resize( size_t( bufferSize ) );
        memcpy( texels.data(), pMappedMemory, size_t( bufferSize ) );
        vmaUnmapMemory( allocator, stagingAllocation );

        bSuccess = true;
    }

    vkDestroyFence( device, fence, nullptr );
    vkDestroyCommandPool( device, pool, nullptr );
    vmaDestroyBuffer( allocator, stagingBuffer, stagingAllocation );

    return bSuccess;
}

#if OTDU_DEVBUILD
// Normal range only (denormals flush to zero, overflow saturates to infinity)
static uint16_t FloatToHalf( const float value ) {
    uint32_t bits = 0;
    memcpy( &bits, &value, sizeof( bits ) );

    const uint16_t sign = uint16_t( ( bits >> 16 ) & 0x8000u );
    const int32_t exponent = int32_t( ( bits >> 23 ) & 0xffu ) - 127 + 15;
    const uint32_t mantissa = bits & 0x7fffffu;
    if ( exponent <= 0 ) {
        return sign;
    }
    if ( exponent >= 31 ) {
        return uint16_t( sign | 0x7c00u );
    }
    return uint16_t( sign | ( uint32_t( exponent ) << 10 ) |
                     ( mantissa >> 13 ) );
}

VkBuffer RenderDevice::createHostVisibleBuffer( const VkDeviceSize size,
                                                const VkBufferUsageFlags usage,
                                                VmaAllocation &outAllocation,
                                                const char *pName ) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    VkBuffer buffer = VK_NULL_HANDLE;
    outAllocation = nullptr;
    if ( vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &buffer,
                          &outAllocation, nullptr ) != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "Failed to allocate a %llu bytes host buffer\n",
                        (unsigned long long)size );
        return VK_NULL_HANDLE;
    }
    return buffer;
}

void RenderDevice::snapshotRenderTarget( GPUTexture *pTexture,
                                         const char *pFilePath ) {
    if ( pTexture == nullptr || pFilePath == nullptr || !bRecordingCommands ) {
        return;
    }

    const uint32_t bytesPerTexel =
        GetVulkanFormatTexelSize( pTexture->ViewFormat );
    if ( bytesPerTexel == 0 ) {
        OTDU_LOG_ERROR( "Unsupported render target format 0x%x\n",
                        (uint32_t)pTexture->ViewFormat );
        return;
    }

    if ( pTexture->SampleCount != VK_SAMPLE_COUNT_1_BIT ) {
        OTDU_LOG_WARN( "Cannot snapshot a multisampled render target\n" );
        return;
    }

    PendingRenderTargetSnapshot snapshot = {};
    snapshot.Size =
        VkDeviceSize( pTexture->Width ) * pTexture->Height * bytesPerTexel;
    snapshot.Buffer = createHostVisibleBuffer(
        snapshot.Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, snapshot.Allocation,
        "RTSnapshotReadback" );
    if ( snapshot.Buffer == VK_NULL_HANDLE ) {
        return;
    }
    snapshot.FilePath =
        std::string( pFilePath ) + "_" + std::to_string( pTexture->Width ) +
        "x" + std::to_string( pTexture->Height ) + "_" +
        std::to_string( uint32_t( pTexture->ViewFormat ) ) + ".raw";

    const VkImageLayout previousLayout = pTexture->CurrentLayout;
    insertImageBarrier(
        pTexture, previousLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT );

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { pTexture->Width, pTexture->Height, 1 };
    vkCmdCopyImageToBuffer( getCommandBuffer(), pTexture->Image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            snapshot.Buffer, 1, &region );

    if ( previousLayout != VK_IMAGE_LAYOUT_UNDEFINED ) {
        insertImageBarrier(
            pTexture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, previousLayout,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT );
    }

    pendingRenderTargetSnapshots.push_back( snapshot );
}

void RenderDevice::fillRenderTargetRegion( GPUTexture *pTexture,
                                           const VkOffset2D offset,
                                           const VkExtent2D extent,
                                           const float color[4] ) {
    if ( pTexture == nullptr || !bRecordingCommands ) {
        return;
    }
    if ( pTexture->ViewFormat != VK_FORMAT_R16G16B16A16_SFLOAT ||
         pTexture->SampleCount != VK_SAMPLE_COUNT_1_BIT ) {
        OTDU_LOG_WARN( "fillRenderTargetRegion: only single sampled RGBA16F "
                       "targets are supported (format 0x%x)\n",
                       (uint32_t)pTexture->ViewFormat );
        return;
    }

    const VkExtent2D clipped = {
        Min( extent.width, pTexture->Width - uint32_t( offset.x ) ),
        Min( extent.height, pTexture->Height - uint32_t( offset.y ) ) };

    PendingRenderTargetSnapshot upload = {};
    upload.Size = VkDeviceSize( clipped.width ) * clipped.height * 8u;
    upload.Buffer =
        createHostVisibleBuffer( upload.Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 upload.Allocation, "RTFillUpload" );
    if ( upload.Buffer == VK_NULL_HANDLE ) {
        return;
    }

    const uint16_t texel[4] = {
        FloatToHalf( color[0] ), FloatToHalf( color[1] ),
        FloatToHalf( color[2] ), FloatToHalf( color[3] ) };
    uint16_t *pTexels = nullptr;
    vmaMapMemory( allocator, upload.Allocation, (void **)&pTexels );
    for ( VkDeviceSize i = 0; i < upload.Size / 8u; i++ ) {
        memcpy( pTexels + i * 4, texel, sizeof( texel ) );
    }
    vmaUnmapMemory( allocator, upload.Allocation );

    const VkImageLayout previousLayout = pTexture->CurrentLayout;
    insertImageBarrier(
        pTexture, previousLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT );

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { offset.x, offset.y, 0 };
    region.imageExtent = { clipped.width, clipped.height, 1 };
    vkCmdCopyBufferToImage( getCommandBuffer(), upload.Buffer, pTexture->Image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

    insertImageBarrier(
        pTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        ( previousLayout != VK_IMAGE_LAYOUT_UNDEFINED )
            ? previousLayout
            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT );

    pendingRenderTargetSnapshots.push_back( upload );
}

void RenderDevice::processPendingRenderTargetDumps() {
    if ( pendingRenderTargetDumps.empty() &&
         pendingRenderTargetSnapshots.empty() ) {
        return;
    }

    waitForIdle();

    for ( const PendingRenderTargetSnapshot &snapshot :
          pendingRenderTargetSnapshots ) {
        if ( !snapshot.FilePath.empty() ) {
            void *pData = nullptr;
            vmaMapMemory( allocator, snapshot.Allocation, &pData );
            std::ofstream stream( snapshot.FilePath.c_str(),
                                  std::ofstream::out | std::ofstream::binary );
            if ( stream.is_open() ) {
                stream.write( (const char *)pData,
                              std::streamsize( snapshot.Size ) );
                OTDU_LOG_INFO( "Wrote render target snapshot '%s'\n",
                               snapshot.FilePath.c_str() );
            } else {
                OTDU_LOG_ERROR( "Failed to open '%s' for writing\n",
                                snapshot.FilePath.c_str() );
            }
            vmaUnmapMemory( allocator, snapshot.Allocation );
        }
        vmaDestroyBuffer( allocator, snapshot.Buffer, snapshot.Allocation );
    }
    pendingRenderTargetSnapshots.clear();

    for ( const PendingRenderTargetDump &dump : pendingRenderTargetDumps ) {
        GPUTexture *pTexture = dump.pTexture;

        std::vector<uint8_t> texels;
        if ( !copyRenderTargetToHost( pTexture, texels ) ) {
            continue;
        }

        const uint32_t bytesPerTexel =
            GetVulkanFormatTexelSize( pTexture->ViewFormat );
        const bool bIsTGACompatible =
            ( pTexture->ViewFormat == VK_FORMAT_B8G8R8A8_UNORM );

        std::string filePath =
            dump.FilePath + ( bIsTGACompatible ? ".tga" : ".raw" );
        std::ofstream stream( filePath.c_str(),
                              std::ofstream::out | std::ofstream::binary );
        if ( !stream.is_open() ) {
            OTDU_LOG_ERROR( "Failed to open '%s' for writing\n",
                            filePath.c_str() );
            continue;
        }

        if ( bIsTGACompatible ) {
            // Uncompressed 32bpp TGA. B8G8R8A8_UNORM texels are already in the
            // channel order TGA expects
            uint8_t header[18] = {};
            header[2] = 2; // uncompressed true-color
            header[12] = uint8_t( pTexture->Width & 0xff );
            header[13] = uint8_t( ( pTexture->Width >> 8 ) & 0xff );
            header[14] = uint8_t( pTexture->Height & 0xff );
            header[15] = uint8_t( ( pTexture->Height >> 8 ) & 0xff );
            header[16] = 32;   // bits per pixel
            header[17] = 0x28; // 8 alpha bits + top-left origin
            stream.write( (const char *)header, sizeof( header ) );
        }

        stream.write( (const char *)texels.data(),
                      std::streamsize( texels.size() ) );
        stream.close();

        OTDU_LOG_INFO( "Dumped render target (%ux%u, %u bytes/texel) to '%s'\n",
                       pTexture->Width, pTexture->Height, bytesPerTexel,
                       filePath.c_str() );
    }

    pendingRenderTargetDumps.clear();
}
#endif
#endif