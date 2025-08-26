/*
 * shell_help.c
 */
#include "private.h"
#include "clish/types.h"
#include "lub/string.h"
//#include "clish/plugin/clish_api.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/*--------------------------------------------------------- */
/*
 * Provide a detailed list of the possible command completions
 */
static void available_commands(clish_shell_t *this,
	clish_help_t *help, const char *line, size_t *max_width,
	clish_context_t *context)
{
	const clish_command_t *cmd;
	clish_shell_iterator_t iter;
	clish_context_t local_context;

	if (max_width)
		*max_width = 0;
	/* Search for COMMAND completions */
	clish_shell_iterator_init(&iter, CLISH_NSPACE_HELP);
	clish_context_init(&local_context, this);
	while ((cmd = clish_shell_find_next_completion(this, line, &iter))) {
		clish_context__set_cmd(&local_context, cmd);
		if(clish_shell_command_test(cmd, &local_context) == BOOL_FALSE)
			continue;

		if (clish_command__get_hidden(cmd) == BOOL_TRUE)
                        continue;
		if (clish_command__get_enabled(cmd) == BOOL_FALSE)
                        continue;
		size_t width;
		const char *name = clish_command__get_suffix(cmd);
		if (max_width) {
			width = strlen(name);
			if (width > *max_width)
				*max_width = width;
		}
		lub_argv_add(help->name, name);
		lub_argv_add(help->help, clish_command__get_text(cmd));
		lub_argv_add(help->detail, clish_command__get_detail(cmd));
	}
}

/*--------------------------------------------------------- */
static int available_params(clish_shell_t *this,
	clish_help_t *help, const clish_command_t *cmd,
	const char *line, size_t *max_width)
{
	unsigned index = lub_string_wordcount(line);
	unsigned idx = lub_string_wordcount(clish_command__get_name(cmd));
	lub_argv_t *argv;
	clish_pargv_t *completion, *pargv;
	unsigned i;
	unsigned cnt = 0;
	clish_pargv_status_t status = CLISH_LINE_OK;
	clish_context_t context;

	/* Empty line */
	if (0 == index)
		return -1;

	if (line[strlen(line) - 1] != ' ')
		index--;

	argv = lub_argv_new(line, 0);

	/* get the parameter definition */
	completion = clish_pargv_new();
	pargv = clish_pargv_new();

	/* Prepare context */
	clish_context_init(&context, this);
	clish_context__set_cmd(&context, cmd);
	clish_context__set_pargv(&context, pargv);

	status = clish_shell_parse_pargv(pargv, cmd, &context,
		clish_command__get_paramv(cmd),
		argv, &idx, completion, index, NULL, NULL);
	clish_pargv_delete(pargv);
	cnt = clish_pargv__get_count(completion);

	/* Calculate the longest name */
	for (i = 0; i < cnt; i++) {
		const clish_param_t *param;
		const clish_parg_t *parg;
		const char *name;
		unsigned clen = 0;

		param = clish_pargv__get_param(completion, i);
		if (clish_param__get_hidden(param) == BOOL_TRUE)
			continue;

		if (clish_param__get_enabled(param) == BOOL_FALSE)
			continue;

		parg = clish_pargv__get_parg(completion, i);
		if (CLISH_PARAM_SUBCOMMAND == clish_param__get_mode(param))
			name = clish_param__get_value(param);
		else
			name = clish_ptype__get_text(clish_param__get_ptype(param));
		if (name)
			clen = strlen(name);
		if (max_width && (clen > *max_width))
			*max_width = clen;
		clish_param_help(param, help, clish_parg__get_value((parg)));
	}
	clish_pargv_delete(completion);
	lub_argv_delete(argv);

	/* It's a completed command */
	if (CLISH_LINE_OK == status)
		return 0;

	/* Incompleted command */
	return -1;
}

/*--------------------------------------------------------- */
void sort_help_command (const lub_argv_t *name , const lub_argv_t *help, int complete_status) {
        int count = 0;
        int indexi,indexj;
        char * temp = NULL;
        const char *str1 = NULL;
        const char *str2 = NULL;

        count = lub_argv__get_count(name);
        /* if the last element is <cr>, then don't sort that the last element */
        if (!complete_status) {
                count = count - 1;
        }

        for (indexi=0;indexi<count-1;indexi++) {
                for (indexj=indexi+1;indexj<count;indexj++) {
                        str1 = lub_argv__get_arg(name, indexi);
                        str2 = lub_argv__get_arg(name, indexj);

                        if ((str1 == NULL) || (str2 == NULL))
                                continue;
                        /* check two strings. if compared string is lowest letter, then swap */
                        if(lub_string_nocasecmp(str1,str2) > 0) {
                                /*swap the name to sort */
                                lub_argv__swap_arg(name,indexi,indexj);
                                /*swap the corresponding help string of command */
                                lub_argv__swap_arg(help,indexi,indexj);
                        }
                }
        }

}

/*--------------------------------------------------------- */
void clish_shell_help(clish_shell_t *this, const char *line, clish_context_t *context)
{
	clish_help_t help;
	size_t max_width = 0;
	const clish_command_t *cmd;
	unsigned int i;
	int complete_status = 0;

	help.name = lub_argv_new(NULL, 0);
	help.help = lub_argv_new(NULL, 0);
	help.detail = lub_argv_new(NULL, 0);

	/* Get COMMAND completions */
	available_commands(this, &help, line, &max_width, context);

	/* Resolve a command */
	cmd = clish_shell_resolve_command(this, line, context);
	/* Search for PARAM completion */
	if (cmd) {
		size_t width = 0;
		complete_status = available_params(this, &help, cmd, line, &width);
		if (width > max_width)
			max_width = width;
		/* Add <cr> if command is completed */
		if (!complete_status) {
			lub_argv_add(help.name, "<cr>");
			lub_argv_add(help.help, NULL);
			lub_argv_add(help.detail, NULL);
		}
	}
	if (lub_argv__get_count(help.name) == 0)
		goto end;
    
	for (i = 0; i < lub_argv__get_count(help.name); i++) {
		if(max_width < strlen(lub_argv__get_arg(help.name, i)))
			max_width = strlen(lub_argv__get_arg(help.name, i));
	}

	/* Sort the help command name and is help strings */
	sort_help_command(help.name, help.help, complete_status);
	/* Print help messages */
	for (i = 0; i < lub_argv__get_count(help.name); i++) {
		if(max_width < strlen(lub_argv__get_arg(help.name, i)))
			max_width = strlen(lub_argv__get_arg(help.name, i));
		fprintf(stderr, "  %-*s  %-s\n", (int)max_width,
			lub_argv__get_arg(help.name, i),
			lub_argv__get_arg(help.help, i) ?
			lub_argv__get_arg(help.help, i) : "");
	}

	/* Print details */
	if ((lub_argv__get_count(help.name) == 1) &&
		(SHELL_STATE_HELPING == this->state)) {
		const char *detail = lub_argv__get_arg(help.detail, 0);
		if (detail)
			fprintf(stderr, "%s\n", detail);
	}

	/* update the state */
	if (this->state == SHELL_STATE_HELPING)
		this->state = SHELL_STATE_OK;
	else
		this->state = SHELL_STATE_HELPING;

end:
	lub_argv_delete(help.name);
	lub_argv_delete(help.help);
	lub_argv_delete(help.detail);
}

/*--------------------------------------------------------- */
