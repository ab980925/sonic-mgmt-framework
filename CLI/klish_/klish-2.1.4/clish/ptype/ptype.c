/*
 * ptype.c
 */
#include "private.h"
#include "lub/string.h"
#include "lub/ctype.h"
#include "lub/argv.h"
#include "lub/argv/private.h"
#include "lub/conv.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/types.h>
#include <dirent.h>
#include <regex.h>

/*--------------------------------------------------------- */
char *clish_ptype_regexp_select__get_ext_help(const clish_ptype_t * this,
        unsigned index)
{
    return (char*)lub_argv__get_arg(this->u.regexp_select.ext_help, index);
}

/*--------------------------------------------------------- */
char *clish_ptype_select__get_ext_help(const clish_ptype_t *this,
                unsigned index)
{
        return (char*)lub_argv__get_arg(this->u.select.ext_help, index);
}

/*--------------------------------------------------------- */
static int ext_help_argv_count(const char *line)
{
	int i = 0, j = 0;
	char *tmp = (char*)line;

	for (tmp = strchr(tmp, '(');tmp;tmp = strchr(tmp, '(')) {
		tmp++;
		i++;
	}
	tmp = (char*) line;

	for (tmp=strchr(tmp, ')');tmp;tmp = strchr(tmp, ')')) {
		tmp++;
		j++;
	}
	if (i != j)
		return -1;
	else
	return i;
}

/*--------------------------------------------------------- */
void ext_help_argv_init(lub_argv_t * this, const char *ext_help, size_t offset)
{
	lub_arg_t *arg;
	const char *lbrk;
	const char *rbrk;
	const char *value;
	size_t value_len;
	char *tmp;

	this->argv = NULL;
	this->argc = 0;

	if (!ext_help)
		return;

	/* first of all count the words in the line */
	this->argc = ext_help_argv_count(ext_help);
	if (0 >= this->argc)
		return;

	/* allocate space to hold the vector */
	arg = this->argv = malloc(sizeof(lub_arg_t) * this->argc);
	assert(arg);
	lbrk = strchr(ext_help, '(');
	rbrk = strchr(ext_help, ')');
	value = ext_help;
	tmp =(char*) value;
	value_len = strlen(ext_help);

	/* then fill out the array with the words */
	for (;lbrk; lbrk = strchr(tmp, '('),rbrk = strchr(tmp, ')')) {
		if (lbrk) {
			value = lbrk + 1;
			if (rbrk)
				value_len = (size_t) (rbrk - value);
		}
		if (value_len) {
			(*arg).arg = lub_string_dupn(value, value_len);
			(*arg).offset = offset;
			(*arg).quoted = BOOL_FALSE;
			offset += value_len;
			if(rbrk)
				tmp = (char*)rbrk + 1;
				arg++;
		}
	}
}

/*--------------------------------------------------------- */
static lub_argv_t *ext_help_argv_store(const char *line, size_t offset)
{
	lub_argv_t *this;

	this = malloc(sizeof(lub_argv_t));
	if (this)
		ext_help_argv_init(this, line, offset);

	return this;
}

/*--------------------------------------------------------- */
int clish_ptype_compare(const void *first, const void *second)
{
	const clish_ptype_t *f = (const clish_ptype_t *)first;
	const clish_ptype_t *s = (const clish_ptype_t *)second;

	return strcmp(f->name, s->name);
}

/*--------------------------------------------------------- */
bool clish_ptype_regexp_select_check_match(const clish_ptype_t *this, const char *text)
{
	char *result = NULL;
	int i = 0;
	bool ret = false;

	if(!this || !text)
		return ret;

	while ((result = clish_ptype_regexp_select__get_name(this, i++))) {
		/* get the next item and check if it is a completion */
		if (result == lub_string_nocasestr(result, text)) {
			lub_string_free(result);
			ret = true;
			break;
		}
		lub_string_free(result);
	}

	return ret;
}

