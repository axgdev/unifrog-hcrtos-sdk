#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <kernel/io.h>
#include <kernel/types.h>
#include <kernel/module.h>
#include <kernel/vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <kernel/lib/console.h>
#include <kernel/lib/fdt_api.h>
#include <hcuapi/sys-blocking-notify.h>
#include <linux/notifier.h>
#include <kernel/notify.h>

#include <kernel/list.h>
#include <nuttx/wqueue.h>

#ifndef UNIFROG_SD_EXPERIMENTAL
#define UNIFROG_SD_EXPERIMENTAL 0
#endif

#define CONFIG_FILEUART_TX_BUF_SIZE 65536
#define FILEUART_FLUSH_DELAY_TICKS \
	(UNIFROG_SD_EXPERIMENTAL ? 1000 : 100)
#define FILEUART_SYNC_INTERVAL_BYTES \
	(UNIFROG_SD_EXPERIMENTAL ? (256 * 1024) : (64 * 1024))

struct fileuart_dev
{
	int fd;
	uint8_t tx_buf[CONFIG_FILEUART_TX_BUF_SIZE];
	char devname[DISK_NAME_LEN];
	uint32_t tx_rd;
	uint32_t tx_wt;
	uint32_t tx_count;
	uint32_t dirty_bytes;
	uint32_t suspend_depth;
	struct work_s work;
	wait_queue_head_t wait;
	SemaphoreHandle_t sem;
};

static struct fileuart_dev g_dev = { 0 };

static void fileuart_delayed_work(void *parameter);

static int fileuart_open_log(const char *dev)
{
	char log_file_path[512] = { 0 };
	int flags = O_CREAT | O_WRONLY | O_APPEND;

	snprintf(log_file_path, sizeof(log_file_path), "/media/%s/log.txt", dev);

	return open(log_file_path, flags, 0666);
}

static void fileuart_close_log_locked(int clear_mount)
{
	if (g_dev.fd >= 0) {
		close(g_dev.fd);
		g_dev.fd = -1;
	}
	g_dev.dirty_bytes = 0;
	if (clear_mount)
		memset(g_dev.devname, 0, sizeof(g_dev.devname));
}

static int fileuart_open_mounted_log_locked(void)
{
	if (g_dev.suspend_depth > 0)
		return -EBUSY;
	if (g_dev.fd >= 0)
		return 0;
	if (!g_dev.devname[0])
		return -ENODEV;
	g_dev.fd = fileuart_open_log(g_dev.devname);
	if (g_dev.fd < 0)
		return -errno;
	g_dev.dirty_bytes = 0;
	return 0;
}

static void fileuart_enqueue_byte(uint8_t ch)
{
	g_dev.tx_buf[g_dev.tx_wt++] = ch;
	g_dev.tx_wt %= CONFIG_FILEUART_TX_BUF_SIZE;

	if (g_dev.tx_count < CONFIG_FILEUART_TX_BUF_SIZE) {
		g_dev.tx_count++;
	} else {
		g_dev.tx_rd++;
		g_dev.tx_rd %= CONFIG_FILEUART_TX_BUF_SIZE;
	}
}

static void fileuart_enqueue(const char *buf, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (buf[i] == '\n')
			fileuart_enqueue_byte('\r');
		fileuart_enqueue_byte(buf[i]);
	}
}

static void fileuart_flush_locked(void)
{
	if (g_dev.tx_count == 0 || fileuart_open_mounted_log_locked() != 0)
		return;

	while (g_dev.fd >= 0 && g_dev.tx_count > 0) {
		uint32_t chunk;
		ssize_t written;

		if (g_dev.tx_rd < g_dev.tx_wt)
			chunk = g_dev.tx_wt - g_dev.tx_rd;
		else
			chunk = CONFIG_FILEUART_TX_BUF_SIZE - g_dev.tx_rd;

		if (chunk > g_dev.tx_count)
			chunk = g_dev.tx_count;

		written = write(g_dev.fd, g_dev.tx_buf + g_dev.tx_rd, chunk);
		if (written <= 0) {
			fileuart_close_log_locked(0);
			return;
		}
		g_dev.tx_rd = (g_dev.tx_rd + (uint32_t)written) %
			CONFIG_FILEUART_TX_BUF_SIZE;
		g_dev.tx_count -= (uint32_t)written;
		g_dev.dirty_bytes += (uint32_t)written;
		if ((uint32_t)written < chunk)
			break;
	}

	if (g_dev.fd >= 0 && g_dev.dirty_bytes >= FILEUART_SYNC_INTERVAL_BYTES) {
		if (fsync(g_dev.fd) == 0)
			g_dev.dirty_bytes = 0;
		else
			fileuart_close_log_locked(0);
	}
}

