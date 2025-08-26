/*
 * ptype.h
 */
#include "clish/pargv.h"
#include "lub/argv.h"
#include "clish/ptype.h"

#include <sys/types.h>
#include <regex.h>

typedef struct clish_ptype_integer_s clish_ptype_integer_t;
struct clish_ptype_integer_s {
	int min;
	int max;
};

typedef struct clish_ptype_select_s clish_ptype_select_t;
struct clish_ptype_select_s {
	lub_argv_t *items;
      lub_argv_t *ext_help;
};

typedef struct clish_ptype_regex_s clish_ptype_regex_t;
struct clish_ptype_regex_s {
	bool_t is_compiled;
	regex_t re;
};

typedef struct clish_ptype_regexp_select_s clish_ptype_regexp_select_t;
struct clish_ptype_regexp_select_s {
      regex_t regexp;
      regex_t alt_regexp;
      lub_argv_t *items;
      lub_argv_t *ext_help;
      lub_argv_t *alt_items;
};

struct clish_ptype_s {
	char *name;
	char *text;
	char *pattern;

      char *ext_pattern;
      char *ext_help;
      char *alt_ext_pattern;
      char *alt_pattern;
      help_type_t usename;
	char *range;
	clish_ptype_method_e method;
	clish_ptype_preprocess_e preprocess;
	unsigned int last_name; /* Index used for auto-completion */
	union {
		clish_ptype_regex_t regex;
		clish_ptype_integer_t integer;
		clish_ptype_select_t select;
              clish_ptype_regexp_select_t regexp_select;
	} u;
	clish_action_t *action;
};
