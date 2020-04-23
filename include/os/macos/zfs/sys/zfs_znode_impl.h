/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

#ifndef	_MACOS_ZFS_SYS_ZNODE_IMPL_H
#define	_MACOS_ZFS_SYS_ZNODE_IMPL_H

#include <sys/list.h>
#include <sys/dmu.h>
#include <sys/sa.h>
#include <sys/zfs_vfsops.h>
#include <sys/rrwlock.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_stat.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_acl.h>
#include <sys/zil.h>
#include <sys/zfs_project.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define ZFS_UIMMUTABLE          0x0000001000000000ull // OSX
#define ZFS_UAPPENDONLY         0x0000004000000000ull // OSX

//#define ZFS_IMMUTABLE  (ZFS_UIMMUTABLE  | ZFS_SIMMUTABLE)
//#define ZFS_APPENDONLY (ZFS_UAPPENDONLY | ZFS_SAPPENDONLY)

#define ZFS_TRACKED             0x0010000000000000ull
#define ZFS_COMPRESSED  0x0020000000000000ull

#define ZFS_SIMMUTABLE          0x0040000000000000ull
#define ZFS_SAPPENDONLY         0x0080000000000000ull

#define SA_ZPL_ADDTIME(z)               z->z_attr_table[ZPL_ADDTIME]
#define SA_ZPL_DOCUMENTID(z)    z->z_attr_table[ZPL_DOCUMENTID]

/*
 * Directory entry locks control access to directory entries.
 * They are used to protect creates, deletes, and renames.
 * Each directory znode has a mutex and a list of locked names.
 */
#define	ZNODE_OS_FIELDS                 \
	struct zfsvfs	*z_zfsvfs;      	\
	struct vnode	*z_vnode;       	\
	uint64_t		z_uid;          	\
	uint64_t		z_gid;          	\
	uint64_t		z_gen;          	\
	uint64_t		z_atime[2];     	\
	uint64_t		z_links;			\
	uint32_t		z_vid;				\
	uint32_t		z_document_id;		\
	uint64_t		z_finder_parentid;	\
	boolean_t		z_finder_hardlink;	\
	uint64_t		z_write_gencount;

#define	ZFS_LINK_MAX	UINT64_MAX

/*
 * ZFS minor numbers can refer to either a control device instance or
 * a zvol. Depending on the value of zss_type, zss_data points to either
 * a zvol_state_t or a zfs_onexit_t.
 */
enum zfs_soft_state_type {
	ZSST_ZVOL,
	ZSST_CTLDEV
};

typedef struct zfs_soft_state {
	enum zfs_soft_state_type zss_type;
	void *zss_data;
} zfs_soft_state_t;

extern minor_t zfsdev_minor_alloc(void);

/*
 * Range locking rules
 * --------------------
 * 1. When truncating a file (zfs_create, zfs_setattr, zfs_space) the whole
 *    file range needs to be locked as RL_WRITER. Only then can the pages be
 *    freed etc and zp_size reset. zp_size must be set within range lock.
 * 2. For writes and punching holes (zfs_write & zfs_space) just the range
 *    being written or freed needs to be locked as RL_WRITER.
 *    Multiple writes at the end of the file must coordinate zp_size updates
 *    to ensure data isn't lost. A compare and swap loop is currently used
 *    to ensure the file size is at least the offset last written.
 * 3. For reads (zfs_read, zfs_get_data & zfs_putapage) just the range being
 *    read needs to be locked as RL_READER. A check against zp_size can then
 *    be made for reading beyond end of file.
 */

/*
 * Convert between znode pointers and vnode pointers
 */
#define	ZTOV(ZP)	((ZP)->z_vnode)
#define	ZTOI(ZP)	((ZP)->z_vnode)
#define	VTOZ(VP)	((znode_t *)vnode_fsnode((VP)))
#define	ITOZ(VP)	((znode_t *)vnode_fsnode((VP)))
#define	zhold(zp)	vhold(ZTOV((zp)))
#define	zrele(zp)	vrele(ZTOV((zp)))

