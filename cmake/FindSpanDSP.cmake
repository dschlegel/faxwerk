# FindSpanDSP
# -----------
# Locates the spandsp library (LGPL, linked dynamically).
#
# Uses pkg-config when available and falls back to find_path/find_library,
# so it also works on Windows/macOS builds without pkg-config. Honors
# CMAKE_PREFIX_PATH and SpanDSP_ROOT.
#
# Result variables:
#   SpanDSP_FOUND, SpanDSP_INCLUDE_DIR, SpanDSP_LIBRARY, SpanDSP_VERSION
# Imported target:
#   SpanDSP::SpanDSP

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_SPANDSP QUIET spandsp)
endif()

find_path(SpanDSP_INCLUDE_DIR
    NAMES spandsp.h
    HINTS ${PC_SPANDSP_INCLUDEDIR} ${PC_SPANDSP_INCLUDE_DIRS}
    PATH_SUFFIXES include)

find_library(SpanDSP_LIBRARY
    NAMES spandsp libspandsp
    HINTS ${PC_SPANDSP_LIBDIR} ${PC_SPANDSP_LIBRARY_DIRS}
    PATH_SUFFIXES lib)

if(PC_SPANDSP_VERSION)
    set(SpanDSP_VERSION "${PC_SPANDSP_VERSION}")
elseif(SpanDSP_INCLUDE_DIR AND EXISTS "${SpanDSP_INCLUDE_DIR}/spandsp/version.h")
    file(STRINGS "${SpanDSP_INCLUDE_DIR}/spandsp/version.h" _spandsp_date
         REGEX "#define[ \t]+SPANDSP_RELEASE_DATE[ \t]+[0-9]+")
    string(REGEX REPLACE ".*SPANDSP_RELEASE_DATE[ \t]+([0-9]+).*" "\\1" SpanDSP_VERSION "${_spandsp_date}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SpanDSP
    REQUIRED_VARS SpanDSP_LIBRARY SpanDSP_INCLUDE_DIR
    VERSION_VAR SpanDSP_VERSION)

if(SpanDSP_FOUND AND NOT TARGET SpanDSP::SpanDSP)
    add_library(SpanDSP::SpanDSP UNKNOWN IMPORTED)
    set_target_properties(SpanDSP::SpanDSP PROPERTIES
        IMPORTED_LOCATION "${SpanDSP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SpanDSP_INCLUDE_DIR}")
    # <spandsp.h> includes <tiffio.h>.
    if(TARGET TIFF::TIFF)
        set_property(TARGET SpanDSP::SpanDSP APPEND PROPERTY INTERFACE_LINK_LIBRARIES TIFF::TIFF)
    endif()
endif()

mark_as_advanced(SpanDSP_INCLUDE_DIR SpanDSP_LIBRARY)
