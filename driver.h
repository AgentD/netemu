#ifndef DRIVER_H
#define DRIVER_H


typedef enum {
	CMD_START = 1,
	CMD_STOP,
	CMD_GRAPH,
} DRIVER_COMMAND;


/* Indicates when to run a driver. The lower the number, the earlier */
typedef enum {
	/* run prior to any other drivers */
	DRV_INIT = 0,

	/* run this driver when instantiating nodes */
	DRV_PRIORITY_NODE = 1,

	/* run this driver when creating switches, after instantiating nodes */
	DRV_PRIORITY_SWITCH = 2,

	/* run this driver when creating arbitrary connections */
	DRV_PRIORITY_CONNECTION = 3,

	/* run after all other drivers */
	DRV_FINISH = 4,

	/* maximum numbe of priority levels */
	DRV_PRIORITY_MAX
} DRIVER_PRIORITY;



typedef struct driver_t {
	int (*start)(struct driver_t *drv);

	int (*stop)(struct driver_t *drv);

	int (*gen_graph)(struct driver_t *drv);

	struct driver_t *next;
} driver_t;


#define EXPORT_DRIVER(drv, prio) \
	static void __attribute__((constructor)) register_##drv(void) { \
		driver_register(&(drv), (prio)); \
	}



int driver_register(driver_t *drv, int priority);

int driver_run(int command);

int driver_command_from_str(const char *str);

#endif /* DRIVER_H */

