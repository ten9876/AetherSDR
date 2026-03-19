cmake_minimum_required(VERSION 3.20)
project(AetherSDR VERSION 0.4.14 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Qt6 components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Multimedia
)

# PortAudio (optional fallback for audio)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PORTAUDIO portaudio-2.0)
endif()

# FFTW3 (required for NR2 spectral noise reduction)
# Windows: run setup-fftw.ps1 first to download prebuilt DLLs
# Linux:   apt install libfftw3-dev
# macOS:   brew install fftw
if(WIN32)
    set(FFTW3_ROOT "${CMAKE_SOURCE_DIR}/third_party/fftw3")
    if(EXISTS "${FFTW3_ROOT}/include/fftw3.h")
        set(FFTW3_FOUND TRUE)
        set(FFTW3_INCLUDE_DIRS "${FFTW3_ROOT}/include")
        set(FFTW3_LIBRARIES "${FFTW3_ROOT}/lib/fftw3.lib")
        set(FFTW3_DLL "${FFTW3_ROOT}/bin/libfftw3-3.dll")
    else()
        message(WARNING "FFTW3 not found. Run setup-fftw.ps1 to download it. NR2 will use fallback FFT.")
    endif()
else()
    if(PkgConfig_FOUND)
        pkg_check_modules(FFTW3 fftw3)
    endif()
    if(NOT FFTW3_FOUND)
        find_library(FFTW3_LIB fftw3)
        find_path(FFTW3_INC fftw3.h)
        if(FFTW3_LIB AND FFTW3_INC)
            set(FFTW3_FOUND TRUE)
            set(FFTW3_LIBRARIES ${FFTW3_LIB})
            set(FFTW3_INCLUDE_DIRS ${FFTW3_INC})
        endif()
    endif()
endif()

# Bundled RADE (BSD-2) — FreeDV Radio Autoencoder digital voice codec
# Opus (with FARGAN/LPCNet) is downloaded and built automatically via ExternalProject
set(RADE_DIR ${CMAKE_SOURCE_DIR}/third_party/radae)
option(ENABLE_RADE "Build with RADE digital voice support (downloads Opus at configure time)" ON)
if(ENABLE_RADE AND EXISTS "${RADE_DIR}/src/rade_api.h")
    # macOS universal binary: build Opus for both architectures
    if(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64.*arm64|arm64.*x86_64")
        set(BUILD_OSX_UNIVERSAL ON)
    endif()
    include(${RADE_DIR}/cmake/BuildOpus.cmake)
    set(RADE_SOURCES
        ${RADE_DIR}/src/rade_api_nopy.c
        ${RADE_DIR}/src/rade_dsp.c
        ${RADE_DIR}/src/rade_ofdm.c
        ${RADE_DIR}/src/rade_bpf.c
        ${RADE_DIR}/src/rade_acq.c
        ${RADE_DIR}/src/rade_tx.c
        ${RADE_DIR}/src/rade_rx.c
        ${RADE_DIR}/src/rade_enc.c
        ${RADE_DIR}/src/rade_dec.c
        ${RADE_DIR}/src/rade_enc_data.c
        ${RADE_DIR}/src/rade_dec_data.c
        ${RADE_DIR}/src/kiss_fft.c
        ${RADE_DIR}/src/kiss_fftr.c
    )
    set_source_files_properties(${RADE_SOURCES} PROPERTIES
        COMPILE_FLAGS "-w -DIS_BUILDING_RADE_API=1 -DRADE_PYTHON_FREE=1"
        INCLUDE_DIRECTORIES "${RADE_DIR}/src"
    )
    set(RADE_FOUND TRUE)
    message(STATUS "RADE enabled (bundled, Opus will be downloaded)")
else()
    set(RADE_FOUND FALSE)
    set(RADE_SOURCES "")
    if(ENABLE_RADE)
        message(STATUS "RADE source not found at ${RADE_DIR}. Digital voice disabled.")
    else()
        message(STATUS "RADE disabled by ENABLE_RADE=OFF.")
    endif()
endif()

# Sources
set(CORE_SOURCES
    src/core/AppSettings.cpp
    src/core/RadioDiscovery.cpp
    src/core/RadioConnection.cpp
    src/core/CommandParser.cpp
    src/core/AudioEngine.cpp
    src/core/SpectralNR.cpp
    src/core/PanadapterStream.cpp
    src/core/RigctlProtocol.cpp
    src/core/RigctlPty.cpp
    src/core/SmartLinkClient.cpp
    src/core/WanConnection.cpp
    src/core/RigctlServer.cpp
    src/core/FirmwareUploader.cpp
    src/core/FirmwareStager.cpp
    src/core/RNNoiseFilter.cpp
    src/core/CwDecoder.cpp
    src/core/Resampler.cpp
    src/core/RADEEngine.cpp
)

if(APPLE)
    list(APPEND CORE_SOURCES src/core/VirtualAudioBridge.cpp)
endif()

set(MODEL_SOURCES
    src/models/RadioModel.cpp
    src/models/SliceModel.cpp
    src/models/MeterModel.cpp
    src/models/TunerModel.cpp
    src/models/TransmitModel.cpp
    src/models/EqualizerModel.cpp
    src/models/TnfModel.cpp
    src/models/BandSettings.cpp
    src/models/AntennaGeniusModel.cpp
)

set(GUI_SOURCES
    src/gui/MainWindow.cpp
    src/gui/ConnectionPanel.cpp
    src/gui/SpectrumWidget.cpp
    src/gui/SpectrumOverlayMenu.cpp
    src/gui/VfoWidget.cpp
    src/gui/RadioSetupDialog.cpp
    src/gui/NetworkDiagnosticsDialog.cpp
    src/gui/MemoryDialog.cpp
    src/gui/SpotSettingsDialog.cpp
    src/gui/ProfileManagerDialog.cpp
    src/gui/PanadapterApplet.cpp
    src/gui/AppletPanel.cpp
    src/gui/RxApplet.cpp
    src/gui/SMeterWidget.cpp
    src/gui/TunerApplet.cpp
    src/gui/TxApplet.cpp
    src/gui/PhoneCwApplet.cpp
    src/gui/PhoneApplet.cpp
    src/gui/EqApplet.cpp
    src/gui/CatApplet.cpp
    src/gui/AntennaGeniusApplet.cpp
)

set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${MODEL_SOURCES}
    ${GUI_SOURCES}
)

