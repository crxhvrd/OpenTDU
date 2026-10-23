file( GLOB OTDU_EXTRACTOR_CORE_SRC "${OTDU_BASE_FOLDER}/tools/asset_extractor/asset_extractor.cpp"
                                   "${OTDU_BASE_FOLDER}/tools/asset_extractor/asset_extractor.h" )

if ( ${OTDU_EXTRACTOR_DXVK} )
    file( GLOB OTDU_DXVK_SRC "${OTDU_THIRDPARTY_FOLDER}/dxvk/src/dxso/*.cpp"
                             "${OTDU_THIRDPARTY_FOLDER}/dxvk/src/dxso/*.h"
                             "${OTDU_THIRDPARTY_FOLDER}/dxvk/src/spirv/*.cpp"
                             "${OTDU_THIRDPARTY_FOLDER}/dxvk/src/spirv/*.h")
else()
    set( OTDU_DXVK_SRC "" )
endif()

add_library( otdu_extractor STATIC ${OTDU_EXTRACTOR_CORE_SRC} ${OTDU_DXVK_SRC} )
set_property(TARGET otdu_extractor PROPERTY FOLDER "Tools")

target_include_directories( otdu_extractor PUBLIC "${OTDU_BASE_FOLDER}/tools" )
target_include_directories( otdu_extractor PRIVATE "${OTDU_THIRDPARTY_FOLDER}/Vulkan-Headers/include" )
if ( ${OTDU_EXTRACTOR_DXVK} )
    target_compile_definitions( otdu_extractor PUBLIC OTDU_EXTRACTOR_DXVK )
    target_include_directories( otdu_extractor PRIVATE "${OTDU_THIRDPARTY_FOLDER}/dxvk/include" )
    target_include_directories( otdu_extractor PRIVATE "${OTDU_THIRDPARTY_FOLDER}/spirv/include" )
endif()

file( GLOB OTDU_EAE_SRC "${OTDU_BASE_FOLDER}/tools/asset_extractor/main.cpp"
                        "${OTDU_SRC_FOLDER}/core/arg_parser.cpp"
                        "${OTDU_SRC_FOLDER}/core/logger.cpp")

if ( ${OTDU_EXTRACTOR_DXVK} )
    file( GLOB OTDU_SPVC_SRC "${OTDU_THIRDPARTY_FOLDER}/spirv_cross/spirv_*.cpp"
                             "${OTDU_THIRDPARTY_FOLDER}/spirv_cross/spirv_*.hpp" )
else()
    set( OTDU_SPVC_SRC "" )
endif()

add_executable( opentdu_assetExtractor ${OTDU_EAE_SRC} ${OTDU_SPVC_SRC} )
set_property(TARGET opentdu_assetExtractor PROPERTY FOLDER "Tools")

target_link_libraries( opentdu_assetExtractor otdu_extractor )
target_compile_definitions( opentdu_assetExtractor PRIVATE OTDU_LOG_STDOUT )

target_include_directories( opentdu_assetExtractor PRIVATE "${OTDU_THIRDPARTY_FOLDER}/Vulkan-Headers/include" )
if ( ${OTDU_EXTRACTOR_DXVK} )
    target_include_directories( opentdu_assetExtractor PRIVATE "${OTDU_THIRDPARTY_FOLDER}/spirv/include" )
    target_include_directories( opentdu_assetExtractor PRIVATE "${OTDU_THIRDPARTY_FOLDER}/spirv_cross/" )
    target_include_directories( opentdu_assetExtractor PRIVATE "${OTDU_THIRDPARTY_FOLDER}/spirv_cross/include" )
endif()

if ( WIN32 )
    set_target_properties( opentdu_assetExtractor PROPERTIES OUTPUT_NAME "opentdu_assetextractor_${CMAKE_GENERATOR_PLATFORM}")
elseif ( APPLE )
    set_target_properties( opentdu_assetExtractor PROPERTIES OUTPUT_NAME "opentdu_assetextractor_darwin")
else()
    set_target_properties( opentdu_assetExtractor PROPERTIES OUTPUT_NAME "opentdu_assetextractor_unix")
endif()

add_custom_command(
    TARGET opentdu_assetExtractor
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${OTDU_RSC_FOLDER}/imgui_vs.bin"
            "${OTDU_BIN_FOLDER}/EURO/Shaders/d3d9/imgui_vs.bin"
    COMMENT "Copying imgui vertex shader binary..."
)
add_custom_command(
    TARGET opentdu_assetExtractor
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${OTDU_RSC_FOLDER}/imgui_ps.bin"
            "${OTDU_BIN_FOLDER}/EURO/Shaders/d3d9/imgui_ps.bin"
    COMMENT "Copying imgui pixel shader binary..."
)
add_custom_command(
    TARGET opentdu_assetExtractor
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${OTDU_RSC_FOLDER}/dbg_font_vs.bin"
            "${OTDU_BIN_FOLDER}/EURO/Shaders/d3d9/dbg_font_vs.bin"
    COMMENT "Copying dbg_font vertex shader binary..."
)
add_custom_command(
    TARGET opentdu_assetExtractor
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${OTDU_RSC_FOLDER}/dbg_font_ps.bin"
            "${OTDU_BIN_FOLDER}/EURO/Shaders/d3d9/dbg_font_ps.bin"
    COMMENT "Copying dbg_font pixel shader binary..."
)
