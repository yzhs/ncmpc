/* ncmpc (Ncurses MPD Client)
 * (c) 2004-2009 The Music Player Daemon Project
 * Project homepage: http://musicpd.org

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "screen.h"
#include "screen_list.h"
#include "screen_utils.h"
#include "config.h"
#include "i18n.h"
#include "charset.h"
#include "mpdclient.h"
#include "utils.h"
#include "options.h"
#include "colors.h"
#include "strfsong.h"
#include "player_command.h"

#ifndef NCMPC_MINI
#include "hscroll.h"
#endif

#include <mpd/client.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <locale.h>

#ifndef NCMPC_MINI
/** welcome message time [s] */
static const GTime SCREEN_WELCOME_TIME = 10;
#endif

/* minimum window size */
static const int SCREEN_MIN_COLS = 14;
static const int SCREEN_MIN_ROWS = 5;

/* screens */

#ifndef NCMPC_MINI
static gboolean welcome = TRUE;
#endif

struct screen screen;
static const struct screen_functions *mode_fn = &screen_playlist;
static const struct screen_functions *mode_fn_prev = &screen_playlist;

gboolean
screen_is_visible(const struct screen_functions *sf)
{
	return sf == mode_fn;
}

void
screen_switch(const struct screen_functions *sf, struct mpdclient *c)
{
	assert(sf != NULL);

	if (sf == mode_fn)
		return;

	mode_fn_prev = mode_fn;

	/* close the old mode */
	if (mode_fn->close != NULL)
		mode_fn->close();

	/* get functions for the new mode */
	mode_fn = sf;

	/* open the new mode */
	if (mode_fn->open != NULL)
		mode_fn->open(c);

	screen_paint(c);
}

void
screen_swap(struct mpdclient *c, const struct mpd_song *song)
{
	if (song != NULL)
	{
		if (false)
			{ /* just a hack to make the ifdefs less ugly */ }
#ifdef ENABLE_SONG_SCREEN
		if (mode_fn_prev == &screen_song)
			screen_song_switch(c, song);
#endif
#ifdef ENABLE_LYRICS_SCREEN
		else if (mode_fn_prev == &screen_lyrics)
			screen_lyrics_switch(c, song, true);
#endif
		else
			screen_switch(mode_fn_prev, c);
	}
	else
		screen_switch(mode_fn_prev, c);
}

static int
find_configured_screen(const char *name)
{
	unsigned i;

	for (i = 0; options.screen_list[i] != NULL; ++i)
		if (strcmp(options.screen_list[i], name) == 0)
			return i;

	return -1;
}

static void
screen_next_mode(struct mpdclient *c, int offset)
{
	int max = g_strv_length(options.screen_list);
	int current, next;
	const struct screen_functions *sf;

	/* find current screen */
	current = find_configured_screen(screen_get_name(mode_fn));
	next = current + offset;
	if (next<0)
		next = max-1;
	else if (next>=max)
		next = 0;

	sf = screen_lookup_name(options.screen_list[next]);
	if (sf != NULL)
		screen_switch(sf, c);
}

static inline int
volume_length(int volume)
{
	if (volume == 100)
		return 3;
	if (volume >= 10 && volume < 100)
		return 2;
	if (volume >= 0 && volume < 10)
		return 1;
	return -1;
}

static void
paint_top_window(const char *header, const struct mpdclient *c)
{
	title_bar_paint(&screen.title_bar, header, c->status);
}

static void
paint_progress_window(struct mpdclient *c)
{
	unsigned elapsed, duration;

	if (c->song != NULL && seek_id == (int)mpd_song_get_id(c->song))
		elapsed = seek_target_time;
	else if (c->status != NULL)
		elapsed = mpd_status_get_elapsed_time(c->status);
	else
		elapsed = 0;

	duration = c->status != NULL &&
		!IS_STOPPED(mpd_status_get_state(c->status))
		? mpd_status_get_total_time(c->status)
		: 0;

	if (progress_bar_set(&screen.progress_bar, elapsed, duration))
		progress_bar_paint(&screen.progress_bar);
}

