#define LOG_TAG "MODULE"
#define ELOG_OUTPUT_LVL ELOG_LVL_ERROR

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/poll.h>
#include <kernel/list.h>
#include <kernel/ld.h>
#include <kernel/vfs.h>
#include <kernel/module.h>
#include <kernel/elog.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static unsigned int module_perf_ms(void)
{
	return (unsigned int)(((unsigned long long)xTaskGetTickCount() * 1000ull) /
		(unsigned long long)configTICK_RATE_HZ);
}

/* Keep module initialization visible in the retained UniFrog boot trace.
 * The hook is weak because this SDK is also used by firmware that does not
 * provide UniFrog's boot-trace runtime. Events 170 and 171 carry the module
 * name in two four-byte ASCII fields and the module timestamp in arg0. */
#define MODULE_TRACE_ENTER 170u
#define MODULE_TRACE_DONE 171u

extern void unifrog_boot_trace_mark(unsigned int event, unsigned int arg0,
	unsigned int arg1, unsigned int arg2) __attribute__((weak));

static void module_trace_name(const char *name, unsigned int *arg1,
	unsigned int *arg2)
{
	unsigned int first = 0;
	unsigned int second = 0;
	int i;

	if (!name)
		name = "";
	for (i = 0; i < 4 && name[i] != '\0'; i++)
		first = (first << 8) | (unsigned char)name[i];
	for (i = 0; i < 4 && name[i + 4] != '\0'; i++)
		second = (second << 8) | (unsigned char)name[i + 4];
	*arg1 = first;
	*arg2 = second;
}

static void module_trace_mark(unsigned int event, unsigned int timestamp,
	const char *name)
{
	unsigned int arg1;
	unsigned int arg2;

	if (!unifrog_boot_trace_mark)
		return;
	module_trace_name(name, &arg1, &arg2);
	unifrog_boot_trace_mark(event, timestamp, arg1, arg2);
}

int module_init2(const char *name, int n_exclude, char *excludes[])
{
	struct mod_init *mod_start = (struct mod_init *)&_module_init_start;
	struct mod_init *mod_end = (struct mod_init *)&_module_init_end;
	struct mod_init *p;
	int i, res;
	unsigned int group_start_ms;
	unsigned int module_start_ms;
	unsigned int module_done_ms;
	unsigned int initialized_count = 0;
	unsigned int skipped_count = 0;
	bool group_init = true;
	bool skip;

	if (!strcmp(name,"core")){
		log_d("init core\n");
		mod_start = (struct mod_init *)&_module_init_core_start;
		mod_end = (struct mod_init *)&_module_init_core_end;
	} else if (!strcmp(name,"postcore")){
		log_d("init postcore\n");
		mod_start = (struct mod_init *)&_module_init_postcore_start;
		mod_end = (struct mod_init *)&_module_init_postcore_end;
	} else if (!strcmp(name,"arch")){
		mod_start = (struct mod_init *)&_module_init_arch_start;
		mod_end = (struct mod_init *)&_module_init_arch_end;
	} else if (!strcmp(name,"system")){
		mod_start = (struct mod_init *)&_module_init_system_start;
		mod_end = (struct mod_init *)&_module_init_system_end;
	} else if (!strcmp(name,"driver")){
		mod_start = (struct mod_init *)&_module_init_driver_start;
		mod_end = (struct mod_init *)&_module_init_driver_end;
	} else if (!strcmp(name,"driver_late")){
		mod_start = (struct mod_init *)&_module_init_driverlate_start;
		mod_end = (struct mod_init *)&_module_init_driverlate_end;
	} else if (!strcmp(name,"others")){
		mod_start = (struct mod_init *)&_module_init_others_start;
		mod_end = (struct mod_init *)&_module_init_others_end;
	} else if (!strcmp(name, "all")) {
		log_d("init all\n");
		mod_start = (struct mod_init *)&_module_init_start;
		mod_end = (struct mod_init *)&_module_init_end;
	} else {
		group_init = false;
	}

	if (group_init) {
		group_start_ms = module_perf_ms();
		for (p = mod_start; p < mod_end; p++) {
			skip = false;
			for (i = 0; i < n_exclude; i++) {
				if (!strcmp(p->name, excludes[i])) {
					skip = true;
					break;
				}
			}

			if (skip || p->initialized == true) {
				skipped_count++;
				continue;
			}

			if (p->init) {
				log_d("module init %s\n", p->name);
				module_start_ms = module_perf_ms();
				module_trace_mark(MODULE_TRACE_ENTER, module_start_ms,
					p->name);
				res = p->init();
				module_done_ms = module_perf_ms();
				module_trace_mark(MODULE_TRACE_DONE, module_done_ms,
					p->name);
				printf("unifrog module_perf group=%s name=%s ret=%d ms=%u total_ms=%u\n",
					name, p->name, res,
					module_done_ms - module_start_ms,
					module_done_ms - group_start_ms);
				if (res) {
					log_w("    --> init %s failed.\n", p->name);
					return res;
				}
			}
			p->initialized = true;
			initialized_count++;
		}

		printf("unifrog module_perf group=%s done ret=0 modules=%u skipped=%u total_ms=%u\n",
			name, initialized_count, skipped_count,
			module_perf_ms() - group_start_ms);
		return 0;
	}

	for (p = mod_start; p < mod_end; p++) {
		if (!strcmp(p->name, name)) {
			if (p->initialized == true) {
				return 0;
			}
			if (p->init) {
				log_w("    --> init %s ...\n", p->name);
				module_start_ms = module_perf_ms();
				module_trace_mark(MODULE_TRACE_ENTER, module_start_ms,
					p->name);
				res = p->init();
				module_done_ms = module_perf_ms();
				module_trace_mark(MODULE_TRACE_DONE, module_done_ms,
					p->name);
				printf("unifrog module_perf group=single name=%s ret=%d ms=%u\n",
					p->name, res, module_done_ms - module_start_ms);
				if (res) {
					log_e("    --> init %s failed.\n", p->name);
					return res;
				}
				log_w("    --> init %s done\n", p->name);
			}
			p->initialized = true;

			return 0;
		}
	}

	return -ENODEV;
}

int module_init(const char *name)
{
	return module_init2(name, 0, NULL);
}

int module_exit2(const char *name, int n_exclude, char *excludes[])
{
	struct mod_init *mod_start = (struct mod_init *)&_module_init_start;
	struct mod_init *mod_end = (struct mod_init *)&_module_init_end;
	struct mod_init *p;
	int i;
	bool skip;

	if (!strcmp(name, "all")) {
		for (p = mod_start; p < mod_end; p++) {
			skip = false;
			for (i = 0; i < n_exclude; i++) {
				if (!strcmp(p->name, excludes[i])) {
					skip = true;
					break;
				}
			}

			if (skip || p->initialized == false) {
				continue;
			}
			if (p->exit)
				p->exit();
			p->initialized = false;
		}

		return 0;
	}

	for (p = mod_start; p < mod_end; p++) {
		if (!strcmp(p->name, name)) {
			if (p->initialized == false) {
				return 0;
			}
			if (p->exit)
				p->exit();
			p->initialized = false;

			return 0;
		}
	}

	return -ENODEV;
}

int module_exit(const char *name)
{
	return module_exit2(name, 0, NULL);
}
