/*
 * shell_word_generator.c
 */
#include <string.h>

#include "private.h"
#include "lub/string.h"
#include "lub/argv.h"
#include "lub/system.h"
#include "clish/command.h"
#include "clish/ptype.h"

/*-------------------------------------------------------- */
void clish_shell_iterator_init(clish_shell_iterator_t * iter,
	clish_nspace_visibility_e field)
{
	iter->last_cmd = NULL;
	iter->field = field;
}

/*--------------------------------------------------------- */
bool_t clish_shell_command_test(const clish_command_t *cmd, void *context)
{
        char *str = NULL;
        char *teststr = NULL;
        bool_t res;

        if (!cmd)
                return BOOL_FALSE;
        teststr = clish_command__get_test(cmd);
        if (!teststr)
                return BOOL_TRUE;
        str = clish_shell_expand(teststr, SHELL_VAR_ACTION, context);
        if (!str)
                return BOOL_FALSE;
        res = lub_system_line_test(str);
        lub_string_free(str);

        return res;
}

/*--------------------------------------------------------- */
const clish_command_t *clish_shell_resolve_command(const clish_shell_t * this,
	const char *line, clish_context_t *context)
{
	        clish_command_t *cmd, *result;
        bool_t test_result = BOOL_TRUE;
        clish_context_t local_context;

        /* Search the current view */
        result = clish_view_resolve_command(clish_shell__get_view(this), line, BOOL_TRUE);
        /* Search the global view */
        cmd = clish_view_resolve_command(this->global, line, BOOL_TRUE);

        result = clish_command_choose_longest(result, cmd);

        if(result) {
                clish_context_init(&local_context, (clish_shell_t *)this);
                clish_context__set_cmd(&local_context, result);
                test_result = clish_shell_command_test(result, &local_context);
                if(test_result == BOOL_FALSE)
                        result = NULL;
        }

        return result;
}

/*--------------------------------------------------------- */
const clish_command_t *clish_shell_resolve_prefix(const clish_shell_t * this,
	const char *line)
{
	clish_command_t *cmd, *result;

	/* Search the current view */
	result = clish_view_resolve_prefix(clish_shell__get_view(this), line, BOOL_TRUE);
	/* Search the global view */
	cmd = clish_view_resolve_prefix(this->global, line, BOOL_TRUE);

	result = clish_command_choose_longest(result, cmd);

	return result;
}

/*-------------------------------------------------------- */
const clish_command_t *clish_shell_find_next_completion(const clish_shell_t *
	this, const char *line, clish_shell_iterator_t * iter)
{
	const clish_command_t *result, *cmd;

	/* ask the local view for next command */
	result = clish_view_find_next_completion(clish_shell__get_view(this),
		iter->last_cmd, line, iter->field, BOOL_TRUE);
	/* ask the global view for next command */
	cmd = clish_view_find_next_completion(this->global,
		iter->last_cmd, line, iter->field, BOOL_TRUE);

	if (clish_command_diff(result, cmd) > 0)
		result = cmd;

	if (!result)
		iter->last_cmd = NULL;
	else
		iter->last_cmd = clish_command__get_name(result);

	return result;
}