static void
paint_status_window(struct mpdclient *c)
{
	WINDOW *w = screen.status_window.w;
	const struct mpd_status *status = c->status;
	enum mpd_state state;
	const struct mpd_song *song = c->song;
	int elapsedTime = 0;
#ifdef NCMPC_MINI
	static char bitrate[1];
#else
	char bitrate[16];
#endif
	const char *str = NULL;
	int x = 0;

	if( time(NULL) - screen.status_timestamp <= options.status_message_time )
		return;

	wmove(w, 0, 0);
	wclrtoeol(w);
	colors_use(w, COLOR_STATUS_BOLD);

	state = status == NULL ? MPD_STATE_UNKNOWN
		: mpd_status_get_state(status);

	switch (state) {
	case MPD_STATE_PLAY:
		str = _("Playing:");
		break;
	case MPD_STATE_PAUSE:
		str = _("[Paused]");
		break;
	case MPD_STATE_STOP:
	default:
		break;
	}

	if (str) {
		waddstr(w, str);
		x += utf8_width(str) + 1;
	}

	/* create time string */
	memset(screen.buf, 0, screen.buf_size);
	if (IS_PLAYING(state) || IS_PAUSED(state)) {
		int total_time = mpd_status_get_total_time(status);
		if (total_time > 0) {
			/*checks the conf to see whether to display elapsed or remaining time */
			if(!strcmp(options.timedisplay_type,"elapsed"))
				elapsedTime = mpd_status_get_elapsed_time(c->status);
			else if(!strcmp(options.timedisplay_type,"remaining"))
				elapsedTime = total_time -
					mpd_status_get_elapsed_time(c->status);

			if (c->song != NULL &&
			    seek_id == (int)mpd_song_get_id(c->song))
				elapsedTime = seek_target_time;

			/* display bitrate if visible-bitrate is true */
#ifndef NCMPC_MINI
			if (options.visible_bitrate) {
				g_snprintf(bitrate, 16,
					   " [%d kbps]",
					   mpd_status_get_kbit_rate(status));
			} else {
				bitrate[0] = '\0';
			}
#endif

			/*write out the time, using hours if time over 60 minutes*/
			if (total_time > 3600) {
				g_snprintf(screen.buf, screen.buf_size,
					   "%s [%i:%02i:%02i/%i:%02i:%02i]",
					   bitrate, elapsedTime/3600, (elapsedTime%3600)/60, elapsedTime%60,
					   total_time / 3600,
					   (total_time % 3600)/60,
					   total_time % 60);
			} else {
				g_snprintf(screen.buf, screen.buf_size,
					   "%s [%i:%02i/%i:%02i]",
					   bitrate, elapsedTime/60, elapsedTime%60,
					   total_time / 60, total_time % 60);
			}
#ifndef NCMPC_MINI
		} else {
			g_snprintf(screen.buf, screen.buf_size,
				   " [%d kbps]",
				   mpd_status_get_kbit_rate(status));
#endif
		}
#ifndef NCMPC_MINI
	} else {
		if (options.display_time) {
			time_t timep;

			time(&timep);
			strftime(screen.buf, screen.buf_size, "%X ",localtime(&timep));
		}
#endif
	}

	/* display song */
	if (IS_PLAYING(state) || IS_PAUSED(state)) {
		char songname[MAX_SONGNAME_LENGTH];
#ifndef NCMPC_MINI
		int width = COLS - x - utf8_width(screen.buf);
#endif

		if (song)
			strfsong(songname, MAX_SONGNAME_LENGTH,
				 options.status_format, song);
		else
			songname[0] = '\0';

		colors_use(w, COLOR_STATUS);
		/* scroll if the song name is to long */
#ifndef NCMPC_MINI
		if (options.scroll && utf8_width(songname) > (unsigned)width) {
			static  scroll_state_t st = { 0, 0 };
			char *tmp = strscroll(songname, options.scroll_sep, width, &st);

			g_strlcpy(songname, tmp, MAX_SONGNAME_LENGTH);
			g_free(tmp);
		}
#endif
		//mvwaddnstr(w, 0, x, songname, width);
		mvwaddstr(w, 0, x, songname);
	}

	/* display time string */
	if (screen.buf[0]) {
		x = screen.status_window.cols - strlen(screen.buf);
		colors_use(w, COLOR_STATUS_TIME);
		mvwaddstr(w, 0, x, screen.buf);
	}

	wnoutrefresh(w);
}

