cmake_minimum_required(VERSION 3.20)
project(AetherSDR VERSION 0.8.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# macOS: ensure Homebrew lib/include paths are in the search path
# (universal builds with CMAKE_OSX_ARCHITECTURES may not search /opt/homebrew by default)
if(APPLE)
    execute_process(COMMAND brew --prefix OUTPUT_VARIABLE HOMEBREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(HOMEBREW_PREFIX)
        link_directories("${HOMEBREW_PREFIX}/lib")
        include_directories("${HOMEBREW_PREFIX}/include")
    endif()
endif()

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
find_package(Qt6 QUIET COMPONENTS SerialPort)
if(Qt6SerialPort_FOUND)
    message(STATUS "Qt6::SerialPort found — serial PTT/CW support enabled")
else()
    message(STATUS "Qt6::SerialPort not found — serial PTT/CW support disabled")
endif()
find_package(Qt6 QUIET COMPONENTS WebSockets)
if(Qt6WebSockets_FOUND)
    message(STATUS "Qt6::WebSockets found — FreeDV Reporter spot source enabled")
else()
    message(STATUS "Qt6::WebSockets not found — FreeDV Reporter spot source disabled")
endif()
find_package(Qt6Keychain QUIET)
if(Qt6Keychain_FOUND)
    message(STATUS "Qt6Keychain found — SmartLink credential persistence enabled")
else()
    message(STATUS "Qt6Keychain not found — SmartLink credential persistence disabled")
endif()

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
    if(MSVC)
        set(RADE_WARN_FLAG "/w")
    else()
        set(RADE_WARN_FLAG "-w")
    endif()
    set_source_files_properties(${RADE_SOURCES} PROPERTIES
        COMPILE_FLAGS "${RADE_WARN_FLAG} -DIS_BUILDING_RADE_API=1 -DRADE_PYTHON_FREE=1"
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

# Opus codec — required for SmartLink compressed audio, independent of RADE.
# When RADE is enabled, Opus comes from RADE's custom build (with FARGAN/OSCE).
# When RADE is disabled, find system libopus.
# Windows: run setup-opus.ps1 first to download prebuilt DLLs
# Linux:   apt install libopus-dev
# macOS:   brew install opus
if(RADE_FOUND)
    set(OPUS_FOUND TRUE)
    message(STATUS "Opus: using RADE's bundled build")
elseif(WIN32)
    set(OPUS_ROOT "${CMAKE_SOURCE_DIR}/third_party/opus")
    if(EXISTS "${OPUS_ROOT}/include/opus/opus.h")
        set(OPUS_FOUND TRUE)
        set(OPUS_INCLUDE_DIRS "${OPUS_ROOT}/include/opus")
        set(OPUS_LIBRARIES "${OPUS_ROOT}/lib/opus.lib")
        message(STATUS "Opus: using prebuilt Windows library (static)")
    else()
        set(OPUS_FOUND FALSE)
        message(WARNING "Opus not found. Run setup-opus.ps1 to download it. "
                        "SmartLink compressed audio will be unavailable.")
    endif()
else()
    if(PkgConfig_FOUND)
        pkg_check_modules(OPUS opus)
    endif()
    if(NOT OPUS_FOUND)
        find_library(OPUS_LIB opus)
        find_path(OPUS_INC opus/opus.h)
        if(OPUS_LIB AND OPUS_INC)
            set(OPUS_FOUND TRUE)
            set(OPUS_LIBRARIES ${OPUS_LIB})
            set(OPUS_INCLUDE_DIRS "${OPUS_INC}/opus")
        endif()
    endif()
    if(OPUS_FOUND)
        message(STATUS "Opus: using system libopus")
    else()
        message(WARNING "Opus not found — SmartLink compressed audio will be unavailable. "
                        "Install libopus-dev (Debian/Ubuntu), opus (Arch/vcpkg), or brew install opus (macOS).")
    endif()
endif()

# GPU-accelerated spectrum/waterfall rendering via QRhi (#391)
# Requires: Qt 6.7+ (QRhiWidget), Qt6::ShaderTools (build-time shader compilation)
# Defaults to ON but auto-disables on Qt < 6.7 (e.g. Ubuntu 24.04 ships Qt 6.4).
option(AETHER_GPU_SPECTRUM "Enable QRhi GPU spectrum rendering" ON)
if(AETHER_GPU_SPECTRUM)
    if(Qt6_VERSION VERSION_LESS "6.7")
        set(AETHER_GPU_SPECTRUM OFF)
        message(STATUS "GPU spectrum rendering disabled (requires Qt 6.7+, found ${Qt6_VERSION})")
    else()
        find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
        # Qt6GuiPrivate provides QRhi headers needed for GPU rendering.
        find_package(Qt6GuiPrivate QUIET)
        message(STATUS "GPU spectrum rendering enabled (QRhi, Qt ${Qt6_VERSION})")
    endif()
else()
    message(STATUS "GPU spectrum rendering disabled (use -DAETHER_GPU_SPECTRUM=ON to enable)")
endif()

# NVIDIA NIM BNR (Background Noise Removal) — GPU-accelerated neural denoising
# Requires: grpc++, protobuf, NVIDIA RTX 4000+ GPU, Docker container
option(ENABLE_BNR "Enable NVIDIA NIM BNR noise removal (requires grpc++)" OFF)
if(ENABLE_BNR)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPC REQUIRED grpc++)
    pkg_check_modules(PROTOBUF REQUIRED protobuf)
    find_program(PROTOC protoc REQUIRED)
    find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

    # Generate C++ stubs from bnr.proto
    set(BNR_PROTO "${CMAKE_SOURCE_DIR}/src/core/proto/bnr.proto")
    set(BNR_GEN_DIR "${CMAKE_BINARY_DIR}/bnr_gen")
    file(MAKE_DIRECTORY ${BNR_GEN_DIR})

    set(BNR_PB_CC "${BNR_GEN_DIR}/bnr.pb.cc")
    set(BNR_PB_H  "${BNR_GEN_DIR}/bnr.pb.h")
    set(BNR_GRPC_CC "${BNR_GEN_DIR}/bnr.grpc.pb.cc")
    set(BNR_GRPC_H  "${BNR_GEN_DIR}/bnr.grpc.pb.h")

    add_custom_command(
        OUTPUT ${BNR_PB_CC} ${BNR_PB_H} ${BNR_GRPC_CC} ${BNR_GRPC_H}
        COMMAND ${PROTOC}
            --proto_path=${CMAKE_SOURCE_DIR}/src/core/proto
            --cpp_out=${BNR_GEN_DIR}
            --grpc_out=${BNR_GEN_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            ${BNR_PROTO}
        DEPENDS ${BNR_PROTO}
        COMMENT "Generating BNR gRPC stubs from bnr.proto"
    )

    set(BNR_SOURCES ${BNR_PB_CC} ${BNR_GRPC_CC})
    message(STATUS "NVIDIA BNR enabled (gRPC ${GRPC_VERSION})")
else()
    set(BNR_SOURCES "")
    message(STATUS "NVIDIA BNR disabled (use -DENABLE_BNR=ON to enable)")
endif()

# Sources
set(CORE_SOURCES
    src/core/AppSettings.cpp
    src/core/RadioDiscovery.cpp
    src/core/RadioConnection.cpp
    src/core/TgxlConnection.cpp
    src/core/CommandParser.cpp
    src/core/AudioEngine.cpp
    src/core/SpectralNR.cpp
    src/core/PanadapterStream.cpp
    src/core/RigctlProtocol.cpp
    src/core/RigctlPty.cpp
    src/core/SmartLinkClient.cpp
    src/core/WanConnection.cpp
    src/core/DxClusterClient.cpp
    src/core/WsjtxClient.cpp
    src/core/PotaClient.cpp
    src/core/RigctlServer.cpp
    src/core/TciServer.cpp
    src/core/TciProtocol.cpp
    src/core/PgxlConnection.cpp
    src/core/FirmwareUploader.cpp
    src/core/DvkWavTransfer.cpp
    src/core/FirmwareStager.cpp
    src/core/RNNoiseFilter.cpp
    src/core/CwDecoder.cpp
    src/core/Resampler.cpp
    src/core/NvidiaBnrFilter.cpp
    src/core/RADEEngine.cpp
    src/core/LogManager.cpp
    src/core/ShortcutManager.cpp
    src/core/SupportBundle.cpp
    src/core/SerialPortController.cpp
    src/core/FlexControlManager.cpp
    src/core/OpusCodec.cpp
    src/core/CtyDatParser.cpp
    src/core/AdifParser.cpp
    src/core/DxccWorkedStatus.cpp
    src/core/DxccColorProvider.cpp
)

if(APPLE)
    list(APPEND CORE_SOURCES src/core/VirtualAudioBridge.cpp src/core/MacMicPermission.mm)
elseif(UNIX)
    # Linux DAX uses PulseAudio pipe modules via pactl (works with PipeWire too)
    list(APPEND CORE_SOURCES src/core/PipeWireAudioBridge.cpp)
    set(HAVE_PIPEWIRE TRUE)
endif()

set(MODEL_SOURCES
    src/models/RadioModel.cpp
    src/models/SliceModel.cpp
    src/models/PanadapterModel.cpp
    src/models/MeterModel.cpp
    src/models/TunerModel.cpp
    src/models/TransmitModel.cpp
    src/models/EqualizerModel.cpp
    src/models/TnfModel.cpp
    src/models/UsbCableModel.cpp
    src/models/DaxIqModel.cpp
    src/models/SpotModel.cpp
    src/models/CwxModel.cpp
    src/models/DvkModel.cpp
    src/models/BandSettings.cpp
    src/models/BandPlanManager.cpp
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
    src/gui/DxClusterDialog.cpp
    src/gui/CwxPanel.cpp
    src/gui/DvkPanel.cpp
    src/gui/AmpApplet.cpp
    src/gui/ProfileManagerDialog.cpp
    src/gui/PanadapterApplet.cpp
    src/gui/PanadapterStack.cpp
    src/gui/PanLayoutDialog.cpp
    src/gui/AppletPanel.cpp
    src/gui/RxApplet.cpp
    src/gui/FilterPassbandWidget.cpp
    src/gui/SMeterWidget.cpp
    src/gui/TunerApplet.cpp
    src/gui/TxApplet.cpp
    src/gui/PhoneCwApplet.cpp
    src/gui/PhoneApplet.cpp
    src/gui/EqApplet.cpp
    src/gui/CatApplet.cpp
    src/gui/MeterSlider.cpp
    src/gui/PhaseKnob.cpp
    src/gui/AntennaGeniusApplet.cpp
    src/gui/TitleBar.cpp
    src/gui/SupportDialog.cpp
    src/gui/KeyboardMapWidget.cpp
    src/gui/ShortcutDialog.cpp
    src/gui/MultiFlexDialog.cpp
    src/gui/WhatsNewDialog.cpp
)

set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${MODEL_SOURCES}
    ${GUI_SOURCES}
    src/generated/WhatsNewData.cpp
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

add_executable(AetherSDR ${ALL_SOURCES} ${RNNOISE_SOURCES} ${GGMORSE_SOURCES} ${RADE_SOURCES} ${BNR_SOURCES} ${RESOURCES})

# Windows: GUI app (no console window) + icon resource
if(WIN32)
    set_target_properties(AetherSDR PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

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
    target_link_libraries(AetherSDR PRIVATE "-framework AVFoundation")
    set(MACOSX_ICON "${CMAKE_SOURCE_DIR}/docs/AetherSDR.icns")
    if(EXISTS "${MACOSX_ICON}")
        target_sources(AetherSDR PRIVATE "${MACOSX_ICON}")
        set_source_files_properties("${MACOSX_ICON}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
    endif()
elseif(WIN32)
    # Windows application icon (taskbar, Start Menu, Alt-Tab)
    set(WIN_RC "${CMAKE_SOURCE_DIR}/docs/AetherSDR.rc")
    if(EXISTS "${WIN_RC}")
        target_sources(AetherSDR PRIVATE "${WIN_RC}")
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
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
)

if(Qt6SerialPort_FOUND)
    target_compile_definitions(AetherSDR PRIVATE HAVE_SERIALPORT)
    target_link_libraries(AetherSDR PRIVATE Qt6::SerialPort)
endif()

if(Qt6WebSockets_FOUND)
    target_compile_definitions(AetherSDR PRIVATE HAVE_WEBSOCKETS)
    target_sources(AetherSDR PRIVATE src/core/FreeDvClient.cpp)
    target_link_libraries(AetherSDR PRIVATE Qt6::WebSockets)
endif()

if(Qt6Keychain_FOUND)
    target_compile_definitions(AetherSDR PRIVATE HAVE_KEYCHAIN)
    target_link_libraries(AetherSDR PRIVATE Qt6Keychain::Qt6Keychain)
endif()

# Bundled RtMidi (MIT license) — MIDI controller support on all platforms
message(STATUS "MIDI controller support enabled (bundled RtMidi)")
target_compile_definitions(AetherSDR PRIVATE HAVE_MIDI)
target_sources(AetherSDR PRIVATE
    third_party/rtmidi/RtMidi.cpp
    src/core/MidiControlManager.cpp
    src/core/MidiSettings.cpp
    src/gui/MidiMappingDialog.cpp)
target_include_directories(AetherSDR PRIVATE third_party/rtmidi)
if(APPLE)
    target_compile_definitions(AetherSDR PRIVATE __MACOSX_CORE__)
    target_link_libraries(AetherSDR PRIVATE "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
elseif(WIN32)
    target_compile_definitions(AetherSDR PRIVATE __WINDOWS_MM__)
    target_link_libraries(AetherSDR PRIVATE winmm)
else()
    target_compile_definitions(AetherSDR PRIVATE __LINUX_ALSA__)
    target_link_libraries(AetherSDR PRIVATE asound)
endif()

# hidapi — USB HID encoder support (Icom RC-28, Griffin PowerMate, Contour Shuttle)
if(PkgConfig_FOUND)
    pkg_check_modules(HIDAPI hidapi-hidraw)
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI hidapi-libusb)
    endif()
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI hidapi)
    endif()
endif()
if(HIDAPI_FOUND)
    message(STATUS "hidapi found — USB HID encoder support enabled")
    target_compile_definitions(AetherSDR PRIVATE HAVE_HIDAPI)
    target_sources(AetherSDR PRIVATE
        src/core/HidEncoderManager.cpp
        src/core/HidDeviceParser.cpp
        src/core/StreamDeckDevice.cpp
        src/core/StreamDeckManager.cpp
        src/core/StreamDeckKeyRenderer.cpp
        src/gui/StreamDeckDialog.cpp)
    set(HIDAPI_NORMALIZED_INCLUDE_DIRS ${HIDAPI_INCLUDE_DIRS})
    foreach(hidapi_dir IN LISTS HIDAPI_INCLUDE_DIRS)
        if(EXISTS "${hidapi_dir}/hidapi.h")
            get_filename_component(hidapi_leaf "${hidapi_dir}" NAME)
            if(hidapi_leaf STREQUAL "hidapi")
                get_filename_component(hidapi_parent "${hidapi_dir}" DIRECTORY)
                list(APPEND HIDAPI_NORMALIZED_INCLUDE_DIRS "${hidapi_parent}")
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES HIDAPI_NORMALIZED_INCLUDE_DIRS)
    target_include_directories(AetherSDR PRIVATE ${HIDAPI_NORMALIZED_INCLUDE_DIRS})
    target_link_libraries(AetherSDR PRIVATE ${HIDAPI_LIBRARIES})
else()
    message(STATUS "hidapi not found — USB HID encoder support disabled")
endif()

if(PORTAUDIO_FOUND)
    target_compile_definitions(AetherSDR PRIVATE HAVE_PORTAUDIO)
    target_include_directories(AetherSDR PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
    target_link_directories(AetherSDR PRIVATE ${PORTAUDIO_LIBRARY_DIRS})
    target_link_libraries(AetherSDR PRIVATE ${PORTAUDIO_LIBRARIES})
endif()

if(FFTW3_FOUND)
    target_compile_definitions(AetherSDR PRIVATE HAVE_FFTW3)
    target_include_directories(AetherSDR PRIVATE ${FFTW3_INCLUDE_DIRS})
    target_link_directories(AetherSDR PRIVATE ${FFTW3_LIBRARY_DIRS})
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
    target_compile_definitions(AetherSDR PRIVATE HAVE_RADE HAVE_OPUS IS_BUILDING_RADE_API=1)
    target_include_directories(AetherSDR PRIVATE ${RADE_DIR}/src)
    if(WIN32)
        target_link_libraries(AetherSDR PRIVATE opus)
    else()
        target_link_libraries(AetherSDR PRIVATE opus m)
    endif()
elseif(OPUS_FOUND)
    target_compile_definitions(AetherSDR PRIVATE HAVE_OPUS)
    target_include_directories(AetherSDR PRIVATE ${OPUS_INCLUDE_DIRS})
    target_link_libraries(AetherSDR PRIVATE ${OPUS_LIBRARIES})
endif()

if(ENABLE_BNR)
    target_compile_definitions(AetherSDR PRIVATE HAVE_BNR)
    target_include_directories(AetherSDR PRIVATE
        ${BNR_GEN_DIR}
        ${GRPC_INCLUDE_DIRS}
        ${PROTOBUF_INCLUDE_DIRS})
    target_link_libraries(AetherSDR PRIVATE ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})
endif()

if(AETHER_GPU_SPECTRUM)
    target_compile_definitions(AetherSDR PRIVATE AETHER_GPU_SPECTRUM)
    if(TARGET Qt6::GuiPrivate)
        target_link_libraries(AetherSDR PRIVATE Qt6::GuiPrivate)
    else()
        # Qt6::GuiPrivate not available (macOS/skipped) — find rhi/ headers manually
        get_target_property(_qt_gui_inc Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
        foreach(_dir IN LISTS _qt_gui_inc)
            if(EXISTS "${_dir}/rhi/qrhi.h")
                # Already in the include path via Qt6::Gui
                break()
            endif()
            # Check versioned private header directory (e.g. QtGui/6.7.3/QtGui/rhi/)
            file(GLOB _priv_dirs "${_dir}/${Qt6_VERSION}/QtGui")
            foreach(_pd IN LISTS _priv_dirs)
                if(EXISTS "${_pd}/rhi/qrhi.h")
                    target_include_directories(AetherSDR PRIVATE "${_pd}/..")
                    message(STATUS "QRhi headers found at ${_pd}/rhi/")
                    break()
                endif()
            endforeach()
        endforeach()
    endif()
    qt_add_shaders(AetherSDR "aether_shaders"
        PREFIX "/shaders"
        FILES
            resources/shaders/texturedquad.vert
            resources/shaders/texturedquad.frag
            resources/shaders/overlay.vert
            resources/shaders/overlay.frag
            resources/shaders/spectrum.vert
            resources/shaders/spectrum.frag
    )
endif()

if(HAVE_PIPEWIRE)
    target_compile_definitions(AetherSDR PRIVATE HAVE_PIPEWIRE)
    message(STATUS "Linux DAX bridge enabled (PulseAudio pipe modules)")
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