/*--------------------------------------------------------- */
static void clish_ptype_regexp_select_get_match(const clish_ptype_t *this,
	const char *text, lub_argv_t *matches)
{
	char *result = NULL;
	int i = 0;

	if(!this || !text || !matches)
		return;

	while ((result = clish_ptype_regexp_select__get_name(this, i++))) {
		/* get the next item and check if it is a completion */
		if (result == lub_string_nocasestr(result, text)){
			lub_argv_add(matches, result);
		}
		lub_string_free(result);
	}
}

/*--------------------------------------------------------- */
static void clish_ptype_init(clish_ptype_t * this,
	const char *name, const char *text, const char *pattern,
	clish_ptype_method_e method, clish_ptype_preprocess_e preprocess,
	const char *ext_pattern, const char *ext_help, const char *alt_ext_pattern,
	const char *alt_pattern)
{
	assert(this);
	assert(name);
	this->name = lub_string_dup(name);
	this->text = NULL;
	this->pattern = NULL;
	this->preprocess = preprocess;
	this->range = NULL;
	this->ext_pattern = NULL;
        this->ext_help = NULL;
        this->alt_ext_pattern = NULL;
        this->alt_pattern = NULL;
	this->usename = BOOL_TRUE;
	this->action = clish_action_new();
	this->u.select.ext_help = NULL;

	if (ext_pattern || ext_help || alt_ext_pattern) {
		/* set the pattern */
		clish_ptype__set_extpattern(this, ext_pattern, method, ext_help,
                                            alt_ext_pattern);
	}

	if (pattern || alt_pattern) {
		/* set the pattern for this type */
		clish_ptype__set_pattern(this, pattern, method, alt_pattern);
	} else {
		/* The method is regexp by default */
		this->method = CLISH_PTYPE_METHOD_REGEXP;
	}
	
	/* set the help text for this type */
	if (text)
		clish_ptype__set_text(this, text);
}

/*--------------------------------------------------------- */
static void clish_ptype_fini(clish_ptype_t * this)
{
	if (this->pattern) {
		switch (this->method) {
		case CLISH_PTYPE_METHOD_REGEXP:
			if (this->u.regex.is_compiled)
				regfree(&this->u.regex.re);
			break;
		case CLISH_PTYPE_METHOD_INTEGER:
		case CLISH_PTYPE_METHOD_UNSIGNEDINTEGER:
			break;
		case CLISH_PTYPE_METHOD_SELECT:
			lub_argv_delete(this->u.select.items);
			break;
		case CLISH_PTYPE_METHOD_REGEXP_SELECT:
			regfree(&this->u.regexp_select.regexp);
			lub_argv_delete(clish_ptype_regexp_select__get_argv(this));
			if(this->alt_pattern)
				regfree(&this->u.regexp_select.alt_regexp);
			if (this->u.regexp_select.ext_help)
				lub_argv_delete(this->u.regexp_select.ext_help);
			break;
		default:
			break;
		}
	}

	lub_string_free(this->name);
	this->name = NULL;
	lub_string_free(this->text);
	this->text = NULL;
	lub_string_free(this->pattern);
	this->pattern = NULL;
	lub_string_free(this->range);
	this->range = NULL;
	lub_string_free(this->ext_pattern);
	this->ext_pattern = NULL;
	lub_string_free(this->ext_help);
	this->ext_help = NULL;
        lub_string_free(this->alt_ext_pattern);
        this->alt_ext_pattern = NULL;
        lub_string_free(this->alt_pattern);
        this->alt_pattern = NULL;
	clish_action_delete(this->action);

}

/*--------------------------------------------------------- */
clish_ptype_t *clish_ptype_new(const char *name,
	const char *help, const char *pattern,
	clish_ptype_method_e method, clish_ptype_preprocess_e preprocess,
	const char *ext_pattern, const char *ext_help, const char *alt_ext_pattern,
	const char *alt_pattern)
{
	clish_ptype_t *this = malloc(sizeof(clish_ptype_t));

	if (this)
		clish_ptype_init(this, name, help, pattern, method, preprocess, 
                                 ext_pattern, ext_help, alt_ext_pattern, alt_pattern);
	return this;
}

/*--------------------------------------------------------- */
void clish_ptype_free(void *data)
{
	clish_ptype_t *this = (clish_ptype_t *)data;
	clish_ptype_fini(this);
	free(this);
}