void
screen_exit(void)
{
	if (mode_fn->close != NULL)
		mode_fn->close();

	screen_list_exit();

	string_list_free(screen.find_history);
	g_free(screen.buf);
	g_free(screen.findbuf);

	title_bar_deinit(&screen.title_bar);
	delwin(screen.main_window.w);
	progress_bar_deinit(&screen.progress_bar);
	delwin(screen.status_window.w);
}

void
screen_resize(struct mpdclient *c)
{
	if (COLS<SCREEN_MIN_COLS || LINES<SCREEN_MIN_ROWS) {
		screen_exit();
		fprintf(stderr, "%s", _("Error: Screen too small"));
		exit(EXIT_FAILURE);
	}

	resizeterm(LINES, COLS);

	screen.cols = COLS;
	screen.rows = LINES;

	title_bar_resize(&screen.title_bar, screen.cols);

	/* main window */
	screen.main_window.cols = screen.cols;
	screen.main_window.rows = screen.rows-4;
	wresize(screen.main_window.w, screen.main_window.rows, screen.cols);
	wclear(screen.main_window.w);

	/* progress window */
	progress_bar_resize(&screen.progress_bar, screen.cols,
			    screen.rows - 2, 0);
	progress_bar_paint(&screen.progress_bar);

	/* status window */
	screen.status_window.cols = screen.cols;
	wresize(screen.status_window.w, 1, screen.cols);
	mvwin(screen.status_window.w, screen.rows-1, 0);

	screen.buf_size = screen.cols;
	g_free(screen.buf);
	screen.buf = g_malloc(screen.cols);

	/* resize all screens */
	screen_list_resize(screen.main_window.cols, screen.main_window.rows);

	/* ? - without this the cursor becomes visible with aterm & Eterm */
	curs_set(1);
	curs_set(0);

	screen_paint(c);
}

void
screen_status_message(const char *msg)
{
	WINDOW *w = screen.status_window.w;

	wmove(w, 0, 0);
	wclrtoeol(w);
	colors_use(w, COLOR_STATUS_ALERT);
	waddstr(w, msg);
	wnoutrefresh(w);
	screen.status_timestamp = time(NULL);
}

void
screen_status_printf(const char *format, ...)
{
	char *msg;
	va_list ap;

	va_start(ap,format);
	msg = g_strdup_vprintf(format,ap);
	va_end(ap);
	screen_status_message(msg);
	g_free(msg);
}

void
screen_init(struct mpdclient *c)
{
	if (COLS < SCREEN_MIN_COLS || LINES < SCREEN_MIN_ROWS) {
		fprintf(stderr, "%s\n", _("Error: Screen too small"));
		exit(EXIT_FAILURE);
	}

	screen.cols = COLS;
	screen.rows = LINES;

	screen.buf  = g_malloc(screen.cols);
	screen.buf_size = screen.cols;
	screen.findbuf = NULL;
	screen.start_timestamp = time(NULL);

	/* create top window */
	title_bar_init(&screen.title_bar, screen.cols, 0, 0);

	/* create main window */
	window_init(&screen.main_window, screen.rows - 4, screen.cols, 2, 0);

	//  leaveok(screen.main_window.w, TRUE); temporary disabled
	keypad(screen.main_window.w, TRUE);

	/* create progress window */
	progress_bar_init(&screen.progress_bar, screen.cols,
			  screen.rows - 2, 0);
	progress_bar_paint(&screen.progress_bar);

	/* create status window */
	window_init(&screen.status_window, 1, screen.cols,
		    screen.rows - 1, 0);

	leaveok(screen.status_window.w, FALSE);
	keypad(screen.status_window.w, TRUE);

#ifdef ENABLE_COLORS
	if (options.enable_colors) {
		/* set background attributes */
		wbkgd(stdscr, COLOR_PAIR(COLOR_LIST));
		wbkgd(screen.main_window.w,     COLOR_PAIR(COLOR_LIST));
		wbkgd(screen.title_bar.window.w, COLOR_PAIR(COLOR_TITLE));
		wbkgd(screen.progress_bar.window.w,
		      COLOR_PAIR(COLOR_PROGRESSBAR));
		wbkgd(screen.status_window.w,   COLOR_PAIR(COLOR_STATUS));
		colors_use(screen.progress_bar.window.w, COLOR_PROGRESSBAR);
	}
#endif

	doupdate();

	/* initialize screens */
	screen_list_init(screen.main_window.w,
			 screen.main_window.cols, screen.main_window.rows);

	if (mode_fn->open != NULL)
		mode_fn->open(c);
}

