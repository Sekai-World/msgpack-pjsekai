set(MSGPACK_PJSEKAI_GENERATED_DIR "${MSGPACK_PJSEKAI_GENERATED_DIR}" CACHE PATH "Directory that contains generated msgpack-pjsekai files")
set(MSGPACK_PJSEKAI_FILE_LIST "${MSGPACK_PJSEKAI_GENERATED_DIR}/msgpack-pjsekai.files")

if(NOT EXISTS "${MSGPACK_PJSEKAI_FILE_LIST}")
    message(FATAL_ERROR "Generated file list not found: ${MSGPACK_PJSEKAI_FILE_LIST}. Run msgpack-pjsekai-gen first or set MSGPACK_PJSEKAI_GENERATED_DIR.")
endif()

add_subdirectory(deps EXCLUDE_FROM_ALL)
set_target_properties(msgpack-c-static PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(STRINGS "${MSGPACK_PJSEKAI_FILE_LIST}" _mpj_sources)
set(_mpj_generated_sources)
foreach(_src IN LISTS _mpj_sources)
    if(_src MATCHES "^[#/]" OR _src MATCHES "^[ \t]*\\*" OR _src STREQUAL "")
        continue()
    endif()
    list(APPEND _mpj_generated_sources "${MSGPACK_PJSEKAI_GENERATED_DIR}/${_src}")
endforeach()

add_library(msgpack_pjsekai STATIC ${_mpj_generated_sources})
add_library(msgpack_pjsekai::generated ALIAS msgpack_pjsekai)

add_library(msgpack_pjsekai_shared SHARED ${_mpj_generated_sources})
set_target_properties(msgpack_pjsekai_shared PROPERTIES OUTPUT_NAME msgpack_pjsekai)
add_library(msgpack_pjsekai::shared ALIAS msgpack_pjsekai_shared)

target_link_libraries(msgpack_pjsekai PRIVATE msgpack-c-static)
target_include_directories(msgpack_pjsekai
    PUBLIC
        "${MSGPACK_PJSEKAI_GENERATED_DIR}"
        "${MSGPACK_PJSEKAI_GENERATED_DIR}/generated"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/include"
)

target_link_libraries(msgpack_pjsekai_shared PRIVATE msgpack-c-static)
target_include_directories(msgpack_pjsekai_shared
    PUBLIC
        "${MSGPACK_PJSEKAI_GENERATED_DIR}"
        "${MSGPACK_PJSEKAI_GENERATED_DIR}/generated"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/include"
)