#define	ZTOZSB(zp) ((zp)->z_zfsvfs)
#define	ITOZSB(vp) ((zfsvfs_t *)vfs_private(vp))
#define	ZTOTYPE(zp)	(vnode_vtype(ZTOV(zp))
#define	ZTOGID(zp) ((zp)->z_gid)
#define	ZTOUID(zp) ((zp)->z_uid)
#define	ZTONLNK(zp) ((zp)->z_links)
#define	Z_ISBLK(type) ((type) == VBLK)
#define	Z_ISCHR(type) ((type) == VCHR)
#define	Z_ISLNK(type) ((type) == VLNK)


/* Called on entry to each ZFS vnode and vfs operation  */
#define	ZFS_ENTER(zfsvfs) \
	{ \
		rrm_enter_read(&(zfsvfs)->z_teardown_lock, FTAG); \
		if ((zfsvfs)->z_unmounted) { \
			ZFS_EXIT(zfsvfs); \
			return (EIO); \
		} \
	}

/* Must be called before exiting the vop */
#define	ZFS_EXIT(zfsvfs) rrm_exit(&(zfsvfs)->z_teardown_lock, FTAG)

/* Verifies the znode is valid */
#define	ZFS_VERIFY_ZP(zp) \
	if ((zp)->z_sa_hdl == NULL) { \
		ZFS_EXIT((zp)->z_zfsvfs); \
		return (EIO); \
	} \

/*
 * Macros for dealing with dmu_buf_hold
 */
#define	ZFS_OBJ_HASH(obj_num)	((obj_num) & (ZFS_OBJ_MTX_SZ - 1))
#define	ZFS_OBJ_MUTEX(zfsvfs, obj_num)	\
	(&(zfsvfs)->z_hold_mtx[ZFS_OBJ_HASH(obj_num)])
#define	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj_num) \
	mutex_enter(ZFS_OBJ_MUTEX((zfsvfs), (obj_num)))
#define	ZFS_OBJ_HOLD_TRYENTER(zfsvfs, obj_num) \
	mutex_tryenter(ZFS_OBJ_MUTEX((zfsvfs), (obj_num)))
#define	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num) \
	mutex_exit(ZFS_OBJ_MUTEX((zfsvfs), (obj_num)))

/* Encode ZFS stored time values from a struct timespec */
#define	ZFS_TIME_ENCODE(tp, stmp)		\
{						\
	(stmp)[0] = (uint64_t)(tp)->tv_sec;	\
	(stmp)[1] = (uint64_t)(tp)->tv_nsec;	\
}

/* Decode ZFS stored time values to a struct timespec */
#define	ZFS_TIME_DECODE(tp, stmp)		\
{						\
	(tp)->tv_sec = (time_t)(stmp)[0];		\
	(tp)->tv_nsec = (long)(stmp)[1];		\
}
#define	ZFS_ACCESSTIME_STAMP(zfsvfs, zp) \
	if ((zfsvfs)->z_atime && !((zfsvfs)->z_vfs->vfs_flag & VFS_RDONLY)) \
		zfs_tstamp_update_setup_ext(zp, ACCESSED, NULL, NULL, B_FALSE);

extern void	zfs_tstamp_update_setup_ext(struct znode *,
    uint_t, uint64_t [2], uint64_t [2], boolean_t have_tx);
extern void zfs_znode_free(struct znode *);

extern zil_get_data_t zfs_get_data;
extern zil_replay_func_t *zfs_replay_vector[TX_MAX_TYPE];
extern int zfsfstype;

extern int zfs_znode_parent_and_name(struct znode *zp, struct znode **dzpp,
    char *buf);

#ifdef	__cplusplus
}
#endif

#endif	/* _MACOS_SYS_FS_ZFS_ZNODE_H */