static void fileuart_schedule_flush(void)
{
	if (g_dev.suspend_depth == 0 && g_dev.devname[0] &&
	    work_available(&g_dev.work))
		work_queue(LPWORK, &g_dev.work, fileuart_delayed_work, NULL,
			FILEUART_FLUSH_DELAY_TICKS);
}

static void fileuart_mount_locked(const char *dev)
{
	if (!dev || !dev[0])
		return;
	if (g_dev.devname[0] && strncmp(g_dev.devname, dev,
	    sizeof(g_dev.devname)) != 0)
		fileuart_close_log_locked(1);
	snprintf(g_dev.devname, sizeof(g_dev.devname), "%s", dev);
}

static int fileuart_matches_mount_locked(const char *dev)
{
	if (!dev || !g_dev.devname[0])
		return 0;
	return strncmp(g_dev.devname, dev, sizeof(g_dev.devname)) == 0;
}

static int fileuart_fs_mount_notify(struct notifier_block *self, unsigned long action, void *dev)
{
	switch (action) {		
	case USB_MSC_NOTIFY_MOUNT:
	case SDMMC_NOTIFY_MOUNT:
		xSemaphoreTake(g_dev.sem, portMAX_DELAY);
		fileuart_mount_locked((char *)dev);
		xSemaphoreGive(g_dev.sem);
		fileuart_schedule_flush();
		break;
	case USB_MSC_NOTIFY_UMOUNT:
	case SDMMC_NOTIFY_UMOUNT:
		xSemaphoreTake(g_dev.sem, portMAX_DELAY);
		if (fileuart_matches_mount_locked((char *)dev))
			fileuart_close_log_locked(1);
		xSemaphoreGive(g_dev.sem);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block fileuart_fs_mount = {
       .notifier_call = fileuart_fs_mount_notify,
};

static void fileuart_delayed_work(void *parameter)
{
	int retry;

	xSemaphoreTake(g_dev.sem, portMAX_DELAY);
	if (g_dev.suspend_depth == 0)
		fileuart_flush_locked();
	retry = g_dev.suspend_depth == 0 && g_dev.tx_count > 0 &&
		g_dev.devname[0];
	xSemaphoreGive(g_dev.sem);
	if (retry)
		fileuart_schedule_flush();
}

void fileuart_set_storage_suspended(int suspended)
{
	int resume;

	if (!g_dev.sem)
		return;

	xSemaphoreTake(g_dev.sem, portMAX_DELAY);
	if (suspended) {
		g_dev.suspend_depth++;
		fileuart_close_log_locked(0);
	} else if (g_dev.suspend_depth > 0) {
		g_dev.suspend_depth--;
	}
	resume = g_dev.suspend_depth == 0 && g_dev.tx_count > 0 &&
		g_dev.devname[0];
	xSemaphoreGive(g_dev.sem);

	if (resume)
		fileuart_schedule_flush();
}

static ssize_t fileuart_write(struct file *filep, const char *buf, size_t size)
{
	if (!uxInterruptNesting) {
		xSemaphoreTake(g_dev.sem, portMAX_DELAY);
		fileuart_enqueue(buf, size);
		xSemaphoreGive(g_dev.sem);
		fileuart_schedule_flush();
	} else {
		fileuart_enqueue(buf, size);
		fileuart_schedule_flush();
	}
	return size;
}

static int fileuart_poll(struct file *filep, poll_table *wait)
{
	int mask = 0;

	poll_wait(filep, &g_dev.wait, wait);

	mask |= POLLOUT | POLLWRNORM;

	return mask;
}

static const struct file_operations fileuart_fops = {
	.open = dummy_open,
	.close = dummy_close,
	.write = fileuart_write,
	.poll = fileuart_poll,
};

static int fileuart_driver_probe(const char *node)
{
	int np;
	const char *path;

	np = fdt_node_probe_by_path(node);
	if (np < 0)
		return 0;

	if (fdt_get_property_string_index(np, "devpath", 0, &path))
		return 0;

	g_dev.sem = xSemaphoreCreateMutex();
	g_dev.fd = -1;
	init_waitqueue_head(&g_dev.wait);
	sys_register_notify(&fileuart_fs_mount);

	register_driver(path, &fileuart_fops, 0666, NULL);

	return 0;
}

static int fileuart_driver_init(void)
{
	int rc = 0;
	rc = fileuart_driver_probe("/hcrtos/fileuart");
	return rc;
}

module_arch(fileuart, fileuart_driver_init, NULL, 0)
