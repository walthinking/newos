/* 
** Copyright 2001, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#include <kernel/kernel.h>
#include <kernel/vfs.h>
#include <kernel/debug.h>
#include <kernel/khash.h>
#include <kernel/heap.h>
#include <kernel/lock.h>
#include <kernel/vm.h>
#include <sys/errors.h>

#include <kernel/arch/cpu.h>

#include <kernel/fs/devfs.h>

#include <libc/string.h>
#include <libc/printf.h>

#define DEVFS_TRACE 0

#if DEVFS_TRACE
#define TRACE(x) dprintf x
#else
#define TRACE(x)
#endif

struct devfs_stream {
	stream_type type;
	union {
		struct stream_dir {
			struct devfs_vnode *dir_head;
			struct devfs_cookie *jar_head;
		} dir;
		struct stream_dev {
			char *full_name;
			struct dev_calls *calls;
		} dev;
	} u;
};

struct devfs_vnode {
	struct devfs_vnode *all_next;
	vnode_id id;
	char *name;
	void *redir_vnode;
	struct devfs_vnode *parent;
	struct devfs_vnode *dir_next;
	struct devfs_stream stream;
};

struct devfs {
	fs_id id;
	mutex lock;
	int next_vnode_id;
	void *vnode_list_hash;
	struct devfs_vnode *root_vnode;
};

struct devfs_cookie {
	struct devfs_stream *s;
	int oflags;
	union {
		struct cookie_dir {
			struct devfs_cookie *next;
			struct devfs_cookie *prev;
			struct devfs_vnode *ptr;
		} dir;
		struct cookie_dev {
			dev_cookie dcookie;
		} dev;
	} u;
};

/* the one and only allowed devfs instance */
static struct devfs *thedevfs = NULL;

#define BOOTFS_HASH_SIZE 16
static unsigned int devfs_vnode_hash_func(void *_v, void *_key, int range)
{
	struct devfs_vnode *v = _v;
	vnode_id *key = _key;

	if(v != NULL)
		return v->id % range;
	else
		return (*key) % range;
}

static int devfs_vnode_compare_func(void *_v, void *_key)
{
	struct devfs_vnode *v = _v;
	vnode_id *key = _key;

	if(v->id == *key)
		return 0;
	else
		return -1;
}

static struct devfs_vnode *devfs_create_vnode(struct devfs *fs, const char *name)
{
	struct devfs_vnode *v;
	
	v = kmalloc(sizeof(struct devfs_vnode));
	if(v == NULL)
		return NULL;

	memset(v, 0, sizeof(struct devfs_vnode));
	v->id = fs->next_vnode_id++;

	v->name = kmalloc(strlen(name) + 1);
	if(v->name == NULL) {
		kfree(v);
		return NULL;
	}
	strcpy(v->name, name);

	return v;
}

static int devfs_delete_vnode(struct devfs *fs, struct devfs_vnode *v, bool force_delete)
{
	// cant delete it if it's in a directory or is a directory
	// and has children
	if(!force_delete && ((v->stream.type == STREAM_TYPE_DIR && v->stream.u.dir.dir_head != NULL) || v->dir_next != NULL)) {
		return ERR_NOT_ALLOWED;
	}

	// remove it from the global hash table
	hash_remove(fs->vnode_list_hash, v);

	if(v->stream.type == STREAM_TYPE_DEVICE)
		if(v->stream.u.dev.full_name)
			kfree(v->stream.u.dev.full_name);
	if(v->name != NULL)
		kfree(v->name);
	kfree(v);

	return 0;
}

static void insert_cookie_in_jar(struct devfs_vnode *dir, struct devfs_cookie *cookie)
{
	cookie->u.dir.next = dir->stream.u.dir.jar_head;
	dir->stream.u.dir.jar_head = cookie;
	cookie->u.dir.prev = NULL;
}

static void remove_cookie_from_jar(struct devfs_vnode *dir, struct devfs_cookie *cookie)
{
	if(cookie->u.dir.next)
		cookie->u.dir.next->u.dir.prev = cookie->u.dir.prev;
	if(cookie->u.dir.prev)
		cookie->u.dir.prev->u.dir.next = cookie->u.dir.next;
	if(dir->stream.u.dir.jar_head == cookie)
		dir->stream.u.dir.jar_head = cookie->u.dir.next;

	cookie->u.dir.prev = cookie->u.dir.next = NULL;
}

