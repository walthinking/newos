/*
** Copyright 2001, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#include <kernel/kernel.h>
#include <boot/stage2.h>
#include <kernel/debug.h>
#include <kernel/heap.h>
#include <kernel/fs/devfs.h>
#include <libc/string.h>
#include <sys/errors.h>

#include "rtl8139_priv.h"

/* allow multiple 8139s */
static rtl8139 *rtl = NULL;

static int rtl8139_open(const char *name, dev_cookie *cookie)
{
	if(!rtl)
		return ERR_IO_ERROR;
	
	return 0;
}

static int rtl8139_freecookie(dev_cookie cookie)
{
	return 0;
}

static int rtl8139_seek(dev_cookie cookie, off_t pos, seek_type st)
{
	return ERR_NOT_ALLOWED;
}

static int rtl8139_close(dev_cookie cookie)
{
	return 0;
}

static ssize_t rtl8139_read(dev_cookie cookie, void *buf, off_t pos, ssize_t len)
{
	if(len < 1500)
		return ERR_VFS_INSUFFICIENT_BUF;
	if(!rtl)
		return ERR_IO_ERROR;
	return rtl8139_rx(rtl, buf, len);
}

static ssize_t rtl8139_write(dev_cookie cookie, const void *buf, off_t pos, ssize_t len)
{
	if(len > 1500)
		return ERR_VFS_INSUFFICIENT_BUF;
	if(len < 0)
		return ERR_INVALID_ARGS;
	if(!rtl)
		return ERR_IO_ERROR;

	rtl8139_xmit(rtl, buf, len);
	return len;
}

static int rtl8139_ioctl(dev_cookie cookie, int op, void *buf, size_t len)
{
	int err;

	dprintf("rtl8139_ioctl: op %d, buf 0x%x, len %d\n", op, buf, len);

	if(!rtl)
		return ERR_IO_ERROR;
	
	switch(op) {
		case 10000: // get the ethernet MAC address
			if(len >= sizeof(rtl->mac_addr)) {
				memcpy(buf, rtl->mac_addr, sizeof(rtl->mac_addr));
				err = 0;
			} else {
				err = ERR_VFS_INSUFFICIENT_BUF;
			}
			break;
		default:
			err = ERR_INVALID_ARGS;
	}
	
	return err;
}

static struct dev_calls rtl8139_hooks = {
	&rtl8139_open,
	&rtl8139_close,
	&rtl8139_freecookie,
	&rtl8139_seek,
	&rtl8139_ioctl,
	&rtl8139_read,
	&rtl8139_write,
	/* no paging here */
	NULL,
	NULL,
	NULL
};

int rtl8139_dev_init(kernel_args *ka)
{
	rtl8139 *rtl;
	int fd;
	
	dprintf("rtl8139_dev_init: entry\n");

	// detect and setup the device
	if(rtl8139_detect(&rtl) < 0) {
		// no rtl8139 here
		dprintf("rtl8139_dev_init: no device found\n");
		return 0;
	}
	
	rtl8139_init(rtl);

	// create device node
	devfs_publish_device("net/rtl8139/0", &rtl8139_hooks);

	return 0;
}
