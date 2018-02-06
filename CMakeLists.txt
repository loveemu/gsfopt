cmake_minimum_required(VERSION 2.8)

project(gsfopt C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VERSION "1.1.0")

add_definitions(-DGSFOPT)

if(MSVC)
    if(CMAKE_CL_64)
        set(MSVC_ARCHITECTURE_NAME x64)
    else()
        set(MSVC_ARCHITECTURE_NAME win32)
    endif()

    # Use bundled dependencies
    set(ZLIB_ROOT ${CMAKE_SOURCE_DIR}/dependencies/zlib)
    set(ZLIB_LIBRARY ${CMAKE_SOURCE_DIR}/dependencies/zlib/lib/${MSVC_ARCHITECTURE_NAME}/zlibstat.lib)
    add_definitions(-DZLIB_WINAPI)
endif()

find_package(ZLIB REQUIRED)

if(MSVC)
    option(STATIC_CRT "Use static CRT libraries" ON)

    # Rewrite command line flags to use /MT if necessary
    if(STATIC_CRT)
        foreach(flag_var
                CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
                CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
            if(${flag_var} MATCHES "/MD")
                string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
            endif(${flag_var} MATCHES "/MD")
        endforeach(flag_var)
    endif()

    # Disable MSVC specific secure error
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

#============================================================================
# gsfopt
#============================================================================

set(SRCS
    src/gsfopt.cpp
    src/PSFFile.cpp
    src/ZlibReader.cpp
    src/ZlibWriter.cpp
)

set(VIOGSF_SRCS
    src/vbam/apu/Blip_Buffer.cpp
    src/vbam/apu/Effects_Buffer.cpp
    src/vbam/apu/Gb_Apu.cpp
    src/vbam/apu/Gb_Oscs.cpp
    src/vbam/apu/Multi_Buffer.cpp
    src/vbam/gba/bios.cpp
    src/vbam/gba/GBA.cpp
    src/vbam/gba/GBA-arm.cpp
    src/vbam/gba/GBA-thumb.cpp
    src/vbam/gba/Sound.cpp
)

add_executable(gsfopt ${SRCS} ${VIOGSF_SRCS})

if(ZLIB_FOUND)
    include_directories(${ZLIB_INCLUDE_DIRS})
    target_link_libraries(gsfopt ${ZLIB_LIBRARIES})
endif(ZLIB_FOUND)
