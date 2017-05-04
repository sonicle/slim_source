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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ORCHESTRATOR_API_H
#define	_ORCHESTRATOR_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/dktp/fdisk.h>
#include <sys/vtoc.h>
#include <libnvpair.h>

#define	OM_NUMPART	(FD_NUMPART + MAX_EXT_PARTS)

extern	int16_t		om_errno;
typedef int16_t		om_handle_t;

/*
 * Callbacks definitions and functions
 */
typedef enum {
	OM_TARGET_TARGET_DISCOVERY = 0,
	OM_SYSTEM_VALIDATION,
	OM_INSTALL_TYPE,
	OM_UPGRADE_TYPE,
	OM_TOOLS_INSTALL_TYPE
} om_callback_type_t;

typedef enum {
	OM_DISK_DISCOVERY = 0,
	OM_PARTITION_DISCOVERY,
	OM_SLICE_DISCOVERY,
	OM_UPGRADE_TARGET_DISCOVERY,
	OM_INSTANCE_DISCOVERY,
	OM_TARGET_INSTANTIATION,
	OM_UPGRADE_CHECK,
	OM_SOFTWARE_UPDATE,
	OM_POSTINSTAL_TASKS,
	OM_INSTALLER_FAILED
} om_milestone_type_t;

typedef struct om_callback_info {
	int			num_milestones; /* number to track for op */
	om_milestone_type_t	curr_milestone;
	om_callback_type_t	callback_type;
	int16_t			percentage_done;
	const char		*message;	/* progress text for GUI */
} om_callback_info_t;

typedef void (*om_callback_t)(om_callback_info_t *, uintptr_t);

typedef enum {
	OM_DTYPE_UNKNOWN = 0,
	OM_DTYPE_ATA,
	OM_DTYPE_SCSI,
	OM_DTYPE_FIBRE,
	OM_DTYPE_USB,
	OM_DTYPE_SATA,
	OM_DTYPE_FIREWIRE
} om_disk_type_t;

typedef enum {
	OM_CTYPE_UNKNOWN = 0,
	OM_CTYPE_SOLARIS,
	OM_CTYPE_LINUXSWAP,
	OM_CTYPE_LINUX
} om_content_type_t;

typedef enum {
	OM_LABEL_UNKNOWN = 0,
	OM_LABEL_VTOC,
	OM_LABEL_GPT,
	OM_LABEL_FDISK
} om_disklabel_type_t;

typedef enum {
	OM_INSTANCE_UFS = 1,
	OM_INSTANCE_ZFS
} om_instance_type_t;

typedef enum {
	OM_UPGRADE_UNKNOWN_ERROR = 2000,
	OM_UPGRADE_NG_ZONE_CONFIURE_PROBLEM,
	OM_UPGRADE_RELEASE_NOT_SUPPORTED,
	OM_UPGRADE_RELEASE_INFO_MISSING,
	OM_UPGRADE_INSTANCE_INCOMPLETE,
	OM_UPGRADE_ROOT_FS_CORRUPTED,
	OM_UPGRADE_MOUNT_ROOT_FAILED,
	OM_UPGRADE_MOUNT_VAR_FAILED,
	OM_UPGRADE_MISSING_CLUSTER_FILE,
	OM_UPGRADE_MISSING_CLUSTERTOC_FILE,
	OM_UPGRADE_MISSING_BOOTENVRC_FILE,
	OM_UPGRADE_WRONG_METACLUSTER
} om_upgrade_message_t;

typedef enum {
	OM_PROC_SUCCESS,
	OM_PROC_INFO_ERR,
	OM_PROC_DIR_ERR,
	OM_PROC_ALREADY_RUNNING,
	OM_PROC_NOT_RUNNING
} om_proc_return_t;

typedef enum {
	OM_UNASSIGNED = 0,
	OM_BOOT,
	OM_ROOT,
	OM_SWAP,
	OM_USR,
	OM_BACKUP,
	OM_STAND,
	OM_VAR,
	OM_HOME,
	OM_ALTSCTR,
	OM_CACHE,
	OM_RESERVED
} om_slice_tag_type_t;

/*
 * on AI slice create action, if slice with specified number already exists,
 *	determine behavior:
 *	- error (default) - treat as configuration error and halt installation
 *	- overwrite - supersede existing definition with new
 */
