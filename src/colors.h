#ifndef COLORS_H
#define COLORS_H

#include <ncurses.h>

enum color {
	COLOR_TITLE = 1,
	COLOR_TITLE_BOLD,
	COLOR_LINE,
	COLOR_LINE_BOLD,
	COLOR_LIST,
	COLOR_LIST_BOLD,
	COLOR_PROGRESSBAR,
	COLOR_STATUS,
	COLOR_STATUS_BOLD,
	COLOR_STATUS_TIME,
	COLOR_STATUS_ALERT,
};

short colors_str2color(const char *str);

int colors_assign(const char *name, const char *value);
int colors_define(const char *name, short r, short g, short b);
int colors_start(void);
int colors_use(WINDOW *w, enum color id);

#endif /* COLORS_H */
