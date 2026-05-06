#ifndef TCD_TERM_H
#define TCD_TERM_H

#include <stddef.h>

void term_init(void);
void term_shutdown(void);
void term_get_size(int *cols, int *rows);

void term_write(const char *s);
void term_write_n(const char *s, size_t n);
void term_writef(const char *fmt, ...);
void term_flush(void);

void term_clear(void);
void term_move(int row, int col);
void term_clear_line(void);
void term_hide_cursor(void);
void term_show_cursor(void);
void term_alt_screen_on(void);
void term_alt_screen_off(void);

void term_reset_attrs(void);
void term_set_fg(const char *color);
void term_set_bg(const char *color);
void term_set_bold(int on);
void term_set_inverse(int on);

#endif