typedef enum {
	OM_ON_EXISTING_ERROR = 0,
	OM_ON_EXISTING_OVERWRITE
} om_on_existing_t;

typedef enum {
	OM_INITIAL_INSTALL = 1,
	OM_UPGRADE
} om_install_type_t;

typedef struct disk_info {
	char			*disk_name;	/* For example c0t0d0 */
	char			*disk_volname;	/* volume name */
	char			*disk_devid;	/* device ID */
	char			*disk_device_path;	/* device path */
	uint32_t		disk_size;	/* Usable size in MB */
	uint32_t		disk_size_total;	/* Total size in MB */
	om_disk_type_t		disk_type;	/* SCSI, IDE, USB etc. */
	char			*vendor;	/* Manufacturer */
	boolean_t		boot_disk;	/* Is it a boot disk? */
	om_disklabel_type_t	label;		/* disk label */
	boolean_t		removable;	/* Is it removable (USB?) */
	char			*serial_number; /* Manufacturer assigned */
	uint32_t		disk_cyl_size;	/* Cylinder Size in sectors */
	uint64_t		disk_size_sec;	/* Disk size in sectors */
	struct disk_info	*next;		/* pointer to next disk */
} disk_info_t;

typedef struct {
	uint8_t			partition_id;
	/*
	 * corresponds to fdisk partition number N in device name format ctdpN
	 *	1 : (FD_NUMPARTS + MAX_EXT_PARTS))
	 */
	uint32_t		partition_size;	/* Size in MB */
	uint32_t		partition_offset;
				/* Offset in MB from start of the disk */
	uint8_t			partition_order; /* Order in the disk */
	uint8_t			partition_type;	/* Solaris/linux swap/X86boot */
	om_content_type_t	content_type;	/* Solaris/Linux */
	boolean_t		active;		/* Is the partition active */
	uint64_t		partition_size_sec;	/* Size in sectors */
	uint64_t		partition_offset_sec;	/* offset in sectors */
} partition_info_t;


/*
 * show information for all partitions on disk
 * indexed by partition number as N in the device name: ctdpN,
 * but zero-based, so pinfo[N-1].partition_id == N
 */
typedef struct {
	char			*disk_name;	/* Disk Name for look up */
	partition_info_t	pinfo[OM_NUMPART]; /* fdisk */
} disk_parts_t;

typedef struct {
	uint8_t			slice_id;	/* sdisk id (0-15) */
	uint64_t		slice_size;	/* Size in sectors */
	uint64_t		slice_offset;	/* in sectors */
	om_slice_tag_type_t	tag;		/* root/swap/unassigned etc. */
	uint8_t			flags;		/* RO/RW, (un)mountable */
} slice_info_t;

typedef struct {
	uint8_t		partition_id;	/* For look up, only for X86 */
	char		*disk_name;	/* Disk Name for look up */
	slice_info_t	sinfo[NDKMAP];	/* vtoc slices */
} disk_slices_t;

typedef	struct {
	char		*disk_name;	/* Where the Instance resides */
	uint8_t		slice;		/* Which Slice (0-15) */
	boolean_t	svm_configured;	/* Part of SVM root */
	char		*svm_info;	/* mirror components */
} ufs_instance_t;

typedef	struct {
	char		*pool_name;	/* More info will be added */
} zfs_instance_t;

typedef struct upgrade_info {
	om_instance_type_t	instance_type;	/* UFS or ZFS */
	union {
		ufs_instance_t	uinfo;
		zfs_instance_t	zinfo;
	} instance;
	char			*solaris_release;
		/* Some thing like "Solaris Developer Express Release 1" */
	boolean_t		zones_installed;
		/* Non global zones configured in the Solaris Instance */
	boolean_t		upgradable;
		/* Does the instance looks okay */
	om_upgrade_message_t	upgrade_message_id;
		/* If an instance can't be upgraded, why? */
	char			*incorrect_zone_list;
		/* List of non-globlal zones not configured correctly */
	struct upgrade_info	*next;	/* link to next Instance */
} upgrade_info_t;

typedef struct locale_info {
	char			*locale_name;
	char			*locale_desc;
	boolean_t 		def_locale;
	struct	locale_info	*next;
} locale_info_t;

typedef struct lang_info {
	/* pointer to all locale_info_t's for language */
	locale_info_t		*locale_info;
	boolean_t 		def_lang; /* Is this the default language */
	char			*lang;	 /* language code name, i.e, 'en' */
	int			n_locales;
	/* Fully expanded language name, translated appropriately */
	char			*lang_name;
	struct 	lang_info	*next;
} lang_info_t;