/* makes sure none of the dircookies point to the vnode passed in */
static void update_dircookies(struct devfs_vnode *dir, struct devfs_vnode *v)
{
	struct devfs_cookie *cookie;
	
	for(cookie = dir->stream.u.dir.jar_head; cookie; cookie = cookie->u.dir.next) {
		if(cookie->u.dir.ptr == v) {
			cookie->u.dir.ptr = v->dir_next;
		}
	}
}


static struct devfs_vnode *devfs_find_in_dir(struct devfs_vnode *dir, const char *path)
{
	struct devfs_vnode *v;

	if(dir->stream.type != STREAM_TYPE_DIR)
		return NULL;
	
	if(!strcmp(path, "."))
		return dir;
	if(!strcmp(path, ".."))
		return dir->parent;

	for(v = dir->stream.u.dir.dir_head; v; v = v->dir_next) {
//		dprintf("devfs_find_in_dir: looking at entry '%s'\n", v->name);
		if(strcmp(v->name, path) == 0) {
//			dprintf("devfs_find_in_dir: found it at 0x%x\n", v);
			return v;
		}
	}
	return NULL;
}

static int devfs_insert_in_dir(struct devfs_vnode *dir, struct devfs_vnode *v)
{
	if(dir->stream.type != STREAM_TYPE_DIR)
		return ERR_INVALID_ARGS;

	v->dir_next = dir->stream.u.dir.dir_head;
	dir->stream.u.dir.dir_head = v;
	
	v->parent = dir;
	return 0;
}

static int devfs_remove_from_dir(struct devfs_vnode *dir, struct devfs_vnode *findit)
{
	struct devfs_vnode *v;
	struct devfs_vnode *last_v;

	for(v = dir->stream.u.dir.dir_head, last_v = NULL; v; last_v = v, v = v->dir_next) {
		if(v == findit) {
			/* make sure all dircookies dont point to this vnode */
			update_dircookies(dir, v);

			if(last_v)
				last_v->dir_next = v->dir_next;
			else
				dir->stream.u.dir.dir_head = v->dir_next;
			v->dir_next = NULL;
			return 0;
		}
	}
	return -1;
}

static int devfs_is_dir_empty(struct devfs_vnode *dir)
{
	if(dir->stream.type != STREAM_TYPE_DIR)
		return false;
	return !dir->stream.u.dir.dir_head;
}

static int devfs_mount(fs_cookie *_fs, fs_id id, void *flags, vnode_id *root_vnid)
{
	struct devfs *fs;
	struct devfs_vnode *v;
	int err;

	TRACE(("devfs_mount: entry\n"));

	if(thedevfs) {
		dprintf("double mount of devfs attempted\n");
		err = ERR_GENERAL;
		goto err;
	}

	fs = kmalloc(sizeof(struct devfs));
	if(fs == NULL) {
		err = ERR_NO_MEMORY;
		goto err;
	}
	
	fs->id = id;
	fs->next_vnode_id = 0;

	err = mutex_init(&fs->lock, "devfs_mutex");
	if(err < 0) {
		goto err1;
	}
	
	fs->vnode_list_hash = hash_init(BOOTFS_HASH_SIZE, (addr)&v->all_next - (addr)v,
		&devfs_vnode_compare_func, &devfs_vnode_hash_func);
	if(fs->vnode_list_hash == NULL) {
		err = ERR_NO_MEMORY;
		goto err2;
	}

	// create a vnode
	v = devfs_create_vnode(fs, "");
	if(v == NULL) {
		err = ERR_NO_MEMORY;
		goto err3;
	}

	// set it up	
	v->parent = v;
	
	// create a dir stream for it to hold
	v->stream.type = STREAM_TYPE_DIR;
	v->stream.u.dir.dir_head = NULL;
	v->stream.u.dir.jar_head = NULL;
	fs->root_vnode = v;

	hash_insert(fs->vnode_list_hash, v);

	*root_vnid = v->id;
	*_fs = fs;

	thedevfs = fs;
	
	return 0;
	
err4:
	devfs_delete_vnode(fs, v, true);
err3:
	hash_uninit(fs->vnode_list_hash);
err2:
	mutex_destroy(&fs->lock);
err1:	
	kfree(fs);
err:
	return err;
}

