cmake_minimum_required(VERSION 3.20)
project(AetherSDR VERSION 0.1.8 LANGUAGES CXX)

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
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO portaudio-2.0)

# Sources
set(CORE_SOURCES
    src/core/RadioDiscovery.cpp
    src/core/RadioConnection.cpp
    src/core/CommandParser.cpp
    src/core/AudioEngine.cpp
    src/core/PanadapterStream.cpp
)

set(MODEL_SOURCES
    src/models/RadioModel.cpp
    src/models/SliceModel.cpp
    src/models/MeterModel.cpp
    src/models/TunerModel.cpp
    src/models/TransmitModel.cpp
    src/models/EqualizerModel.cpp
)

set(GUI_SOURCES
    src/gui/MainWindow.cpp
    src/gui/FrequencyDial.cpp
    src/gui/ConnectionPanel.cpp
    src/gui/SpectrumWidget.cpp
    src/gui/AppletPanel.cpp
    src/gui/RxApplet.cpp
    src/gui/SMeterWidget.cpp
    src/gui/TunerApplet.cpp
    src/gui/TxApplet.cpp
    src/gui/PhoneCwApplet.cpp
    src/gui/PhoneApplet.cpp
    src/gui/EqApplet.cpp
)

set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${MODEL_SOURCES}
    ${GUI_SOURCES}
)

add_executable(AetherSDR ${ALL_SOURCES})

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
    target_link_libraries(AetherSDR PRIVATE ${PORTAUDIO_LIBRARIES})
endif()

# Compiler warnings
target_compile_options(AetherSDR PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g3 -fsanitize=address>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)
target_link_options(AetherSDR PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
)

# Install
install(TARGETS AetherSDR DESTINATION bin)
