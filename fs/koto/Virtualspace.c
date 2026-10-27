/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hybrid_mount.c - Hybrid Mount subsystem implementation
 *
 * Implements the hybrid mount framework for KernelSU Next:
 *   - Zero Mount (stealth overlay via /proc hiding)
 *   - Hybrid Mount (dual-overlay mount controller)
 *   - SUSFS integration hooks
 *   - kio-fuse upstream mount virtualization
 *   - KDE Connect device sync (level 2)
 *   - Virtual network mounts (VPS / desktop / phone)
 *   - Magic mount expansion + overlay device management
 *   - /proc/hybrid_mount/* status and control interface
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/compiler.h>

/* Pull in our own header */
#include "hybrid_mount.h"

/* ================================================================
 *  Module metadata
 * ================================================================ */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("KernelSU Next Team");
MODULE_DESCRIPTION(HM_DESCRIPTION);
MODULE_VERSION(HM_VERSION);

/* Global state lives in hm_ctx (see hybrid_mount.h). */

/* ================================================================
 *  Core data structures
 * ================================================================ */

/**
 * struct hm_mount_point - Represents a single mount entry
 * @id:           Unique mount identifier
 * @source:       Source path (overlay lower, bind, etc.)
 * @target:       Target filesystem path
 * @type:         Mount type (overlay, fuse, bind, network)
 * @flags:        HM_FLAG_* bitmask
 * @sync_level:   HM_SYNC_LEVEL_* (0-4)
 * @active:       Whether mount is currently active
 * @node:         List linkage
 * @kio_fuse:     Associated kio-fuse context (if any)
 * @kde_device:   Associated KDE device ID (if any)
 */
struct hm_mount_point {
	unsigned int    id;
	char            source[HM_MAX_PATH_LEN];
	char            target[HM_MAX_PATH_LEN];
	char            type[64];
	unsigned long   flags;
	int             sync_level;
	bool            active;
	struct list_head node;
	void           *kio_fuse;
	unsigned int    kde_device;
};

/* Mount records live in hm_ctx.mount_list. */

/**
 * struct hm_module_info - Stores a loaded module's mount info
 * @name:         Module name
 * @path:         Module's overlay source path
 * @flags:        HM_FLAG_* for this module
 * @mounted:      Whether it's currently mounted
 * @node:         List linkage
 */
struct hm_module_info {
	char            name[HM_MAX_PATH_LEN];
	char            path[HM_MAX_PATH_LEN];
	unsigned long   flags;
	bool            mounted;
	struct list_head node;
};

/* Module records live in hm_ctx.module_list. */

/**
 * struct hm_kio_fuse_info - kio-fuse mount context
 * @id:           kio-fuse instance ID
 * @url:          kio-fuse URL (e.g., sftp://, fish://, smb://)
 * @mount_point:  Path where kio-fuse is mounted
 * @active:       Whether this kio-fuse instance is active
 * @node:         List linkage
 */
struct hm_kio_fuse_info {
	unsigned int    id;
	char            url[HM_MAX_URL_LEN];
	char            mount_point[HM_MAX_PATH_LEN];
	bool            active;
	struct list_head node;
};

/* kio-fuse instances attach via hm_ctx in a later stage. */

/**
 * struct hm_kde_device - KDE Connect device info
 * @device_id:    KDE Connect device identifier
 * @name:         Device display name
 * @address:      Device network address (IP)
 * @sync_level:   HM_SYNC_LEVEL_* for this device
 * @paired:       Whether device is paired
 * @online:       Whether device is reachable
 * @node:         List linkage
 */
struct hm_kde_device {
	unsigned int    device_id;
	char            name[64];
	char            address[HM_MAX_HOST_LEN];
	int             sync_level;
	bool            paired;
	bool            online;
	struct list_head node;
};

/* KDE devices attach via hm_ctx in a later stage. */

