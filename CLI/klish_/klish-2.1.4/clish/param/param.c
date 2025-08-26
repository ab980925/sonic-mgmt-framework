/*
 * param.c
 *
 * This file provides the implementation of the "param" class
 */
#include "private.h"
#include "lub/string.h"
#include "clish/types.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------
 * PRIVATE METHODS
 *--------------------------------------------------------- */
static void clish_param_init(clish_param_t *this, const char *name,
	const char *text, const char *ptype_name)
{
	this->name = lub_string_dup(name);
	this->text = lub_string_dup(text);
	this->ptype_name = lub_string_dup(ptype_name);

	/* Set up defaults */
	this->ptype = NULL;
	this->defval = NULL;
	this->mode = CLISH_PARAM_COMMON;
	this->optional = BOOL_FALSE;
	this->order = BOOL_FALSE;
	this->value = NULL;
	this->hidden = BOOL_FALSE;
	this->test = NULL;
	this->completion = NULL;
	this->access = NULL;
	this->viewname = NULL;
	this->viewid = NULL;
	this->recursive = BOOL_FALSE;

	this->paramv = clish_paramv_new();
	this->enabled = BOOL_FALSE;
}

/*--------------------------------------------------------- */
static void clish_param_fini(clish_param_t * this)
{
	/* deallocate the memory for this instance */
	lub_string_free(this->defval);
	lub_string_free(this->name);
	lub_string_free(this->text);
	lub_string_free(this->ptype_name);
	lub_string_free(this->value);
	lub_string_free(this->test);
	lub_string_free(this->completion);
	lub_string_free(this->access);
	lub_string_free(this->viewname);
	lub_string_free(this->viewid);

	this->viewname = NULL;
	this->viewid = NULL;
	
	clish_paramv_delete(this->paramv);
}

/*---------------------------------------------------------
 * PUBLIC META FUNCTIONS
 *--------------------------------------------------------- */
clish_param_t *clish_param_new(const char *name, const char *text,
	const char *ptype_name)
{
	clish_param_t *this = malloc(sizeof(clish_param_t));

	if (this)
		clish_param_init(this, name, text, ptype_name);
	return this;
}

/*---------------------------------------------------------
 * PUBLIC METHODS
 *--------------------------------------------------------- */
void clish_param_delete(clish_param_t * this)
{
	clish_param_fini(this);
	free(this);
}

/*--------------------------------------------------------- */
void clish_param_insert_param(clish_param_t * this, clish_param_t * param)
{
	return clish_paramv_insert(this->paramv, param);
}

/*---------------------------------------------------------
 * PUBLIC ATTRIBUTES
 *--------------------------------------------------------- */
void clish_param__set_ptype_name(clish_param_t *this, const char *ptype_name)
{
	if (this->ptype_name)
		lub_string_free(this->ptype_name);
	this->ptype_name = lub_string_dup(ptype_name);
}

/*--------------------------------------------------------- */
const char * clish_param__get_ptype_name(const clish_param_t *this)
{
	return this->ptype_name;
}

/*--------------------------------------------------------- */
const char *clish_param__get_name(const clish_param_t * this)
{
	if (!this)
		return NULL;
	return this->name;
}

/*--------------------------------------------------------- */
const char *clish_param__get_text(const clish_param_t * this)
{
	return this->text;
}

/*--------------------------------------------------------- */
const char *clish_param__get_range(const clish_param_t * this)
{
	return clish_ptype__get_range(this->ptype);
}

/*--------------------------------------------------------- */
clish_ptype_t *clish_param__get_ptype(const clish_param_t * this)
{
	return this->ptype;
}

/*--------------------------------------------------------- */
void clish_param__set_ptype(clish_param_t *this, clish_ptype_t *ptype)
{
	this->ptype = ptype;
}

/*--------------------------------------------------------- */
void clish_param__set_default(clish_param_t * this, const char *defval)
{
	assert(!this->defval);
	this->defval = lub_string_dup(defval);
}

/*--------------------------------------------------------- */
const char *clish_param__get_default(const clish_param_t * this)
{
	return this->defval;
}

/*--------------------------------------------------------- */
void clish_param__set_mode(clish_param_t * this, clish_param_mode_e mode)
{
	assert(this);
	this->mode = mode;
}

/*--------------------------------------------------------- */
clish_param_mode_e clish_param__get_mode(const clish_param_t * this)
{
	return this->mode;
}

