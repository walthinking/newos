/*
** Copyright 2001, Manuel J. Petit. All rights reserved.
** Distributed under the terms of the NewOS License.
*/

#include <unistd.h>
#include <errno.h>
#include <sys/syscalls.h>

ssize_t
pwrite(int fd, void const *buf, size_t len, off_t pos)
{
	int retval;

	retval= _kern_write(fd, buf, pos, len);

	if(retval< 0) {
		errno = retval;
	}

	return retval;
}