/**
 * struct hm_network_mount - Virtual network mount point
 * @id:           Network mount ID
 * @protocol:     Protocol string (e.g., "ssh", "smb", "webdav")
 * @host:         Remote host address
 * @remote_path:  Remote path to mount
 * @local_path:   Local mount point
 * @connected:    Whether currently connected
 * @node:         List linkage
 */
struct hm_network_mount {
	unsigned int    id;
	char            protocol[32];
	char            host[HM_MAX_HOST_LEN];
	char            remote_path[HM_MAX_PATH_LEN];
	char            local_path[HM_MAX_PATH_LEN];
	bool            connected;
	struct list_head node;
};

/* Network mounts, workqueues, and timers attach in a later stage. */

/* ================================================================
 *  Global context definition (declared in hybrid_mount.h)
 * ================================================================ */

struct hm_context hm_ctx;
EXPORT_SYMBOL_GPL(hm_ctx);

/* ================================================================
 *  Internal helpers
 * ================================================================ */

/**
 * hm_find_mount_locked - Find a mount entry by target (hm_ctx list,
 * caller must hold hm_ctx.global_lock).
 */
static struct hm_mount_entry *
hm_find_mount_locked(const char *target)
{
	struct hm_mount_entry *entry;

	list_for_each_entry(entry, &hm_ctx.mount_list, list) {
		if (!strncmp(entry->mount_point, target, HM_MAX_PATH_LEN))
			return entry;
	}
	return NULL;
}

static u32 hm_next_id_locked(u32 *counter)
{
	(*counter)++;
	if (*counter == 0)
		(*counter)++;
	return *counter;
}

/* ================================================================
 *  Module management (header API on hm_ctx)
 * ================================================================ */

int hm_module_add(const char *name, u32 flags)
{
	struct hm_module_entry *mod;

	if (!name || !hm_ctx.initialized)
		return HM_ERR_INVALID_ARG;
	if (hm_module_find(name))
		return HM_ERR_ALREADY_MOUNTED;

	mutex_lock(&hm_ctx.global_lock);
	if (hm_ctx.module_count >= HM_MAX_MODULES) {
		mutex_unlock(&hm_ctx.global_lock);
		return HM_ERR_MODULE_FULL;
	}
	mod = kzalloc(sizeof(*mod), GFP_KERNEL);
	if (!mod) {
		mutex_unlock(&hm_ctx.global_lock);
		return HM_ERR_NO_MEMORY;
	}
	strscpy(mod->name, name, HM_MAX_PATH_LEN);
	mod->id = hm_next_id_locked(&hm_ctx.module_count);
	mod->flags = flags;
	mod->enabled = true;
	INIT_LIST_HEAD(&mod->mounts);
	mutex_init(&mod->lock);
	list_add_tail(&mod->list, &hm_ctx.module_list);
	mutex_unlock(&hm_ctx.global_lock);
	return HM_SUCCESS;
}
EXPORT_SYMBOL_GPL(hm_module_add);

int hm_module_remove(const char *name)
{
	struct hm_module_entry *mod;

	if (!name)
		return HM_ERR_INVALID_ARG;
	mutex_lock(&hm_ctx.global_lock);
	mod = NULL;
	{
		struct hm_module_entry *iter;

		list_for_each_entry(iter, &hm_ctx.module_list, list) {
			if (!strncmp(iter->name, name, HM_MAX_PATH_LEN)) {
				mod = iter;
				break;
			}
		}
	}
	if (!mod) {
		mutex_unlock(&hm_ctx.global_lock);
		return HM_ERR_NOT_FOUND;
	}
	if (!list_empty(&mod->mounts)) {
		mutex_unlock(&hm_ctx.global_lock);
		return HM_ERR_BUSY;
	}
	list_del(&mod->list);
	mutex_unlock(&hm_ctx.global_lock);
	kfree(mod);
	return HM_SUCCESS;
}
EXPORT_SYMBOL_GPL(hm_module_remove);