/*--------------------------------------------------------- */
char *clish_param_validate(const clish_param_t * this, const char *text)
{
	if (CLISH_PARAM_SUBCOMMAND == clish_param__get_mode(this) &&
		CLISH_PTYPE_METHOD_REGEXP_SELECT != clish_ptype__get_method(this->ptype)) {
		if (lub_string_nocasecmp(clish_param__get_value(this), text))
			return NULL;
    }
	return clish_ptype_translate(this->ptype, text);
}

/*--------------------------------------------------------- */
void clish_param_help(const clish_param_t * this, clish_help_t *help, 
                       const char *pval)
{
	const char *range = clish_ptype__get_range(this->ptype);
	const char *name = NULL;
	char *str = NULL;
	char *ext_help = NULL;
	help_type_t usename = USE_NAME;
	clish_ptype_method_e method = clish_ptype__get_method(this->ptype);

	if (CLISH_PARAM_SWITCH == clish_param__get_mode(this)) {
		unsigned rec_paramc = clish_param__get_param_count(this);
		clish_param_t *cparam;
		unsigned i;

		for (i = 0; i < rec_paramc; i++) {
			cparam = clish_param__get_param(this, i);
			if (!cparam)
				break;
			clish_param_help(cparam, help, pval);
		}
		lub_string_free((char*)range);
		return;
	}

	if (CLISH_PARAM_SUBCOMMAND == clish_param__get_mode(this)) {
		if (method != CLISH_PTYPE_METHOD_REGEXP_SELECT)
			name = clish_param__get_value(this);
	} else {
		if (!(name = clish_ptype__get_text(this->ptype)))
			name = clish_ptype__get_name(this->ptype);
	}

	if (method == CLISH_PTYPE_METHOD_REGEXP_SELECT) {
		clish_ptype_t *ptype = this->ptype;
		usename = clish_ptype__get_usename(ptype);
		int i = 0, cnt = clish_ptype_regexp_select__get_argv_count(ptype);
		const char *pname = NULL;
		char *val_str = NULL;
		for (;i < cnt; i++) {
			pname = clish_ptype_regexp_select__get_name(ptype, i);
			if (!pval || !pname || !strncasecmp(pname, pval, strlen(pval))) {
                if (!pval && !pname)
                    usename = USE_VALUE;
				if (usename == USE_NAME) {
					if (i)
						lub_string_cat(&str, "\n  ");
						lub_string_cat(&str, pname);
				} else if (usename == USE_VALUE) {
                    val_str = clish_ptype_regexp_select__get_value(ptype, i);
                    if (val_str) {
                         lub_string_cat(&str, "<");
                         lub_string_cat(&str, val_str);
                         lub_string_cat(&str, ">");
                    }
                    /* Free the heap memory returned by allocation func 
                     * clish_ptype_regexp_select__get_value (..)  after
                     * data present at that memory location has been 
                     * concantenated with str to avoid resource leak.
                     */
                    if(val_str != NULL) {
                        lub_string_free(val_str);
                        val_str = NULL;
                    }
				} else {
					if (!pval) {
						lub_argv_add(help->name, pname);
						lub_argv_add(help->help, clish_ptype_regexp_select__get_ext_help(ptype, i));
						if (pname) {
							lub_string_free((char*)pname);
							pname = NULL;
						}
						continue;
					} else {
						lub_string_cat(&str, pname);
						lub_string_cat(&ext_help, clish_ptype_regexp_select__get_ext_help(ptype, i));
					}
				}
				break;
            }
            /* Variable pname going out of scope,free memory to avoid resource leaks.*/
            if(pname != NULL) {
                lub_string_free((char*)pname);
                pname = NULL;
            }
		}
		/* Variable pname going out of scope,free memory to avoid resource leaks.*/
		if(pname != NULL) {
		    lub_string_free((char*)pname);
		    pname = NULL;
		}
	} else {
		if (range) {
			lub_string_cat(&str, "<");
			lub_string_cat(&str, range);
			lub_string_cat(&str, ">");
		} else {
			lub_string_cat(&str, name);
		}
	}

	switch (method) {
		case CLISH_PTYPE_METHOD_SELECT:
			{
				clish_ptype_t *ptype = this->ptype;
				if (clish_ptype_select__get_help(ptype, help, pval) != 0) {
					lub_argv_add(help->name, str);
					lub_argv_add(help->help, this->text);
				}
			}
			break;
		case CLISH_PTYPE_METHOD_REGEXP_SELECT:
			{
				if (usename != USE_RANGE) {
					if (str) {
						lub_argv_add(help->name, str);
						lub_argv_add(help->help, this->text);
					}
				} else if (pval) {
					lub_argv_add(help->name, str);
					lub_argv_add(help->help, ext_help);
				}
			}
			break;
		default:
			{
				lub_argv_add(help->name, str);
				lub_argv_add(help->help, this->text);
			}
			break;
	}

	if(str) {
		lub_string_free(str);
	}
	lub_argv_add(help->detail, NULL);
}