/*--------------------------------------------------------- */
static char *clish_ptype_select__get_name(const clish_ptype_t *this,
	unsigned int index)
{
	char *res = NULL;
	size_t name_len;
	const char *arg = lub_argv__get_arg(this->u.select.items, index);

	if (!arg)
		return NULL;
	name_len = strlen(arg);
	const char *lbrk = strchr(arg, '(');
	if (lbrk)
		name_len = (size_t) (lbrk - arg);
	res = lub_string_dupn(arg, name_len);

	return res;
}

/*--------------------------------------------------------- */
char *clish_ptype_method_select__get_name(const clish_ptype_t *this, unsigned int index)
{

        return clish_ptype_select__get_name(this,index);

}

/*--------------------------------------------------------- */
static char *clish_ptype_select__get_value(const clish_ptype_t *this,
	unsigned int index)
{
	char *res = NULL;
	const char *lbrk, *rbrk, *value;
	size_t value_len;
	const char *arg = lub_argv__get_arg(this->u.select.items, index);

	if (!arg)
		return NULL;

	lbrk = strchr(arg, '(');
	rbrk = strchr(arg, ')');
	value = arg;
	value_len = strlen(arg);
	if (lbrk) {
		value = lbrk + 1;
		if (rbrk)
			value_len = (size_t) (rbrk - value);
	}
	res = lub_string_dupn(value, value_len);

	return res;
}

/*--------------------------------------------------------- */
char *clish_ptype_regexp_select__get_name(const clish_ptype_t * this,
	unsigned index)
{
	char *result = NULL;
	const char *arg = lub_argv__get_arg(clish_ptype_regexp_select__get_argv(this), index);
	if (arg) {
		size_t name_len = strlen(arg);
		const char *lbrk = strchr(arg, '(');
		if (lbrk)
			name_len = (size_t) (lbrk - arg);
		if (name_len)
			result = lub_string_dupn(arg, name_len);
	}
	return result;
}

/*--------------------------------------------------------- */
char *clish_ptype_regexp_select__get_value(const clish_ptype_t * this,
	unsigned index)
{
	char *result = NULL;
	const char *arg = lub_argv__get_arg(clish_ptype_regexp_select__get_argv(this), index);
	if (arg) {
		const char *lbrk = strchr(arg, '(');
		const char *rbrk = strchr(arg, ')');
		const char *value = arg;
		size_t value_len = strlen(arg);
		if (lbrk) {
			value = lbrk + 1;
			if (rbrk)
				value_len = (size_t) (rbrk - value);
		}
		if (value_len)
			result = lub_string_dupn(value, value_len);
	}
	return result;
}

/*--------------------------------------------------------- */
static void clish_ptype__set_range(clish_ptype_t * this)
{
	char tmp[80];

	/* Now set up the range values */
	switch (this->method) {
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_REGEXP:
		/* Nothing more to do */
		break;
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_INTEGER:
		/* Setup the integer range */
		snprintf(tmp, sizeof(tmp), "%d..%d",
			this->u.integer.min, this->u.integer.max);
		tmp[sizeof(tmp) - 1] = '\0';
		this->range = lub_string_dup(tmp);
		break;
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_UNSIGNEDINTEGER:
		/* Setup the unsigned integer range */
		snprintf(tmp, sizeof(tmp), "%u..%u",
			(unsigned int)this->u.integer.min,
			(unsigned int)this->u.integer.max);
		tmp[sizeof(tmp) - 1] = '\0';
		this->range = lub_string_dup(tmp);
		break;
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_SELECT:
	{
		/* Setup the selection values to the help text */
		unsigned int i;

		for (i = 0; i < lub_argv__get_count(this->u.select.items); i++) {
			char *name = clish_ptype_select__get_name(this, i);

			if (i > 0)
				lub_string_cat(&this->range, "/");
			snprintf(tmp, sizeof(tmp), "%s", name);
			tmp[sizeof(tmp) - 1] = '\0';
			lub_string_cat(&this->range, tmp);
			lub_string_free(name);
		}
		break;
	}
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_REGEXP_SELECT:
	{
		/* Setup the selection values to the help text */
		unsigned int i, cnt;
		cnt = lub_argv__get_count(clish_ptype_regexp_select__get_argv(this));
		for (i = 0; i < cnt; i++) {
			char *name = clish_ptype_regexp_select__get_name(this, i);
			char *text = clish_ptype_regexp_select__get_value(this, i);

			if (name) {
				if (i < cnt-1)
					snprintf(tmp, sizeof(tmp),"%s/", name);
				else
					snprintf(tmp, sizeof(tmp), "%s", name);
			} else if (text)
				snprintf(tmp, sizeof(tmp),"%s", text);

			tmp[sizeof(tmp) - 1] = '\0';
			lub_string_cat(&this->range, tmp);
			if (name)
				lub_string_free(name);
			if (text)
				lub_string_free(text);
		}
		break;
	}
	/*------------------------------------------------- */
	default:
		break;
	/*------------------------------------------------- */
	}
}

