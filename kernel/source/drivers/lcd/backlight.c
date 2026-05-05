#define ELOG_OUTPUT_LVL ELOG_LVL_ERROR

#define MODULE_NAME "/dev/backlight"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <kernel/io.h>
#include <kernel/ld.h>
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include <kernel/lib/console.h>
#include <kernel/elog.h>
#include <kernel/lib/fdt_api.h>
#include <kernel/module.h>
#include <hcuapi/gpio.h>
#include <dt-bindings/gpio/gpio.h>
#include <hcuapi/pinpad.h>
#include <hcuapi/pinmux.h>
#include <kernel/drivers/hc_clk_gate.h>
#include <nuttx/pwm/pwm.h>
#include <kernel/delay.h>
#include "lcd_main.h"

#define BACKLIGHT_PINPAD_GPIO_NUM 5

typedef struct pinpad_gpio
{
	u32 padctl;
	bool active;
}pinpad_gpio_s;

typedef struct pinpad_gpio_pack
{
	pinpad_gpio_s pad[BACKLIGHT_PINPAD_GPIO_NUM];
	u32 num;
}pinpad_gpio_pack_t;

struct pwm_bl_data {
	pinpad_gpio_pack_t gpio_backlight;
	u32 max_brightness;
	u32 *levels;
	u32 dft_brightness;
	u32 dft_brightness_max;
	struct pwm_info_s blpwm_info;
	const char *pwmbl_path;
	int duty_cycle;
	unsigned int scale;
	int lcd_default_off;
	struct pinmux_setting *pwm_active_state;
};

static struct pwm_bl_data *pbldev = NULL;

#define BOOT_TRACE_SDK_BACKLIGHT_PROBE_BEGIN 210u
#define BOOT_TRACE_SDK_BACKLIGHT_DEFAULT_OFF 211u
#define BOOT_TRACE_SDK_BACKLIGHT_PWM_DUTY 212u
#define BOOT_TRACE_SDK_BACKLIGHT_GPIO_STATUS 213u
#define BOOT_TRACE_SDK_BACKLIGHT_WRITE 214u

extern void unifrog_boot_trace_mark(unsigned int event, unsigned int arg0,
				    unsigned int arg1, unsigned int arg2)
	__attribute__((weak));

static void boot_trace_mark(unsigned int event, unsigned int arg0,
			    unsigned int arg1, unsigned int arg2)
{
	if (unifrog_boot_trace_mark)
		unifrog_boot_trace_mark(event, arg0, arg1, arg2);
}

static unsigned backlight_r05_mux(void)
{
	return ((volatile unsigned char *)&PINMUXR)[PINPAD_R05 - 64];
}

static u32 backlight_clamp_percent(u32 level)
{
	return level > 100 ? 100 : level;
}

static u32 backlight_duty_to_percent(u32 duty)
{
	u32 scale = pbldev->scale ? pbldev->scale : 100;

	if(duty > scale)
		duty = scale;
	return (duty * 100 + scale / 2) / scale;
}

static u32 backlight_level_to_duty(u32 level)
{
	u32 scale = pbldev->scale ? pbldev->scale : 100;

	level = backlight_clamp_percent(level);
	if(pbldev->levels && pbldev->max_brightness > 0)
	{
		u32 best = 0;
		u32 best_delta = 101;
		u32 i;

		for(i = 0;i < pbldev->max_brightness;i++)
		{
			u32 percent = backlight_duty_to_percent(pbldev->levels[i]);
			u32 delta = percent > level ? percent - level : level - percent;
			if(delta < best_delta)
			{
				best = i;
				best_delta = delta;
			}
		}
		return pbldev->levels[best] > scale ? scale : pbldev->levels[best];
	}

	return (level * scale + 50) / 100;
}

static int backlight_set_pwm_duty(u32 level)
{
	struct pwm_info_s info = pbldev->blpwm_info;
	u32 duty_cycle;
	int fd;
	int ret_set;
	int ret_start;

	if(pbldev->pwmbl_path == NULL)
		return -1;
	if(info.period_ns == 0)
		return -1;

	if(pbldev->pwm_active_state)
		pinmux_select_setting(pbldev->pwm_active_state);

	fd = open(pbldev->pwmbl_path, O_RDWR);
	if(fd < 0)
	{
		printf("unifrog backlight pwm open_fail path=%s level=%lu mux_r05=%u\n",
			pbldev->pwmbl_path,(unsigned long)level,backlight_r05_mux());
		return -1;
	}

	duty_cycle = backlight_level_to_duty(level);
	info.duty_ns = info.period_ns * duty_cycle / (pbldev->scale ? pbldev->scale : 100);
	ret_set = ioctl(fd, PWMIOC_SETCHARACTERISTICS, &info);
	ret_start = ret_set == 0 ? ioctl(fd, PWMIOC_START, 0) : ret_set;
	close(fd);
	boot_trace_mark(BOOT_TRACE_SDK_BACKLIGHT_PWM_DUTY, level, duty_cycle,
		((unsigned int)(ret_set & 0xffff) << 16) |
		(unsigned int)(ret_start & 0xffff));
	printf("unifrog backlight pwm level=%lu duty=%lu duty_ns=%lu period_ns=%lu polarity=%d mux_r05=%u ret_set=%d ret_start=%d\n",
		(unsigned long)level,(unsigned long)duty_cycle,
		(unsigned long)info.duty_ns,(unsigned long)info.period_ns,info.polarity,
		backlight_r05_mux(),ret_set,ret_start);
	if(ret_set != 0 || ret_start != 0)
		return -1;
	return 0;
}

