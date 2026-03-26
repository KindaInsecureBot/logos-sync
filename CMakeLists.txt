cmake_minimum_required(VERSION 3.21)
project(logos-pipe VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ───────────────────────────────────────────────────────────────────
option(BUILD_MODULE "Build headless sync_module plugin" OFF)
option(BUILD_TESTS  "Build QTest unit tests (no SDK needed)" OFF)

# Paths injected by Makefile or Nix (required for BUILD_MODULE)
set(LOGOS_CPP_SDK_ROOT  "" CACHE PATH "Path to logos-cpp-sdk merged prefix")
set(LOGOS_LIBLOGOS_ROOT "" CACHE PATH "Path to logos-liblogos merged prefix")

# ── Shared setup ──────────────────────────────────────────────────────────────
if(BUILD_MODULE)
    set(CMAKE_AUTOMOC ON)

    set(LOGOS_INCLUDE_DIRS
        "${LOGOS_CPP_SDK_ROOT}/include"
        "${LOGOS_CPP_SDK_ROOT}/include/cpp"
        "${LOGOS_LIBLOGOS_ROOT}/include"
    )

    # logos_sdk static lib (from logos-cpp-sdk)
    find_library(LOGOS_SDK_LIB logos_sdk
        HINTS "${LOGOS_CPP_SDK_ROOT}/lib"
        REQUIRED
    )
    # logos_core shared lib (from logos-liblogos)
    find_library(LOGOS_CORE_LIB logos_core
        HINTS "${LOGOS_LIBLOGOS_ROOT}/lib"
        REQUIRED
    )
endif()

# ── Target: sync_module (headless) ────────────────────────────────────────────
if(BUILD_MODULE)
    # Module-only: Core + Qml + RemoteObjects only — no Quick/Widgets
    find_package(Qt6 REQUIRED COMPONENTS Core Qml RemoteObjects)

    qt_add_plugin(sync_module_plugin
        CLASS_NAME SyncModule
        PLUGIN_TYPE generic
    )

    configure_file(src/metadata.json ${CMAKE_CURRENT_BINARY_DIR}/metadata.json COPYONLY)

    target_sources(sync_module_plugin PRIVATE
        src/content_store.cpp
        src/channel_sync.cpp
        src/channel_indexer.cpp
        src/peer_sync.cpp
        src/sync_module.cpp
    )

    target_include_directories(sync_module_plugin PRIVATE
        src
        ${LOGOS_INCLUDE_DIRS}
    )

    # logos_sdk BEFORE Qt6::RemoteObjects (linker ordering)
    target_link_libraries(sync_module_plugin PRIVATE
        Qt6::Core
        Qt6::Qml
        Qt6::RemoteObjects
        ${LOGOS_SDK_LIB}
        Qt6::RemoteObjects   # listed twice: SDK pulls RemoteObjects symbols
        ${LOGOS_CORE_LIB}
    )

    target_compile_definitions(sync_module_plugin PRIVATE
        SYNC_MODULE_BUILD
    )

    set_target_properties(sync_module_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "sync_module_plugin"
    )

    install(TARGETS sync_module_plugin
        LIBRARY DESTINATION modules/sync
    )
    install(FILES modules/sync/manifest.json
        DESTINATION modules/sync
    )
endif()

# ── Target: unit tests (BUILD_TESTS=ON, no Logos SDK required) ────────────────
if(BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Core Test)
    set(CMAKE_AUTOMOC ON)
    enable_testing()

    # tests/ first → logos_api_client.h stub shadows the real SDK header
    # src/  → application headers
    set(TEST_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    macro(add_sync_test target sources)
        add_executable(${target} ${sources})
        target_include_directories(${target} PRIVATE ${TEST_INCLUDE_DIRS})
        target_compile_definitions(${target} PRIVATE BUILD_TESTS)
        target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Test)
        add_test(NAME ${target} COMMAND ${target})
    endmacro()

    add_sync_test(test_sync_types
        "tests/test_sync_types.cpp"
    )

    add_sync_test(test_content_store
        "tests/test_content_store.cpp;src/content_store.cpp"
    )

    add_sync_test(test_channel_sync
        "tests/test_channel_sync.cpp;src/channel_sync.cpp"
    )

    add_sync_test(test_peer_sync
        "tests/test_peer_sync.cpp;src/peer_sync.cpp"
    )

    add_sync_test(test_channel_indexer
        "tests/test_channel_indexer.cpp;src/channel_indexer.cpp"
    )
endif()