/*--------------------------------------------------------- */
static const char *method_names[] = {
	"regexp",
	"integer",
	"unsignedInteger",
	"select",
	"code",
	"regexp_select"
};

/*--------------------------------------------------------- */
const char *clish_ptype__get_method_name(clish_ptype_method_e method)
{
	if (method >= CLISH_PTYPE_METHOD_MAX)
		return NULL;
	return method_names[method];
}

/*--------------------------------------------------------- */
clish_ptype_method_e clish_ptype__get_method(const clish_ptype_t * this)
{
	return (clish_ptype_method_e) (this->method);
}

/*--------------------------------------------------------- */
/* Return value of CLISH_PTYPE_METHOD_MAX indicates an illegal method */
clish_ptype_method_e clish_ptype_method_resolve(const char *name)
{
	unsigned int i;

	if (NULL == name)
		return CLISH_PTYPE_METHOD_REGEXP;
	for (i = 0; i < CLISH_PTYPE_METHOD_MAX; i++) {
		if (!strcmp(name, method_names[i]))
			break;
	}

	return (clish_ptype_method_e)i;
}

/*--------------------------------------------------------- */
static const char *preprocess_names[] = {
	"none",
	"toupper",
	"tolower",
        "mode"
};

/*--------------------------------------------------------- */
const char *clish_ptype__get_preprocess_name(clish_ptype_preprocess_e preprocess)
{
	if (preprocess >= CLISH_PTYPE_PRE_MAX)
		return NULL;

	return preprocess_names[preprocess];
}

/*--------------------------------------------------------- */
/* Return value of CLISH_PTYPE_PRE_MAX indicates an illegal method */
clish_ptype_preprocess_e clish_ptype_preprocess_resolve(const char *name)
{
	unsigned int i;

	if (NULL == name)
		return CLISH_PTYPE_PRE_NONE;
	for (i = 0; i < CLISH_PTYPE_PRE_MAX; i++) {
		if (!strcmp(name, preprocess_names[i]))
			break;
	}

	return (clish_ptype_preprocess_e)i;
}

/*--------------------------------------------------------- */
void clish_ptype_word_generator(clish_ptype_t * this,
	lub_argv_t *matches, const char *text,  const char *penultimate_text)
{
	char *result = NULL;
	unsigned int i = 0;
	bool ret = false;

	/* Only METHOD_SELECT has completions */
	if (this->method != CLISH_PTYPE_METHOD_SELECT &&
		this->method != CLISH_PTYPE_METHOD_REGEXP_SELECT)
		return;

	/* First of all simply try to validate the result */
	if(this->method == CLISH_PTYPE_METHOD_SELECT)
	{
		result = clish_ptype_validate(this, text, BOOL_TRUE);
		if (result) {
			lub_argv_add(matches, result);
			lub_string_free(result);
			return;
		}

		/* Iterate possible completion */
		while ((result = clish_ptype_select__get_name(this, i++))) {
			/* get the next item and check if it is a completion */
			if (result == lub_string_nocasestr(result, text))
				lub_argv_add(matches, result);
			lub_string_free(result);
		}
	} else {
		/*  Only for case like  "interface vl",On tab, we need
		 *  clish_ptype_word_generator to get list of completion
		 *  matches.For all case like "interface vlan " and
                 *  "interface vlan 1" and "interface vlan vl", On <tab>,
                 *  auto-completion is not needed.
                 *  For these cases, clish_ptype_regexp_select_check_match
                 *  called with penultimate text will return true.
                 *  This ensures we never attempt to call
                 *  clish_ptype_regexp_select_get_match.
                 */
                ret = clish_ptype_regexp_select_check_match(this, penultimate_text);
                if(ret)
                        return;

                clish_ptype_regexp_select_get_match(this, text, matches);
	}

}

