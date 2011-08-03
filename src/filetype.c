/* vifm
 * Copyright (C) 2001 Ken Steen.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <curses.h>
#include <regex.h>

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "status.h"

#include "filetype.h"

static char *
to_regex(const char *global)
{
	char *result = strdup("^");
	size_t result_len = 1;
	while(*global != '\0')
	{
		if(strchr("^.[$()|+{", *global) != NULL)
		{
			result = realloc(result, result_len + 2 + 1 + 1);
			result[result_len++] = '\\';
		}
		else if(*global == '\\')
		{
			result = realloc(result, result_len + 2 + 1 + 1);
			result[result_len++] = *global++;
		}
		else if(*global == '?')
		{
			result = realloc(result, result_len + 1 + 1 + 1);
			result[result_len++] = '.';
			global++;
			continue;
		}
		else if(*global == '*')
		{
			if(result_len == 1)
			{
				result = realloc(result, result_len + 9 + 1 + 1);
				result[result_len++] = '(';
				result[result_len++] = '[';
				result[result_len++] = '^';
				result[result_len++] = '.';
				result[result_len++] = ']';
				result[result_len++] = '.';
				result[result_len++] = '*';
				result[result_len++] = ')';
				result[result_len++] = '?';
			}
			else
			{
				result = realloc(result, result_len + 2 + 1 + 1);
				result[result_len++] = '.';
				result[result_len++] = '*';
			}
			global++;
			continue;
		}
		else
		{
			result = realloc(result, result_len + 1 + 1 + 1);
		}
		result[result_len++] = *global++;
	}
	result[result_len++] = '$';
	result[result_len] = '\0';
	return result;
}

static int
global_matches(const char *global, const char *file)
{
	char *regex;
	regex_t re;

	regex = to_regex(global);

	if(regcomp(&re, regex, REG_EXTENDED | REG_ICASE) == 0)
	{
		if(regexec(&re, file, 0, NULL, 0) == 0)
		{
			regfree(&re);
			free(regex);
			return 1;
		}
	}
	regfree(&re);
	free(regex);
	return 0;
}

static int
get_filetype_number(const char *file, int count, assoc_t *array)
{
	int x;

	for(x = 0; x < count; x++)
	{
		char *exptr = NULL;

		/* Only one extension */
		if((exptr = strchr(array[x].ext, ',')) == NULL)
		{
			if(global_matches(array[x].ext, file))
				return x;
		}
		else
		{
			char *ex_copy = strdup(array[x].ext);
			char *free_this = ex_copy;
			char *exptr2 = NULL;
			while((exptr = exptr2 = strchr(ex_copy, ',')) != NULL)
			{
				*exptr = '\0';
				exptr2++;

				if(global_matches(ex_copy, file))
				{
					free(free_this);
					return x;
				}

				ex_copy = exptr2;
			}
			if(global_matches(ex_copy, file))
			{
				free(free_this);
				return x;
			}
			free(free_this);
		}
	}
	return -1;
}

static char *
get_default_program_for_file_inner(char *file, int for_x)
{
	assoc_t *arr = for_x ? xfiletypes : filetypes;
	int count = for_x ? cfg.xfiletypes_num : cfg.filetypes_num;
	int x = get_filetype_number(file, count, arr);
	char *strptr = NULL;
	char *ptr = NULL;
	char *program_name = NULL;

	if(x < 0)
		return NULL;

	strptr = strdup(arr[x].com);

	/* Only one program */
	if((ptr = strchr(strptr, ',')) == NULL)
	{
		program_name = strdup(strptr);
	}
	else
	{
		*ptr = '\0';
		program_name = strdup(strptr);
	}
	free(strptr);
	return program_name;
}

char *
get_default_program_for_file(char *file)
{
	if(!curr_stats.is_console)
	{
		char *result;
		if((result = get_default_program_for_file_inner(file, 1)) != NULL)
			return result;
	}
	return get_default_program_for_file_inner(file, 0);
}

char *
get_viewer_for_file(char *file)
{
	int x = get_filetype_number(file, cfg.fileviewers_num, fileviewers);

	if(x < 0)
		return NULL;

	return fileviewers[x].com;
}