static void backlight_set_gpio_status(char value)
{
	u32 i=0;
	if(!pbldev)
		return;
	for(i=0;i<pbldev->gpio_backlight.num;i++)
	{
		gpio_configure(pbldev->gpio_backlight.pad[i].padctl, GPIO_DIR_OUTPUT);
		if(value != 0)
			lcd_gpio_set_output(pbldev->gpio_backlight.pad[i].padctl, !pbldev->gpio_backlight.pad[i].active);
		else
			lcd_gpio_set_output(pbldev->gpio_backlight.pad[i].padctl, pbldev->gpio_backlight.pad[i].active);
	}
	boot_trace_mark(BOOT_TRACE_SDK_BACKLIGHT_GPIO_STATUS, value,
		pbldev->gpio_backlight.num, 0);
	log_d("%s %d gpios=%ld value=%d\n",__func__,__LINE__,pbldev->gpio_backlight.num,value);
}

static int backlight_apply_level(u32 level)
{
	level = backlight_clamp_percent(level);
	if(level == 0)
	{
		if(pbldev->pwmbl_path)
			(void)backlight_set_pwm_duty(0);
		backlight_set_gpio_status(0);
		pbldev->duty_cycle = 0;
		return 0;
	}

	if(pbldev->pwmbl_path)
	{
		if(backlight_set_pwm_duty(level) == 0)
		{
			pbldev->duty_cycle = backlight_duty_to_percent(backlight_level_to_duty(level));
			return 0;
		}
		backlight_set_gpio_status(1);
		pbldev->duty_cycle = 100;
		return -EIO;
	}

	backlight_set_gpio_status(1);
	pbldev->duty_cycle = 100;
	return 0;
}

static int backlight_close(struct file *filep)
{
	return 0;
}

static int backlight_open(struct file *filep)
{
	return 0;
}

static ssize_t backlight_write(struct file *filep, const char *buffer, size_t buflen)
{
	u32 level;

	if(buffer == NULL)
		return -EFAULT;
	if(buflen == 0)
		return 0;
	log_d("%s %d %d %ld\n",__func__,__LINE__,buffer[0],buflen);

	level = (unsigned char)buffer[0];
	if(backlight_apply_level(level) != 0)
		return -EIO;
	boot_trace_mark(BOOT_TRACE_SDK_BACKLIGHT_WRITE, level, pbldev->duty_cycle, 0);
	printf("unifrog backlight write level=%lu current=%d\n",
		(unsigned long)level,pbldev->duty_cycle);
	return buflen;
}

static ssize_t backlight_read(struct file *filep, char *buffer, size_t buflen)
{
	if(buffer == NULL)
		return -EFAULT;
	if(buflen == 0)
		return 0;

	buffer[0] = (char)pbldev->duty_cycle;

	return buflen;
}

static const struct file_operations backlight_fops = {
	.open = backlight_open,
	.close = backlight_close,
	.read = backlight_read,
	.write = backlight_write,
};

