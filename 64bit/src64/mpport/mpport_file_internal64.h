#ifndef MOWKOW64_MPPORT_FILE_INTERNAL64_H
#define MOWKOW64_MPPORT_FILE_INTERNAL64_H

#include "py/lexer.h"
#include "py/runtime.h"

/* Initialized VM and active NLR required. Missing files return NULL for the
 * console command; returned lexer owns its reader/source until mp_parse frees it.
 */
mp_lexer_t *mpport_file_lexer64(const char *path);
/* Copy file bytes into a Python string; release temporary bytes on success/NLR.
 * Missing files raise ENOENT; read size mismatches raise EIO.
 */
mp_obj_t mpport_file_text64(const char *path);

#endif
