#define ELOG_OUTPUT_LVL ELOG_LVL_ERROR
#define LOG_TAG "AUTOMNT"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <hcuapi/sys-blocking-notify.h>
#include <kernel/list.h>
#include <kernel/module.h>
#include <kernel/elog.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <nuttx/wqueue.h>
#include <linux/ctype.h>
#include <kernel/notify.h>

static LIST_HEAD(__mounters);

#define AUTOMOUNT_RETRY_DELAY 1000
#define AUTOMOUNT_MAX_RETRIES 8

struct automounter_state_s {
	struct list_head list;
	char devname[DISK_NAME_LEN];
	bool mounted;
	bool notify_umount;
	unsigned int mount_attempts;
	struct work_s mount_dwork;
	struct work_s umount_dwork;
};

static struct automounter_state_s *automount_find(const char *devname)
{
	struct automounter_state_s *curr, *next;

	list_for_each_entry_safe (curr, next, &__mounters, list) {
		if(!strcmp(devname, curr->devname))
			return curr;
	}

	return NULL;
}

static void automount_schedule_retry(struct automounter_state_s *state);

static int automount_try_mount(struct automounter_state_s *state)
{
	char devname[DISK_NAME_LEN + 7];
	char mntname[DISK_NAME_LEN + 7];
	int vfat_errno = 0;
	int ntfs_errno = 0;

	if (!state || !state->devname[0])
		return -EINVAL;

	snprintf(devname, sizeof(devname), "/dev/%s", state->devname);
	snprintf(mntname, sizeof(mntname), "/media/%s", state->devname);

	(void)mkdir("/media", 0777);
	(void)mkdir(mntname, 0777);

	state->mount_attempts++;
	errno = 0;
	if (mount(devname, mntname, "vfat", 0, NULL) < 0) {
		vfat_errno = errno;
		if (vfat_errno == EBUSY)
			goto mounted;
		log_e("mount vfat fail %s errno %d, try ntfs\n",
			devname, vfat_errno);
		errno = 0;
		if (mount(devname, mntname, "ntfs", 0, NULL) < 0) {
			ntfs_errno = errno;
			if (ntfs_errno == EBUSY)
				goto mounted;
			sys_notify_event(USB_MSC_NOTIFY_MOUNT_FAIL,
				(void *)state->devname);
			log_e("mount fail %s attempt %u vfat_errno %d ntfs_errno %d\n",
				devname, state->mount_attempts,
				vfat_errno, ntfs_errno);
			automount_schedule_retry(state);
			return -EIO;
		}
	}

mounted:
	state->mounted = true;
	state->notify_umount = false;
	state->mount_attempts = 0;
	log_e("mounted %s on %s\n", state->devname, mntname);
	sys_notify_event(USB_MSC_NOTIFY_MOUNT, (void *)state->devname);
	return 0;
}

static void automount_mount_retry(void *arg)
{
	struct automounter_state_s *state = (struct automounter_state_s *)arg;

	if (!state || state->mounted)
		return;

	(void)automount_try_mount(state);
}

static void automount_schedule_retry(struct automounter_state_s *state)
{
	if (!state || state->mounted)
		return;
	if (state->mount_attempts >= AUTOMOUNT_MAX_RETRIES) {
		log_e("mount retry exhausted %s attempts %u\n",
			state->devname, state->mount_attempts);
		return;
	}
	if (work_available(&state->mount_dwork))
		work_queue(LPWORK, &state->mount_dwork, automount_mount_retry,
			(void *)state, AUTOMOUNT_RETRY_DELAY);
}

static void autoumount_fs(void *arg)
{
	struct automounter_state_s *state = (struct automounter_state_s *)arg;
	char mntname[DISK_NAME_LEN + 7];

	if (state == NULL)
		return;
	if (state->notify_umount == false) {
		sys_notify_event(USB_MSC_NOTIFY_UMOUNT, (void *)state->devname);
		state->notify_umount = true;
	}

	snprintf(mntname, sizeof(mntname), "/media/%s", state->devname);
	if (umount(mntname) < 0) {
		log_e("umount %s fail, try 2 seconds later\n", mntname);
		sys_notify_event(USB_MSC_NOTIFY_UMOUNT_FAIL, (void *)state->devname);
		if (work_available(&state->umount_dwork))
			work_queue(LPWORK, &state->umount_dwork,
				autoumount_fs, (void *)state, 2000);
		return;
	}

	log_e("umount %s\n", mntname);
	list_del_init(&state->list);
	free(state);

	return;
}

static void automount_fs(struct removable_notify_info *info)
{
	struct automounter_state_s *state;

	if (!info || !info->devname[0])
		return;

	state = automount_find(info->devname);
	if (state) {
		log_e("connect duplicate %s mounted %d attempts %u\n",
			state->devname, state->mounted, state->mount_attempts);
		if (state->mounted)
			sys_notify_event(USB_MSC_NOTIFY_MOUNT,
				(void *)state->devname);
		else
			(void)automount_try_mount(state);
		return;
	}

	state = malloc(sizeof(*state));
	if (!state) {
		log_e("No memory!\n");
		return;
	}

	memset(state, 0, sizeof(*state));

	INIT_LIST_HEAD(&state->list);
	memcpy(state->devname, info->devname, sizeof(state->devname));
	list_add_tail(&state->list, &__mounters);

	(void)automount_try_mount(state);
}

static void automount_disconnect(struct removable_notify_info *info)
{
	struct automounter_state_s *state;

	if (!info || !info->devname[0])
		return;
	state = automount_find(info->devname);
	if (!state)
		return;
	(void)work_cancel(LPWORK, &state->mount_dwork);
	if (!state->mounted) {
		list_del_init(&state->list);
		free(state);
		return;
	}
	autoumount_fs(state);
}

static int automount_notify(struct notifier_block *self,
			       unsigned long action, void *param)
{
	switch (action) {
	case USB_MSC_NOTIFY_CONNECT:
	case SDMMC_NOTIFY_CONNECT:
		automount_fs((struct removable_notify_info *)param);
		break;
	case USB_MSC_NOTIFY_DISCONNECT:
	case SDMMC_NOTIFY_DISCONNECT:
		automount_disconnect((struct removable_notify_info *)param);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block automount_nb = {
	.notifier_call = automount_notify,
};

static int automount_initialize(void)
{
	sys_register_notify(&automount_nb);
	return 0;
}

module_system(automount, automount_initialize, NULL, 4)
