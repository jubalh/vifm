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

#include "utils_nix.h"
#include "utils_int.h"

#include <sys/select.h> /* select() FD_SET FD_ZERO */
#include <sys/stat.h> /* O_RDONLY O_WRONLY S_* */
#include <sys/time.h> /* timeval */
#include <sys/types.h> /* gid_t mode_t pid_t uid_t */
#include <sys/wait.h> /* waitpid */
#include <fcntl.h> /* open() close() */
#include <grp.h> /* getgrnam() getgrgid_r() */
#include <pwd.h> /* getpwnam() getpwuid_r() */
#include <unistd.h> /* X_OK dup() dup2() getpid() pause() */

#include <assert.h> /* assert() */
#include <ctype.h> /* isdigit() */
#include <errno.h> /* EINTR errno */
#include <signal.h> /* SIGINT SIGTSTP SIGCHLD SIG_DFL SIG_BLOCK SIG_UNBLOCK
                       sigset_t kill() sigaddset() sigemptyset() signal()
                       sigprocmask() */
#include <stddef.h> /* NULL size_t */
#include <stdio.h> /* FILE stderr fdopen() fprintf() snprintf() */
#include <stdlib.h> /* atoi() free() */
#include <string.h> /* strchr() strdup() strlen() strncmp() */

#include "../cfg/config.h"
#include "../compat/os.h"
#include "../ui/cancellation.h"
#include "../running.h"
#include "../status.h"
#include "env.h"
#include "filemon.h"
#include "fs.h"
#include "fs_limits.h"
#include "log.h"
#include "macros.h"
#include "mntent.h" /* mntent setmntent() getmntent() endmntent() */
#include "path.h"
#include "str.h"
#include "utils.h"

/* Types of mount point information for get_mount_point_traverser_state. */
typedef enum
{
	MI_MOUNT_POINT, /* Path to the mount point. */
	MI_FS_TYPE,     /* File system name. */
}
mntinfo;

/* State for get_mount_point() traverser. */
typedef struct
{
	mntinfo type;     /* Type of information to put into the buffer. */
	const char *path; /* Path whose mount point we're looking for. */
	size_t buf_len;   /* Output buffer length. */
	char *buf;        /* Output buffer. */
	size_t curr_len;  /* Max length among all found points (len of buf string). */
}
get_mount_point_traverser_state;

static int get_mount_info_traverser(struct mntent *entry, void *arg);
static void free_mnt_entries(struct mntent *entries, unsigned int nentries);
struct mntent * read_mnt_entries(unsigned int *nentries);
static int clone_mnt_entry(struct mntent *lhs, const struct mntent *rhs);
static void free_mnt_entry(struct mntent *entry);
static int starts_with_list_item(const char str[], const char list[]);
static int find_path_prefix_index(const char path[], const char list[]);

void
pause_shell(void)
{
	run_in_shell_no_cls(PAUSE_CMD);
}

