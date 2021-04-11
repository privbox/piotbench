#include <stdio.h>
#include <string.h>
#define NEED_DEBUG 1
#include "include/debug.h"
#include "include/command.h"

void show_uint_param(struct param_t *param, unsigned char *opaque)
{
	unsigned int val = *(unsigned int *)opaque;
	debug("%s: %u", param->name, val);
}
void show_int_param(struct param_t *param, unsigned char *opaque)
{
	int val = *(int *)opaque;
	debug("%s: %d", param->name, val);
}
void show_str_param(struct param_t *param, unsigned char *opaque)
{
	const char *val = (const char *)opaque;
	debug("%s: \"%s\"", param->name, val);
}

static int param_try_set(const char *arg, struct param_t *param, void *cfg) {
	unsigned char dest[PARAM_VALUE_MAX];
	int res = sscanf(arg, param->fmt, dest) == 1;
	if (res == 1)
		memcpy(cfg + param->offset, dest, param->size);
	return res;
}

static void param_set_default(struct param_t *param, void *cfg)
{
	memcpy(cfg + param->offset, &param->default_.str, param->size);
}

#define FOREACH_PARAMS(cursor, params)			\
	for (struct param_t *cursor = &params[0]; cursor->name; cursor++)

static int parse_params(int argc, char **argv, void *cfg, struct param_t *params)
{
	FOREACH_PARAMS(p, params)
		param_set_default(p, cfg);

	for (int i = 1; i < argc; i++)
	{
		int found = 0;
		FOREACH_PARAMS(p, params)
		{
			if (param_try_set(argv[i], p, cfg))
			{
				found = 1;
				break;
			}
		}
		if (!found)
		{
			debug("Invalid parameter: %s", argv[i]);
			return -1;
		}
	}

	FOREACH_PARAMS(p, params)
		p->show_fn(p, cfg + p->offset);

	return 0;
}

int parse_command_args(int argc, char **argv, void *cfg, struct command_t *command)
{
	int res = parse_params(argc, argv, cfg, command->params);
#if 0
	if (res) {
		show_usage(command);
	}
#endif
	return res;
}