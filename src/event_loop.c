/* vifm
 * Copyright (C) 2001 Ken Steen.
 * Copyright (C) 2011 xaizek.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "event_loop.h"

#include <curses.h>

#include <unistd.h> /* select() */

#include <assert.h> /* assert() */
#include <signal.h> /* signal() */
#include <stddef.h> /* NULL size_t wchar_t wint_t */
#include <string.h> /* memmove() strncpy() */
#include <wchar.h> /* wcslen() wcscmp() */

#include "cfg/config.h"
#include "engine/keys.h"
#include "engine/mode.h"
#include "modes/modes.h"
#include "ui/statusbar.h"
#include "ui/ui.h"
#include "utils/log.h"
#include "utils/macros.h"
#include "utils/utils.h"
#include "background.h"
#include "filelist.h"
#include "fileview.h"
#include "ipc.h"
#include "status.h"

static int ensure_term_is_ready(void);
static int get_char_async_loop(WINDOW *win, wint_t *c, int timeout);
static void process_scheduled_updates(void);
static void process_scheduled_updates_of_view(FileView *view);
static int should_check_views_for_changes(void);
static void check_view_for_changes(FileView *view);

/* Current input buffer. */
static const wchar_t *curr_input_buf;
/* Current position in current input buffer. */
static const int *curr_input_buf_pos;

void
event_loop(const int *quit)
{
	/* TODO: refactor this function event_loop(). */

	LOG_FUNC_ENTER;

	const wchar_t *const prev_input_buf = curr_input_buf;
	const int *const prev_input_buf_pos = curr_input_buf_pos;

	wchar_t input_buf[128];
	int input_buf_pos;

	int last_result = 0;
	int wait_for_enter = 0;
	int timeout = cfg.timeout_len;

	input_buf[0] = L'\0';
	input_buf_pos = 0;
	curr_input_buf = &input_buf[0];
	curr_input_buf_pos = &input_buf_pos;

	while(!*quit)
	{
		wint_t c;
		size_t counter;
		int got_input;

		if(!ensure_term_is_ready())
		{
			wait_for_enter = 0;
			continue;
		}

		lwin.user_selection = 1;
		rwin.user_selection = 1;

		modes_pre();

		/* Waits for timeout then skips if no keypress.  Short-circuit if we're not
		 * waiting for the next key after timeout. */
		do
		{
			check_background_jobs();

			got_input = get_char_async_loop(status_bar, &c, timeout) != ERR;
			if(!got_input && input_buf_pos == 0)
			{
				timeout = cfg.timeout_len;
				continue;
			}
			break;
		}
		while(1);

		/* Ensure that current working directory is set correctly (some pieces of
		 * code rely on this). */
		(void)vifm_chdir(flist_get_dir(curr_view));

		if(got_input && input_buf_pos != ARRAY_LEN(input_buf) - 2)
		{
			if(c == L'\x1a') /* Ctrl-Z */
			{
				def_prog_mode();
				endwin();
				stop_process();
				continue;
			}

			if(wait_for_enter)
			{
				wait_for_enter = 0;
				curr_stats.save_msg = 0;
				clean_status_bar();
				if(c == L'\x0d')
					continue;
			}

			input_buf[input_buf_pos++] = c;
			input_buf[input_buf_pos] = L'\0';
		}

		if(wait_for_enter && !got_input)
		{
			continue;
		}

		counter = get_key_counter();
		if(!got_input && last_result == KEYS_WAIT_SHORT)
		{
			last_result = execute_keys_timed_out(input_buf);
			counter = get_key_counter() - counter;
			assert(counter <= input_buf_pos);
			if(counter > 0)
			{
				memmove(input_buf, input_buf + counter,
						(wcslen(input_buf) - counter + 1)*sizeof(wchar_t));
			}
		}
		else
		{
			if(got_input)
			{
				curr_stats.save_msg = 0;
			}

			last_result = execute_keys(input_buf);

			counter = get_key_counter() - counter;
			assert(counter <= input_buf_pos);
			if(counter > 0)
			{
				input_buf_pos -= counter;
				memmove(input_buf, input_buf + counter,
						(wcslen(input_buf) - counter + 1)*sizeof(wchar_t));
			}

			if(last_result == KEYS_WAIT || last_result == KEYS_WAIT_SHORT)
			{
				if(got_input)
				{
					modupd_input_bar(input_buf);
				}

				if(last_result == KEYS_WAIT_SHORT && wcscmp(input_buf, L"\033") == 0)
				{
					timeout = 1;
				}

				if(counter > 0)
				{
					clear_input_bar();
				}

				if(!curr_stats.save_msg && curr_view->selected_files &&
						!vle_mode_is(CMDLINE_MODE))
				{
					print_selected_msg();
				}
				continue;
			}
		}

		timeout = cfg.timeout_len;

		process_scheduled_updates();

		input_buf_pos = 0;
		input_buf[0] = L'\0';
		clear_input_bar();

		if(is_status_bar_multiline())
		{
			wait_for_enter = 1;
			update_all_windows();
			continue;
		}

		/* Ensure that current working directory is set correctly (some pieces of
		 * code rely on this).  PWD could be changed during command execution, but
		 * it should be correct for modes_post() in case of preview modes. */
		(void)vifm_chdir(flist_get_dir(curr_view));
		modes_post();
	}

	curr_input_buf = prev_input_buf;
	curr_input_buf_pos = prev_input_buf_pos;
}