# Bundled RNNoise (Mozilla/Xiph BSD-3) — client-side neural noise suppression
set(RNNOISE_DIR ${CMAKE_SOURCE_DIR}/third_party/rnnoise)
set(RNNOISE_SOURCES
    ${RNNOISE_DIR}/src/denoise.c
    ${RNNOISE_DIR}/src/celt_lpc.c
    ${RNNOISE_DIR}/src/kiss_fft.c
    ${RNNOISE_DIR}/src/pitch.c
    ${RNNOISE_DIR}/src/rnn.c
    ${RNNOISE_DIR}/src/nnet.c
    ${RNNOISE_DIR}/src/nnet_default.c
    ${RNNOISE_DIR}/src/rnnoise_data.c
    ${RNNOISE_DIR}/src/rnnoise_tables.c
    ${RNNOISE_DIR}/src/parse_lpcnet_weights.c
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    list(APPEND RNNOISE_SOURCES
        ${RNNOISE_DIR}/src/x86/x86cpu.c
        ${RNNOISE_DIR}/src/x86/x86_dnn_map.c
        ${RNNOISE_DIR}/src/x86/nnet_sse4_1.c
        ${RNNOISE_DIR}/src/x86/nnet_avx2.c
    )
endif()

# Suppress warnings and set include paths for bundled C code
set_source_files_properties(${RNNOISE_SOURCES} PROPERTIES
    INCLUDE_DIRECTORIES "${RNNOISE_DIR}/include;${RNNOISE_DIR}/src;${RNNOISE_DIR}/src/x86"
)
# x86 RTCD (runtime CPU detection) + per-file SIMD flags
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    if(MSVC)
        # MSVC: uses _MSC_VER path in x86cpu.c (intrin.h), no CPU_INFO_BY_C needed
        # MSVC doesn't need -msse4.1/-mavx flags — intrinsics are always available
        set_source_files_properties(${RNNOISE_SOURCES} PROPERTIES
            COMPILE_FLAGS "/w /DRNN_ENABLE_X86_RTCD=1"
        )
        set_source_files_properties(${RNNOISE_DIR}/src/x86/nnet_sse4_1.c PROPERTIES
            COMPILE_FLAGS "/w /DRNN_ENABLE_X86_RTCD=1 /D__SSE4_1__"
        )
        set_source_files_properties(${RNNOISE_DIR}/src/x86/nnet_avx2.c PROPERTIES
            COMPILE_FLAGS "/w /DRNN_ENABLE_X86_RTCD=1 /arch:AVX2 /D__AVX2__"
        )
    else()
        # GCC/Clang: use cpuid.h intrinsic and per-file ISA flags
        set_source_files_properties(${RNNOISE_SOURCES} PROPERTIES
            COMPILE_FLAGS "-w -DRNN_ENABLE_X86_RTCD=1 -DCPU_INFO_BY_C=1"
        )
        set_source_files_properties(${RNNOISE_DIR}/src/x86/nnet_sse4_1.c PROPERTIES
            COMPILE_FLAGS "-w -DRNN_ENABLE_X86_RTCD=1 -DCPU_INFO_BY_C=1 -msse4.1"
        )
        set_source_files_properties(${RNNOISE_DIR}/src/x86/nnet_avx2.c PROPERTIES
            COMPILE_FLAGS "-w -DRNN_ENABLE_X86_RTCD=1 -DCPU_INFO_BY_C=1 -mavx -mfma -mavx2"
        )
    endif()
else()
    set_source_files_properties(${RNNOISE_SOURCES} PROPERTIES
        COMPILE_FLAGS "-w"
    )
endif()

# Bundled ggmorse (MIT) — CW Morse code decoder
set(GGMORSE_DIR ${CMAKE_SOURCE_DIR}/third_party/ggmorse)
set(GGMORSE_SOURCES
    ${GGMORSE_DIR}/src/ggmorse.cpp
    ${GGMORSE_DIR}/src/resampler.cpp
)
set_source_files_properties(${GGMORSE_SOURCES} PROPERTIES
    INCLUDE_DIRECTORIES "${GGMORSE_DIR}/include;${GGMORSE_DIR}/src"
)
if(MSVC)
    set_source_files_properties(${GGMORSE_SOURCES} PROPERTIES COMPILE_FLAGS "/w")
else()
    set_source_files_properties(${GGMORSE_SOURCES} PROPERTIES COMPILE_FLAGS "-w")
endif()

qt_add_resources(RESOURCES resources.qrc)

add_executable(AetherSDR ${ALL_SOURCES} ${RNNOISE_SOURCES} ${GGMORSE_SOURCES} ${RADE_SOURCES} ${RESOURCES})

# macOS app bundle
if(APPLE)
    set_target_properties(AetherSDR PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.aethersdr.AetherSDR"
        MACOSX_BUNDLE_BUNDLE_NAME "AetherSDR"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "AetherSDR.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
    )
    set(MACOSX_ICON "${CMAKE_SOURCE_DIR}/docs/AetherSDR.icns")
    if(EXISTS "${MACOSX_ICON}")
        target_sources(AetherSDR PRIVATE "${MACOSX_ICON}")
        set_source_files_properties("${MACOSX_ICON}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
    endif()
endif()

target_include_directories(AetherSDR PRIVATE
    src/
    ${RNNOISE_DIR}/include
    ${RNNOISE_DIR}/src
    ${GGMORSE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/r8brain
)

target_link_libraries(AetherSDR PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
)

if(PORTAUDIO_FOUND)
    target_compile_definitions(AetherSDR PRIVATE HAVE_PORTAUDIO)
    target_include_directories(AetherSDR PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
    target_link_directories(AetherSDR PRIVATE ${PORTAUDIO_LIBRARY_DIRS})
    target_link_libraries(AetherSDR PRIVATE ${PORTAUDIO_LIBRARIES})
endif()

if(FFTW3_FOUND)
    target_compile_definitions(AetherSDR PRIVATE HAVE_FFTW3)
    target_include_directories(AetherSDR PRIVATE ${FFTW3_INCLUDE_DIRS})
    target_link_libraries(AetherSDR PRIVATE ${FFTW3_LIBRARIES})
    # Copy DLL to build dir on Windows
    if(WIN32 AND FFTW3_DLL)
        add_custom_command(TARGET AetherSDR POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FFTW3_DLL}" "$<TARGET_FILE_DIR:AetherSDR>"
            COMMENT "Copying libfftw3-3.dll to build directory"
        )
    endif()
endif()

if(RADE_FOUND)
    target_compile_definitions(AetherSDR PRIVATE HAVE_RADE)
    target_include_directories(AetherSDR PRIVATE ${RADE_DIR}/src)
    target_link_libraries(AetherSDR PRIVATE opus m)
endif()

# Pass project version to code
target_compile_definitions(AetherSDR PRIVATE
    AETHERSDR_VERSION="${PROJECT_VERSION}"
)

# Compiler warnings
if(MSVC)
    target_compile_options(AetherSDR PRIVATE /W3 /Zc:__cplusplus /permissive- /utf-8)
else()
    target_compile_options(AetherSDR PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g3 -fsanitize=address>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    )
    target_link_options(AetherSDR PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address>
    )
endif()


# ── Install rules ────────────────────────────────────────────────────────────
include(GNUInstallDirs)

if(APPLE)
    install(TARGETS AetherSDR
        BUNDLE DESTINATION .
    )
else()
    install(TARGETS AetherSDR
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES AetherSDR.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    )
    install(FILES docs/logo-circle.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
        RENAME aethersdr.png
    )
endif()
