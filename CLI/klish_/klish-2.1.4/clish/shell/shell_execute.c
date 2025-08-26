/*
 * shell_execute.c
 */
#include "private.h"
#include "lub/string.h"
#include "lub/argv.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>

#define CONFIG_VIEW        "configure-view"

static const clish_parg_t* clish_shell__get_parg(const clish_context_t *context);

int get_index(char *str)
{
        int i = 0;
        int len = 0;
        len = strlen(str);
        while (i < len)
        {
                if (isdigit(*(str+i)))
                        break;
                i++;
        }
        return i;
}

/*-------------------------------------------------------- */
const clish_parg_t* clish_shell__get_parg(const clish_context_t *context)
{
        const clish_pargv_t *pargv;
        if (context) {
                if ((pargv = clish_context__get_pargv(context))) {
                        int i;
                        clish_ptype_t *ptype = NULL;
                        clish_parg_t *parg = NULL;

                        for (i = 0; i < clish_pargv__get_count((clish_pargv_t *)pargv); i++) {
                                parg = clish_pargv__get_parg((clish_pargv_t *)pargv, i);
                                if (parg) {
                                        /*
                                         *  Return the parg only for CLISH_PTYPE_REGEXP_SELECT ptype
                                         */
                                        ptype = (clish_ptype_t *)clish_parg__get_ptype(parg);
                                        if (CLISH_PTYPE_METHOD_REGEXP_SELECT ==
                                                        clish_ptype__get_method(ptype)) {
                                                return parg;
                                        }
                                }
                        }
                }
        }
        return NULL;
}

/* Empty signal handler to ignore signal but don't use SIG_IGN. */
static void sigignore(int signo)
{
	signo = signo; /* Happy compiler */
	return;
}

/*-------------------------------------------------------- */
static int clish_shell_lock(const char *lock_path)
{
	int i;
	int res = -1;
	int lock_fd = -1;
	struct flock lock;

	if (!lock_path)
		return -1;
	lock_fd = open(lock_path, O_WRONLY | O_CREAT, 00644);
	if (-1 == lock_fd) {
		fprintf(stderr, "Warning: Can't open lockfile %s.\n", lock_path);
		return -1;
	}
#ifdef FD_CLOEXEC
	fcntl(lock_fd, F_SETFD, fcntl(lock_fd, F_GETFD) | FD_CLOEXEC);
#endif
	memset(&lock, 0, sizeof(lock));
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	for (i = 0; i < CLISH_LOCK_WAIT; i++) {
		res = fcntl(lock_fd, F_SETLK, &lock);
		if (res != -1)
			break;
		if (EINTR == errno)
			continue;
		if ((EAGAIN == errno) || (EACCES == errno)) {
			if (0 == i)
				fprintf(stderr,
					"Warning: Try to get lock. Please wait...\n");
			sleep(1);
			continue;
		}
		if (EINVAL == errno)
			fprintf(stderr, "Error: Locking isn't supported by OS, consider \"--lockless\".\n");
		break;
	}
	if (res == -1) {
		fprintf(stderr, "Error: Can't get lock.\n");
		close(lock_fd);
		return -1;
	}
	return lock_fd;
}

/*-------------------------------------------------------- */
static void clish_shell_unlock(int lock_fd)
{
	struct flock lock;

	if (lock_fd == -1)
		return;
	memset(&lock, 0, sizeof(lock));
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	fcntl(lock_fd, F_SETLK, &lock);
	close(lock_fd);
}

