#include "../driver.h"

#include <stdio.h>


static int init_graph(driver_t *drv)
{
	(void)drv;
	printf("graph network {\n");
	return 0;
}

static int finish_graph(driver_t *drv)
{
	(void)drv;
	printf("}\n");
	return 0;
}


static driver_t graph_init_drv = {
	.start = NULL,
	.stop = NULL,
	.gen_graph = init_graph,
};

static driver_t graph_finish_drv = {
	.start = NULL,
	.stop = NULL,
	.gen_graph = finish_graph,
};


EXPORT_DRIVER(graph_init_drv, DRV_INIT)
EXPORT_DRIVER(graph_finish_drv, DRV_FINISH)