/*--------------------------------------------------------- */
static char *clish_ptype_validate_or_translate(clish_ptype_t * this,
	const char *text, bool_t translate, bool_t isHelp)
{
	char *result = lub_string_dup(text);
        bool is_alt_regex_required = false; 
	assert(this->pattern);

	switch (this->preprocess) {
	/*----------------------------------------- */
	case CLISH_PTYPE_PRE_NONE:
		break;
	/*----------------------------------------- */
	case CLISH_PTYPE_PRE_TOUPPER:
	{
		char *p = result;
		while (*p) {
			*p = lub_ctype_toupper(*p);
			p++;
		}
		break;
	}
	/*----------------------------------------- */
	case CLISH_PTYPE_PRE_TOLOWER:
	{
		char *p = result;
		while (*p) {
			*p = lub_ctype_tolower(*p);
			p++;
		}
		break;
	}
        /*----------------------------------------- */
        case CLISH_PTYPE_PRE_MODE:
        {
                if(nos_use_alt_name() && (this->alt_pattern)) 
                        is_alt_regex_required = true;
                break;
        }
	/*----------------------------------------- */
	default:
		break;
	}

	/* Validate according the specified method */
	switch (this->method) {
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_REGEXP:
		/* Lazy compilation of the regular expression */
		if (!this->u.regex.is_compiled) {
			if (regcomp(&this->u.regex.re, this->pattern,
				REG_NOSUB | REG_EXTENDED)) {
				lub_string_free(result);
				result = NULL;
				break;
			}
			this->u.regex.is_compiled = BOOL_TRUE;
		}

		if (regexec(&this->u.regex.re, result, 0, NULL, 0)) {
			lub_string_free(result);
			result = NULL;
		}
		break;
	/*------------------------------------------------- */
        case CLISH_PTYPE_METHOD_REGEXP_SELECT:
        {
        	unsigned i;

		if(isHelp){
			for (i = 0; i < lub_argv__get_count(clish_ptype_regexp_select__get_argv(this)); i++) {
				char *name = clish_ptype_regexp_select__get_name(this, i);
				char *value = clish_ptype_regexp_select__get_value(this, i);
				int tmp = 0;

                		if (name) tmp = lub_string_nocasecmp(result, name);
                               	lub_string_free((BOOL_TRUE == translate) ? name : value);
                                if (0 == tmp) {
                                        lub_string_free(result);
                                        result = ((BOOL_TRUE == translate) ? value : name);
                                        break;
                                } else {
                                        lub_string_free((BOOL_TRUE == translate) ? value : name);
                                }
			}
                        if (i == lub_argv__get_count(clish_ptype_regexp_select__get_argv(this))) {
                                // failed to find a match
                                lub_string_free(result);
                                result = NULL;
                        }
                } else {
                       	/* test the regular expression against the string */
                        /* lint -e64 Type mismatch (arg. no. 4) */
                        /*
                         * lint seems to equate regmatch_t[] as being of type regmatch_t !
                         */

                        if (is_alt_regex_required) {
                            if (0 != regexec(&this->u.regexp_select.alt_regexp, result, 0, NULL, 0)) {
                                lub_string_free(result);
                                result = NULL;                        
                            }
                        } else { 
                            if (0 != regexec(&this->u.regexp_select.regexp, result, 0, NULL, 0)) {
                          	lub_string_free(result);
                                result = NULL;
                            }
                        }
                        if (result) {
                        	int index = -1, j;
                                bool_t matched = BOOL_FALSE;
                                char *new_result = NULL, *tmp = NULL;
                                /* Loop through possible help string options,
                                 * such as ethernet, vlan and portchannel
                                 * if given CLI matches first two character
                                 * like po 10 then start at array index 3 and
                                 * skip if any space lies between po and 10.
                                 * If given CLI is like p 10 then start at
				 * array index 2 and skip if any space lies
                                 * between p and 10.
                                 */
                                for (j = 0; j < lub_argv__get_count(clish_ptype_regexp_select__get_argv(this)); j++) {
                                	char *name = clish_ptype_regexp_select__get_name(this, j);
                                       	/* In the below piece of code, we try to expand the name.
                                           If user has eth1, it needs to be converted to ethernet1.
                                           Similarly,if user gives "ethernet 1", the space need to be
                                           stripped */
                                	if (name) {
                                                   /*User could have entered short form eth.So check if
                                                   the entered string is substring of the name after
                                                   stripping the port number portion */
                                                index = get_index(result);
					       	if (!strncasecmp(name, result, index)) {
							tmp = result + index;
                                                        matched = BOOL_TRUE;
                                                }
                                                if (matched == BOOL_TRUE) {
                                                	while (isspace(*tmp))
                                                        	tmp++;
                                                                new_result = lub_string_dup(name);
                                                                lub_string_cat(&new_result, tmp);
								lub_string_free(result);
                                                                result = new_result;
                                			/* Variable name going out of scope will leaks the storage it
                                 			 * points to. So free memory pointed by it
                                 			 * to avoid resource leak.
                                 			 */
                                				if(name != NULL) {
                                    					lub_string_free(name);
                                    					name = NULL;
                                				}
                                                                break;
                                                }
                                        } else {
                                                matched = BOOL_TRUE;
                            			/* Variable name going out of scope will leaks the storage it
                             			 * points to. So free memory pointed by it
                             			 * to avoid resource leak.  */
                            			if(name != NULL) {
                                			lub_string_free(name);
                                			name = NULL;
                            			}
                                                break;
                                        }
                        		/* Variable name going out of scope will leaks the storage it
                         		 * points to. So free memory pointed by it
                         		 * to avoid resource leak.
                         		 */
                        		if(name != NULL) {
                            			lub_string_free(name);
                            			name = NULL;
                        		}
                                }
                                if (matched == BOOL_FALSE) {
                                	lub_string_free(result);
                                        result = NULL;
                                }
                        }
                }
                /*lint +e64 */
                break;
	}

	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_INTEGER:
	{
		/* first of all check that this is a number */
		bool_t ok = BOOL_TRUE;
		const char *p = result;
		int value = 0;

		if (*p == '-')
			p++;
		while (*p) {
			if (!lub_ctype_isdigit(*p++)) {
				ok = BOOL_FALSE;
				break;
			}
		}
		if (BOOL_FALSE == ok) {
			lub_string_free(result);
			result = NULL;
			break;
		}
		/* Convert to number and check the range */
		if ((lub_conv_atoi(result, &value, 0) < 0) ||
			(value < this->u.integer.min) ||
			(value > this->u.integer.max)) {
			lub_string_free(result);
			result = NULL;
			break;
		}
		break;
	}
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_UNSIGNEDINTEGER:
	{
		/* first of all check that this is a number */
		bool_t ok = BOOL_TRUE;
		const char *p = result;
		unsigned int value = 0;
		while (p && *p) {
			if (!lub_ctype_isdigit(*p++)) {
				ok = BOOL_FALSE;
				break;
			}
		}
		if (BOOL_FALSE == ok) {
			lub_string_free(result);
			result = NULL;
			break;
		}
		/* Convert to number and check the range */
		if ((lub_conv_atoui(result, &value, 0) < 0) ||
			(value < (unsigned)this->u.integer.min) ||
			(value > (unsigned)this->u.integer.max)) {
			lub_string_free(result);
			result = NULL;
			break;
		}
		break;
	}
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_SELECT:
	{
		unsigned int i;
		for (i = 0; i < lub_argv__get_count(this->u.select.items); i++) {
			char *name = clish_ptype_select__get_name(this, i);
			char *value = clish_ptype_select__get_value(this, i);
			int tmp = lub_string_nocasecmp(result, name);
			lub_string_free((BOOL_TRUE == translate) ? name : value);
			if (0 == tmp) {
				lub_string_free(result);
				result = ((BOOL_TRUE == translate) ? value : name);
				break;
			}
			lub_string_free((BOOL_TRUE == translate) ? value : name);
		}
		if (i == lub_argv__get_count(this->u.select.items)) {
			/* failed to find a match */
			lub_string_free(result);
			result = NULL;
			break;
		}
		break;
	}
	/*------------------------------------------------- */
	default:
		break;
	}
	return (char *)result;
}