/*----------------------------------------------------------- */
int clish_shell_execute(clish_context_t *context, char **out)
{
	clish_shell_t *this = clish_context__get_shell(context);
	const clish_command_t *cmd = clish_context__get_cmd(context);
	int result = 0;
	const char *lock_path = clish_shell__get_lockfile(this);
	int lock_fd = -1;
	clish_view_t *cur_view = clish_shell__get_view(this);
	unsigned int saved_wdog_timeout = this->wdog_timeout;

	assert(cmd);

	/* Pre-change view if the command is from another depth/view */
	{
		clish_view_restore_e restore = clish_command__get_restore(cmd);
		if ((CLISH_RESTORE_VIEW == restore) &&
			(clish_command__get_pview(cmd) != cur_view)) {
			clish_view_t *view = clish_command__get_pview(cmd);
			clish_shell__set_pwd(this, NULL, view, NULL, context);
		} else if ((CLISH_RESTORE_DEPTH == restore) &&
			(clish_command__get_depth(cmd) < this->depth)) {
			this->depth = clish_command__get_depth(cmd);
		}
	}

	/* Lock the lockfile */
	if (lock_path && clish_action__get_lock(clish_command__get_action(cmd))) {
		lock_fd = clish_shell_lock(lock_path);
		if (-1 == lock_fd) {
			result = -1;
			goto error; /* Can't set lock */
		}
	}

	/* Execute ACTION */
	clish_context__set_action(context, clish_command__get_action(cmd));
	result = clish_shell_exec_action(context, out);

	/* Call config callback */
	if (!result)
		clish_shell_exec_config(context);

	/* Call logging callback */
	if (clish_shell__get_log(this) &&
		clish_shell_check_hook(context, CLISH_SYM_TYPE_LOG)) {
		char *full_line = clish_shell__get_full_line(context);
		clish_shell_exec_log(context, full_line, result);
		lub_string_free(full_line);
	}

	if (clish_shell__get_canon_out(this) &&
		!clish_command__get_internal(cmd)) {
		char *space = NULL;
		char *full_line = clish_shell__get_full_line(context);
		if (this->depth > 0) {
			space = malloc(this->depth + 1);
			memset(space, ' ', this->depth);
			space[this->depth] = '\0';
		}
		printf("%s%s\n", space ? space : "", full_line);
		lub_string_free(full_line);
		if (space)
			free(space);
	}

	/* Unlock the lockfile */
	if (lock_fd != -1)
		clish_shell_unlock(lock_fd);

	/* Move into the new view */
        if (!result) {
                int cnt, i;
                char *viewname;
                char *configureviewname = CONFIG_VIEW;
                char *exec_view_name = "enable-view";
                const char *cmdview, *cmdviewid, *paramview = NULL, *paramviewid = NULL;
                const clish_param_t *param;
                clish_pargv_t *pargv = clish_context__get_pargv(context);
                const char *cur_cmd = NULL;
                clish_view_t *parentview = clish_command__get_pview(cmd);

                /* Check whether view and view id attributes are there in PARAM
                 * If so, use the one from the PARAM. If not, use the COMMAND attributes
                 */
                cmdview= clish_command__get_viewname(cmd);
                cmdviewid = clish_command__get_viewid(cmd);
                cur_cmd = clish_command__get_name(cmd);
                cnt = clish_pargv__get_count(pargv);
                for (i = 0; i < cnt; i++) {
                        const char *tempview, *tempviewid;
                        param = clish_pargv__get_param(pargv, i);
                        tempview   = clish_param__get_viewname(param);
                        tempviewid = clish_param__get_viewid(param);
                        if(tempview) {
                                paramview = tempview;
                        }
                        if(tempviewid) {
                                paramviewid = tempviewid;
                        }
                }

                if(paramview) {
                        cmdview= paramview;
                }
                if(paramviewid) {
                        cmdviewid = paramviewid;
                }

                viewname = clish_shell_expand(cmdview, SHELL_VAR_NONE, context);

                if (viewname) {
                        /* Search for the view */
                        clish_view_t *view = clish_shell_find_view(this, viewname);
                        if (!view)
                                fprintf(stderr, "System error: Can't "
                                        "change view to %s\n", viewname);
                        lub_string_free(viewname);

                        /* Save the PWD */
                        if (view) {
                                char *line = clish_shell__get_line(context);
                                clish_shell__set_pwd(this, line, view, (char*)cmdviewid, context);
                                lub_string_free(line);
                        }
                }
        }
   	
	/* Set appropriate timeout. Workaround: Don't turn on  watchdog
	on the "set watchdog <timeout>" command itself. */
	if (this->wdog_timeout && saved_wdog_timeout) {
		tinyrl__set_timeout(this->tinyrl, this->wdog_timeout);
		this->wdog_active = BOOL_TRUE;
		fprintf(stderr, "Warning: The watchdog is active. Timeout is %u "
			"seconds.\nWarning: Press any key to stop watchdog.\n",
			this->wdog_timeout);
	} else
		tinyrl__set_timeout(this->tinyrl, this->idle_timeout);

error:
	return result;
}

