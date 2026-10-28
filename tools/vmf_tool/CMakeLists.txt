file( GLOB OTDU_EVT_SRC "${OTDU_BASE_FOLDER}/tools/vmf_tool/main.cpp"
                        "${OTDU_SRC_FOLDER}/core/arg_parser.cpp"
                        "${OTDU_SRC_FOLDER}/core/logger.cpp"
                        "${OTDU_SRC_FOLDER}/core/mutex.cpp"
                        "${OTDU_SRC_FOLDER}/flash/vmf/vmf_document.cpp"
                        "${OTDU_SRC_FOLDER}/flash/vmf/vmf_movie_builder.cpp"
                        "${OTDU_SRC_FOLDER}/filesystem/bank_writer.cpp")

add_executable( opentdu_vmfTool ${OTDU_EVT_SRC} )
set_property(TARGET opentdu_vmfTool PROPERTY FOLDER "Tools")

if ( WIN32 )
    target_link_libraries( opentdu_vmfTool winmm Pathcch Shlwapi )
endif ( WIN32 )

if ( WIN32 )
    set_target_properties( opentdu_vmfTool PROPERTIES OUTPUT_NAME "opentdu_vmftool_${CMAKE_GENERATOR_PLATFORM}")
elseif ( APPLE )
    set_target_properties( opentdu_vmfTool PROPERTIES OUTPUT_NAME "opentdu_vmftool_darwin")
else()
    set_target_properties( opentdu_vmfTool PROPERTIES OUTPUT_NAME "opentdu_vmftool_unix")
endif()

add_definitions( -DOTDU_LOG_STDOUT )
add_definitions( -DOTDU_FILE_NO_CONTEXT_SUPPORT )
