/* Included in the writable data segment of the recording NRO. */
. = ALIGN(8);
PROVIDE_HIDDEN(__mini_gcov_begin = .);
KEEP(*(.gcov_info))
PROVIDE_HIDDEN(__mini_gcov_end = .);
