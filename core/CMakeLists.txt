cmake_minimum_required(VERSION 3.14)
project(PollingCorePlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

logos_module(
    NAME polling_core
    SOURCES
        src/polling_core_interface.h
        src/polling_core_plugin.h
        src/polling_core_plugin.cpp
    EXTERNAL_LIBS
        logosdelivery
)