/*----------------------------------------------------------- */
/* Execute oaction. It suppose the forked process to get
 * script's stdout. Then forked process write the output back
 * to klish.
 */
static int clish_shell_exec_oaction(clish_hook_oaction_fn_t func,
	void *context, const char *script, char **out)
{
	int result = -1;
	int real_stdout; /* Saved stdout handler */
	int pipe1[2], pipe2[2];
	pid_t cpid = -1;
	konf_buf_t *buf;

	if (pipe(pipe1))
		return -1;
	if (pipe(pipe2))
		goto stdout_error;

	/* Create process to read script's stdout */
	cpid = fork();
	if (cpid == -1) {
		fprintf(stderr, "Error: Can't fork the stdout-grabber process.\n"
			"Error: The ACTION will be not executed.\n");
		goto stdout_error;
	}

	/* Child: read action's stdout */
	if (cpid == 0) {
		lub_list_t *l;
		lub_list_node_t *node;
		struct iovec *iov;
		const int rsize = CLISH_STDOUT_CHUNK; /* Read chunk size */
		size_t cur_size = 0;
		ssize_t r = 0;

		close(pipe1[1]);
		close(pipe2[0]);
		l = lub_list_new(NULL, NULL);

		/* Read the result of script execution */
		while (1) {
			ssize_t ret;
			iov = malloc(sizeof(*iov));
			iov->iov_len = rsize;
			iov->iov_base = malloc(iov->iov_len);
			do {
				ret = readv(pipe1[0], iov, 1);
			} while ((ret < 0) && (errno == EINTR));
			if (ret <= 0) { /* Error or EOF */
				free(iov->iov_base);
				free(iov);
				break;
			}
			iov->iov_len = ret;
			lub_list_add(l, iov);
			/* Check the max size of buffer */
			cur_size += ret;
			if (cur_size >= CLISH_STDOUT_MAXBUF)
				break;
		}
		close(pipe1[0]);

		/* Write the result of script back to klish */
		while ((node = lub_list__get_head(l))) {
			iov = lub_list_node__get_data(node);
			lub_list_del(l, node);
			lub_list_node_free(node);
			r = write(pipe2[1], iov->iov_base, iov->iov_len);
			free(iov->iov_base);
			free(iov);
		}
		close(pipe2[1]);

		lub_list_free(l);
		_exit(r < 0 ? 1 : 0);
	}

	real_stdout = dup(STDOUT_FILENO);
	dup2(pipe1[1], STDOUT_FILENO);
	close(pipe1[0]);
	close(pipe1[1]);
	close(pipe2[1]);

	result = func(context, script);

	/* Restore real stdout */
	dup2(real_stdout, STDOUT_FILENO);
	close(real_stdout);
	/* Read the result of script execution */
	buf = konf_buf_new(pipe2[0]);
	while (konf_buf_read(buf) > 0);
	*out = konf_buf__dup_line(buf);
	konf_buf_delete(buf);
	close(pipe2[0]);
	/* Wait for the stdout-grabber process */
	while (waitpid(cpid, NULL, 0) != cpid);

	return result;

stdout_error:
	close(pipe1[0]);
	close(pipe1[1]);
	return -1;
}

static int clish_shell_exec_sym_api(const clish_sym_t *sym, clish_hook_action_fn_t *func,
	       				clish_context_t *context, char *script, char **out)	
{
	int result = -1;
	/* CLISH_SYM_API_SIMPLE */
	if (clish_sym__get_api(sym) == CLISH_SYM_API_SIMPLE) {
		result = ((clish_hook_action_fn_t *)func)(context, script, out);
	/* CLISH_SYM_API_STDOUT and output is not needed */
	} else if ((clish_sym__get_api(sym) == CLISH_SYM_API_STDOUT) && (!out)) {
		result = ((clish_hook_oaction_fn_t *)func)(context, script);
	/* CLISH_SYM_API_STDOUT and outpus is needed */
	} else if (clish_sym__get_api(sym) == CLISH_SYM_API_STDOUT) {
		result = clish_shell_exec_oaction((clish_hook_oaction_fn_t *)func,
							context, script, out);
	}
	return result;
}

