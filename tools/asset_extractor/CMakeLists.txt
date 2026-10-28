file( GLOB OTDU_EXTRACTOR_CORE_SRC "${OTDU_BASE_FOLDER}/tools/asset_extractor/asset_extractor.cpp"
                                   "${OTDU_BASE_FOLDER}/tools/asset_extractor/asset_extractor.h"
                                   "${OTDU_SRC_FOLDER}/filesystem/bank_writer.cpp"
                                   "${OTDU_SRC_FOLDER}/filesystem/virtual_file_system.cpp"
                                   "${OTDU_SRC_FOLDER}/filesystem/file_path_register.cpp"
                                   "${OTDU_SRC_FOLDER}/flash/vmf/vmf_document.cpp"
                                   "${OTDU_SRC_FOLDER}/flash/vmf/vmf_movie_builder.cpp" )

add_library( otdu_extractor STATIC ${OTDU_EXTRACTOR_CORE_SRC} )
set_property(TARGET otdu_extractor PROPERTY FOLDER "Tools")

target_include_directories( otdu_extractor PUBLIC "${OTDU_BASE_FOLDER}/tools" )
target_include_directories( otdu_extractor PRIVATE ${Vulkan_INCLUDE_DIRS} )

file( GLOB OTDU_EAE_SRC "${OTDU_BASE_FOLDER}/tools/asset_extractor/main.cpp"
                        "${OTDU_SRC_FOLDER}/core/arg_parser.cpp"
                        "${OTDU_SRC_FOLDER}/core/logger.cpp")

add_executable( opentdu_assetExtractor ${OTDU_EAE_SRC} )
set_property(TARGET opentdu_assetExtractor PROPERTY FOLDER "Tools")

target_link_libraries( opentdu_assetExtractor otdu_extractor )
target_compile_definitions( opentdu_assetExtractor PRIVATE OTDU_LOG_STDOUT )

target_include_directories( opentdu_assetExtractor PRIVATE ${Vulkan_INCLUDE_DIRS} )

if ( WIN32 )
    set_target_properties( opentdu_assetExtractor PROPERTIES OUTPUT_NAME "opentdu_assetextractor_${CMAKE_GENERATOR_PLATFORM}")
elseif ( APPLE )
    set_target_properties( opentdu_assetExtractor PROPERTIES OUTPUT_NAME "opentdu_assetextractor_darwin")
else()
    set_target_properties( opentdu_assetExtractor PROPERTIES OUTPUT_NAME "opentdu_assetextractor_unix")
endif()
