#ifndef MOWKOW64_GUI_INTERNAL64_H
#define MOWKOW64_GUI_INTERNAL64_H

struct CONSOLE64;

/* Single-core transactions: caller keeps IRQs saved across prepare, task
 * startup and publish/abort. All helpers and allocator/task operations used
 * here are non-sleeping. Lock order is GUI/lifecycle guard -> sheet/FIFO/task/
 * memory irq-save guards. A queue wake happens outside the FIFO's own guard.
 */
int gui_prepare_console64(struct CONSOLE64 *con, const char *title);
void gui_publish_console64(struct CONSOLE64 *con);
int gui_close_console_checked64(struct CONSOLE64 *con);
int gui_toggle_window_checked64(void);
int gui_has_console64(const struct CONSOLE64 *con);
/* Post-commit buffer retirement is bounded and retains failed frees for retry. */
int gui_reclaim64(void);
/* Task-context main-loop refresh; generation polling does not redraw unchanged state. */
void gui_update_status64(void);

#endif