int devfs_unmount(fs_cookie _fs)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v;
	struct hash_iterator i;
	
	TRACE(("devfs_unmount: entry fs = 0x%x\n", fs));
	
	// delete all of the vnodes
	hash_open(fs->vnode_list_hash, &i);
	while((v = (struct devfs_vnode *)hash_next(fs->vnode_list_hash, &i)) != NULL) {
		devfs_delete_vnode(fs, v, true);
	}
	hash_close(fs->vnode_list_hash, &i, false);
	
	hash_uninit(fs->vnode_list_hash);
	mutex_destroy(&fs->lock);
	kfree(fs);
	
	return 0;
}

static int devfs_sync(fs_cookie fs)
{
	TRACE(("devfs_sync: entry\n"));
	
	return 0;
}

static int devfs_lookup(fs_cookie _fs, fs_vnode _dir, const char *name, vnode_id *id)
{
	struct devfs *fs = (struct devfs *)_fs;
	struct devfs_vnode *dir = (struct devfs_vnode *)_dir;
	struct devfs_vnode *v;
	struct devfs_vnode *v1;
	int err;

	TRACE(("devfs_lookup: entry dir 0x%x, name '%s'\n", dir, name));

	if(dir->stream.type != STREAM_TYPE_DIR)
		return ERR_VFS_NOT_DIR;

	mutex_lock(&fs->lock);

	// look it up
	v = devfs_find_in_dir(dir, name);
	if(!v) {
		err = ERR_NOT_FOUND;
		goto err;
	}

	err = vfs_get_vnode(fs->id, v->id, (fs_vnode *)&v1);
	if(err < 0) {
		goto err;
	}

	*id = v->id;

	err = NO_ERROR;

err:
	mutex_unlock(&fs->lock);

	return err;
}

static int devfs_getvnode(fs_cookie _fs, vnode_id id, fs_vnode *v, bool r)
{
	struct devfs *fs = (struct devfs *)_fs;
	int err;

	TRACE(("devfs_getvnode: asking for vnode 0x%x 0x%x, r %d\n", id, r));

	if(!r)
		mutex_lock(&fs->lock);

	*v = hash_lookup(fs->vnode_list_hash, &id);

	if(!r)
		mutex_unlock(&fs->lock);

	TRACE(("devfs_getnvnode: looked it up at 0x%x\n", *v));

	if(*v)
		return 0;
	else
		return ERR_NOT_FOUND;
}

static int devfs_putvnode(fs_cookie _fs, fs_vnode _v, bool r)
{
	struct devfs_vnode *v = (struct devfs_vnode *)_v;

	TRACE(("devfs_putvnode: entry on vnode 0x%x 0x%x, r %d\n", v->id, r));
	
	return 0; // whatever
}

static int devfs_removevnode(fs_cookie _fs, fs_vnode _v, bool r)
{
	struct devfs *fs = (struct devfs *)_fs;
	struct devfs_vnode *v = (struct devfs_vnode *)_v;
	struct devfs_vnode dummy;
	int err;

	TRACE(("devfs_removevnode: remove 0x%x (0x%x 0x%x), r %d\n", v, v->id, r));

	if(!r)
		mutex_lock(&fs->lock);

	if(v->dir_next) {
		// can't remove node if it's linked to the dir
		panic("devfs_removevnode: vnode 0x%x asked to be removed is present in dir\n");
	}

	devfs_delete_vnode(fs, v, false);

	err = 0;

err:
	if(!r)
		mutex_unlock(&fs->lock);

	return err;
}