/*--------------------------------------------------------- */
char *clish_ptype_validate(clish_ptype_t * this, const char *text, bool_t isHelp)
{
	return clish_ptype_validate_or_translate(this, text, BOOL_FALSE, BOOL_TRUE);
}

/*--------------------------------------------------------- */
char *clish_ptype_translate(clish_ptype_t * this, const char *text)
{
	return clish_ptype_validate_or_translate(this, text, BOOL_TRUE, BOOL_FALSE);
}

CLISH_GET_STR(ptype, name);
CLISH_SET_STR_ONCE(ptype, text);
CLISH_GET_STR(ptype, text);
CLISH_SET_ONCE(ptype, clish_ptype_preprocess_e, preprocess);
CLISH_GET_STR(ptype, range);
CLISH_GET(ptype, clish_action_t *, action);

/*--------------------------------------------------------- */
void clish_ptype__set_pattern(clish_ptype_t * this,
	const char *pattern, clish_ptype_method_e method, const char *alt_pattern)
{
	assert(NULL == this->pattern);
	this->method = method;

	switch (this->method) {
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_REGEXP:
	{
		lub_string_cat(&this->pattern, "^");
		lub_string_cat(&this->pattern, pattern);
		lub_string_cat(&this->pattern, "$");
		/* Use lazy mechanism to compile regular expressions */
		this->u.regex.is_compiled = BOOL_FALSE;
		break;
	}
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_INTEGER:
		/* default the range to that of an integer */
		this->u.integer.min = INT_MIN;
		this->u.integer.max = INT_MAX;
		this->pattern = lub_string_dup(pattern);
		/* now try and read the specified range */
		sscanf(this->pattern, "%d..%d",
			&this->u.integer.min, &this->u.integer.max);
		break;
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_UNSIGNEDINTEGER:
		/* default the range to that of an unsigned integer */
		this->u.integer.min = 0;
		this->u.integer.max = (int)UINT_MAX;
		this->pattern = lub_string_dup(pattern);
		/* now try and read the specified range */
		sscanf(this->pattern, "%u..%u",
			(unsigned int *)&this->u.integer.min,
			(unsigned int *)&this->u.integer.max);
		break;
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_SELECT:
		this->pattern = lub_string_dup(pattern);
		/* store a vector of item descriptors */
		this->u.select.items = lub_argv_new(this->pattern, 0);
		break;
	/*------------------------------------------------- */
	case CLISH_PTYPE_METHOD_REGEXP_SELECT:
                {
                        int result;
                        /* only the expression is allowed */
                        lub_string_cat(&this->pattern, "^");
                        lub_string_cat(&this->pattern, pattern);
                        lub_string_cat(&this->pattern, "$");
                        /* compile the regular expression for later use */
                        result = regcomp(&(this->u.regexp_select.regexp), this->pattern,
                                 REG_EXTENDED);
                        assert(0 == result);

                        if(alt_pattern){
                                /* only the expression is allowed */
                                lub_string_cat(&this->alt_pattern, "^");
                                lub_string_cat(&this->alt_pattern, alt_pattern);
                                lub_string_cat(&this->alt_pattern, "$");
                                /* compile the regular expression for later use */
                                result = regcomp(&(this->u.regexp_select.alt_regexp), this->alt_pattern,
                                         REG_EXTENDED);
                                assert(0 == result);
                        }
                        break;
                }

	/*------------------------------------------------- */
	default:
		break;
	}
	/* now set up the range details */
	clish_ptype__set_range(this);
}


