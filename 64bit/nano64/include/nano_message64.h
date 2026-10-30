#ifndef MOWKOW64_NANO_MESSAGE64_H
#define MOWKOW64_NANO_MESSAGE64_H

/* A scope starts before formatting arguments are evaluated. Normal UI is
 * Korean; each error snapshots its owning console's mode. No locale or
 * allocated translation state is shared with another process or console.
 */
void nano_message_begin64(int error);
void nano_message_end64(void);
const char *nano_message64(const char *source);

#endif
