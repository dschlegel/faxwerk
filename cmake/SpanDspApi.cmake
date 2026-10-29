# Detects which spandsp API generation is available.
#
# spandsp 0.0.6 (Ubuntu/Debian):
#   T.30 phase handlers: int handler(t30_state_t *s, void *user_data, int result)
#   span_log_set_message_handler(logging_state_t *s, message_handler_func_t func)
# Upstream spandsp (freeswitch/spandsp, 0.0.7 / 3.x, used on Windows/macOS):
#   T.30 phase handlers: int handler(void *user_data, int result)
#   span_log_set_message_handler(logging_state_t *s, message_handler_func_t func, void *user_data)
#
# Sets (cached) FAXWERK_SPANDSP_NEW_API and FAXWERK_SPANDSP_LOG_USER_DATA.

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

# Re-run the probes when a different spandsp gets picked up.
if(NOT "${_FAXWERK_SPANDSP_PROBED_FOR}" STREQUAL "${SpanDSP_INCLUDE_DIR}")
    unset(FAXWERK_SPANDSP_NEW_API CACHE)
    unset(FAXWERK_SPANDSP_LOG_USER_DATA CACHE)
endif()
set(_FAXWERK_SPANDSP_PROBED_FOR "${SpanDSP_INCLUDE_DIR}" CACHE INTERNAL "")

cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_INCLUDES "${SpanDSP_INCLUDE_DIR}" ${TIFF_INCLUDE_DIRS})
set(CMAKE_REQUIRED_QUIET ON)
# Compile only: the probes are about signatures, linking is irrelevant.
set(_faxwerk_saved_try_compile_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

check_cxx_source_compiles("
#include <spandsp.h>
static int phase_b(void *, int) { return 0; }
static void phase_e(void *, int) {}
void probe(t30_state_t *s)
{
    t30_set_phase_b_handler(s, phase_b, nullptr);
    t30_set_phase_e_handler(s, phase_e, nullptr);
}
" FAXWERK_SPANDSP_NEW_API)

check_cxx_source_compiles("
#include <spandsp.h>
static void log_handler(void *, int, const char *) {}
void probe(logging_state_t *s)
{
    span_log_set_message_handler(s, log_handler, nullptr);
}
" FAXWERK_SPANDSP_LOG_USER_DATA)

set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_faxwerk_saved_try_compile_type}")
cmake_pop_check_state()

if(FAXWERK_SPANDSP_NEW_API)
    message(STATUS "spandsp API: new (upstream 0.0.7/3.x phase handlers)")
else()
    message(STATUS "spandsp API: old (0.0.6 phase handlers)")
endif()
if(FAXWERK_SPANDSP_LOG_USER_DATA)
    message(STATUS "spandsp logging: message handler with user_data")
else()
    message(STATUS "spandsp logging: message handler without user_data")
endif()
