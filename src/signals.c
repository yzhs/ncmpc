/* ncmpc (Ncurses MPD Client)
 * (c) 2004-2017 The Music Player Daemon Project
 * Project homepage: http://musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "signals.h"
#include "screen.h"
#include "Compiler.h"

#include <glib-unix.h>

#include <stdbool.h>

static int sigwinch_pipes[2];

static gboolean
handle_quit_signal(gpointer data)
{
	GMainLoop *main_loop = data;

	g_main_loop_quit(main_loop);
	return false;
}

static gboolean
sigwinch_event(gcc_unused GIOChannel *source,
               gcc_unused GIOCondition condition, gpointer data)
{
	struct mpdclient *c = data;

	char ignoreme[64];
	if (1 > read(sigwinch_pipes[0], ignoreme, 64))
		exit(EXIT_FAILURE);

	endwin();
	refresh();
	screen_resize(c);

	return TRUE;
}

static void
catch_sigwinch(gcc_unused int sig)
{
	if (1 != write(sigwinch_pipes[1], "", 1))
		exit(EXIT_FAILURE);
}

void
signals_init(GMainLoop *main_loop, struct mpdclient *c)
{
	/* setup quit signals */
	g_unix_signal_add(SIGTERM, handle_quit_signal, main_loop);
	g_unix_signal_add(SIGINT, handle_quit_signal, main_loop);
	g_unix_signal_add(SIGHUP, handle_quit_signal, main_loop);

	/* setup signal behavior - SIGCONT */

	struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	act.sa_handler = catch_sigwinch;
	if (sigaction(SIGCONT, &act, NULL) < 0) {
		perror("sigaction(SIGCONT)");
		exit(EXIT_FAILURE);
	}

	/* setup SIGWINCH */

	act.sa_flags = SA_RESTART;
	act.sa_handler = catch_sigwinch;
	if (sigaction(SIGWINCH, &act, NULL) < 0) {
		perror("sigaction(SIGWINCH)");
		exit(EXIT_FAILURE);
	}

#ifndef WIN32
	if (!pipe(sigwinch_pipes) &&
		!fcntl(sigwinch_pipes[1], F_SETFL, O_NONBLOCK)) {
		GIOChannel *sigwinch_channel = g_io_channel_unix_new(sigwinch_pipes[0]);
		g_io_add_watch(sigwinch_channel, G_IO_IN, sigwinch_event, c);
		g_io_channel_unref(sigwinch_channel);
	}
	else {
		perror("sigwinch pipe creation failed");
		exit(EXIT_FAILURE);
	}
#endif

	/* ignore SIGPIPE */

	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) < 0) {
		perror("sigaction(SIGPIPE)");
		exit(EXIT_FAILURE);
	}
}

void
signals_deinit(void)
{
	close(sigwinch_pipes[0]);
	close(sigwinch_pipes[1]);
}
