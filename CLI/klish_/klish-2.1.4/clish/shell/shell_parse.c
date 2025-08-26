/*
 * shell_parse.c
 */

#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "lub/string.h"
#include "lub/system.h"
#include "private.h"
#include "clish/pargv.h"

/*----------------------------------------------------------- */
clish_pargv_status_e clish_shell_parse(
	clish_shell_t *this, const char *line,
	const clish_command_t **ret_cmd, clish_pargv_t **pargv,
	clish_context_t *orig_context, unsigned *err_len)
{
	clish_pargv_status_e result = CLISH_BAD_CMD;
	clish_context_t context;
	const clish_command_t *cmd;
	lub_argv_t *argv = NULL;
	unsigned int idx;
        unsigned int errArg = 0;/* find the error param*/
        unsigned int strMatchLen =0; /*find the exact position of error*/

	*ret_cmd = cmd = clish_shell_resolve_command(this, line, orig_context);
	if (!cmd)
		return result;

	/* Now construct the parameters for the command */
	/* Prepare context */
	*pargv = clish_pargv_new();
	clish_context_init(&context, this);
	clish_context__set_cmd(&context, cmd);
	clish_context__set_pargv(&context, *pargv);

	idx = lub_string_wordcount(clish_command__get_name(cmd));
	argv = lub_argv_new(line, 0);
	result = clish_shell_parse_pargv(*pargv, cmd, &context,
		clish_command__get_paramv(cmd),
		argv, &idx, NULL, 0, &errArg, &strMatchLen);
        
        /*find the error param exact character*/
        if ((CLISH_BAD_PARAM == result) || (CLISH_BAD_CMD == result)) {
            int argcnt = lub_argv__get_count(argv);
            int index =0;
            int arglen = 0;
            for (index=0;(index<errArg) && (errArg<argcnt);index++) 
            {
                const char *argval = lub_argv__get_arg(argv,index);
                if( argval) {
               	    arglen += strlen(argval);
                        arglen ++;
                }
            }
            if(err_len && arglen)
                *err_len = arglen + strMatchLen;	
        }

	lub_argv_delete(argv);
	if (CLISH_LINE_OK != result) {
		clish_pargv_delete(*pargv);
		*pargv = NULL;
	}

	return result;
}

/*--------------------------------------------------------- */
static bool_t is_valid_regexp_select_iteration(const clish_param_t *param,
        unsigned idx, unsigned need_index, unsigned is_switch, const char* arg)
{
	clish_ptype_t *ptype = clish_param__get_ptype(param);
	bool_t res = BOOL_FALSE;

	/* 
	 * This is the case where user enter interface vlan ? to get help
	 * option for the CLISH_PTYPE_METHOD_REGEXP_SELECT ptype.
	 */
	if (((idx + 1 == need_index) && 
		CLISH_PTYPE_METHOD_REGEXP_SELECT == clish_ptype__get_method(ptype))) {
		res = BOOL_TRUE;
	} else if ((idx + 1 == need_index) && is_switch) {
		/* 
		 * When current PARAM is switch, check if we have a sub PARAM with method
		 * CLISH_PTYPE_METHOD_REGEXP_SELECT inside the switch.If so, check if the current
		 * matches the regexp_select pattern.If it matches we return true from this
		 * function, indicating the caller to include the REGEXP_SELECT PARAM
		 * to the available_param for help.Otherwise return false.
		 * For CLI like "management route 1.1.1.0/24", we have a switch with a PARAM
		 * with method REGEXP_SELECT.The switch also includes another PARAM
		 * for nexthop IP.Now if user executes "management route 1.1.1.0/24 1.1.1.1 ?",
		 * the last entered value "1.1.1.1" won't match with the regexp_select pattern
		 * and hence we will return false from this function.The caller will hence
		 * exclude the regexp_select PARAM from available_param
		 */
		unsigned i, rec_paramc = clish_param__get_param_count(param);
		clish_param_t *cparam = NULL;
		for (i = 0; i < rec_paramc; i++) {
			cparam = clish_param__get_param(param, i);
			if (!cparam)
				break;

			if (CLISH_PARAM_SUBCOMMAND == clish_param__get_mode(cparam) ||
				CLISH_PARAM_COMMON == clish_param__get_mode(cparam)) {
				const clish_ptype_t *ptype =
						clish_param__get_ptype(cparam);
				if (ptype && (CLISH_PTYPE_METHOD_REGEXP_SELECT ==
					clish_ptype__get_method(ptype))) {
					const char *name = NULL;
					int j = 0, cnt =0;
					cnt = clish_ptype_regexp_select__get_argv_count(ptype);
					for (;j < cnt; j++) {
						name = clish_ptype_regexp_select__get_name(ptype, j);
						if ((arg) && (name && ((name == lub_string_nocasestr(name, arg))
			
									))) {
							res = BOOL_TRUE;
							lub_string_free((char *)name);
							break;
						}
						if(name)
							lub_string_free((char *)name);
					}
				}
			}
		}
	}

	return res;
}