typedef enum {
	/*
	 * select algorithm for partition allocation
	 * in om_validate_and_resize_partitions()
	 *
	 * in the GUI scheme, starting offsets for new partitions are
	 * computed in om_validate_and_resize_partitions()
	 *
	 * for AI,the starting offsets are either chosen by the user or
	 * computed when the create action is issued
	 * before om_validate_and_resize_partitions() is called
	 */
	GUI_allocation = 1,
	AI_allocation
} partition_allocation_scheme_t;

/*
 * testing breakpoints - calls exit(2) at designated points in code
 */
typedef enum {
	OM_no_breakpoint = 0,
	OM_breakpoint_before_TI,
	OM_breakpoint_after_TI
} om_breakpoint_t;

/*
 * failure structure - it contains error code and related strings desribing
 * where and why the failure happened
 */
typedef struct om_failure {
	int	code;

	char 	*source;	/* where the failure happened */
	char	*reason;	/* why the failure happened */
} om_failure_t;


#define	OM_PREINSTALL	1

#define	ONEMB		1048576

#define	OM_SUCCESS	0
#define	OM_FAILURE	-1

#define	OM_UNKNOWN_STRING	"unknown"
#define	OM_PARTITION_UNKNOWN	99
#define	OM_SLICE_UNKNOWN	99
#define	OM_INVALID_MILESTONE	-1

#define	OM_MIN_MEDIA_SIZE	8192

#define	OM_MAX_VOL_NUM		1

#define	OM_CPIO_TRANSFER	0
#define	OM_IPS_TRANSFER		1

/*
 * allocate maximum possible size for slice or partition
 */
#define	OM_MAX_SIZE		0

/*
 * Attributes for nv_list to pass data to perform install/upgrade
 */
#define	OM_ATTR_INSTALL_TYPE		"install_type"
#define	OM_ATTR_UPGRADE_TARGET		"upgrade_target"
#define	OM_ATTR_DISK_NAME		"disk_name"
#define	OM_ATTR_TIMEZONE_INFO		"timezone"
#define	OM_ATTR_DEFAULT_LOCALE		"default_locale"
#define	OM_ATTR_HOST_NAME		"host_name"
#define	OM_ATTR_ROOT_PASSWORD		"root_password"
#define	OM_ATTR_USER_NAME		"user_name"
#define	OM_ATTR_LOGIN_NAME		"login_name"
#define	OM_ATTR_USER_PASSWORD		"user_password"
#define	OM_ATTR_LOCALES_LIST		"locales_list"
#define	OM_ATTR_TRANSFER		"transfer_params"
#define	OM_ATTR_INSTALL_TEST		"install_test"
#define	OM_ATTR_SWAP_SIZE		"swap_size"
#define	OM_ATTR_DUMP_SIZE		"dump_size"

#define	OM_DEFAULT_ROOT_PASSWORD	""
#define	OM_DEFAULT_USER_PASSWORD	""
/*
 * Target discovery - Disk related error ids
 */
#define	OM_TD_DISCOVERY_FAILED			101
#define	OM_DISCOVERY_NEEDED			102
#define	OM_NO_DISKS_FOUND			103
#define	OM_TD_IN_PROGRESS			104
#define	OM_NO_PARTITION_FOUND			105
#define	OM_NO_SPACE				106
#define	OM_INVALID_DISK_PARTITION		107
#define	OM_NO_UPGRADE_TARGETS_FOUND		108
#define	OM_FORMAT_UNKNOWN			109
#define	OM_BAD_DISK_NAME			110
#define	OM_CONFIG_EXCEED_DISK_SIZE		111
#define	OM_NO_UPGRADE_TARGET_NAME		112
#define	OM_UNSUPPORTED_CONFIG			113
#define	OM_TRANSFER_FAILED			114
#define	OM_ZFS_ROOT_POOL_EXISTS			115