void
screen_paint(struct mpdclient *c)
{
	const char *title = NULL;

	if (mode_fn->get_title != NULL)
		title = mode_fn->get_title(screen.buf, screen.buf_size);

	/* paint the title/header window */
	if( title )
		paint_top_window(title, c);
	else
		paint_top_window("", c);

	/* paint the bottom window */

	paint_progress_window(c);
	paint_status_window(c);

	/* paint the main window */

	wclear(screen.main_window.w);
	if (mode_fn->paint != NULL)
		mode_fn->paint();

	/* move the cursor to the origin */

	if (!options.hardware_cursor)
		wmove(screen.main_window.w, 0, 0);

	wnoutrefresh(screen.main_window.w);

	/* tell curses to update */
	doupdate();
}

void
screen_update(struct mpdclient *c)
{
#ifndef NCMPC_MINI
	static bool initialized = false;
	static bool repeat;
	static bool random_enabled;
	static bool single;
	static bool consume;
	static unsigned crossfade;
	static unsigned dbupdate;

	/* print a message if mpd status has changed */
	if (c->status != NULL) {
		if (!initialized) {
			repeat = mpd_status_get_repeat(c->status);
			random_enabled = mpd_status_get_random(c->status);
			single = mpd_status_get_single(c->status);
			consume = mpd_status_get_consume(c->status);
			crossfade = mpd_status_get_crossfade(c->status);
			dbupdate = mpd_status_get_update_id(c->status);
			initialized = true;
		}

		if (repeat != mpd_status_get_repeat(c->status))
			screen_status_printf(mpd_status_get_repeat(c->status) ?
					     _("Repeat mode is on") :
					     _("Repeat mode is off"));

		if (random_enabled != mpd_status_get_random(c->status))
			screen_status_printf(mpd_status_get_random(c->status) ?
					     _("Random mode is on") :
					     _("Random mode is off"));

		if (single != mpd_status_get_single(c->status))
			screen_status_printf(mpd_status_get_single(c->status) ?
					     /* "single" mode means
						that MPD will
						automatically stop
						after playing one
						single song */
					     _("Single mode is on") :
					     _("Single mode is off"));

		if (consume != mpd_status_get_consume(c->status))
			screen_status_printf(mpd_status_get_consume(c->status) ?
					     /* "consume" mode means
						that MPD removes each
						song which has
						finished playing */
					     _("Consume mode is on") :
					     _("Consume mode is off"));

		if (crossfade != mpd_status_get_crossfade(c->status))
			screen_status_printf(_("Crossfade %d seconds"),
					     mpd_status_get_crossfade(c->status));

		if (dbupdate != 0 &&
		    dbupdate != mpd_status_get_update_id(c->status)) {
			screen_status_printf(_("Database updated"));
		}

		repeat = mpd_status_get_repeat(c->status);
		random_enabled = mpd_status_get_random(c->status);
		single = mpd_status_get_single(c->status);
		consume = mpd_status_get_consume(c->status);
		crossfade = mpd_status_get_crossfade(c->status);
		dbupdate = mpd_status_get_update_id(c->status);
	}

	/* update title/header window */
	if (welcome && options.welcome_screen_list &&
	    time(NULL)-screen.start_timestamp <= SCREEN_WELCOME_TIME)
		paint_top_window("", c);
	else
#endif
	if (mode_fn->get_title != NULL) {
		paint_top_window(mode_fn->get_title(screen.buf,screen.buf_size), c);
#ifndef NCMPC_MINI
		welcome = FALSE;
#endif
	} else
		paint_top_window("", c);

	/* update progress window */
	paint_progress_window(c);

	/* update status window */
	paint_status_window(c);

	/* update the main window */
	if (mode_fn->update != NULL)
		mode_fn->update(c);

	/* move the cursor to the origin */

	if (!options.hardware_cursor)
		wmove(screen.main_window.w, 0, 0);

	wnoutrefresh(screen.main_window.w);

	/* tell curses to update */
	doupdate();
}

