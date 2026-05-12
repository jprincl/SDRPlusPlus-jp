#
# Run with -DSRC=<source-dir> -P this-script.
#
# libiio finds libxml2's Config.cmake but drops LIBXML2_DEFINITIONS. In a
# static libxml2 build that loses LIBXML_STATIC, so libiio objects reference
# __imp_xml* symbols. Propagate the definitions supplied by libxml2.
#
# MSVC Debug (/Od) does not reliably discard disabled-backend calls guarded by
# ordinary `if (WITH_*_BACKEND)` constants. libiio intentionally omits source
# files for disabled backends, so those unevaluated calls can still leave
# unresolved symbols in the static archive. Add a small source file with
# preprocessor-guarded stubs for disabled backends.
#
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_helpers.cmake)

set(_cmake "${SRC}/CMakeLists.txt")
file(READ "${_cmake}" _content)
patch_replace_or_fail(_content
    "include_directories(\${LIBXML2_INCLUDE_DIR})\n\tlist(APPEND LIBS_TO_LINK \${LIBXML2_LIBRARIES})"
    "include_directories(\${LIBXML2_INCLUDE_DIR})\n\tadd_definitions(\${LIBXML2_DEFINITIONS})\n\tlist(APPEND LIBS_TO_LINK \${LIBXML2_LIBRARIES})")
patch_replace_or_fail(_content
    "set(LIBIIO_CFILES backend.c channel.c device.c context.c buffer.c utilities.c scan.c sort.c)"
    "set(LIBIIO_CFILES backend.c channel.c device.c context.c buffer.c utilities.c scan.c sort.c sdrpp_disabled_backends.c)")
file(WRITE "${_cmake}" "${_content}")

file(WRITE "${SRC}/sdrpp_disabled_backends.c" [=[
#include "iio-config.h"
#include "iio-private.h"
#include "dns_sd.h"

#include <errno.h>

#if !WITH_LOCAL_BACKEND
struct iio_context * local_create_context(void)
{
    errno = ENOSYS;
    return NULL;
}

int local_context_scan(struct iio_scan_result *scan_result)
{
    (void) scan_result;
    return -ENODEV;
}
#endif

#if !WITH_SERIAL_BACKEND
struct iio_context * serial_create_context_from_uri(const char *uri)
{
    (void) uri;
    errno = ENOSYS;
    return NULL;
}
#endif

#if !HAVE_DNS_SD
int dnssd_context_scan(struct iio_scan_result *scan_result)
{
    (void) scan_result;
    return -ENODEV;
}

int dnssd_discover_host(char *addr_str, size_t addr_len, uint16_t *port)
{
    (void) addr_str;
    (void) addr_len;
    (void) port;
    return -ENOENT;
}

int dnssd_resolve_host(const char *hostname, char *ip_addr, const int addr_len)
{
    (void) hostname;
    (void) ip_addr;
    (void) addr_len;
    return -ENOENT;
}
#endif
]=])
message(STATUS "Patched ${_cmake}")
