/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

static struct fat_blkdrv *FatfsDrives[FF_VOLUMES];

int fatfs_register_blkdrv(struct fat_blkdrv *blkdrv, BYTE *out_pdrv)
{
	if (!blkdrv || !out_pdrv)
		return -1;

	for (BYTE i = 0; i < FF_VOLUMES; i++) {
		if (!FatfsDrives[i]) {
			FatfsDrives[i] = blkdrv;
			*out_pdrv = i;
			return 0;
		}
	}

	return -1;
}

void fatfs_unregister_blkdrv(BYTE pdrv, struct fat_blkdrv *blkdrv)
{
	if (pdrv >= FF_VOLUMES)
		return;
	if (!blkdrv || FatfsDrives[pdrv] == blkdrv)
		FatfsDrives[pdrv] = NULL;
}

static struct fat_blkdrv *fatfs_blkdrv(BYTE pdrv)
{
	if (pdrv >= FF_VOLUMES)
		return NULL;
	return FatfsDrives[pdrv];
}


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	struct geometry geo = { 0 };
	struct fat_blkdrv *blkdrv = fatfs_blkdrv(pdrv);

	if (!blkdrv)
		return STA_NODISK;

	if (blkdrv->fs_fd > 0) {
		int ret;
		ret = ioctl(blkdrv->fs_fd, BIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo));
		if (ret < 0)
			return STA_NODISK;

		if (!geo.geo_available || geo.geo_mediachanged)
			return STA_NODISK;

		return RES_OK;
	} else if (blkdrv->fs_blkdriver) {
		struct inode *inode = blkdrv->fs_blkdriver;
		if (!inode || !inode->u.i_bops || !inode->u.i_bops->geometry ||
		    inode->u.i_bops->geometry(inode, &geo) != OK ||
		    !geo.geo_available || geo.geo_mediachanged) {
			return STA_NODISK;
		}

		return RES_OK;
	}

	return STA_NODISK;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	if (!fatfs_blkdrv(pdrv))
		return STA_NODISK;
	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	struct fat_blkdrv *blkdrv = fatfs_blkdrv(pdrv);

	if (!blkdrv)
		return RES_NOTRDY;

	if (blkdrv->fs_fd > 0) {
		ssize_t nread;
		off_t seekpos;
		off_t fpos;

		/* Convert the sector number to a byte offset */
		fpos = sector << blkdrv->fs_hwsectshift;

		/* Seek to that offset */

		seekpos = lseek(blkdrv->fs_fd, fpos, SEEK_SET);
		if (seekpos == (off_t)-1) {
			return RES_ERROR;
		} else if (seekpos != fpos) {
			return RES_ERROR;
		}

		/* Read the sector to that offset.  Partial read are not expected. */

		nread = read(blkdrv->fs_fd, buff, count << blkdrv->fs_hwsectshift);
		if (nread < 0) {
			return RES_ERROR;
		} else if (nread != (ssize_t)(count << blkdrv->fs_hwsectshift)) {
			return RES_ERROR;
		}

		return RES_OK;
	} else if (blkdrv->fs_blkdriver) {
		struct inode *inode = blkdrv->fs_blkdriver;
		if (inode && inode->u.i_bops && inode->u.i_bops->read) {
			ssize_t nsectorsread = inode->u.i_bops->read(inode, buff, sector, count);
			if (nsectorsread == (ssize_t)count) {
				return RES_OK;
			}
		}
	}

	return RES_PARERR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	struct fat_blkdrv *blkdrv = fatfs_blkdrv(pdrv);

	if (!blkdrv)
		return RES_NOTRDY;

	if (blkdrv->fs_fd > 0) {
		ssize_t nwritten;
		off_t seekpos;
		off_t fpos;

		/* Convert the sector number to a byte offset */

		fpos = sector << blkdrv->fs_hwsectshift;

		/* Seek to that offset */

		seekpos = lseek(blkdrv->fs_fd, fpos, SEEK_SET);
		if (seekpos == (off_t)-1) {
			return RES_ERROR;
		} else if (seekpos != fpos) {
			return RES_ERROR;
		}

		/* Write the sector to that offset.  Partial writes are not expected. */

		nwritten = write(blkdrv->fs_fd, buff, count << blkdrv->fs_hwsectshift);
		if (nwritten < 0) {
			return RES_ERROR;
		} else if (nwritten != (ssize_t)(count << blkdrv->fs_hwsectshift)) {
			return RES_ERROR;
		}

		return RES_OK;
	} else if (blkdrv->fs_blkdriver) {
		struct inode *inode = blkdrv->fs_blkdriver;
		if (inode && inode->u.i_bops && inode->u.i_bops->write) {
			ssize_t nsectorswritten = inode->u.i_bops->write(inode, buff, sector, count);
			if (nsectorswritten == (ssize_t)count) {
				return RES_OK;
			}
		}
	}

	return RES_PARERR;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	struct fat_blkdrv *blkdrv = fatfs_blkdrv(pdrv);

	if (!blkdrv)
		return RES_NOTRDY;

	switch (cmd) {
	case CTRL_SYNC:
	case CTRL_TRIM:
		break;
	case GET_SECTOR_COUNT:
		*(LBA_t *)buff = (LBA_t)blkdrv->fs_hwnsectors;
		break;
	case GET_SECTOR_SIZE:
		*(WORD *)buff = (WORD)blkdrv->fs_hwsectorsize;
		break;
	case GET_BLOCK_SIZE:
		*(DWORD *)buff = (DWORD)blkdrv->fs_hwsectorsize;
		break;
	default:
		return RES_PARERR;
	}
	return RES_OK;
}

DWORD get_fattime(void)
{
	time_t t = time(NULL);
	struct tm tmr;
	localtime_r(&t, &tmr);
	int year = tmr.tm_year < 80 ? 0 : tmr.tm_year - 80;
	return ((DWORD)(year) << 25) | ((DWORD)(tmr.tm_mon + 1) << 21) |
	       ((DWORD)tmr.tm_mday << 16) | (WORD)(tmr.tm_hour << 11) |
	       (WORD)(tmr.tm_min << 5) | (WORD)(tmr.tm_sec >> 1);
}
