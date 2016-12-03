#include <string.h>
#include <stdio.h>

#include "driver.h"


static driver_t *drivers[DRV_PRIORITY_MAX];


static const struct {
	const char *name;
	int cmd;
} command_map[] = {
	{ "start", CMD_START },
	{ "stop", CMD_STOP },
	{ "graph", CMD_GRAPH },
};


int driver_register(driver_t *drv, int priority)
{
	if (priority < 0 || priority > DRV_PRIORITY_MAX) {
		fputs("Tried to register a driver with "
			"invalid priority", stderr);
		return -1;
	}

	drv->next = drivers[priority];
	drivers[priority] = drv;
	return 0;
}

int driver_run(int command)
{
	driver_t *drv;
	int i, ret;

	if (command == CMD_STOP) {
		for (i = DRV_PRIORITY_MAX - 1; i >= 0; --i) {
			for (drv = drivers[i]; drv != NULL; drv = drv->next) {
				ret = drv->stop ? drv->stop(drv) : 0;
			}
		}
	} else {
		for (i = 0; i < DRV_PRIORITY_MAX; ++i) {
			for (drv = drivers[i]; drv != NULL; drv = drv->next) {
				ret = 0;

				switch (command) {
				case CMD_START:
					if (drv->start)
						ret = drv->start(drv);
					break;
				case CMD_GRAPH:
					if (drv->gen_graph)
						ret = drv->gen_graph(drv);
					break;
				default:
					goto fail_cmd;
				}

				if (ret)
					return -1;
			}
		}
	}

	return 0;
fail_cmd:
	fputs("[BUG][driver_run] unkown command\n", stderr);
	return -1;
}

int driver_command_from_str(const char *str)
{
	size_t i;

	for (i = 0; i < sizeof(command_map) / sizeof(command_map[0]); ++i) {
		if (!strcmp(command_map[i].name, str))
			return command_map[i].cmd;
	}

	return 0;
}
