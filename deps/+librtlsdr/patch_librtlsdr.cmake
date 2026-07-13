#
# Run with -DSRC=<source-dir> -P this-script.
#
# Streaming-teardown crash fix, kept as a proper git patch because it is
# intended for an upstream pull request (github.com/AlexandreRouma/rtl-sdr,
# and applicable to osmocom rtl-sdr as well).
#
# rtlsdr_read_async()'s cancellation loop can exit with transfers still owned
# by the kernel (libusb_handle_events() error, or the dev_lost unplug path
# which only does one zero-timeout event pass) and then frees them, leaving
# freed nodes on libusb's flying-transfers list and crashing inside
# libusb_close(). See the patch header for the full analysis.
#
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_helpers.cmake)

patch_apply_git_or_fail("${SRC}" "${CMAKE_CURRENT_LIST_DIR}/0001-fix-use-after-free-in-async-teardown.patch")
