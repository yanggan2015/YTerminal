#ifndef YT_UI_TERM_H
#define YT_UI_TERM_H

#include "session.h"

HWND term_view_create(HWND parent, Session *s, HINSTANCE inst);
void term_view_destroy(HWND h);
Session *term_view_session(HWND h);
Backend *term_view_backend(HWND h);
int term_view_cols(HWND h);
int term_view_rows(HWND h);
const wchar_t *term_view_title(HWND h);
void term_view_set_title(HWND h, const wchar_t *title);
const wchar_t *term_view_status_text(HWND h);
int term_view_state(HWND h);
void term_view_focus(HWND h);
void term_view_send_text(HWND h, const char *text, size_t len);
void term_view_broadcast_text(HWND h, const char *text, size_t len);
void term_view_toggle_log(HWND h);
int term_view_is_logging(HWND h);
void term_view_search(HWND h);
void term_view_clear(HWND h);
void term_view_select_all(HWND h);
void term_view_copy(HWND h);
int term_view_has_selection(HWND h);
void term_view_paste_clipboard(HWND h);
void term_view_reconnect(HWND h);

#endif
