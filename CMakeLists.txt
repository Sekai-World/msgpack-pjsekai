cmake_minimum_required(VERSION 3.16)

project(msgpack_pjsekai VERSION 0.4.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MSGPACK_PJSEKAI_BUILD_GENERATOR "Build the dump.cs to msgpack-c generator" ON)
option(MSGPACK_PJSEKAI_BUILD_GENERATED "Build generated msgpack-pjsekai sources" OFF)
set(MSGPACK_PJSEKAI_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/msgpack" CACHE PATH "Directory that contains generated msgpack-pjsekai files")

if(MSGPACK_PJSEKAI_BUILD_GENERATOR)
    add_subdirectory(cli)
endif()

if(MSGPACK_PJSEKAI_BUILD_GENERATED)
    add_subdirectory(msgpack)
endif()