/*--------------------------------------------------------- */
static bool_t line_test(const clish_param_t *param, void *context)
{
	char *str = NULL;
	const char *teststr = NULL;
	bool_t res;

	if (!param)
		return BOOL_FALSE;
	teststr = clish_param__get_test(param);
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
clish_pargv_status_e clish_shell_parse_pargv(clish_pargv_t *pargv,
	const clish_command_t *cmd,
	void *context,
	clish_paramv_t *paramv,
	const lub_argv_t *argv,
	unsigned *idx, clish_pargv_t *last, unsigned need_index,
	unsigned *errP, unsigned *strmatchLen)
{
	unsigned argc = lub_argv__get_count(argv);
	unsigned index = 0;
	unsigned nopt_index = 0;
	clish_param_t *nopt_param = NULL;
	unsigned i;
	clish_pargv_status_t retval;
	unsigned paramc = clish_paramv__get_count(paramv);
	int up_level = 0; /* Is it a first level of param nesting? */
	int recursive_start = 0;

	assert(pargv);
	assert(cmd);

	/* Check is it a first level of PARAM nesting. */
	if (paramv == clish_command__get_paramv(cmd))
		up_level = 1;

	while (index < paramc) {
		const char *arg = NULL;
		clish_param_t *param = clish_paramv__get_param(paramv, index);
		clish_param_t *cparam = NULL;
		int is_switch = 0;
		clish_ptype_t *ptype = NULL;
		char *cmd_name = NULL;

		if (!param)
			return CLISH_BAD_PARAM;

                if (idx && errP) {
                    if(*idx <= argc) {
                     *errP = *idx;
                    }
                }

		/* Use real arg or PARAM's default value as argument */
		if (*idx < argc)
			arg = lub_argv__get_arg(argv, *idx);

		/* Is parameter in "switch" mode? */
		if (CLISH_PARAM_SWITCH == clish_param__get_mode(param))
			is_switch = 1;

		/* Check the 'test' conditions */
		if (!line_test(param, context)) {
			index++;
			continue;
		}

		ptype = clish_param__get_ptype(param);
		cmd_name = (char*)clish_command__get_name(cmd);
		/* Add param for help and completion */
		/* In order to show the help string for interface, we need to process  
		 * *idx + 1 = need_index, this is true for some of the show command which uses 
		 * CLISH_PTYPE_REGEXP_SELECT ptypes example 'do show ip ospf 4 interface vlan'.
		 */
		if (last && ((*idx == need_index)||
			is_valid_regexp_select_iteration(param, *idx, need_index, is_switch, arg)) &&
			(NULL == clish_pargv_find_arg(pargv, clish_param__get_name(param)))) {
			if (is_switch) {
				unsigned rec_paramc = clish_param__get_param_count(param);
				bool keyword_match = false;
				for (i = 0; i < rec_paramc; i++) {
					cparam = clish_param__get_param(param, i);
					if (!cparam)
						break;
					if (!line_test(cparam, context))
						continue;
					ptype = clish_param__get_ptype(cparam);

					if (CLISH_PARAM_SUBCOMMAND == clish_param__get_mode(cparam) || 
						((CLISH_PARAM_COMMON == clish_param__get_mode(cparam)) &&
						(CLISH_PTYPE_METHOD_REGEXP_SELECT ==
						 clish_ptype__get_method(ptype)))) {
						const char *pname =
							clish_param__get_value(cparam);
					    clish_ptype_t *ptype = 
								clish_param__get_ptype(cparam);
						if ((CLISH_PTYPE_METHOD_REGEXP_SELECT != clish_ptype__get_method(ptype)) &&
						(!arg || (arg && (pname == lub_string_nocasestr(pname, arg))))) {
							clish_pargv_insert(last, cparam, arg);
							if(arg) {
								/*arg non NULL means partial keyword entered */
								keyword_match = true;
							}
						} else if (CLISH_PTYPE_METHOD_REGEXP_SELECT ==
							clish_ptype__get_method(ptype)) {
							const char *name = NULL;
							int j = 0, cnt =0;
							cnt = clish_ptype_regexp_select__get_argv_count(ptype);
							for (;j < cnt; j++) {
								name = clish_ptype_regexp_select__get_name(ptype, j);
								if ((!arg) || (name && (name == lub_string_nocasestr(name, arg)))) {
									if (*idx + 1 == need_index || *idx == need_index) {
										clish_pargv_insert(last, cparam, arg);
										if(arg) {
											/*arg non NULL means partial keyword entered */
											keyword_match = true;
										}
									}
									if (strncmp(cmd_name, IFACE_CMD, strlen(IFACE_CMD))) {
                                                                        	/* Handling show and other non interface
                                                                         	 * related config commands such as
                                                                         	 * spanning-tree interface. */
                                                                                if (*idx + 1 == need_index)
                                                                                        clish_ptype__set_usename(ptype, USE_VALUE);
                                                                                else if (cnt > 1)
                                                                                        clish_ptype__set_usename(ptype, USE_RANGE);
                                                                                else
                                                                                        clish_ptype__set_usename(ptype, USE_NAME);
									} else {
                                                                                /* Interface specific commands handling */
                                                                                if (*idx + 1 == need_index)
                                                                                        clish_ptype__set_usename(ptype, USE_VALUE);
                                                                                else
                                                                                        clish_ptype__set_usename(ptype, USE_NAME);
                                                                        }
									break;
								}

								if (name) {
								   	lub_string_free((char*)name);
									name = NULL;
								}
							}
							if (name) lub_string_free((char*)name);
						}
					} else {
						if((*idx == need_index) && !keyword_match) {
							/* The following clish_pargv_insert inserts a non-keyword PARAM
							   which is part of a switch to the help list.Suppose there is a switch
							   between non-keyword  "<ip-addr>" and regexp_select PARAM "vlan".
							   On execution of "management route 1.1.1.0/24 vlan?" or
							   "management route 1.1.1.0/24 vlan ?", we should not be
							   inserting the non-keyword.*idx will be need_index-1 for
							   "management route 1.1.1.0/24 vlan ?".So the check
							   (*idx == need_index)" will take care of preventing the non-keyword
							   from getting added for "vlan ?".For the case vlan?, the keyword_match
							   flag will track if any keyword is added already.if added already
							   skip adding the non-keyword.This solution expects that the keywords
							   are added before the non-keywords in the switch */
							clish_pargv_insert(last,
								cparam, arg);
						}
					}
				}
			} else {
				if (CLISH_PARAM_SUBCOMMAND == clish_param__get_mode(param) ||
						((CLISH_PARAM_COMMON == clish_param__get_mode(param)) &&
						(CLISH_PTYPE_METHOD_REGEXP_SELECT ==
						 clish_ptype__get_method(ptype)))) {
					const char *pname =
						clish_param__get_value(param);
					clish_ptype_t *ptype =
						clish_param__get_ptype(param);
					if ((CLISH_PTYPE_METHOD_REGEXP_SELECT != clish_ptype__get_method(ptype)) && 
					(!arg || (arg && (pname == lub_string_nocasestr(pname, arg)))))
						clish_pargv_insert(last, param, arg);
					else if (CLISH_PTYPE_METHOD_REGEXP_SELECT ==
						clish_ptype__get_method(ptype)) {
						const char *name;
						int j = 0, cnt =0;
						cnt = clish_ptype_regexp_select__get_argv_count(ptype);
						for (;j < cnt; j++) {
							name = clish_ptype_regexp_select__get_name(ptype, j);
							if ((!arg) || (name && ((name == lub_string_nocasestr(name, arg))))) {
								clish_pargv_insert(last, param, arg);
                                                                if (strncmp(cmd_name, IFACE_CMD, strlen(IFACE_CMD))) {
                                                                        if (*idx + 1 == need_index)
                                                                                clish_ptype__set_usename(ptype, USE_VALUE);
                                                                        else if (cnt > 1)
                                                                                clish_ptype__set_usename(ptype, USE_RANGE);
                                                                        else
                                                                                clish_ptype__set_usename(ptype, USE_NAME);
                                                                } else {
                                                                        if (*idx + 1 == need_index)
                                                                                clish_ptype__set_usename(ptype, USE_VALUE);
                                                                        else
                                                                                clish_ptype__set_usename(ptype, USE_NAME);
                                                                }
								break;
							}
							if(name != NULL) {
								lub_string_free((char*)name);
								name = NULL;
							}
						}
                        			/* handle resource leak */
                        			if(name != NULL) {
                            				lub_string_free((char*)name);
                            				name = NULL;
                        			}
					}
				} else {
					clish_pargv_insert(last, param, arg);
				}
			}
		}

		/* Set parameter value */
		{
			char *validated = NULL;
			clish_paramv_t *rec_paramv =
			    clish_param__get_paramv(param);
			unsigned rec_paramc =
			    clish_param__get_param_count(param);

			/* Save the index of last non-option parameter
			 * to restore index if the optional parameters
			 * will be used.
			 */
			if (!clish_param__get_optional(param)) {
				nopt_param = param;
				nopt_index = index;
			}

			/* Validate the current parameter. */
			if (clish_pargv_find_arg(pargv, clish_param__get_name(param))) { 
				/* Duplicated parameter */
				validated = NULL;
			} else if (is_switch) {
				for (i = 0; i < rec_paramc; i++) {
					cparam = clish_param__get_param(param, i);
					if (!cparam)
						break;
					if (!line_test(cparam, context))
						continue;
					if ((validated = arg ?
						clish_param_validate(cparam, arg) : NULL)) {
						rec_paramv = clish_param__get_paramv(cparam);
						rec_paramc = clish_param__get_param_count(cparam);
						break;
					} else {
						/*  For CLISH_PTYPE_REGEXP_SELECT method, we try to match the
						 *  input arg against a PARAM. If it is not matching we try
						 *  concatenating the next arg to this arg and try matching
						 *  the concatenated string with the PARAM.
						 */
						if(!validated)
						{
							clish_ptype_method_e method =
								clish_ptype__get_method(clish_param__get_ptype(cparam));
							if(method == CLISH_PTYPE_METHOD_REGEXP_SELECT)
							{
								char  *arg2 = NULL, *arg_backup = (char*)arg;
								
								arg2 = lub_string_dup(arg);
								if((*idx + 1) < argc)
								{
									(*idx)++;
									arg = lub_argv__get_arg(argv, *idx);
									lub_string_cat(&arg2, arg);
									arg = arg2;
									validated = arg ?
									clish_param_validate(cparam, arg) : NULL;
									if(!validated)
									{
										/*Not matching even after concatinating next arg
										So revert to old arg itself to try matching to 
										next param*/
										(*idx)--;
										arg = arg_backup;
										lub_string_free(arg2);
										arg2 = NULL;
									}
									else
									{
										rec_paramv = clish_param__get_paramv(cparam);
										rec_paramc = clish_param__get_param_count(cparam);
										lub_string_free(arg2);
										break;
									}
								}

								if(!validated) {
									const char *name;
									int j = 0, cnt =0;
									cnt = clish_ptype_regexp_select__get_argv_count(clish_param__get_ptype(cparam));
									for (;j < cnt; j++) {
										name = clish_ptype_regexp_select__get_name(clish_param__get_ptype(cparam), j);
										if ((arg) && (name && ((name == lub_string_nocasestr(name, arg))))) {
											if(((*idx + 1) < argc)&& (errP)&&(strmatchLen)) {
												*errP = (*idx + 1);
												*strmatchLen = 0;
											}
											break;
										} else if(arg && strmatchLen) {
											int stmatch = lub_string_equal_part_nocase(name,arg,BOOL_TRUE);
											if( stmatch > (*strmatchLen))
												*strmatchLen = stmatch;
										}
										if(name != NULL) {
											lub_string_free((char*)name);
											name = NULL;
										}
									}
								}

								/* Handle resource leak */
 								if(arg2 != NULL) {
 									lub_string_free(arg2);
 									arg2 = NULL;
								}
							} else if(method == CLISH_PTYPE_METHOD_SELECT){
						                int i=0;
						                char *val = NULL;
						                /* Iterate possible completion */
						                while (cparam && (val = clish_ptype_method_select__get_name(clish_param__get_ptype(cparam), i++))) {
						                        /* check for the name */
									unsigned strmatch = lub_string_equal_part_nocase(val,arg,BOOL_TRUE);
									if(strmatchLen && strmatch>*strmatchLen)
										*strmatchLen = strmatch;
                						}

							} else if(arg && strmatchLen){
								char *val = clish_param__get_value(cparam);
								unsigned strmatch = lub_string_equal_part_nocase(val,arg,BOOL_TRUE);
								if(strmatch>*strmatchLen)
									*strmatchLen = strmatch;
                                                        }
						}
					}
				}
			} else {
				validated = arg ?
					clish_param_validate(param, arg) : NULL;
				/*  For CLISH_PTYPE_REGEXP_SELECT method, we try to match the
				 *  input arg against a PARAM. If it is not matching we try
				 *  concatenating the next arg to this arg and try matching
				 *  the concatenated string with the PARAM.
				 */
				if(!validated)
				{
					clish_ptype_method_e method = clish_ptype__get_method(clish_param__get_ptype(param));
					if(method == CLISH_PTYPE_METHOD_REGEXP_SELECT)
					{
						char  *arg2 = NULL;
						
						arg2 = lub_string_dup(arg);
						if((*idx + 1) < argc)
						{
							(*idx)++;
							arg = lub_argv__get_arg(argv, *idx);
							lub_string_cat(&arg2, arg);
							arg = arg2;
							validated = arg ?
							clish_param_validate(param, arg) : NULL;
							if(!validated)
							{
								/*Not matching even after concatinating next arg
								So revert to old arg itself to try matching to 
								next param*/
								(*idx)--;
							}
						}
						if(!validated) {
							const char *name;
							int j = 0, cnt =0; 
							cnt = clish_ptype_regexp_select__get_argv_count(clish_param__get_ptype(param));
							for (;j < cnt; j++) {
								name = clish_ptype_regexp_select__get_name(clish_param__get_ptype(param), j);
								if ((arg) && (name && ((name == lub_string_nocasestr(name, arg))))) {
									if(((*idx + 1) < argc)&& (errP) && strmatchLen) {
										*errP = (*idx + 1);
										*strmatchLen = 0; 
									}
									break;
								} else if(arg && strmatchLen) {
									int strmatch = lub_string_equal_part_nocase(name,arg,BOOL_TRUE);
									if(strmatch>*strmatchLen)
										*strmatchLen = strmatch;
								}
								if(name != NULL) {
									lub_string_free((char*)name);
									name = NULL;
								}
							}
						}

					} else if(method == CLISH_PTYPE_METHOD_SELECT){
				                int i=0;
				                char *val = NULL;
				                /* Iterate possible completion */
				                while (param && (val = clish_ptype_method_select__get_name(clish_param__get_ptype(param), i++))) {
				                        /* check for the name */
							unsigned strmatch = lub_string_equal_part_nocase(val,arg,BOOL_TRUE);
							if(strmatchLen && strmatch>*strmatchLen) { *strmatchLen = strmatch; }
							lub_string_free(val);
                				}

					} else if(arg && strmatchLen){
						char *val = clish_param__get_value(param);
						int strmatch = lub_string_equal_part_nocase(val,arg,BOOL_TRUE);
						if(strmatch > *strmatchLen)
							*strmatchLen = strmatch;
                                        }
				}
			}

			if (validated) {
				/* add (or update) this parameter */
				if (is_switch) {
					clish_pargv_insert(pargv, param,
						clish_param__get_name(cparam));
					clish_pargv_insert(pargv, cparam,
						validated);
				} else {
					if (clish_pargv_insert(pargv, param,
						validated) < 0) {
						lub_string_free(validated);
						return CLISH_BAD_PARAM;
					}
				}
				lub_string_free(validated);

				/* Next command line argument */
				/* Don't change idx if this is the last
				   unfinished optional argument.
				 */
				if (!(clish_param__get_optional(param) &&
					(*idx == need_index) &&
					(need_index == (argc - 1)))) {
					(*idx)++;
					/* Walk through the nested parameters */
					if (rec_paramc) {
						retval = clish_shell_parse_pargv(pargv, cmd,
							context, rec_paramv,
							argv, idx, last, need_index, errP, strmatchLen);
						if (CLISH_LINE_OK != retval)
							return retval;
					}
				}

				/* Choose the next parameter */
				if (clish_param__get_optional(param) &&
					!clish_param__get_order(param)) {
					if (nopt_param)
						index = nopt_index + 1;
					else
						index = 0;
				} else {
					/* Save non-option position in
					   case of ordered optional param */
					nopt_param = param;
					nopt_index = index;
					index++;
				}

			} else {
				/* Choose the next parameter if current
				 * is not validated.
				 */
				if (clish_param__get_optional(param))
					index++;
				else {
					if (!arg)
						break;
					else {
						clish_pargv_insert(pargv, param,
								   clish_param__get_name(param));
						return CLISH_BAD_PARAM;
					}
				}
			}
		}
	}

	/* Check for non-optional parameters without values */
	if ((*idx >= argc) && (index < paramc)) {
		unsigned j = index;
		const clish_param_t *param;
		while (j < paramc) {
			param = clish_paramv__get_param(paramv, j++);
			if (BOOL_TRUE != clish_param__get_optional(param))
				return CLISH_LINE_PARTIAL;
		}
	}

	/* If the number of arguments is bigger than number of
	 * params than it's a args. So generate the args entry
	 * in the list of completions.
	 */
	if (last && up_level &&
			clish_command__get_args(cmd) &&
			(clish_pargv__get_count(last) == 0) &&
			(*idx <= argc) && (index >= paramc) && (need_index != 0)) {
		clish_pargv_insert(last, clish_command__get_args(cmd), "");
	}

	/*
	 * if we've satisfied all the parameters we can now construct
	 * an 'args' parameter if one exists
	 */
	if (up_level && (*idx < argc) && (index >= paramc)) {
		const char *arg = lub_argv__get_arg(argv, *idx);
		const clish_param_t *param = clish_command__get_args(cmd);
		char *args = NULL;

		if (!param)
			return CLISH_BAD_CMD;

		/*
		 * put all the argument into a single string
		 */
		while (NULL != arg) {
			bool_t quoted = lub_argv__get_quoted(argv, *idx);
			if (BOOL_TRUE == quoted) {
				lub_string_cat(&args, "\"");
			}
			/* place the current argument in the string */
			lub_string_cat(&args, arg);
			if (BOOL_TRUE == quoted) {
				lub_string_cat(&args, "\"");
			}
			(*idx)++;
			arg = lub_argv__get_arg(argv, *idx);
			if (NULL != arg) {
				/* add a space if there are more arguments */
				lub_string_cat(&args, " ");
			}
		}
		/* add (or update) this parameter */
		clish_pargv_insert(pargv, param, args);
		lub_string_free(args);
	}

	return CLISH_LINE_OK;
}

CLISH_SET(shell, clish_shell_state_e, state);
CLISH_GET(shell, clish_shell_state_e, state);