#define	OM_NO_INSTALL_TARGET			201
#define	OM_BAD_INSTALL_TARGET			202
#define	OM_NO_INSTALL_TYPE			203
#define	OM_BAD_INSTALL_TYPE			204
#define	OM_INITIAL_INSTALL_PROFILE_FAILED	205
#define	OM_INITIAL_INSTALL_FAILED		206
#define	OM_SIZE_IS_SMALL			207
#define	OM_TARGET_INSTANTIATION_FAILED		208
#define	OM_NO_TARGET_ATTRS			209
#define	OM_NO_HOSTNAME_ATTRS			210
#define	OM_NO_UNAME_ATTRS			211
#define	OM_NO_LNAME_ATTRS			212
#define	OM_NO_UPASSWD_ATTRS			213
#define	OM_NO_RPASSWD_ATTRS			214
#define	OM_NO_HOSTNAME				215
#define	OM_NO_UNAME				216
#define	OM_NO_LNAME				217
#define	OM_NO_UPASSWD				218
#define	OM_NO_RPASSWD				219
#define	OM_SLICES_OVERLAP			220

#define	OM_NO_UPGRADE_TARGET			301
#define	OM_BAD_UPGRADE_TARGET			302
#define	OM_NOT_UFS_UPGRADE_TARGET		303
#define	OM_UPGRADE_PROFILE_FAILED		304
#define	OM_UPGRADE_FAILED			305
#define	OM_CANNOT_LOAD_MEDIA			306
#define	OM_NOT_ENOUGH_SPACE			307
#define	OM_SPACE_CHECK_FAILURE			308
#define	OM_CANNOT_UMOUNT_ROOT_SWAP		309
#define	OM_UPGRADE_NOT_ALLOWED			310

#define	OM_ERROR_THREAD_CREATE			901
#define	OM_NO_PROGRESS_FILE			902
#define	OM_NO_PROCESS				903
#define	OM_INVALID_USER				904
#define	OM_TOOLS_INSTALL_FAILURE		905
#define	OM_MISSING_TOOLS_SCRIPT			906
#define	OM_CANT_CREATE_VTOC_TARGET		907
#define	OM_CANT_CREATE_ZPOOL			908
#define	OM_CANT_CREATE_ZVOL			909
#define	OM_ALREADY_EXISTS			910
#define	OM_PROTECTED				911
#define	OM_BAD_INPUT				999

/*
 * Locale, language discovery related error codes
 */
#define	OM_NO_LOCALE_DIR			401
#define	OM_PERMS				402
#define	OM_TOO_MANY_FD				403
#define	OM_FOUND				404
#define	OM_NO_LOCALES				405
#define	OM_NOT_LANG				406
#define	OM_INVALID_LANG_LIST			407
#define	OM_INVALID_LOCALE			408

/*
 * Timezone related error codes
 */
#define	OM_TIMEZONE_NOT_SET			600
#define	OM_INVALID_TIMEZONE			601

/*
 * Install Completion Task related error codes
 */
#define	OM_ICT_FAILURE				800

/*
 * Nodename/hostname failures
 */

#define	OM_SET_NODENAME_FAILURE			500
#define	OM_NO_SUCH_DB_FILE			501
#define	OM_CANT_OPEN_FILE			502
#define	OM_CANT_CREATE_TMP_FILE			503
#define	OM_CANT_WRITE_TMP_FILE			504
#define	OM_CANT_WRITE_FILE			505
#define	OM_SETNODE_FAILURE			506
#define	OM_INVALID_NODENAME			507
#define	OM_CANT_DUP_DESC			508
#define	OM_EEPROM_ERROR				509

#define	OM_CANT_EXEC				1001

/* disk_target.c */
om_handle_t	om_initiate_target_discovery(om_callback_t td_cb);
void		om_free_target_data(om_handle_t handle);

/* disk_info.c */
disk_info_t	*om_get_disk_info(om_handle_t handle, int *total);
void		om_free_disk_info(om_handle_t handle, disk_info_t *dinfo);
disk_info_t	*om_duplicate_disk_info(om_handle_t handle, disk_info_t *dinfo);
disk_info_t	**om_convert_linked_disk_info_to_array(om_handle_t handle,
		    disk_info_t *dinfo, int total);
void		om_free_disk_info_array(om_handle_t handle,
		    disk_info_t **dinfo);
disk_info_t	*om_get_boot_disk(disk_info_t *disk_list);
disk_info_t	*om_find_disk_by_ctd_name(disk_info_t *disk_list,
		    char *ctd_name);

/* disk_parts.c */
disk_parts_t	*om_get_disk_partition_info(om_handle_t handle, char *diskname);
void		om_free_disk_partition_info(om_handle_t handle,
			disk_parts_t *dpinfo);