void clish_ptype__set_extpattern(clish_ptype_t * this,
                const char *ext_pattern, clish_ptype_method_e method, 
                const char *ext_help, const char *alt_ext_pattern)
{
        assert(NULL == this->pattern);
        this->method = method;

        switch (this->method) {
                /*------------------------------------------------- */

                case CLISH_PTYPE_METHOD_REGEXP_SELECT:
                        this->u.regexp_select.items = NULL;
                        this->u.regexp_select.ext_help = NULL;
                        this->u.regexp_select.alt_items = NULL;
                        if (ext_pattern) {
                                this->ext_pattern = lub_string_dup(ext_pattern);
                                /* store a vector of item descriptors */
                                this->u.regexp_select.items = lub_argv_new(this->ext_pattern, 0);
                        }
                        if (ext_help) {
                                this->ext_help =  lub_string_dup(ext_help);
                                /* store a vector of item descriptors */
                                this->u.regexp_select.ext_help = ext_help_argv_store(this->ext_help, 0);
                        }
                        if (alt_ext_pattern) {
                                this->alt_ext_pattern = lub_string_dup(alt_ext_pattern);
                                /* store a vector of item descriptors */
                                this->u.regexp_select.alt_items = lub_argv_new(this->alt_ext_pattern, 0);
                        }
                        break;

		case CLISH_PTYPE_METHOD_SELECT:
                        if (ext_help) {
                                this->ext_help =  lub_string_dup(ext_help);
                                /* store a vector of item descriptors */
                                this->u.select.ext_help = ext_help_argv_store(this->ext_help, 0);
                        }
                        break;
        default:
            break;
        }
}

