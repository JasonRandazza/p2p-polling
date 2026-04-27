cmake_minimum_required(VERSION 3.14)
project(PollingUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

logos_module(
    NAME polling_ui
    REP_FILE src/polling_ui.rep
    SOURCES
        src/polling_ui_interface.h
        src/polling_ui_plugin.h
        src/polling_ui_plugin.cpp
)
