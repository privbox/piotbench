#ifndef __BENCHMARK_COMMAND_H
#define __BENCHMARK_COMMAND_H

#define PARAM_VALUE_MAX	128

struct param_t {
	const char *name;
	const char *desc;
	const char *fmt;
	union {
		char str[PARAM_VALUE_MAX];
		unsigned long num;
	} default_;
	unsigned long offset;
	size_t size;
	void (*show_fn)(struct param_t *, unsigned char *);
};

void show_uint_param(struct param_t *param, unsigned char *opaque);
void show_int_param(struct param_t *param, unsigned char *opaque);

void show_str_param(struct param_t *param, unsigned char *opaque);

#define _PARAM(cfg_type, field_name, fmttype, desc_, default_key, default_value, show_fn_)	\
{													\
	.name = #field_name,							\
	.desc = desc_,									\
	.fmt = "--" #field_name "=" fmttype,			\
	.default_ = {									\
		.default_key = default_value,				\
	},												\
	.size = sizeof (((cfg_type *) 0)->field_name),	\
	.offset = offsetof(cfg_type, field_name),		\
	.show_fn = show_fn_,							\
}
#define PARAM_UINT(cfg_type, field_name, desc, default_value)	\
	_PARAM(cfg_type, field_name, "%u", desc, num, default_value, show_uint_param)
#define PARAM_INT(cfg_type, field_name, desc, default_value)	\
	_PARAM(cfg_type, field_name, "%d", desc, num, default_value, show_int_param)
#define PARAM_STR(cfg_type, field_name, desc, default_value)	\
	_PARAM(cfg_type, field_name, "%s", desc, str, default_value, show_str_param)
#define LAST_PARAM	\
	{ .name = NULL }

struct command_t {
	const char *progname;
	const char *description;
	struct param_t *params;
};

int parse_command_args(int argc, char **argv, void *cfg, struct command_t *server_command);

#endif // __BENCHMARK_COMMAND_H