int
run_in_shell_no_cls(char command[])
{
	typedef void (*sig_handler)(int);

	int pid;
	int result;
	extern char **environ;
	sig_handler sigtstp_handler;

	if(command == NULL)
		return 1;

	sigtstp_handler = signal(SIGTSTP, SIG_DFL);

	/* We need to block SIGCHLD signal.  One can't just set it to SIG_DFL, because
	 * it will possibly cause missing of SIGCHLD from a background process
	 * (job). */
	(void)set_sigchld(1);

	pid = fork();
	if(pid == -1)
	{
		signal(SIGTSTP, sigtstp_handler);
		(void)set_sigchld(0);
		return -1;
	}
	if(pid == 0)
	{
		char *args[4];

		signal(SIGTSTP, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		(void)set_sigchld(0);

		args[0] = cfg.shell;
		args[1] = "-c";
		args[2] = command;
		args[3] = NULL;
		execve(cfg.shell, args, environ);
		exit(127);
	}

	result = get_proc_exit_status(pid);

	signal(SIGTSTP, sigtstp_handler);
	(void)set_sigchld(0);

	return result;
}

void
recover_after_shellout(void)
{
	/* Do nothing.  No need to recover anything on this platform. */
}

void
wait_for_data_from(pid_t pid, FILE *f, int fd)
{
	const struct timeval ts_init = { .tv_sec = 0, .tv_usec = 1000 };
	struct timeval ts;
	int select_result;

	fd_set read_ready;
	FD_ZERO(&read_ready);

	fd = (f != NULL) ? fileno(f) : fd;

	do
	{
		process_cancel_request(pid);
		ts = ts_init;
		FD_SET(fd, &read_ready);
		select_result = select(fd + 1, &read_ready, NULL, NULL, &ts);
	}
	while(select_result == 0 || (select_result == -1 && errno == EINTR));
}

int
set_sigchld(int block)
{
	const int action = block ? SIG_BLOCK : SIG_UNBLOCK;
	sigset_t sigchld_mask;

	return sigemptyset(&sigchld_mask) == -1
	    || sigaddset(&sigchld_mask, SIGCHLD) == -1
	    || sigprocmask(action, &sigchld_mask, NULL) == -1;
}

void
process_cancel_request(pid_t pid)
{
	if(ui_cancellation_requested())
	{
		if(kill(pid, SIGINT) != 0)
		{
			LOG_SERROR_MSG(errno, "Failed to send SIGINT to " PRINTF_PID_T, pid);
		}
	}
}

int
get_proc_exit_status(pid_t pid)
{
	do
	{
		int status;
		if(waitpid(pid, &status, 0) == -1)
		{
			if(errno != EINTR)
			{
				LOG_SERROR_MSG(errno, "waitpid()");
				return -1;
			}
		}
		else
		{
			return status;
		}
	}
	while(1);
}

void _gnuc_noreturn
run_from_fork(int pipe[2], int err_only, char cmd[])
{
	char *args[4];
	int nullfd;

	/* Redirect stderr and maybe stdout to write end of the pipe. */
	if(dup2(pipe[1], STDERR_FILENO) == -1)
	{
		exit(1);
	}
	if(err_only)
	{
		close(STDOUT_FILENO);
	}
	else
	{
		if(dup2(pipe[1], STDOUT_FILENO) == -1)
		{
			exit(1);
		}
	}

	/* Close read end of pipe. */
	close(pipe[0]);

	close(STDIN_FILENO);

	/* Send stdout, stdin to /dev/null */
	if((nullfd = open("/dev/null", O_RDONLY)) != -1)
	{
		if(dup2(nullfd, STDIN_FILENO) == -1)
		{
			exit(1);
		}
		if(err_only && dup2(nullfd, STDOUT_FILENO) == -1)
		{
			exit(1);
		}
	}

	args[0] = cfg.shell;
	args[1] = "-c";
	args[2] = cmd;
	args[3] = NULL;

	execvp(args[0], args);
	exit(1);
}

void
get_perm_string(char buf[], int len, mode_t mode)
{
	static const char *const perm_sets[] =
	{ "---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx" };
	int u, g, o;

	u = (mode & S_IRWXU) >> 6;
	g = (mode & S_IRWXG) >> 3;
	o = (mode & S_IRWXO);

	snprintf(buf, len, "-%s%s%s", perm_sets[u], perm_sets[g], perm_sets[o]);

	if(S_ISLNK(mode))
		buf[0] = 'l';
	else if(S_ISDIR(mode))
		buf[0] = 'd';
	else if(S_ISBLK(mode))
		buf[0] = 'b';
	else if(S_ISCHR(mode))
		buf[0] = 'c';
	else if(S_ISFIFO(mode))
		buf[0] = 'p';
	else if(S_ISSOCK(mode))
		buf[0] = 's';

	if(mode & S_ISVTX)
		buf[9] = (buf[9] == '-') ? 'T' : 't';
	if(mode & S_ISGID)
		buf[6] = (buf[6] == '-') ? 'S' : 's';
	if(mode & S_ISUID)
		buf[3] = (buf[3] == '-') ? 'S' : 's';
}

int
refers_to_slower_fs(const char from[], const char to[])
{
	const int i = find_path_prefix_index(to, cfg.slow_fs_list);
	/* When destination is not on slow file system, no performance penalties are
	 * expected. */
	if(i == -1)
	{
		return 0;
	}

	/* Otherwise, same slowdown as we have for the source location is bearable. */
	return find_path_prefix_index(from, cfg.slow_fs_list) != i;
}

int
is_on_slow_fs(const char full_path[])
{
	char fs_name[PATH_MAX];
	get_mount_point_traverser_state state =
	{
		.type = MI_FS_TYPE,
		.path = full_path,
		.buf_len = sizeof(fs_name),
		.buf = fs_name,
		.curr_len = 0UL,
	};

	/* Empty list optimization. */
	if(cfg.slow_fs_list[0] == '\0')
	{
		return 0;
	}

	if(traverse_mount_points(&get_mount_info_traverser, &state) == 0)
	{
		if(state.curr_len > 0)
		{
			if(starts_with_list_item(fs_name, cfg.slow_fs_list))
			{
				return 1;
			}
		}
	}

	return find_path_prefix_index(full_path, cfg.slow_fs_list) != -1;
}

int
get_mount_point(const char path[], size_t buf_len, char buf[])
{
	get_mount_point_traverser_state state =
	{
		.type = MI_MOUNT_POINT,
		.path = path,
		.buf_len = buf_len,
		.buf = buf,
		.curr_len = 0UL,
	};
	return traverse_mount_points(&get_mount_info_traverser, &state);
}

/* traverse_mount_points client that gets mount point info for a given path. */
static int
get_mount_info_traverser(struct mntent *entry, void *arg)
{
	get_mount_point_traverser_state *const state = arg;
	if(path_starts_with(state->path, entry->mnt_dir))
	{
		const size_t new_len = strlen(entry->mnt_dir);
		if(new_len > state->curr_len)
		{
			state->curr_len = new_len;
			switch(state->type)
			{
				case MI_MOUNT_POINT:
					copy_str(state->buf, state->buf_len, entry->mnt_dir);
					break;
				case MI_FS_TYPE:
					copy_str(state->buf, state->buf_len, entry->mnt_type);
					break;

				default:
					assert(0 && "Unknown mount information type.");
					break;
			}
		}
	}
	return 0;
}

int
traverse_mount_points(mptraverser client, void *arg)
{
	/* Cached mount entries, updated only when /etc/mtab changes. */
	static filemon_t mtab_mon;
	static struct mntent *entries;
	static unsigned int nentries;

	filemon_t mon;
	unsigned int i;

	/* Check for cache validity. */
	if(filemon_from_file("/etc/mtab", &mon) != 0 ||
			!filemon_equal(&mon, &mtab_mon))
	{
		filemon_assign(&mtab_mon, &mon);
		free_mnt_entries(entries, nentries);
		entries = read_mnt_entries(&nentries);
	}

	if(nentries == 0U)
	{
		return 1;
	}

	for(i = 0; i < nentries; ++i)
	{
		client(&entries[i], arg);
	}

	return 0;
}

/* Frees array of mount entries. */
static void
free_mnt_entries(struct mntent *entries, unsigned int nentries)
{
	unsigned int i;

	for(i = 0; i < nentries; ++i)
	{
		free_mnt_entry(&entries[i]);
	}

	free(entries);
}

/* Reads in array of mount entries.  Always sets *nentries.  Returns the array,
 * which might be NULL if empty.  On memory allocation error, skips entries. */
struct mntent *
read_mnt_entries(unsigned int *nentries)
{
	FILE *f;
	struct mntent *entries = NULL;
	struct mntent *ent;

	*nentries = 0U;

	if((f = setmntent("/etc/mtab", "r")) == NULL)
	{
		return NULL;
	}

	while((ent = getmntent(f)) != NULL)
	{
		void *p = realloc(entries, sizeof(*entries)*(*nentries + 1));
		if(p != NULL)
		{
			entries = p;
			if(clone_mnt_entry(&entries[*nentries], ent) == 0)
			{
				++*nentries;
			}
		}
	}

	endmntent(f);

	return entries;
}

/* Clones *rhs mount entry into *lhs, which is assumed to do not contain data.
 * Returns zero on success, otherwise non-zero is returned. */
static int
clone_mnt_entry(struct mntent *lhs, const struct mntent *rhs)
{
	lhs->mnt_fsname = strdup(rhs->mnt_fsname);
	lhs->mnt_dir = strdup(rhs->mnt_dir);
	lhs->mnt_type = strdup(rhs->mnt_type);
	lhs->mnt_opts = strdup(rhs->mnt_opts);
	lhs->mnt_freq = rhs->mnt_freq;
	lhs->mnt_passno = rhs->mnt_passno;

	if(lhs->mnt_fsname == NULL || lhs->mnt_dir == NULL || lhs->mnt_type == NULL ||
			lhs->mnt_opts == NULL)
	{
		free_mnt_entry(lhs);
		return 1;
	}

	return 0;
}

/* Frees single mount entry. */
static void
free_mnt_entry(struct mntent *entry)
{
	free(entry->mnt_fsname);
	free(entry->mnt_dir);
	free(entry->mnt_type);
	free(entry->mnt_opts);
}

/* Checks that the str has at least one of comma separated list (the list) items
 * as a prefix.  Returns non-zero if so, otherwise zero is returned. */
static int
starts_with_list_item(const char str[], const char list[])
{
	char *const list_copy = strdup(list);

	char *prefix = list_copy, *state = NULL;
	while((prefix = split_and_get(prefix, ',', &state)) != NULL)
	{
		if(starts_with(str, prefix))
		{
			break;
		}
	}

	free(list_copy);

	return prefix != NULL;
}

/* Finds such elemenent of comma separated list of paths (the list) that the
 * path is prefixed with it.  Returns the index or -1 on failure. */
static int
find_path_prefix_index(const char path[], const char list[])
{
	char *const list_copy = strdup(list);

	char *prefix = list_copy, *state = NULL;
	int i = 0;
	while((prefix = split_and_get(prefix, ',', &state)) != NULL)
	{
		if(path_starts_with(path, prefix))
		{
			break;
		}
		++i;
	}

	free(list_copy);

	return (prefix == NULL) ? -1 : i;
}

unsigned int
get_pid(void)
{
	return getpid();
}

int
get_uid(const char user[], uid_t *uid)
{
	if(isdigit(user[0]))
	{
		*uid = atoi(user);
	}
	else
	{
		struct passwd *p;

		p = getpwnam(user);
		if(p == NULL)
			return 1;

		*uid = p->pw_uid;
	}
	return 0;
}

int
get_gid(const char group[], gid_t *gid)
{
	if(isdigit(group[0]))
	{
		*gid = atoi(group);
	}
	else
	{
		struct group *g;

		g = getgrnam(group);
		if(g == NULL)
			return 1;

		*gid = g->gr_gid;
	}
	return 0;
}

int
S_ISEXE(mode_t mode)
{
	return ((S_IXUSR | S_IXGRP | S_IXOTH) & mode);
}

int
executable_exists(const char path[])
{
	return os_access(path, X_OK) == 0 && !is_dir(path);
}

int
get_exe_dir(char dir_buf[], size_t dir_buf_len)
{
	/* This operation isn't supported on *nix-like operating systems. */
	return 1;
}

EnvType
get_env_type(void)
{
	return ET_UNIX;
}

ExecEnvType
get_exec_env_type(void)
{
	const char *const term = env_get("TERM");
	if(term != NULL && ends_with(term, "linux"))
	{
		return EET_LINUX_NATIVE;
	}
	else
	{
		const char *const display = env_get("DISPLAY");
		return is_null_or_empty(display) ? EET_EMULATOR : EET_EMULATOR_WITH_X;
	}
}

ShellType
get_shell_type(const char shell_cmd[])
{
	return ST_NORMAL;
}

int
format_help_cmd(char cmd[], size_t cmd_size)
{
	int bg;
	char *const escaped = escape_filename(cfg.config_dir, 0);
	snprintf(cmd, cmd_size, "%s %s/" VIFM_HELP, cfg_get_vicmd(&bg), escaped);
	free(escaped);
	return bg;
}

void
display_help(const char cmd[])
{
	(void)shellout(cmd, -1, 1);
}

int
update_dir_mtime(FileView *view)
{
	return filemon_from_file(view->curr_dir, &view->mon);
}

void
wait_for_signal(void)
{
	pause();
}

void
stop_process(void)
{
	void (*saved_stp_sig_handler)(int) = signal(SIGTSTP, SIG_DFL);
	kill(0, SIGTSTP);
	signal(SIGTSTP, saved_stp_sig_handler);
}

void
update_terminal_settings(void)
{
	/* Do nothing. */
}

void
get_uid_string(const dir_entry_t *entry, int as_num, size_t buf_len, char buf[])
{
	/* Cache for the last requested user id. */
	static uid_t last_uid = (uid_t)-1;
	static char uid_buf[26];

	if(entry->uid != last_uid)
	{
		char buf[sysconf(_SC_GETPW_R_SIZE_MAX) + 1];
		struct passwd pwd_b;
		struct passwd *pwd_buf;

		last_uid = entry->uid;

		if(as_num ||
				getpwuid_r(last_uid, &pwd_b, buf, sizeof(buf), &pwd_buf) != 0 ||
				pwd_buf == NULL)
		{
			snprintf(uid_buf, sizeof(uid_buf), "%d", (int)last_uid);
		}
		else
		{
			copy_str(uid_buf, sizeof(uid_buf), pwd_buf->pw_name);
		}
	}

	copy_str(buf, buf_len, uid_buf);
}

void
get_gid_string(const dir_entry_t *entry, int as_num, size_t buf_len, char buf[])
{
	/* Cache for the last requested group id. */
	static gid_t last_gid = (gid_t)-1;
	static char gid_buf[26];

	if(entry->gid != last_gid)
	{
		char buf[sysconf(_SC_GETGR_R_SIZE_MAX) + 1];
		struct group group_b;
		struct group *group_buf;

		last_gid = entry->gid;

		if(as_num ||
				getgrgid_r(last_gid, &group_b, buf, sizeof(buf), &group_buf) != 0 ||
				group_buf == NULL)
		{
			snprintf(gid_buf, sizeof(gid_buf), "%d", (int)last_gid);
		}
		else
		{
			copy_str(gid_buf, sizeof(gid_buf), group_buf->gr_name);
		}
	}

	copy_str(buf, buf_len, gid_buf);
}

FILE *
reopen_terminal(void)
{
	FILE *fp;
	int outfd, ttyfd;

	outfd = dup(STDOUT_FILENO);
	if(outfd == -1)
	{
		fprintf(stderr, "Failed to store original output stream.");
		return NULL;
	}

	fp = fdopen(outfd, "w");
	if(fp == NULL)
	{
		fprintf(stderr, "Failed to open original output stream.");
		return NULL;
	}

	ttyfd = open("/dev/tty", O_WRONLY);
	if(ttyfd == -1)
	{
		fclose(fp);
		fprintf(stderr, "Failed to open terminal for output.");
		return NULL;
	}
	if(dup2(ttyfd, STDOUT_FILENO) == -1)
	{
		fclose(fp);
		fprintf(stderr, "Failed to setup terminal as standard output stream.");
		return NULL;
	}

	return fp;
}

FILE *
read_cmd_output(const char cmd[])
{
	FILE *fp;
	pid_t pid;
	int out_pipe[2];

	if(pipe(out_pipe) != 0)
	{
		return NULL;
	}

	pid = fork();
	if(pid == (pid_t)-1)
	{
		return NULL;
	}

	if(pid == 0)
	{
		run_from_fork(out_pipe, 0, (char *)cmd);
		return NULL;
	}

	/* Close write end of pipe. */
	close(out_pipe[1]);

	fp = fdopen(out_pipe[0], "r");
	if(fp == NULL)
	{
		close(out_pipe[0]);
	}
	return fp;
}

const char *
get_installed_data_dir(void)
{
	return PACKAGE_DATA_DIR;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
