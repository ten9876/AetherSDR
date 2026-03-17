cmake_minimum_required(VERSION 3.20)
project(AetherSDR VERSION 0.3.90 LANGUAGES CXX)

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

# Sources
set(CORE_SOURCES
    src/core/AppSettings.cpp
    src/core/RadioDiscovery.cpp
    src/core/RadioConnection.cpp
    src/core/CommandParser.cpp
    src/core/AudioEngine.cpp
    src/core/PanadapterStream.cpp
    src/core/RigctlProtocol.cpp
    src/core/RigctlPty.cpp
    src/core/SmartLinkClient.cpp
    src/core/WanConnection.cpp
    src/core/RigctlServer.cpp
)

set(MODEL_SOURCES
    src/models/RadioModel.cpp
    src/models/SliceModel.cpp
    src/models/MeterModel.cpp
    src/models/TunerModel.cpp
    src/models/TransmitModel.cpp
    src/models/EqualizerModel.cpp
    src/models/BandSettings.cpp
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
)

set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${MODEL_SOURCES}
    ${GUI_SOURCES}
)

qt_add_resources(RESOURCES resources.qrc)

add_executable(AetherSDR ${ALL_SOURCES} ${RESOURCES})

# macOS app bundle
if(APPLE)
    set_target_properties(AetherSDR PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.aethersdr.AetherSDR"
        MACOSX_BUNDLE_BUNDLE_NAME "AetherSDR"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "AetherSDR.icns"
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
