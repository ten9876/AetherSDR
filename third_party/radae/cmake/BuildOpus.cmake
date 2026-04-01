message(STATUS "Will build opus with FARGAN")

set(CONFIGURE_COMMAND ./autogen.sh && ./configure --with-pic --enable-osce --enable-dred --disable-shared --disable-doc --disable-extra-programs)

if (CMAKE_CROSSCOMPILING)
set(CONFIGURE_COMMAND ${CONFIGURE_COMMAND} --host=${CMAKE_C_COMPILER_TARGET} --target=${CMAKE_C_COMPILER_TARGET})
endif (CMAKE_CROSSCOMPILING)

if (NOT DEFINED OPUS_URL)
set(OPUS_URL https://github.com/xiph/opus/archive/940d4e5af64351ca8ba8390df3f555484c567fbb.zip)
endif (NOT DEFINED OPUS_URL)

include(ExternalProject)

# ── Windows: build Opus via CMake (no autotools) ─────────────────────────
if(WIN32)
    ExternalProject_Add(build_opus
        DOWNLOAD_EXTRACT_TIMESTAMP NO
        URL ${OPUS_URL}
        PATCH_COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=<SOURCE_DIR>
            -DRADE_DIR=${RADE_DIR}
            -P ${RADE_DIR}/cmake/PatchOpusNnet.cmake
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DBUILD_SHARED_LIBS=OFF
            -DOPUS_BUILD_TESTING=OFF
            -DOPUS_BUILD_PROGRAMS=OFF
            -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF
            -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF
            -DOPUS_DRED=ON
            -DOPUS_OSCE=ON
        BUILD_BYPRODUCTS <BINARY_DIR>/opus${CMAKE_STATIC_LIBRARY_SUFFIX}
                         <BINARY_DIR>/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}
        INSTALL_COMMAND ""
    )

    ExternalProject_Get_Property(build_opus BINARY_DIR)
    ExternalProject_Get_Property(build_opus SOURCE_DIR)
    add_library(opus STATIC IMPORTED)
    add_dependencies(opus build_opus)

    # CMake produces opus.lib (MSVC) or libopus.a (MinGW)
    if(MSVC)
        set(_opus_lib "${BINARY_DIR}/opus${CMAKE_STATIC_LIBRARY_SUFFIX}")
    else()
        set(_opus_lib "${BINARY_DIR}/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endif()
    set_target_properties(opus PROPERTIES
        IMPORTED_LOCATION "${_opus_lib}"
        IMPORTED_IMPLIB   "${_opus_lib}"
    )

    include_directories(${SOURCE_DIR}/dnn ${SOURCE_DIR}/celt ${SOURCE_DIR}/include ${SOURCE_DIR})

# ── macOS universal: build twice and lipo ────────────────────────────────
elseif(APPLE AND BUILD_OSX_UNIVERSAL)
# Opus ./configure doesn't behave properly when built as a universal binary;
# build it twice and use lipo to create a universal libopus.a instead.
ExternalProject_Add(build_opus_x86
    DOWNLOAD_EXTRACT_TIMESTAMP NO
    BUILD_IN_SOURCE 1
    PATCH_COMMAND sh -c "patch dnn/nnet.h < ${RADE_DIR}/src/opus-nnet.h.diff"
    CONFIGURE_COMMAND ${CONFIGURE_COMMAND} --host=x86_64-apple-darwin --target=x86_64-apple-darwin CFLAGS=-arch\ x86_64\ -O2\ -mmacosx-version-min=10.11
    BUILD_COMMAND make -j4
    INSTALL_COMMAND ""
    URL ${OPUS_URL}
)
ExternalProject_Add(build_opus_arm
    DOWNLOAD_EXTRACT_TIMESTAMP NO
    BUILD_IN_SOURCE 1
    PATCH_COMMAND sh -c "patch dnn/nnet.h < ${RADE_DIR}/src/opus-nnet.h.diff"
    CONFIGURE_COMMAND ${CONFIGURE_COMMAND} --host=aarch64-apple-darwin --target=aarch64-apple-darwin CFLAGS=-arch\ arm64\ -O2\ -mmacosx-version-min=10.11
    BUILD_COMMAND make -j4
    INSTALL_COMMAND ""
    URL ${OPUS_URL}
)

ExternalProject_Get_Property(build_opus_arm BINARY_DIR)
ExternalProject_Get_Property(build_opus_arm SOURCE_DIR)
set(OPUS_ARM_BINARY_DIR ${BINARY_DIR})
ExternalProject_Get_Property(build_opus_x86 BINARY_DIR)
set(OPUS_X86_BINARY_DIR ${BINARY_DIR})

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}
    COMMAND lipo ${OPUS_ARM_BINARY_DIR}/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX} ${OPUS_X86_BINARY_DIR}/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX} -output ${CMAKE_CURRENT_BINARY_DIR}/libopus${CMAKE_STATIC_LIBRARY_SUFFIX} -create
    DEPENDS build_opus_arm build_opus_x86)

add_custom_target(
    libopus.a
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libopus${CMAKE_STATIC_LIBRARY_SUFFIX})

include_directories(${SOURCE_DIR}/dnn ${SOURCE_DIR}/celt ${SOURCE_DIR}/include ${SOURCE_DIR})

add_library(opus STATIC IMPORTED)
add_dependencies(opus libopus.a)
set_target_properties(opus PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}"
)

# ── Unix/Linux: autotools ────────────────────────────────────────────────
else()
ExternalProject_Add(build_opus
    DOWNLOAD_EXTRACT_TIMESTAMP NO
    BUILD_IN_SOURCE 1
    PATCH_COMMAND sh -c "patch dnn/nnet.h < ${RADE_DIR}/src/opus-nnet.h.diff"
    CONFIGURE_COMMAND ${CONFIGURE_COMMAND}
    BUILD_COMMAND make -j4
    INSTALL_COMMAND ""
    URL ${OPUS_URL}
    BUILD_BYPRODUCTS <BINARY_DIR>/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}
)

ExternalProject_Get_Property(build_opus BINARY_DIR)
ExternalProject_Get_Property(build_opus SOURCE_DIR)
add_library(opus STATIC IMPORTED)
add_dependencies(opus build_opus)

set_target_properties(opus PROPERTIES
    IMPORTED_LOCATION "${BINARY_DIR}/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}"
    IMPORTED_IMPLIB   "${BINARY_DIR}/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}"
)

include_directories(${SOURCE_DIR}/dnn ${SOURCE_DIR}/celt ${SOURCE_DIR}/include ${SOURCE_DIR})
endif()