/*--------------------------------------------------------- */
void clish_param_help_arrow(const clish_param_t * this, size_t offset)
{
	fprintf(stderr, "%*c\n", (int)offset, '^');

	this = this; /* Happy compiler */
}

/*--------------------------------------------------------- */
clish_param_t *clish_param__get_param(const clish_param_t * this,
	unsigned index)
{
	return clish_paramv__get_param(this->paramv, index);
}

/*--------------------------------------------------------- */
clish_paramv_t *clish_param__get_paramv(clish_param_t * this)
{
	return this->paramv;
}

/*--------------------------------------------------------- */
unsigned int clish_param__get_param_count(const clish_param_t * this)
{
	return clish_paramv__get_count(this->paramv);
}

/*--------------------------------------------------------- */
void clish_param__set_optional(clish_param_t * this, bool_t optional)
{
	this->optional = optional;
}

/*--------------------------------------------------------- */
bool_t clish_param__get_optional(const clish_param_t * this)
{
	return this->optional;
}

/*--------------------------------------------------------- */
void clish_param__set_order(clish_param_t * this, bool_t order)
{
	this->order = order;
}

/*--------------------------------------------------------- */
bool_t clish_param__get_order(const clish_param_t * this)
{
	return this->order;
}

/*--------------------------------------------------------- */

/* paramv methods */

/*--------------------------------------------------------- */
static void clish_paramv_init(clish_paramv_t * this)
{
	this->paramc = 0;
	this->paramv = NULL;
}

/*--------------------------------------------------------- */
static void clish_paramv_fini(clish_paramv_t * this)
{
	unsigned i;

	/* finalize each of the parameter instances */
	for (i = 0; i < this->paramc; i++) {
		clish_param_delete(this->paramv[i]);
	}
	/* free the parameter vector */
	free(this->paramv);
	this->paramc = 0;
}

/*--------------------------------------------------------- */
void clish_param__set_value(clish_param_t * this, const char * value)
{
	assert(!this->value);
	this->value = lub_string_dup(value);
}

/*--------------------------------------------------------- */
char *clish_param__get_value(const clish_param_t * this)
{
	if (this->value)
		return this->value;
	return this->name;
}

/*--------------------------------------------------------- */
void clish_param__set_hidden(clish_param_t * this, bool_t hidden)
{
	this->hidden = hidden;
}

/*--------------------------------------------------------- */
bool_t clish_param__get_hidden(const clish_param_t * this)
{
	return this->hidden;
}

/*--------------------------------------------------------- */
void clish_param__set_test(clish_param_t * this, const char *test)
{
	assert(!this->test);
	this->test = lub_string_dup(test);
}

/*--------------------------------------------------------- */
char *clish_param__get_test(const clish_param_t *this)
{
	return this->test;
}

/*--------------------------------------------------------- */
void clish_param__set_completion(clish_param_t *this, const char *completion)
{
	assert(!this->completion);
	this->completion = lub_string_dup(completion);
}

/*--------------------------------------------------------- */
char *clish_param__get_completion(const clish_param_t *this)
{
	return this->completion;
}

/*--------------------------------------------------------- */
void clish_param__set_access(clish_param_t *this, const char *access)
{
	if (this->access)
		lub_string_free(this->access);
	this->access = lub_string_dup(access);
}

/*--------------------------------------------------------- */
char *clish_param__get_access(const clish_param_t *this)
{
	return this->access;
}

/*--------------------------------------------------------- */
void clish_param__set_enabled(clish_param_t * this, bool_t enabled)
{
	this->enabled = enabled;
}

/*--------------------------------------------------------- */
bool_t clish_param__get_enabled(const clish_param_t * this)
{
	//return this->enabled;
	return BOOL_TRUE;
}