static int devfs_open(fs_cookie _fs, fs_vnode _v, file_cookie *_cookie, stream_type st, int oflags)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;
	struct devfs_cookie *cookie;
	int err = 0;
	int start = 0;

	TRACE(("devfs_open: vnode 0x%x, oflags 0x%x\n", v, oflags));

	if(st != STREAM_TYPE_ANY && st != v->stream.type) {
		err = ERR_VFS_WRONG_STREAM_TYPE;
		goto err;
	}

	cookie = kmalloc(sizeof(struct devfs_cookie));
	if(cookie == NULL) {
		err = ERR_NO_MEMORY;
		goto err;
	}

	mutex_lock(&fs->lock);	

	cookie->s = &v->stream;
	switch(v->stream.type) {
		case STREAM_TYPE_DIR:
			cookie->u.dir.ptr = v->stream.u.dir.dir_head;
			break;
		case STREAM_TYPE_DEVICE:
			// call the device call, but unlock the devfs first
			mutex_unlock(&fs->lock);
			err = v->stream.u.dev.calls->dev_open(v->stream.u.dev.full_name, &cookie->u.dev.dcookie);
			mutex_lock(&fs->lock);
			break;
		default:
			err = ERR_VFS_WRONG_STREAM_TYPE;
			kfree(cookie);
	}

	*_cookie = cookie;

err1:
	mutex_unlock(&fs->lock);
err:	
	return err;
}

static int devfs_close(fs_cookie _fs, fs_vnode _v, file_cookie _cookie)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;
	struct devfs_cookie *cookie = _cookie;
	int err;

	TRACE(("devfs_close: entry vnode 0x%x, cookie 0x%x\n", v, cookie));

	if(v->stream.type == STREAM_TYPE_DEVICE) {
		// pass the call through to the underlying device
		err = v->stream.u.dev.calls->dev_close(cookie->u.dev.dcookie);
	} else {
		err = 0;
	}

	return err;
}

static int devfs_freecookie(fs_cookie _fs, fs_vnode _v, file_cookie _cookie)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;
	struct devfs_cookie *cookie = _cookie;

	TRACE(("devfs_freecookie: entry vnode 0x%x, cookie 0x%x\n", v, cookie));

	if(v->stream.type == STREAM_TYPE_DEVICE) {
		// pass the call through to the underlying device
		v->stream.u.dev.calls->dev_freecookie(cookie->u.dev.dcookie);
	}

	if(cookie)
		kfree(cookie);

	return 0;
}

static int devfs_fsync(fs_cookie _fs, fs_vnode _v)
{
	return 0;
}

static ssize_t devfs_read(fs_cookie _fs, fs_vnode _v, file_cookie _cookie, void *buf, off_t pos, ssize_t len)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;
	struct devfs_cookie *cookie = _cookie;
	bool is_locked = false;
	ssize_t err = 0;

	TRACE(("devfs_read: vnode 0x%x, cookie 0x%x, pos 0x%x 0x%x, len 0x%x\n", v, cookie, pos, len));

	switch(cookie->s->type) {
		case STREAM_TYPE_DIR: {
			mutex_lock(&fs->lock);
			is_locked = true;

			if(cookie->u.dir.ptr == NULL) {
				err = 0;
				break;
			}

			if((ssize_t)strlen(cookie->u.dir.ptr->name) + 1 > len) {
				err = ERR_VFS_INSUFFICIENT_BUF;
				goto err;
			}
			
			err = user_strcpy(buf, cookie->u.dir.ptr->name);
			if(err < 0)
				goto err;

			err = strlen(cookie->u.dir.ptr->name) + 1;
			
			cookie->u.dir.ptr = cookie->u.dir.ptr->dir_next;
			break;
		}
		case STREAM_TYPE_DEVICE:
			// pass the call through to the device
			err = v->stream.u.dev.calls->dev_read(cookie->u.dev.dcookie, buf, pos, len);
			break;
		default:
			err = ERR_INVALID_ARGS;
	}
err:
	if(is_locked)
		mutex_unlock(&fs->lock);

	return err;
}

static ssize_t devfs_write(fs_cookie _fs, fs_vnode _v, file_cookie _cookie, const void *buf, off_t pos, ssize_t len)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;
	struct devfs_cookie *cookie = _cookie;

	TRACE(("devfs_write: vnode 0x%x, cookie 0x%x, pos 0x%x 0x%x, len 0x%x\n", v, cookie, pos, len));
	
	if(v->stream.type == STREAM_TYPE_DEVICE) {
		return v->stream.u.dev.calls->dev_write(cookie->u.dev.dcookie, buf, pos, len);
	} else {
		return ERR_VFS_READONLY_FS;
	}
}