static int backlight_probe(const char *node)
{
	int ret;
	u32 tmpVal = 0;
	u32 i = 0;
	const char *path=NULL;
	int np = fdt_node_probe_by_path(node);

	if(np < 0){
		goto error;
	}

	boot_trace_mark(BOOT_TRACE_SDK_BACKLIGHT_PROBE_BEGIN, (unsigned int)np, 0, 0);
	log_d("%s %d\n",__func__,__LINE__);
	if(pbldev != NULL) return 0;

	pbldev = (struct pwm_bl_data *)malloc(sizeof(struct pwm_bl_data));
	if(pbldev == NULL)
	{
		log_e("malloc error\n");
		goto malloc_error;
	}

	memset(pbldev, 0, sizeof(struct pwm_bl_data));

	if (fdt_get_property_data_by_name(np, "backlight-gpios-rtos", &pbldev->gpio_backlight.num) == NULL)
	{
		pbldev->gpio_backlight.num = 0;
	}
	else
	{
		pbldev->dft_brightness = 1;
	}

	pbldev->gpio_backlight.num >>= 3;

	if(pbldev->gpio_backlight.num > BACKLIGHT_PINPAD_GPIO_NUM)
		pbldev->gpio_backlight.num = BACKLIGHT_PINPAD_GPIO_NUM;

	for(i=0;i<pbldev->gpio_backlight.num;i++){
		pbldev->gpio_backlight.pad[i].padctl = PINPAD_INVALID;
		pbldev->gpio_backlight.pad[i].active = GPIO_ACTIVE_HIGH;

		if(fdt_get_property_u_32_index(np, "backlight-gpios-rtos", i * 2, &tmpVal)==0)
			pbldev->gpio_backlight.pad[i].padctl = tmpVal;
		if(fdt_get_property_u_32_index(np, "backlight-gpios-rtos", i * 2 + 1, &tmpVal)==0)
			pbldev->gpio_backlight.pad[i].active = tmpVal;
		log_d("%s %d pad = %d active = %d\n",__func__,__LINE__,pbldev->gpio_backlight.pad[i].padctl,pbldev->gpio_backlight.pad[i].active);
	}

	pbldev->dft_brightness_max = 1;
	if (fdt_get_property_string_index(np, "backlight-pwmdev", 0, &path)==0)
	{
		pbldev->pwmbl_path = path;
		pbldev->pwm_active_state = fdt_get_property_pinmux(np, "active");
		

		pbldev->max_brightness = 0;
		if (fdt_get_property_data_by_name(np, "brightness-levels", &pbldev->max_brightness) == NULL)
			pbldev->max_brightness = 0;

		pbldev->max_brightness >>= 2;
		
		if(pbldev->max_brightness > 0)
		{
			pbldev->levels = malloc(pbldev->max_brightness * sizeof(u32));
			if(pbldev->levels)
			{
				for(i = 0;i < pbldev->max_brightness;i++)
				{
					fdt_get_property_u_32_index(np, "brightness-levels", i, &pbldev->levels[i]);
					log_d("%s %d brightness-levels = %d\n",__func__,__LINE__,pbldev->levels[i]);
				}
			}
			pbldev->scale = 0;
			if (pbldev->levels) {
				for (i = 0; i < pbldev->max_brightness; i++)
					if (pbldev->levels[i] > pbldev->scale)
					{
						pbldev->dft_brightness_max = i;
						pbldev->scale = pbldev->levels[i];
					}
				if(pbldev->scale == 0)
					pbldev->scale = 100;
			} else {
				pbldev->scale = 100;
			}
		}
		else
		{
			pbldev->levels = NULL;
			pbldev->scale = 100;
		}
		ret = fdt_get_property_u_32_index(np, "default-brightness-level", 0, &tmpVal);
		if(ret == 0) {
			if(pbldev->levels && pbldev->max_brightness > 0) {
				if(tmpVal >= pbldev->max_brightness)
					tmpVal = pbldev->max_brightness - 1;
				pbldev->dft_brightness = backlight_duty_to_percent(pbldev->levels[tmpVal]);
			} else {
				pbldev->dft_brightness = backlight_clamp_percent(tmpVal);
			}
		} else if(pbldev->levels && pbldev->max_brightness > 0) {
			pbldev->dft_brightness = backlight_duty_to_percent(
				pbldev->levels[pbldev->dft_brightness_max]);
		} else {
			pbldev->dft_brightness = 100;
		}

		ret = fdt_get_property_u_32_index(np, "backlight-frequency", 0, &tmpVal);
		if(ret == 0)
			pbldev->blpwm_info.period_ns = 1000000000/tmpVal;
		ret = fdt_get_property_u_32_index(np, "polarity", 0, &tmpVal);
		if(ret == 0)
			pbldev->blpwm_info.polarity = tmpVal ? true : false;

		printf("unifrog backlight probe pwm=%s period_ns=%lu polarity=%d levels=%lu default=%lu scale=%u max_index=%lu mux_r05=%u active_state=%d\n",
			pbldev->pwmbl_path,(unsigned long)pbldev->blpwm_info.period_ns,
			pbldev->blpwm_info.polarity,(unsigned long)pbldev->max_brightness,
			(unsigned long)pbldev->dft_brightness,pbldev->scale,
			(unsigned long)pbldev->dft_brightness_max,backlight_r05_mux(),
			pbldev->pwm_active_state ? 1 : 0);
	}
	else
	{
		pbldev->pwmbl_path = NULL;
	}

	pbldev->duty_cycle = pbldev->dft_brightness;
	pbldev->lcd_default_off = fdt_property_read_bool(np, "default-off");
	if(pbldev->lcd_default_off) {
		boot_trace_mark(BOOT_TRACE_SDK_BACKLIGHT_DEFAULT_OFF,
			pbldev->dft_brightness, pbldev->duty_cycle, 0);
		goto backlight_register;
	}

	backlight_apply_level(pbldev->dft_brightness);

backlight_register:
	return register_driver(MODULE_NAME, &backlight_fops, 0666, NULL);
malloc_error:
error:
	pbldev=NULL;
	return 0;
}

static int backlight_init(void)
{
	backlight_probe("/hcrtos/backlight");
	return 0;
}

static int backlight_exit(void)
{
	if(pbldev!=NULL) {
		if(pbldev->levels)
			free(pbldev->levels);
		if(pbldev->pwm_active_state)
			free(pbldev->pwm_active_state);
		free(pbldev);
		pbldev = NULL;
	}
	unregister_driver(MODULE_NAME);
	return 0;
}

module_driver(backlight, backlight_init, backlight_exit, 2)