/*--------------------------------------------------------- */
help_type_t clish_ptype__get_usename(const clish_ptype_t * this)
{
        return this->usename;
}
/*--------------------------------------------------------- */
void clish_ptype__set_usename(clish_ptype_t * this, help_type_t val)
{
        this->usename = val;
}

/*--------------------------------------------------------- */
lub_argv_t *clish_ptype_regexp_select__get_argv(const clish_ptype_t * this)
{
        if (!this)
            return NULL;
        if ((this->preprocess == CLISH_PTYPE_PRE_MODE) && nos_use_alt_name())
            return (this->u.regexp_select.alt_items);
        else
            return (this->u.regexp_select.items);
}

/*--------------------------------------------------------- */
int clish_ptype_regexp_select__get_argv_count(const clish_ptype_t * this)
{
    return (lub_argv__get_count(clish_ptype_regexp_select__get_argv(this)));
}

/*--------------------------------------------------------- */
char *clish_ptype_regexp_select__get_argname(const clish_ptype_t * this,
	unsigned index)
{
        if (!this)
        	return NULL;
	return (clish_ptype_regexp_select__get_name(this, index));
}

/*--------------------------------------------------------- */
int clish_ptype_select__get_help(const clish_ptype_t *this, clish_help_t *help, const char *pval)
{
        if(NULL != this->u.select.ext_help) {
                unsigned i;
                char *name, *ext_help;
                if(pval) {
                        for (i = 0; i < lub_argv__get_count(this->u.select.ext_help);
                                        i++) {
                                name = clish_ptype_select__get_name(this, i);
                                if(NULL == name) {
                                        continue;
                                }
                                if(strncmp(pval, name, strlen(pval)) == 0) {
                                        ext_help = clish_ptype_select__get_ext_help(this, i);
                                        lub_argv_add(help->name, name);
                                        lub_argv_add(help->help, ext_help);
                                }
                /*Overwriting pointer name while looping will
                 * leak the storage that name pointed to. So
                 * free the memory before overriding to avoid
                 * resource leak.
                 */
                 lub_string_free(name);
                 name = NULL;
                        }
                } else {
                        for (i = 0; i < lub_argv__get_count(this->u.select.ext_help);
                                        i++) {
                                name = clish_ptype_select__get_name(this, i);
                                if(NULL == name) {
                                        continue;
                                }
                                ext_help = clish_ptype_select__get_ext_help(this, i);
                                lub_argv_add(help->name, name);
                                lub_argv_add(help->help, ext_help);
                /*Overwriting pointer name while looping will 
                 * leak the storage that name pointed to. So 
                 * free the memory before overriding to avoid
                 * resource leak.
                 */
                 lub_string_free(name);
                 name = NULL;
                        }
                }

                return 0;
        }
        return -1;
}