/*----------------------------------------------------------- */
int clish_shell_exec_action(clish_context_t *context, char **out)
{
	int result = -1;
	const clish_sym_t *sym;
	char *script;
	clish_hook_action_fn_t *func = NULL;

	const clish_action_t *action = clish_context__get_action(context);
	clish_shell_t *shell = clish_context__get_shell(context);
	clish_parg_t *parg = NULL;
        clish_ptype_t *ptype = NULL;
        clish_ptype_method_e method = CLISH_PTYPE_METHOD_REGEXP;

	bool_t intr = clish_action__get_interrupt(action);
	/* Signal vars */
	struct sigaction old_sigint, old_sigquit, old_sighup;
	struct sigaction sa;
	sigset_t old_sigs;

	if (!(sym = clish_action__get_builtin(action)))
		return 0;
	if (shell->dryrun && !clish_sym__get_permanent(sym))
		return 0;
	if (!(func = clish_sym__get_func(sym))) {
		fprintf(stderr, "Error: Default ACTION symbol is not specified.\n");
		return -1;
	}
	script = clish_shell_expand(clish_action__get_script(action), SHELL_VAR_ACTION, context);

	/* Ignore and block SIGINT, SIGQUIT, SIGHUP.
	 * The SIG_IGN is not a case because it will be inherited
	 * while a fork(). It's necessary to ignore signals because
	 * the klish itself and ACTION script share the same terminal.
	 */
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigignore; /* Empty signal handler */
	sigaction(SIGINT, &sa, &old_sigint);
	sigaction(SIGQUIT, &sa, &old_sigquit);
	sigaction(SIGHUP, &sa, &old_sighup);
	/* Block signals for children processes. The block state is inherited. */
	if (!intr) {
		sigset_t sigs;
		sigemptyset(&sigs);
		sigaddset(&sigs, SIGINT);
		sigaddset(&sigs, SIGQUIT);
		sigaddset(&sigs, SIGHUP);
		sigprocmask(SIG_BLOCK, &sigs, &old_sigs);
	}

	parg = (clish_parg_t*)clish_shell__get_parg(context);
	if (!parg || !(ptype = (clish_ptype_t *)clish_parg__get_ptype(parg)))
	{
		result = clish_shell_exec_sym_api(sym, func, context, script, out);
	} else {
                method = clish_ptype__get_method(ptype);
                if (method == CLISH_PTYPE_METHOD_REGEXP_SELECT)
                {
                        /* interface CLISH_PTYPE_REGEXP_SELECT type handling */
                        char *res = NULL;
                        char *ptr = NULL;
                        int index = -1, j;
                        bool_t matched = BOOL_FALSE;
                        bool_t isEthernet = BOOL_FALSE;
                        char *new_result = NULL;
                        char *name = NULL;
                        char *value = NULL;

                        lub_argv_t *pargv = clish_ptype_regexp_select__get_argv(ptype);
                        res = lub_string_dup(clish_parg__get_value(parg));
                        /* Loop through possible help string options,
                         * such as ethernet, vlan and portchannel
                         * if given CLI matches first two character
                         * like po 10 then start at array index 3 and
                         * skip if any space lies between po and 10.
                         * If given CLI is like p 10 then start at
			                          * array index 2 and skip if any space lies
                         * between p and 10.
                         */
                        for (j = 0; j < lub_argv__get_count(pargv); j++) {
                                /* Overwriting ptr name while looping
                                 * will leak dynamic memory that name
                                 * points to. So free memory that name points to
                                 * before overwriting.
                                 */
                                if(name != NULL) {
                                        lub_string_free(name);
                                        name = NULL;
                                }
                                if(value != NULL) {
                                        lub_string_free(value);
                                        value = NULL;
                                }
                                name = clish_ptype_regexp_select__get_argname(ptype, j);
                                value = clish_ptype_regexp_select__get_value(ptype, j);
                                if (name && strncasecmp(name, res, strlen(name))) {
                                        index = get_index(res);
                                        if (!strncasecmp(name, res, index)) {
                                                ptr = res + index;
                                                matched = BOOL_TRUE;
                                        }
                                        if (matched == BOOL_TRUE) {
                                                while (isspace(*ptr))
                                                        ptr++;
                                                new_result = lub_string_dup(name);
                                                lub_string_cat(&new_result, ptr);
                                                lub_string_free(res);
                                                res = new_result;
                                                break;
                                        }
				} else {
                                        /* CLI is given matches with complete help string
                                         * such as ethernet, vlan, portchannel, so nothing
                                         * do to here.
                                         */
                                        if (name)
                                                ptr = res + strlen(name);
                                        else
                                                ptr = res + get_index(res);
                                        matched = BOOL_TRUE;
                                        break;
                                }
                        }
                        if (matched == BOOL_FALSE) {
                                lub_string_free(res);
                                res = NULL;
                        }

			result = clish_shell_exec_sym_api(sym, func, context, script, out);
                        isEthernet = BOOL_FALSE;
                        lub_string_free(res);

            		/* Free memory to avoid resource leak */
            		if(name != NULL) {
                		lub_string_free(name);
                		name = NULL;
            		}
            		if(value != NULL) {
                		lub_string_free(value);
                		value = NULL;
            		}
                } else {
			result = clish_shell_exec_sym_api(sym, func, context, script, out);
                }
        }

	/* Restore SIGINT, SIGQUIT, SIGHUP */
	if (!intr) {
		sigprocmask(SIG_SETMASK, &old_sigs, NULL);
		/* Is the signals delivery guaranteed here (before
		   sigaction restore) for previously blocked and
		   pending signals? The simple test is working well.
		   I don't want to use sigtimedwait() function because
		   it needs a realtime extensions. The sigpending() with
		   the sleep() is not nice too. Report bug if clish will
		   get the SIGINT after non-interruptable action.
		*/
	}
	sigaction(SIGINT, &old_sigint, NULL);
	sigaction(SIGQUIT, &old_sigquit, NULL);
	sigaction(SIGHUP, &old_sighup, NULL);

	if (script) lub_string_free(script);

	return result;
}