disk_parts_t	*om_validate_and_resize_disk_partitions(om_handle_t handle,
			disk_parts_t *dpinfo, partition_allocation_scheme_t);
disk_parts_t    *om_duplicate_disk_partition_info(om_handle_t handle,
			disk_parts_t *dparts);
int		om_set_disk_partition_info(om_handle_t handle,
			disk_parts_t *dp);
boolean_t	om_create_partition(uint8_t, uint64_t, uint64_t, boolean_t,
			boolean_t);
boolean_t	om_delete_partition(uint8_t, uint64_t, uint64_t);
boolean_t	om_finalize_fdisk_info_for_TI(void);
disk_parts_t	*om_init_disk_partition_info(disk_info_t *);
void		om_create_target_partition_info_if_absent(void);
void		om_invalidate_slice_info(void);
boolean_t	om_install_partition_is_logical(void);

/* disk_slices.c */
disk_slices_t   *om_get_slice_info(om_handle_t handle, char *diskname);
int		om_get_device_target_info(uint8_t *slice_number,
			char **diskname);
void		om_free_disk_slice_info(om_handle_t handle,
			disk_slices_t *dsinfo);
disk_slices_t   *om_duplicate_slice_info(om_handle_t handle,
			disk_slices_t *dslices);
int		om_set_slice_info(om_handle_t, disk_slices_t *);
boolean_t	om_create_slice(uint8_t, uint64_t, om_slice_tag_type_t,
			om_on_existing_t);
boolean_t	om_delete_slice(uint8_t);
boolean_t	om_preserve_slice(uint8_t);
disk_slices_t   *om_init_slice_info(const char *);
boolean_t	om_finalize_vtoc_for_TI(uint8_t);

/* upgrade_target.c */
upgrade_info_t	*om_get_upgrade_targets(om_handle_t handle, uint16_t *found);
upgrade_info_t  *om_get_upgrade_targets_by_disk(om_handle_t handle,
		    char *diskname, uint16_t *found);
void		om_free_upgrade_targets(om_handle_t handle,
		    upgrade_info_t *uinfo);
upgrade_info_t  *om_duplicate_upgrade_targets(om_handle_t handle,
		    upgrade_info_t *uiptr);
boolean_t	om_is_upgrade_target_valid(om_handle_t handle,
		    upgrade_info_t *uinfo, om_callback_t ut_cb);

/* perform_slim_install.c */
int	om_perform_install(nvlist_t *uchoices, om_callback_t inst_cb);
uint64_t	om_get_min_size(char *media, char *distro);
uint64_t	om_get_recommended_size(char *media, char *distro);
uint32_t	om_get_max_usable_disk_size(void);
boolean_t	om_is_automated_installation(void);
int		om_unmount_target_be(void);


uid_t		om_get_user_uid(void);
char		*om_encrypt_passwd(char *passwd, char *username);
void		om_set_breakpoint(om_breakpoint_t breakpoint);

/* locale.c */
locale_info_t	*om_get_def_locale(locale_info_t *loclist);
lang_info_t	*om_get_install_lang_info(int *total);
char		**om_get_install_lang_names(int *total);
lang_info_t  	*om_get_lang_info(int *total);
char		**om_get_lang_names(int *total);
locale_info_t	*om_get_locale_info(char *lang, int *total);
char		**om_get_locale_names(char *lang, int *total);
void		om_save_locale(char *locale, boolean_t install_only);
int   		om_set_install_lang_by_value(lang_info_t *localep);
int   		om_set_install_lang_by_name(char *lang);
int		om_set_default_locale_by_name(char *locale);
void		om_free_lang_info(lang_info_t *langp);
void		om_free_lang_names(char **listp);
void		om_free_locale_info(locale_info_t *localep);

/* timezone.c */
int		om_set_time_zone(char *timezone);

/* om_misc.c */
int16_t	om_get_error();
void om_set_error(int16_t errno);
char *om_get_failure_source(int16_t err_code);
char *om_get_failure_reason(int16_t err_code);
boolean_t om_is_valid_failure_code(int16_t err_code);


/* om_proc.c */
om_proc_return_t	om_process_running();

/* Test functions */
int	om_test_target_discovery();

#ifdef __cplusplus
}
#endif

#endif	/* _ORCHESTRATOR_API_H */