/*--------------------------------------------------------- */
void clish_shell_param_generator(clish_shell_t *this, lub_argv_t *matches,
	const clish_command_t *cmd, const char *line, unsigned offset)
{
	const char *name = clish_command__get_name(cmd);
	char *text = lub_string_dup(&line[offset]);
	clish_ptype_t *ptype;
	unsigned idx = lub_string_wordcount(name);
	/* get the index of the current parameter */
	unsigned index = lub_string_wordcount(line) - idx;
	clish_context_t context;

	if ((0 != index) || (offset && line[offset - 1] == ' ')) {
		lub_argv_t *argv = lub_argv_new(line, 0);
		clish_pargv_t *pargv = clish_pargv_new();
		clish_pargv_t *completion = clish_pargv_new();
		unsigned completion_index = 0;
		const clish_param_t *param = NULL;
		const char *penultimate_text = NULL;
		/* if there is some text for the parameter then adjust the index */
		if ((0 != index) && (text[0] != '\0')) {
                        index--;
                        /* text is the string after last space.
                           If user enters "interface ethernet"
                           "interface ethernet 1/", text will be "ethernet".
                           and "1/" respectively.The penultimate_text for these
                           cases will be string before the text which is
                           "interface" and "ethernet" respectively.
                         */
                        penultimate_text = lub_string_dup(lub_argv__get_arg(argv,
                                                lub_argv__get_count(argv)-2));
                } else {
                        /* If user enters "interface ethernet " or "interface ",
                           text will be a NULL string.The penultimate_text for
                           these cases will be the string before the last space,
                           which is "ethernet" and "interface" for the above
                           examples respectively */
                        penultimate_text = lub_string_dup(lub_argv__get_arg(argv,
                                                lub_argv__get_count(argv)-1));

			/*If a command is matched, then pressing tab without current 
			command complete, it should not show the next available param. 
			Example: Pressing tab after "show interface", shows interface,
			interface-naming, breakout and etc.
			But expected output is interface and interface-naming.(Breakout and etc are params.) 
			No need to print the next available param for "interface " nowitself. */
			if (!(lub_string_nocasecmp(line, clish_command__get_name(cmd)))) {
				idx--;
			}
                }


		/* Parse command line to get completion pargv's */
		/* Prepare context */
		clish_context_init(&context, this);
		clish_context__set_cmd(&context, cmd);
		clish_context__set_pargv(&context, pargv);

		clish_shell_parse_pargv(pargv, cmd, &context,
			clish_command__get_paramv(cmd),
			argv, &idx, completion, index + idx, NULL, NULL);
		lub_argv_delete(argv);

		while ((param = clish_pargv__get_param(completion,
			completion_index++))) {
			const char *result = NULL;
			/* The param is args so it has no completion */
			if (param == clish_command__get_args(cmd))
				continue;
			/* The switch has no completion string */
			if (CLISH_PARAM_SWITCH == clish_param__get_mode(param))
				continue;
			if (BOOL_TRUE == clish_param__get_hidden(param))
				continue;
			/* The subcommand is identified by it's value */
			if (CLISH_PARAM_SUBCOMMAND ==
				clish_param__get_mode(param)) {

				const clish_ptype_t *ptype = clish_param__get_ptype(param);
                		if (CLISH_PTYPE_METHOD_REGEXP_SELECT != 
						clish_ptype__get_method(ptype))
					result = clish_param__get_value(param);
				if (result)
					lub_argv_add(matches, result);
			}
			/* The 'completion' field of PARAM */
			if (clish_param__get_completion(param)) {
				char *str, *q;
				char *saveptr = NULL;
				str = clish_shell_expand(
					clish_param__get_completion(param), SHELL_VAR_ACTION, &context);
				if (str) {
					for (q = strtok_r(str, " \n", &saveptr);
						q; q = strtok_r(NULL, " \n", &saveptr)) {
						if (q == strstr(q, text))
							lub_argv_add(matches, q);
					}
					lub_string_free(str);
				}
			}
			/* The common PARAM. Let ptype do the work */
			if ((ptype = clish_param__get_ptype(param))) {
                		if (CLISH_PTYPE_METHOD_REGEXP_SELECT != clish_ptype__get_method(ptype))
                                	clish_ptype_word_generator(ptype, matches, text, NULL);
                		else {
                                	clish_ptype_word_generator(ptype, matches, text, penultimate_text);
                		}

			}
		}
		clish_pargv_delete(completion);
		clish_pargv_delete(pargv);
		if(penultimate_text)
                        lub_string_free((char *)penultimate_text);

	}

	lub_string_free(text);
}

/*--------------------------------------------------------- */