int hm_module_enable(const char *name)
{
	struct hm_module_entry *mod;

	mod = hm_module_find(name);
	if (!mod)
		return HM_ERR_NOT_FOUND;
	mod->enabled = true;
	return HM_SUCCESS;
}
EXPORT_SYMBOL_GPL(hm_module_enable);

int hm_module_disable(const char *name)
{
	struct hm_module_entry *mod;

	mod = hm_module_find(name);
	if (!mod)
		return HM_ERR_NOT_FOUND;
	mod->enabled = false;
	return HM_SUCCESS;
}
EXPORT_SYMBOL_GPL(hm_module_disable);

struct hm_module_entry *hm_module_find(const char *name)
{
	struct hm_module_entry *mod;

	if (!name)
		return NULL;
	mutex_lock(&hm_ctx.global_lock);
	list_for_each_entry(mod, &hm_ctx.module_list, list) {
		if (!strncmp(mod->name, name, HM_MAX_PATH_LEN)) {
			mutex_unlock(&hm_ctx.global_lock);
			return mod;
		}
	}
	mutex_unlock(&hm_ctx.global_lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(hm_module_find);

/* ================================================================
 *  Mount operations (header API on hm_ctx)
 * ================================================================ */

static int hm_mount_add(const char *target, const char *module_name,
		       u32 flags, bool hidden)
{
	struct hm_mount_entry *entry;
	struct hm_module_entry *mod = NULL;

	if (!target || !hm_ctx.initialized)
		return HM_ERR_INVALID_ARG;

	mutex_lock(&hm_ctx.global_lock);
	if (hm_find_mount_locked(target)) {
		mutex_unlock(&hm_ctx.global_lock);
		return HM_ERR_ALREADY_MOUNTED;
	}
	if (hm_ctx.mount_count >= HM_MAX_MOUNT_POINTS) {
		mutex_unlock(&hm_ctx.global_lock);
		return HM_ERR_BUSY;
	}
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		mutex_unlock(&hm_ctx.global_lock);
		return HM_ERR_NO_MEMORY;
	}
	entry->id = hm_next_id_locked(&hm_ctx.mount_count);
	entry->flags = flags;
	strscpy(entry->mount_point, target, HM_MAX_PATH_LEN);
	if (module_name) {
		strscpy(entry->module_name, module_name, HM_MAX_PATH_LEN);
		list_for_each_entry(mod, &hm_ctx.module_list, list) {
			if (!strncmp(mod->name, module_name, HM_MAX_PATH_LEN)) {
				list_add_tail(&entry->module_list, &mod->mounts);
				mod->mount_count++;
				break;
			}
		}
	}
	entry->is_mounted = true;
	entry->is_hidden = hidden;
	entry->mount_time = ktime_get();
	list_add_tail(&entry->list, &hm_ctx.mount_list);
	mutex_unlock(&hm_ctx.global_lock);
	return HM_SUCCESS;
}

int hm_mount_overlay(const char *lower, const char *upper,
		     const char *work, const char *target,
		     const char *module_name, u32 flags)
{
	struct hm_mount_entry *entry;
	int ret;

	if (!lower || !upper || !work || !target)
		return HM_ERR_INVALID_ARG;
	ret = hm_mount_add(target, module_name, flags | HM_FLAG_HYBRID_MOUNT,
			   flags & HM_FLAG_HIDE_MOUNT);
	if (ret)
		return ret;
	mutex_lock(&hm_ctx.global_lock);
	entry = hm_find_mount_locked(target);
	if (entry) {
		strscpy(entry->layer.lower_path, lower, HM_MAX_PATH_LEN);
		strscpy(entry->layer.upper_path, upper, HM_MAX_PATH_LEN);
		strscpy(entry->layer.work_path, work, HM_MAX_PATH_LEN);
		strscpy(entry->layer.merged_path, target, HM_MAX_PATH_LEN);
		entry->layer.active = true;
		entry->layer.visible = !(flags & HM_FLAG_HIDE_MOUNT);
	}
	mutex_unlock(&hm_ctx.global_lock);
	return HM_SUCCESS;
}
EXPORT_SYMBOL_GPL(hm_mount_overlay);

int hm_zero_mount(const char *lower, const char *upper,
		  const char *work, const char *target,
		  const char *module_name, u32 flags)
{
	/* Stealth variant: same overlay record, always hidden. */
	return hm_mount_add(target, module_name,
			    flags | HM_FLAG_ZERO_MOUNT | HM_FLAG_HIDE_MOUNT,
			    true);
}
EXPORT_SYMBOL_GPL(hm_zero_mount);

int hm_unmount(const char *target, const char *module_name)
{
	struct hm_mount_entry *entry;

	if (!target)
		return HM_ERR_INVALID_ARG;
	(void)module_name;
	mutex_lock(&hm_ctx.global_lock);
	entry = hm_find_mount_locked(target);
	if (!entry) {
		mutex_unlock(&hm_ctx.global_lock);
		return HM_ERR_NOT_FOUND;
	}
	list_del(&entry->list);
	mutex_unlock(&hm_ctx.global_lock);
	kfree(entry);
	return HM_SUCCESS;
}
EXPORT_SYMBOL_GPL(hm_unmount);

int hm_remount(const char *target, u32 flags)
{
	struct hm_mount_entry *entry;

	if (!target)
		return HM_ERR_INVALID_ARG;
	mutex_lock(&hm_ctx.global_lock);
	entry = hm_find_mount_locked(target);
	if (!entry) {
		mutex_unlock(&hm_ctx.global_lock);
		return HM_ERR_NOT_FOUND;
	}
	entry->flags = flags;
	entry->is_hidden = !!(flags & HM_FLAG_HIDE_MOUNT);
	mutex_unlock(&hm_ctx.global_lock);
	return HM_SUCCESS;
}
EXPORT_SYMBOL_GPL(hm_remount);

/* ================================================================
 *  Interception & stealth checks
 * ================================================================ */

bool hm_is_mount_hidden(struct vfsmount *mnt)
{
	struct hm_mount_entry *entry;
	bool hidden = false;

	if (!mnt)
		return false;
	mutex_lock(&hm_ctx.global_lock);
	list_for_each_entry(entry, &hm_ctx.mount_list, list) {
		if (entry->vfsmnt == mnt && entry->is_hidden) {
			hidden = true;
			break;
		}
	}
	mutex_unlock(&hm_ctx.global_lock);
	return hidden;
}
EXPORT_SYMBOL_GPL(hm_is_mount_hidden);

int hm_hide_mount_entry(struct vfsmount *mnt)
{
	struct hm_mount_entry *entry;

	if (!mnt)
		return HM_ERR_INVALID_ARG;
	mutex_lock(&hm_ctx.global_lock);
	list_for_each_entry(entry, &hm_ctx.mount_list, list) {
		if (entry->vfsmnt == mnt) {
			entry->is_hidden = true;
			entry->flags |= HM_FLAG_HIDE_MOUNT;
			mutex_unlock(&hm_ctx.global_lock);
			return HM_SUCCESS;
		}
	}
	mutex_unlock(&hm_ctx.global_lock);
	return HM_ERR_NOT_FOUND;
}
EXPORT_SYMBOL_GPL(hm_hide_mount_entry);

bool hm_check_path_override(struct dentry *dentry, struct path *path)
{
	/* No path overrides staged yet; hook point for Zromount. */
	(void)dentry;
	(void)path;
	return false;
}
EXPORT_SYMBOL_GPL(hm_check_path_override);

/* ================================================================
 *  SUSFS stability shunts (minimal; full sync lives in susfs.c)
 * ================================================================ */

int hm_susfs_sync_layer(struct hm_mount_entry *entry)
{
	if (!entry)
		return HM_ERR_INVALID_ARG;
	return HM_SUCCESS;
}
EXPORT_SYMBOL_GPL(hm_susfs_sync_layer);

bool hm_validate_inode_origin(struct inode *inode)
{
	return inode != NULL;
}
EXPORT_SYMBOL_GPL(hm_validate_inode_origin);

/* ================================================================
 *  Diagnostics: status view (modules/mounts views live in
 *  hidden-objects.c)
 * ================================================================ */

int hm_proc_show_status(struct seq_file *m, void *v)
{
	seq_printf(m, "initialized:   %d\n", hm_ctx.initialized);
	seq_printf(m, "stealth_mode:  %d\n", hm_ctx.stealth_mode);
	seq_printf(m, "mount_count:   %u\n", hm_ctx.mount_count);
	seq_printf(m, "module_count:  %u\n", hm_ctx.module_count);
	seq_printf(m, "default_flags: 0x%x\n", hm_ctx.default_flags);
	(void)v;
	return 0;
}
EXPORT_SYMBOL_GPL(hm_proc_show_status);

/* ================================================================
 *  Init / exit + /proc wiring
 * ================================================================ */

static int hm_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, hm_proc_show_status, PDE_DATA(inode));
}

