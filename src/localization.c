/*
 * Rufus: The Reliable USB Formatting Utility
 * Localization functions, a.k.a. "Everybody is doing it wrong but me!"
 * Copyright © 2013 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stddef.h>

#include "rufus.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"
#include "localization_data.h"

/* c control ID (no space, no quotes), s: quoted string, i: 32 bit signed integer,  */
// Remember to update the size of the array in localization.h when adding/removing elements
const loc_parse parse_cmd[8] = {
	{ 'l', LC_LOCALE, "s" },
	{ 'v', LC_VERSION, "ii" },
	{ 't', LC_TEXT, "cs" },
	{ 'p', LC_PARENT, "c" },
	{ 'r', LC_RESIZE, "cii" },
	{ 'm', LC_MOVE, "cii" },
	{ 'f', LC_FONT, "si" },
	{ 'd', LC_DIRECTION, "i" },
};
int  loc_line_nr = 0;
char loc_filename[32];

void free_loc_cmd(loc_cmd* lcmd)
{
	if (lcmd == NULL)
		return;
	safe_free(lcmd->text[0]);
	safe_free(lcmd->text[1]);
	free(lcmd);
}

void loc_dlg_add(int index, loc_cmd* lcmd)
{
	if ((lcmd == NULL) || (index < 0) || (index >= ARRAYSIZE(loc_dlg))) {
		uprintf("loc_dlg_add: invalid parameter\n");
		return;
	}
	list_add(&lcmd->list, &loc_dlg[index].list);
}

// TODO: rename this to something_localization()
void free_loc_dlg(void)
{
	size_t i = 0;
	loc_cmd *lcmd, *next;

	for (i=0; i<ARRAYSIZE(loc_dlg); i++) {
		if (list_empty(&loc_dlg[i].list))
			continue;
		list_for_each_entry_safe(lcmd, next, &loc_dlg[i].list, list, loc_cmd) {
			list_del(&lcmd->list);
			free_loc_cmd(lcmd);
		}
	}
}

/*
 * We need to initialize the command lists
 */
void init_localization(void) {
	size_t i;

	for (i=0; i<ARRAYSIZE(loc_dlg); i++)
		list_init(&loc_dlg[i].list);
}

/*
 * Yada. Should be called during init
 * if hDlg is NULL, we try to apply the commands against an active Window
 * if dlg_id is negative, we try to apply all
 */
void apply_localization(int dlg_id, HWND hDlg)
{
	loc_cmd* lcmd;
	HWND hCtrl = NULL;
	int id_start = IDD_DIALOG, id_end = IDD_DIALOG + ARRAYSIZE(loc_dlg);

	if ((dlg_id >= id_start) && (dlg_id < id_end)) {
		// If we have a valid dialog_id, just process that one dialog
		id_start = dlg_id;
		id_end = dlg_id + 1;
		if (hDlg != NULL) {
			loc_dlg[dlg_id-IDD_DIALOG].hDlg = hDlg;
		}
	}

	for (dlg_id = id_start; dlg_id < id_end; dlg_id++) {
		hDlg = loc_dlg[dlg_id-IDD_DIALOG].hDlg;
		if ((!IsWindow(hDlg)) || (list_empty(&loc_dlg[dlg_id-IDD_DIALOG].list)))
			continue;

		list_for_each_entry(lcmd, &loc_dlg[dlg_id-IDD_DIALOG].list, list, loc_cmd) {
			if (lcmd->command <= LC_TEXT) { // TODO: should always be the case
				if (lcmd->ctrl_id == dlg_id) {
					if (dlg_id == IDD_DIALOG) {
						luprint("operation forbidden (main dialog title cannot be changed)");
						continue;
					}
					hCtrl = hDlg;
				} else {
					hCtrl = GetDlgItem(hDlg, lcmd->ctrl_id);
				}
				if (hCtrl == NULL) {
					loc_line_nr = lcmd->line_nr;
					luprintf("control '%s' is not part of dialog '%s'\n",
						lcmd->text[0], control_id[dlg_id-IDD_DIALOG].name);
				}
			}

			switch(lcmd->command) {
			// NB: For commands that take an ID, ctrl_id is always a valid index at this stage
			case LC_TEXT:
				if (hCtrl != NULL) {
					SetWindowTextU(hCtrl, lcmd->text[1]);
				}
				break;
			case LC_MOVE:
				if (hCtrl != NULL) {
					ResizeMoveCtrl(hDlg, hCtrl, lcmd->num[0], lcmd->num[1], 0, 0);
				}
				break;
			case LC_RESIZE:
				if (hCtrl != NULL) {
					ResizeMoveCtrl(hDlg, hCtrl, 0, 0, lcmd->num[0], lcmd->num[1]);
				}
				break;
			}
		}
	}
}

// Can't use isWindow() against our existing HWND to avoid this call
// as handles are recycled.
void reset_localization(int dlg_id)
{
	loc_dlg[dlg_id-IDD_DIALOG].hDlg = NULL;
}

// TODO: we need to store a revert for every action we execute here,
// or do we want to reinstantiate the dialogs?
BOOL dispatch_loc_cmd(loc_cmd* lcmd)
{
	size_t i;
	static int dlg_index = 0;

	if (lcmd == NULL)
		return FALSE;

//	uprintf("cmd #%d: ('%s', '%s') (%d, %d)\n",
//		lcmd->command, lcmd->text[0], lcmd->text[1], lcmd->num[0], lcmd->num[1]);

	if (lcmd->command <= LC_TEXT) {
		// Any command up to LC_TEXT takes a control ID in text[0]
		for (i=0; i<ARRAYSIZE(control_id); i++) {
			if (safe_strcmp(lcmd->text[0], control_id[i].name) == 0) {
				lcmd->ctrl_id = control_id[i].id;
				break;
			}
		}
		if (lcmd->ctrl_id < 0) {
			luprintf("unknown control '%s'\n", lcmd->text[0]);
			goto err;
		}
	}

	switch(lcmd->command) {
	// NB: Form commands that take an ID, ctrl_id is always a valid index at this stage
	case LC_TEXT:
	case LC_MOVE:
	case LC_RESIZE:
		loc_dlg_add(dlg_index, lcmd);
		break;
	case LC_PARENT:
		if ((lcmd->ctrl_id-IDD_DIALOG) > ARRAYSIZE(loc_dlg)) {
			luprintf("'%s' is not a dialog ID\n", lcmd->text[0]);
			goto err;
		}
		dlg_index = lcmd->ctrl_id - IDD_DIALOG;
		free_loc_cmd(lcmd);
		break;
	default:
		free_loc_cmd(lcmd);
		break;
	}
	return TRUE;

err:
	free_loc_cmd(lcmd);
	return FALSE;
}