/*----------------------------------------------------------- */
const void *clish_shell_check_hook(const clish_context_t *clish_context, int type)
{
	clish_sym_t *sym;
	clish_shell_t *shell = clish_context__get_shell(clish_context);
	const void *func;

	if (!(sym = shell->hooks[type]))
		return NULL;
	if (shell->dryrun && !clish_sym__get_permanent(sym))
		return NULL;
	if (!(func = clish_sym__get_func(sym)))
		return NULL;

	return func;
}

/*----------------------------------------------------------- */
CLISH_HOOK_CONFIG(clish_shell_exec_config)
{
	clish_hook_config_fn_t *func = NULL;
	func = clish_shell_check_hook(clish_context, CLISH_SYM_TYPE_CONFIG);
	return func ? func(clish_context) : 0;
}

/*----------------------------------------------------------- */
CLISH_HOOK_LOG(clish_shell_exec_log)
{
	clish_hook_log_fn_t *func = NULL;
	func = clish_shell_check_hook(clish_context, CLISH_SYM_TYPE_LOG);
	return func ? func(clish_context, line, retcode) : 0;
}

/*----------------------------------------------------------- */
char *clish_shell_mkfifo(clish_shell_t * this, char *name, size_t n)
{
	int res;

	if (n < 1) /* Buffer too small */
		return NULL;
	do {
		strncpy(name, this->fifo_temp, n);
		name[n - 1] = '\0';
		mktemp(name);
		if (name[0] == '\0')
			return NULL;
		res = mkfifo(name, 0600);
	} while ((res < 0) && (EEXIST == errno));

	return name;
}

/*----------------------------------------------------------- */
int clish_shell_rmfifo(clish_shell_t * this, const char *name)
{
	return unlink(name);
}

CLISH_SET(shell, bool_t, log);
CLISH_GET(shell, bool_t, log);
CLISH_SET(shell, bool_t, dryrun);
CLISH_GET(shell, bool_t, dryrun);
CLISH_SET(shell, bool_t, canon_out);
CLISH_GET(shell, bool_t, canon_out);
