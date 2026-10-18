cmake_minimum_required(VERSION 3.16)
project(dash_carplay_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets WebSockets Test Bluetooth DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
get_filename_component(DASH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(carplay-native-test
    client_test.cpp
    ${DASH_ROOT}/src/app/phone_controls.cpp
    ${DASH_ROOT}/src/app/receiver_settings.cpp
    ${DASH_ROOT}/include/app/receiver_settings.hpp
    ${DASH_ROOT}/src/carplay/client.cpp
    ${DASH_ROOT}/src/carplay/audio_output.cpp
    ${DASH_ROOT}/src/carplay/video_frame.cpp
    ${DASH_ROOT}/src/carplay/controls.cpp
    ${DASH_ROOT}/src/carplay/surface.cpp
    ${DASH_ROOT}/src/carplay/widget.cpp
    ${DASH_ROOT}/src/carplay/touch_ui.cpp
    ${DASH_ROOT}/src/carplay/phones.cpp
    ${DASH_ROOT}/src/carplay/phone_wizard.cpp
    ${DASH_ROOT}/src/carplay/pairing_agent.cpp
    ${DASH_ROOT}/include/carplay/client.hpp
    ${DASH_ROOT}/include/carplay/surface.hpp
    ${DASH_ROOT}/include/carplay/widget.hpp
    ${DASH_ROOT}/include/carplay/touch_ui.hpp
    ${DASH_ROOT}/include/carplay/phones.hpp
    ${DASH_ROOT}/include/carplay/pairing_agent.hpp
    ${DASH_ROOT}/include/carplay/phone_wizard.hpp)
target_compile_definitions(carplay-native-test PRIVATE DASH_CARPLAY
    DASH_CARPLAY_RUNNER="${DASH_ROOT}/carplay/run.sh"
    DASH_CARPLAY_IDENTITY_DIR="${DASH_ROOT}/carplay/runtime/identity")
target_include_directories(carplay-native-test PRIVATE ${DASH_ROOT}/include)
target_link_libraries(carplay-native-test PRIVATE Qt5::Widgets Qt5::WebSockets Qt5::Bluetooth Qt5::DBus Qt5::Test PkgConfig::GST)
add_test(NAME carplay-native COMMAND carplay-native-test)
add_executable(carplay-media-benchmark media_benchmark.cpp
    ${DASH_ROOT}/src/carplay/client.cpp ${DASH_ROOT}/src/carplay/controls.cpp
    ${DASH_ROOT}/src/carplay/audio_output.cpp
    ${DASH_ROOT}/src/carplay/video_frame.cpp
    ${DASH_ROOT}/src/carplay/surface.cpp ${DASH_ROOT}/src/app/phone_controls.cpp
    ${DASH_ROOT}/include/carplay/client.hpp ${DASH_ROOT}/include/carplay/surface.hpp)
target_compile_definitions(carplay-media-benchmark PRIVATE DASH_CARPLAY)
target_include_directories(carplay-media-benchmark PRIVATE ${DASH_ROOT}/include)
target_link_libraries(carplay-media-benchmark PRIVATE Qt5::Widgets Qt5::WebSockets PkgConfig::GST)
set(CARPLAY_TEST_ENV "QT_QPA_PLATFORM=offscreen")
if(APPLE)
    # Homebrew bundles many unrelated hardware/Python plugins. Test only the real
    # decoder/fixture dependencies, avoiding device probes during registry scanning.
    pkg_get_variable(GST_PLUGIN_DIR gstreamer-1.0 pluginsdir)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")
    foreach(plugin coreelements app playback typefindfunctions videotestsrc videoconvertscale videoparsersbad x264 libav)
        set(plugin_file "${GST_PLUGIN_DIR}/libgst${plugin}.dylib")
        if(NOT EXISTS "${plugin_file}")
            message(FATAL_ERROR "Missing native test plugin: ${plugin_file}")
        endif()
        file(CREATE_LINK "${plugin_file}" "${CMAKE_CURRENT_BINARY_DIR}/plugins/libgst${plugin}.dylib" SYMBOLIC)
    endforeach()
    if(EXISTS "${GST_PLUGIN_DIR}/libgstosxaudio.dylib")
        file(CREATE_LINK "${GST_PLUGIN_DIR}/libgstosxaudio.dylib" "${CMAKE_CURRENT_BINARY_DIR}/plugins/libgstosxaudio.dylib" SYMBOLIC)
    endif()
    list(APPEND CARPLAY_TEST_ENV
        "GST_PLUGIN_SYSTEM_PATH_1_0=${CMAKE_CURRENT_BINARY_DIR}/plugins"
        "GST_REGISTRY_1_0=${CMAKE_CURRENT_BINARY_DIR}/registry.bin")
endif()
set_tests_properties(carplay-native PROPERTIES ENVIRONMENT "${CARPLAY_TEST_ENV}" TIMEOUT 45)

option(CARPLAY_TEST_DASH_SHUTDOWN "Test Dash configuration lifetime with installed OpenAuto" OFF)
if(CARPLAY_TEST_DASH_SHUTDOWN)
    list(APPEND CMAKE_MODULE_PATH "${DASH_ROOT}/cmake_modules")
    find_package(openauto REQUIRED)
    find_package(Qt5 REQUIRED COMPONENTS Multimedia SerialBus)
    add_executable(dash-config-lifetime-test config_lifetime_test.cpp
        ${DASH_ROOT}/src/app/config.cpp ${DASH_ROOT}/include/app/config.hpp)
    target_include_directories(dash-config-lifetime-test PRIVATE ${DASH_ROOT}/include)
    target_link_libraries(dash-config-lifetime-test PRIVATE Qt5::Widgets Qt5::Multimedia Qt5::SerialBus openauto)
    add_test(NAME dash-config-lifetime COMMAND dash-config-lifetime-test)
    set_tests_properties(dash-config-lifetime PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
endif()