static int devfs_seek(fs_cookie _fs, fs_vnode _v, file_cookie _cookie, off_t pos, seek_type st)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;
	struct devfs_cookie *cookie = _cookie;
	int err = 0;

	TRACE(("devfs_seek: vnode 0x%x, cookie 0x%x, pos 0x%x 0x%x, seek_type %d\n", v, cookie, pos, st));
	
	switch(cookie->s->type) {
		case STREAM_TYPE_DIR:
			mutex_lock(&fs->lock);
			switch(st) {
				// only valid args are seek_type SEEK_SET, pos 0.
				// this rewinds to beginning of directory
				case SEEK_SET:
					if(pos == 0) {
						cookie->u.dir.ptr = cookie->s->u.dir.dir_head;
					} else {
						err = ERR_INVALID_ARGS;
					}
					break;
				case SEEK_CUR:
				case SEEK_END:
				default:
					err = ERR_INVALID_ARGS;
			}
			mutex_unlock(&fs->lock);
			break;
		case STREAM_TYPE_DEVICE:
			err = v->stream.u.dev.calls->dev_seek(cookie->u.dev.dcookie, pos, st);
			break;
		default:
			err = ERR_INVALID_ARGS;
	}

	return err;
}

static int devfs_ioctl(fs_cookie _fs, fs_vnode _v, file_cookie _cookie, int op, void *buf, size_t len)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;
	struct devfs_cookie *cookie = _cookie;

	TRACE(("devfs_ioctl: vnode 0x%x, cookie 0x%x, op %d, buf 0x%x, len 0x%x\n", _v, _cookie, op, buf, len));

	if(v->stream.type == STREAM_TYPE_DEVICE) {
		return v->stream.u.dev.calls->dev_ioctl(cookie->u.dev.dcookie, op, buf, len);
	} else {
		return ERR_INVALID_ARGS;
	}
}

static int devfs_canpage(fs_cookie _fs, fs_vnode _v)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;

	TRACE(("devfs_canpage: vnode 0x%x\n", v));

	if(v->stream.type == STREAM_TYPE_DEVICE) {
		if(!v->stream.u.dev.calls->dev_canpage)
			return 0;
		return v->stream.u.dev.calls->dev_canpage();
	} else {
		return 0;
	}
}

static ssize_t devfs_readpage(fs_cookie _fs, fs_vnode _v, iovecs *vecs, off_t pos)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;

	TRACE(("devfs_readpage: vnode 0x%x, vecs 0x%x, pos 0x%x 0x%x\n", v, vecs, pos));

	if(v->stream.type == STREAM_TYPE_DEVICE) {
		if(!v->stream.u.dev.calls->dev_readpage)
			return ERR_NOT_ALLOWED;
		return v->stream.u.dev.calls->dev_readpage(vecs, pos);
	} else {
		return ERR_NOT_ALLOWED;
	}
}

static ssize_t devfs_writepage(fs_cookie _fs, fs_vnode _v, iovecs *vecs, off_t pos)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;

	TRACE(("devfs_writepage: vnode 0x%x, vecs 0x%x, pos 0x%x 0x%x\n", v, vecs, pos));

	if(v->stream.type == STREAM_TYPE_DEVICE) {
		if(!v->stream.u.dev.calls->dev_writepage)
			return ERR_NOT_ALLOWED;
		return v->stream.u.dev.calls->dev_writepage(vecs, pos);
	} else {
		return ERR_NOT_ALLOWED;
	}
}

static int devfs_create(fs_cookie _fs, fs_vnode _dir, const char *name, stream_type st, void *create_args, vnode_id *new_vnid)
{
	return ERR_VFS_READONLY_FS;
}

static int devfs_unlink(fs_cookie _fs, fs_vnode _dir, const char *name)
{
	return ERR_VFS_READONLY_FS;
}

static int devfs_rename(fs_cookie _fs, fs_vnode _olddir, const char *oldname, fs_vnode _newdir, const char *newname)
{
	return ERR_VFS_READONLY_FS;
}

static int devfs_rstat(fs_cookie _fs, fs_vnode _v, struct file_stat *stat)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;

	TRACE(("devfs_rstat: vnode 0x%x (0x%x 0x%x), stat 0x%x\n", v, v->id, stat));

	stat->vnid = v->id;
	stat->type = v->stream.type;
	stat->size = 0;

	return 0;
}