/* Ensures that terminal is in proper state for a loop iteration.  Returns
 * non-zero if so, otherwise zero is returned. */
static int
ensure_term_is_ready(void)
{
	is_term_working();

	update_terminal_settings();

	if(curr_stats.term_state == TS_TOO_SMALL)
	{
		ui_display_too_small_term_msg();
		wait_for_signal();
		return 0;
	}

	if(curr_stats.term_state == TS_BACK_TO_NORMAL)
	{
		wint_t c;

		wtimeout(status_bar, 0);
		while(wget_wch(status_bar, &c) != ERR);
		curr_stats.term_state = TS_NORMAL;
		modes_redraw();
		wtimeout(status_bar, cfg.timeout_len);

		curr_stats.save_msg = 0;
		status_bar_message("");
	}

	return 1;
}

/* Sub-loop of the main loop that "asynchronously" queries for the input
 * performing the following tasks while waiting for input:
 *  - checks for new IPC messages;
 *  - checks whether contents of displayed directories changed;
 *  - redraws UI if requested.
 * Returns KEY_CODE_YES for functional keys, OK for wide character and ERR
 * otherwise (e.g. after timeout). */
static int
get_char_async_loop(WINDOW *win, wint_t *c, int timeout)
{
	const int IPC_F = (ipc_enabled() && ipc_server()) ? 10 : 1;

	do
	{
		int i;

		process_scheduled_updates();

		if(should_check_views_for_changes())
		{
			check_view_for_changes(curr_view);
			check_view_for_changes(other_view);
		}

		for(i = 0; i < IPC_F; ++i)
		{
			int result;

			ipc_check();
			wtimeout(win, MIN(cfg.min_timeout_len, timeout)/IPC_F);

			result = wget_wch(win, c);
			if(result != ERR)
			{
				return result;
			}

			process_scheduled_updates();
		}

		timeout -= cfg.min_timeout_len;
	}
	while(timeout > 0);

	return ERR;
}

/* Updates TUI or its elements if something is scheduled. */
static void
process_scheduled_updates(void)
{
	if(fetch_redraw_scheduled())
	{
		modes_redraw();
	}

	if(vle_mode_get_primary() != MENU_MODE)
	{
		process_scheduled_updates_of_view(curr_view);
		process_scheduled_updates_of_view(other_view);
	}
}

/* Performs postponed updates for the view, if any. */
static void
process_scheduled_updates_of_view(FileView *view)
{
	if(!window_shows_dirlist(view))
	{
		return;
	}

	switch(ui_view_query_scheduled_event(view))
	{
		case UUE_NONE:
			/* Nothing to do. */
			break;
		case UUE_REDRAW:
			redraw_view_imm(view);
			break;
		case UUE_RELOAD:
			load_saving_pos(view, 1);
			if(view == curr_view && !is_status_bar_multiline())
			{
				ui_ruler_update(view);
			}
			break;
		case UUE_FULL_RELOAD:
			load_saving_pos(view, 0);
			break;

		default:
			assert(0 && "Unexpected type of scheduled UI event.");
			break;
	}
}

/* Checks whether views should be checked against external changes.  Returns
 * non-zero is so, otherwise zero is returned. */
static int
should_check_views_for_changes(void)
{
	return !is_status_bar_multiline()
	    && !is_in_menu_like_mode()
	    && NONE(vle_mode_is, CMDLINE_MODE, MSG_MODE);
}

/* Updates view in case directory it displays was changed externally. */
static void
check_view_for_changes(FileView *view)
{
	if(window_shows_dirlist(view))
	{
		check_if_filelist_have_changed(view);
	}
}

void
update_input_buf(void)
{
	if(curr_stats.use_input_bar)
	{
		werase(input_win);
		wprintw(input_win, "%ls", (curr_input_buf == NULL) ? L"" : curr_input_buf);
		wrefresh(input_win);
	}
}

int
is_input_buf_empty(void)
{
	return curr_input_buf_pos == NULL || *curr_input_buf_pos == 0;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