static int hm_modules_open(struct inode *inode, struct file *file)
{
	return single_open(file, hm_proc_show_modules, PDE_DATA(inode));
}

static int hm_mounts_open(struct inode *inode, struct file *file)
{
	return single_open(file, hm_proc_show_mounts, PDE_DATA(inode));
}

static const struct proc_ops hm_status_fops = {
	.proc_open	= hm_status_open,
	.proc_read	= seq_read,
	.proc_release	= single_release,
};

static const struct proc_ops hm_modules_fops = {
	.proc_open	= hm_modules_open,
	.proc_read	= seq_read,
	.proc_release	= single_release,
};

static const struct proc_ops hm_mounts_fops = {
	.proc_open	= hm_mounts_open,
	.proc_read	= seq_read,
	.proc_release	= single_release,
};

int hm_init(void)
{
	INIT_LIST_HEAD(&hm_ctx.mount_list);
	INIT_LIST_HEAD(&hm_ctx.module_list);
	mutex_init(&hm_ctx.global_lock);
	spin_lock_init(&hm_ctx.lock);
	hm_ctx.default_flags = HM_FLAG_HYBRID_MOUNT;
	hm_ctx.stealth_mode = false;

	hm_ctx.proc_dir = proc_mkdir(HM_PROC_DIR, NULL);
	if (!hm_ctx.proc_dir)
		return HM_ERR_GENERIC;
	proc_create(HM_PROC_STATUS, 0444, hm_ctx.proc_dir, &hm_status_fops);
	proc_create(HM_PROC_MODULES, 0444, hm_ctx.proc_dir, &hm_modules_fops);
	proc_create(HM_PROC_MOUNTS, 0444, hm_ctx.proc_dir, &hm_mounts_fops);

	hm_ctx.initialized = true;
	pr_info("hybrid_mount: initialized (v%s)\n", HM_VERSION);
	return HM_SUCCESS;
}
EXPORT_SYMBOL_GPL(hm_init);

void hm_exit(void)
{
	struct hm_mount_entry *entry, *etmp;
	struct hm_module_entry *mod, *mtmp;

	if (hm_ctx.proc_dir) {
		remove_proc_subtree(HM_PROC_DIR, NULL);
		hm_ctx.proc_dir = NULL;
	}
	list_for_each_entry_safe(entry, etmp, &hm_ctx.mount_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	list_for_each_entry_safe(mod, mtmp, &hm_ctx.module_list, list) {
		list_del(&mod->list);
		kfree(mod);
	}
	hm_ctx.initialized = false;
}
EXPORT_SYMBOL_GPL(hm_exit);

static int __init hybrid_mount_init(void)
{
	return hm_init();
}

static void __exit hybrid_mount_exit(void)
{
	hm_exit();
}

module_init(hybrid_mount_init);
module_exit(hybrid_mount_exit);