#ifdef HAVE_GETMOUSE
int
screen_get_mouse_event(struct mpdclient *c, unsigned long *bstate, int *row)
{
	MEVENT event;

	/* retrieve the mouse event from ncurses */
	getmouse(&event);
	/* calculate the selected row in the list window */
	*row = event.y - screen.title_bar.window.rows;
	/* copy button state bits */
	*bstate = event.bstate;
	/* if button 2 was pressed switch screen */
	if (event.bstate & BUTTON2_CLICKED) {
		screen_cmd(c, CMD_SCREEN_NEXT);
		return 1;
	}

	return 0;
}
#endif

void
screen_cmd(struct mpdclient *c, command_t cmd)
{
#ifndef NCMPC_MINI
	welcome = FALSE;
#endif

	if (mode_fn->cmd != NULL && mode_fn->cmd(c, cmd))
		return;

	if (handle_player_command(c, cmd))
		return;

	switch(cmd) {
	case CMD_TOGGLE_FIND_WRAP:
		options.find_wrap = !options.find_wrap;
		screen_status_printf(options.find_wrap ?
				     _("Find mode: Wrapped") :
				     _("Find mode: Normal"));
		break;
	case CMD_TOGGLE_AUTOCENTER:
		options.auto_center = !options.auto_center;
		screen_status_printf(options.auto_center ?
				     _("Auto center mode: On") :
				     _("Auto center mode: Off"));
		break;
	case CMD_SCREEN_UPDATE:
		screen_paint(c);
		break;
	case CMD_SCREEN_PREVIOUS:
		screen_next_mode(c, -1);
		break;
	case CMD_SCREEN_NEXT:
		screen_next_mode(c, 1);
		break;
	case CMD_SCREEN_PLAY:
		screen_switch(&screen_playlist, c);
		break;
	case CMD_SCREEN_FILE:
		screen_switch(&screen_browse, c);
		break;
#ifdef ENABLE_HELP_SCREEN
	case CMD_SCREEN_HELP:
		screen_switch(&screen_help, c);
		break;
#endif
#ifdef ENABLE_SEARCH_SCREEN
	case CMD_SCREEN_SEARCH:
		screen_switch(&screen_search, c);
		break;
#endif
#ifdef ENABLE_ARTIST_SCREEN
	case CMD_SCREEN_ARTIST:
		screen_switch(&screen_artist, c);
		break;
#endif
#ifdef ENABLE_SONG_SCREEN
	case CMD_SCREEN_SONG:
		screen_switch(&screen_song, c);
		break;
#endif
#ifdef ENABLE_KEYDEF_SCREEN
	case CMD_SCREEN_KEYDEF:
		screen_switch(&screen_keydef, c);
		break;
#endif
#ifdef ENABLE_LYRICS_SCREEN
	case CMD_SCREEN_LYRICS:
		screen_switch(&screen_lyrics, c);
		break;
#endif
#ifdef ENABLE_OUTPUTS_SCREEN
	case CMD_SCREEN_OUTPUTS:
		screen_switch(&screen_outputs, c);
		break;
	case CMD_SCREEN_SWAP:
		screen_swap(c, NULL);
		break;
#endif

	default:
		break;
	}
}
