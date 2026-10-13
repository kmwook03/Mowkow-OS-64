/*
 * Parse first-party headers directly under MPY_PORT_CFLAGS in both builds.
 * A header reached through a vendor system header inherits suppressed warnings.
 * Include our config/libc shims first so their guards cannot hide diagnostics.
 * This translation unit defines no runtime symbols.
 */
#include "mpconfigport.h"
#include "libc/assert.h"
#include "libc/math.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/unistd.h"

/* HAL/private bridges use core types; config/libc have already been checked. */
#include "py/mpconfig.h"
#include "mphalport.h"
#include "mpport_file_internal64.h"
