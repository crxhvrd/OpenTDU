file( GLOB OTDU_EMP_SRC "${OTDU_BASE_FOLDER}/tools/mod_packer/main.cpp"
                        "${OTDU_SRC_FOLDER}/core/arg_parser.cpp"
                        "${OTDU_SRC_FOLDER}/core/logger.cpp"
                        "${OTDU_SRC_FOLDER}/core/mutex.cpp"
                        "${OTDU_SRC_FOLDER}/filesystem/file_map.cpp")

add_executable( opentdu_modPacker ${OTDU_EMP_SRC} )
set_property(TARGET opentdu_modPacker PROPERTY FOLDER "Tools")

if ( WIN32 )
    target_link_libraries( opentdu_modPacker winmm Pathcch Shlwapi )
endif ( WIN32 )

if ( WIN32 )
    set_target_properties( opentdu_modPacker PROPERTIES OUTPUT_NAME "opentdu_modpacker_${CMAKE_GENERATOR_PLATFORM}")
elseif ( APPLE )
    set_target_properties( opentdu_modPacker PROPERTIES OUTPUT_NAME "opentdu_modpacker_darwin")
else()
    set_target_properties( opentdu_modPacker PROPERTIES OUTPUT_NAME "opentdu_modpacker_unix")
endif()

add_definitions( -DOTDU_LOG_STDOUT )
add_definitions( -DOTDU_FILE_NO_CONTEXT_SUPPORT )
