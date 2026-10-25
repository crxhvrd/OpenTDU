file( GLOB OTDU_ESP_SRC "${OTDU_BASE_FOLDER}/tools/shader_packer/main.cpp"
                        "${OTDU_SRC_FOLDER}/core/arg_parser.cpp"
                        "${OTDU_SRC_FOLDER}/core/logger.cpp" )

add_executable( opentdu_shaderPacker ${OTDU_ESP_SRC} )
set_property(TARGET opentdu_shaderPacker PROPERTY FOLDER "Tools")
target_compile_definitions( opentdu_shaderPacker PRIVATE OTDU_LOG_STDOUT )

if ( WIN32 )
    set_target_properties( opentdu_shaderPacker PROPERTIES OUTPUT_NAME "opentdu_shaderpacker_${CMAKE_GENERATOR_PLATFORM}")
elseif ( APPLE )
    set_target_properties( opentdu_shaderPacker PROPERTIES OUTPUT_NAME "opentdu_shaderpacker_darwin")
else()
    set_target_properties( opentdu_shaderPacker PROPERTIES OUTPUT_NAME "opentdu_shaderpacker_unix")
endif()