static int devfs_wstat(fs_cookie _fs, fs_vnode _v, struct file_stat *stat, int stat_mask)
{
	struct devfs *fs = _fs;
	struct devfs_vnode *v = _v;

	TRACE(("devfs_wstat: vnode 0x%x (0x%x 0x%x), stat 0x%x\n", v, v->id, stat));

	// cannot change anything
	return ERR_NOT_ALLOWED;
}

static struct fs_calls devfs_calls = {
	&devfs_mount,
	&devfs_unmount,
	&devfs_sync,

	&devfs_lookup,

	&devfs_getvnode,
	&devfs_putvnode,
	&devfs_removevnode,
	
	&devfs_open,
	&devfs_close,
	&devfs_freecookie,
	&devfs_fsync,

	&devfs_read,
	&devfs_write,
	&devfs_seek,
	&devfs_ioctl,

	&devfs_canpage,
	&devfs_readpage,
	&devfs_writepage,

	&devfs_create,
	&devfs_unlink,
	&devfs_rename,

	&devfs_rstat,
	&devfs_wstat,
};

int bootstrap_devfs()
{
	region_id rid;
	vm_region_info rinfo;
	
	dprintf("bootstrap_devfs: entry\n");
		
	return vfs_register_filesystem("devfs", &devfs_calls);
}

int devfs_publish_device(const char *path, struct dev_calls *calls)
{
	int err = 0;
	int i, last;
	char temp[SYS_MAX_PATH_LEN+1];
	struct devfs_vnode *dir;
	struct devfs_vnode *v;
	bool at_leaf;

	TRACE(("devfs_publish_device: entry path '%s', hooks 0x%x\n", path, calls));
	
	if(!thedevfs) {
		panic("devfs_publish_device called before devfs mounted\n");
		return ERR_GENERAL;
	}

	// copy the path over to a temp buffer so we can munge it
	strncpy(temp, path, SYS_MAX_PATH_LEN);
	temp[SYS_MAX_PATH_LEN] = 0;

	mutex_lock(&thedevfs->lock);

	// create the path leading to the device
	// parse the path passed in, stripping out '/'
	dir = thedevfs->root_vnode;
	v = NULL;
	i = 0;
	last = 0;
	at_leaf = false;
	for(;;) {
		if(temp[i] == 0) {
			at_leaf = true; // we'll be done after this one
		} else if(temp[i] == '/') {
			temp[i] = 0;
			i++;
		} else {
			i++;
			continue;
		}

		TRACE(("\tpath component '%s'\n", &temp[last]));
	
		// we have a path component
		v = devfs_find_in_dir(dir, &temp[last]);
		if(v) {
			if(!at_leaf) {
				// we are not at the leaf of the path, so as long as
				// this is a dir we're okay
				if(v->stream.type == STREAM_TYPE_DIR) {
					last = i;
					dir = v;
					continue;
				}
			}
			// we are at the leaf and hit another node
			// or we aren't but hit a non-dir node.
			// we're screwed
			err = ERR_VFS_ALREADY_EXISTS;
			goto err1;
		} else {
			v = devfs_create_vnode(thedevfs, &temp[last]);
			if(!v) {
				err = ERR_NO_MEMORY;
				goto err1;
			}
		}

		// set up the new vnode
		if(at_leaf) {
			// this is the last component
			v->stream.type = STREAM_TYPE_DEVICE;
			v->stream.u.dev.full_name = kmalloc(strlen(path) + 1);
			if(!v->stream.u.dev.full_name) {
				devfs_delete_vnode(thedevfs, v, false);
				err = ERR_NO_MEMORY;
				goto err1;
			}
			strcpy(v->stream.u.dev.full_name, path);
			v->stream.u.dev.calls = calls;
		} else {
			// this is a dir
			v->stream.type = STREAM_TYPE_DIR;
			v->stream.u.dir.dir_head = NULL;
			v->stream.u.dir.jar_head = NULL;
		}

		hash_insert(thedevfs->vnode_list_hash, v);

		devfs_insert_in_dir(dir, v);

		if(at_leaf)
			break;
		last = i;
		dir = v;
	}

err1:
	mutex_unlock(&thedevfs->lock);
err:
	return err;
}

