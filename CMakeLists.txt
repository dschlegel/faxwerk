cmake_minimum_required(VERSION 3.21)

project(Faxwerk
    VERSION 0.1.0
    DESCRIPTION "Send PDF documents as fax through a SIP-capable home router"
    LANGUAGES C CXX)

set(FAXWERK_PRODUCT_NAME "Faxwerk" CACHE STRING
    "Product name shown to users (must not contain third-party trademarks)")
string(TOUPPER "${FAXWERK_PRODUCT_NAME}" _faxwerk_name_upper)
if(_faxwerk_name_upper MATCHES "FRITZ")
    message(FATAL_ERROR "FAXWERK_PRODUCT_NAME must not contain the trademark 'FRITZ'.")
endif()

option(FAXWERK_BUILD_TESTS "Build the unit and integration tests" ON)
option(FAXWERK_WERROR "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network Gui Pdf)
find_package(TIFF REQUIRED)
find_package(SpanDSP REQUIRED)
include(SpanDspApi)

function(faxwerk_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(FAXWERK_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(FAXWERK_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_subdirectory(src/core)


message(STATUS "Product name:   ${FAXWERK_PRODUCT_NAME}")
message(STATUS "spandsp:        ${SpanDSP_LIBRARY} (version ${SpanDSP_VERSION})")