static char *
get_all_programs_for_file_inner(char *file, int for_x)
{
	assoc_t *arr = for_x ? xfiletypes : filetypes;
	int count = for_x ? cfg.xfiletypes_num : cfg.filetypes_num;
	int x = get_filetype_number(file, count, arr);

	if(x > -1)
		return arr[x].com;

	return NULL;
}

char *
get_all_programs_for_file(char *file)
{
	if(!curr_stats.is_console)
	{
		char *result;
		if((result = get_all_programs_for_file_inner(file, 1)) != NULL)
			return result;
	}
	return get_all_programs_for_file_inner(file, 0);
}

static int
add_assoc(assoc_t **arr, int count, const char *extension, const char *programs)
{
	*arr = realloc(*arr, (count + 1)*sizeof(assoc_t));

	(*arr)[count].ext = strdup(extension);
	(*arr)[count].com = strdup(programs);
	return count + 1;
}

static void
set_ext_programs(const char *extension, const char *programs, int for_x)
{
	int x;
	assoc_t *arr;
	int count;

	if(extension[0] == '\0')
		return;

	arr = for_x ? xfiletypes : filetypes;
	count = for_x ? cfg.xfiletypes_num : cfg.filetypes_num;

	for(x = 0; x < count; x++)
		if(strcasecmp(arr[x].ext, extension) == 0)
			break;
	if(x == count)
	{
		if(for_x)
			cfg.xfiletypes_num = add_assoc(&xfiletypes, cfg.xfiletypes_num, extension,
					programs);
		else
			cfg.filetypes_num = add_assoc(&filetypes, cfg.filetypes_num, extension,
					programs);
	}
	else
	{
		free(arr[x].com);
		arr[x].com = strdup(programs);
	}
}

void
set_programs(const char *extensions, const char *programs, int x)
{
	char *exptr;
	if((exptr = strchr(extensions, ',')) == NULL)
	{
		set_ext_programs(extensions, programs, x);
	}
	else
	{
		char *ex_copy = strdup(extensions);
		char *free_this = ex_copy;
		char *exptr2 = NULL;
		while((exptr = exptr2 = strchr(ex_copy, ',')) != NULL)
		{
			*exptr = '\0';
			exptr2++;

			set_ext_programs(ex_copy, programs, x);

			ex_copy = exptr2;
		}
		set_ext_programs(ex_copy, programs, x);
		free(free_this);
	}
}

static void
set_ext_viewer(const char *extension, const char *viewer)
{
	int x;

	if(extension[0] == '\0')
		return;

	for(x = 0; x < cfg.fileviewers_num; x++)
		if(strcasecmp(fileviewers[x].ext, extension) == 0)
			break;
	if(x == cfg.fileviewers_num)
	{
		cfg.fileviewers_num = add_assoc(&fileviewers, cfg.fileviewers_num,
				extension, viewer);
	}
	else
	{
		free(fileviewers[x].com);
		fileviewers[x].com = strdup(viewer);
	}
}

void
set_fileviewer(const char *extensions, const char *viewer)
{
	char *exptr;
	if((exptr = strchr(extensions, ',')) == NULL)
	{
		set_ext_viewer(extensions, viewer);
	}
	else
	{
		char *ex_copy = strdup(extensions);
		char *free_this = ex_copy;
		char *exptr2 = NULL;
		while((exptr = exptr2 = strchr(ex_copy, ',')) != NULL)
		{
			*exptr = '\0';
			exptr2++;

			set_ext_viewer(ex_copy, viewer);

			ex_copy = exptr2;
		}
		set_ext_viewer(ex_copy, viewer);
		free(free_this);
	}
}

static void
reset_list(assoc_t **arr, int *size)
{
	int i;

	for(i = 0; i < *size; i++)
	{
		free((*arr)[i].ext);
		free((*arr)[i].com);
	}

	free(*arr);
	*arr = NULL;
	*size = 0;
}

void
reset_filetypes(void)
{
	reset_list(&filetypes, &cfg.filetypes_num);
}

void
reset_xfiletypes(void)
{
	reset_list(&xfiletypes, &cfg.xfiletypes_num);
}

void
reset_fileviewers(void)
{
	reset_list(&fileviewers, &cfg.fileviewers_num);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
