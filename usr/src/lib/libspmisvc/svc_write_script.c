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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */



#include "spmicommon_api.h"
#include "spmisoft_lib.h"
#include "spmisvc_lib.h"
#include "svc_templates.h"
#include "instzones_lib.h"
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <libintl.h>
#include <netinet/in.h>
#include <sys/mntent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/zone.h>
#include <assert.h>

enum instance_type { UNIQUE, OVERWRITE };

struct admin_file {
	struct admin_file *next;
	char		  admin_name[4];
	enum instance_type inst_type;
	char		  basedir[2];  /* must be at end of struct */
};

struct softinfo_merge_entry {
	struct softinfo_merge_entry	*next;
	Modinfo				*new_mi;
	Modinfo				*cur_mi;
};

/* Local Statics and Constants */

#define	streq(s1, s2) (strcmp((s1), (s2)) == 0)

#define	DEBUG_CALLBACK_FILE "/tmp/upgrade_callback.sh"

static int num_pkgs_in_zone = 0;
static char **g_zonelist = NULL;

#define	GLOBALZONE "global"
#define	NZLIST "nzlist"
#define	GZONE(z) (((z) == NULL) ? "global" : (z))
#define	ZLIST(z) (((z) == NULL) ? NZLIST : (z))

static struct admin_file *adminfile_head = NULL;
static int	admin_seq = 0;
static FILE	*fp;
static int	start_perm_printed = 0;
static char	ascii_number[30];
static Module	*s_newproductmod;
static Module	*s_localmedia;
static Product	*s_newproduct;
static struct softinfo_merge_entry *softinfo_merge_chain = NULL;
static struct softinfo_merge_entry *smep_cur = NULL;

/* Externals */

extern int	g_is_swm;
extern int	g_online_upgrade;
extern char 	*g_swmscriptpath;
static char	newver[MAXPATHLEN];
static int 	copyright_printed = 0;
static int	action_count;

/* Public Function Prototypes */

int		write_script(Module *, int);
void		set_umount_script_fcn(int (*)(FILE *, int), void (*)(FILE *));

/* Library Function Prototypes */
void		generate_swm_script(char *);

/* Local Function Prototypes */

static void	gen_inetboot_files(Module *);
static void	gen_softinfo(Product *);
static int	gen_rm_svc(Module *);
static int	gen_mv_svc(Module *, Module *);
static int	archinlist(char *, Arch *);
static void	gen_add_svc(Module *);
static int	save_and_rm(Node *, caddr_t);
static void	_save_and_rm(Modinfo *, Module *);
static void	remove_patches(Module *);
static int	walk_pkgrm_f(Node *, caddr_t);
static void	_walk_pkgrm_f(Modinfo *, Module *);
static int	walk_root_kvm_softinfo(Node *, caddr_t);
static void	_walk_root_kvm_softinfo(Modinfo *, Product *);
static void	_gen_root_kvm_softinfo(Modinfo *, Product *);
static int	gen_softinfo_merge_chain(Node *, caddr_t);
static void	_gen_softinfo_merge_chain(Modinfo *, Product *);
static char	*getrealbase(Modinfo *);
static int	pkgadd_or_spool(Node *, caddr_t);
static void	_pkgadd_or_spool(Modinfo *, Module *);
static char	*newadmin(char *, enum instance_type);
static void	modify_existing_svc(Module *);
static void	add_new_isa_svc(Module *);
static void	gen_share_commands(Module *);
static int	restore_perm(Node *, caddr_t);
static void	_restore_perm(Modinfo *, Module *);
static char	*cvtperm(mode_t);
static char	*cvtuid(uid_t);
static char	*cvtgid(gid_t);
int (*script_fcn)(FILE *fp, int do_root) = NULL;
void (*install_boot_fcn)(FILE *fp) = NULL;
static Product	*get_product(void);
static void	gen_locales_installed(FILE *, Product *, char *);
static char	*locales_installed_path(void);
static char	*inst_release_path(Product *prod);
static char	*softinfo_services_path(void);
static char	*cluster_path(Product *);
static char	*clustertoc_path(Product *);
static void	gen_softinfo_locales(FILE *, Product *, Product *);
static void	merge_softinfo_locales(FILE *, Product *);
static char	*upgrade_cleanup_path(void);
static char	*upgrade_failedpkgs_path(void);
static char	*upgrade_restart_path(void);
static void	upgrade_clients_inetboot(Module *, Product *);
static void	remove_entire_svc(Module *);
static char	*build_upgrade_debug_callback(void);
static char	*pkgs_tobeadded_path(void);
static void	determine_zonelist(Module **, Module *);
static void	add_to_zonelist(char *);
static void	generate_zonelist(Modinfo *, int);


/* ******************************************************************** */
/*			PUBLIC SUPPORT FUNCTIONS			*/
/* ******************************************************************** */

/*
 * write_script()
 * Parameters:
 *	prodmod	-
 *	do_sync - 1 to include "sync", "0" to not include "sync" in upgrade
 * Return:
 * Status:
 *	public
 */
int
write_script(Module * prodmod, int do_sync)
{
	int		i;
	char	scriptpath[MAXPATHLEN];
	char	buf[5 * MAXPATHLEN];
	char	timestr[20];
	char	*root_path;
	char	service[MAXPATHLEN];
	char	clientarch[30], *karch;
	long	timestamp;
	Module	*mod, *cmod;
	Module	*modzlist[MAX_ZONEID];

	/*
	 * Reinitialize start_perm_printed in case write_script is called
	 * more than once. This can occur and is seen in bug 4010183
	 * which this fixes.
	 */
	start_perm_printed = 0;
	action_count = 0;
	s_newproductmod = prodmod;
	s_localmedia = get_localmedia();
	s_newproduct = prodmod->info.prod;

	timestamp = time((long *)0);
	(void) sprintf(timestr, "%ld", timestamp);

	root_path = get_rootdir();

	if (g_is_swm == 1)
		(void) strcpy(scriptpath, g_swmscriptpath);
	else if (s_localmedia->info.media->med_flags & BASIS_OF_UPGRADE) {
		if (*root_path == '\0') {
			(void) strcpy(scriptpath,
			    upgrade_script_path(s_newproduct));
		} else {
			(void) strcpy(scriptpath, root_path);
			(void) strcat(scriptpath,
			    upgrade_script_path(s_newproduct));
		}
	} else {
		if (*root_path == '\0') {
			(void) strcpy(scriptpath,
			    upgrade_script_path(s_localmedia->sub->info.prod));
		} else {
			(void) strcpy(scriptpath, root_path);
			(void) strcat(scriptpath,
			    upgrade_script_path(s_localmedia->sub->info.prod));
		}
	}

	if (GetSimulation(SIM_EXECUTE))
		(void) strcpy(scriptpath, "/tmp/upgrade_script");

	if ((fp = fopen(scriptpath, "w")) == (FILE *)NULL) {
		write_notice(ERRMSG, dgettext("SUNW_INSTALL_LIBSVC",
		    "Cannot create upgrade script: %s\n"), strerror(errno));
		return (-1);
	}

	(void) chmod(scriptpath, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	/*
	 * SYNC_CALL allows the opportunity to call /bin/sync before and after a
	 * package is added - when doing a stand alone upgrade, a sync is done
	 * before and after each package is added so that the upgrade can be
	 * continued by rebooting the system.  During a "live upgrade" this sync
	 * is not necessary and in fact slows down the upgrade
	 * by as much as 50%.
	 */

	scriptwrite(fp, LEVEL0|CONTINUE, script_start,
		"SYNC_CALL", (do_sync == 0 ? "# /bin/sync" : "/bin/sync"),
		"TIMESTAMP", timestr,
		"RESTART_PATH", upgrade_restart_path(),
		"CLEANUP_PATH", upgrade_cleanup_path(),
		"UPGRADE_FAILED_PKGS", upgrade_failedpkgs_path(),
		"DEBUG_CALLBACK", build_upgrade_debug_callback(),
		(char *)0);

	if (g_is_swm == 1) {
		scriptwrite(fp, LEVEL0|CONTINUE, init_swm_coalesce, (char *)0);
	} else {
		scriptwrite(fp, LEVEL0|CONTINUE, init_coalesce, (char *)0);
		if (script_fcn) {
			(*script_fcn)(fp, FALSE);
		}
	}

	/* Initialize the virtual package log */
	scriptwrite(fp, LEVEL0|CONTINUE, init_virtual_pkg_log,
	    "VIRTUAL_PKG_LOG", pkgs_tobeadded_path(),
	    (char *)0);

	/*
	 * Generate commands to upgrade var/sadm directory
	 * structure. This is only needed during an upgrade because it
	 * is assumed that if we are adding services the new directory
	 * structure is already present( >= Solaris 2.5), or not
	 * needed (< Solaris 2.5).
	 */

	if (s_localmedia->info.media->med_flags & BASIS_OF_UPGRADE) {
		/* Make sure new directories are present */
		scriptwrite(fp, LEVEL0|CONTINUE, mk_varsadm_dirs,
		    "CLIENTROOT", "/", (char *)0);

		/* Move file in old directory to new locations */
		scriptwrite(fp, LEVEL0|CONTINUE, mv_varsadm_files,
		    "CLIENTROOT", "/", (char *)0);
	}

	if (is_server())
		scriptwrite(fp, LEVEL0|CONTINUE, init_inetboot_dir, (char *)0);

	/* for each client, set up the new var/sadm directory structure, */

	/* find all clients */
	for (mod = get_media_head(); mod != NULL; mod = mod->next) {
		if (mod->info.media->med_type == INSTALLED &&
		    !streq(mod->info.media->med_dir, "/") &&
		    mod->sub->info.prod->p_zonename == NULL &&
		    mod->info.media->med_flags & BASIS_OF_UPGRADE) {
			/* Make new var/sadm directory tree and */
			/* move files from the old directories to */
			/* the new locations */
			scriptwrite(fp, LEVEL0|CONTINUE, mk_varsadm_dirs,
			    "CLIENTROOT", mod->sub->info.prod->p_rootdir,
			    (char *)0);
			scriptwrite(fp, LEVEL0|CONTINUE, mv_varsadm_files,
			    "CLIENTROOT", mod->sub->info.prod->p_rootdir,
			    (char *)0);
		}
	}

	/*
	 * If a server, modify the service links, remove old services,
	 * update the softinfo files.
	 */

	for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
		if (mod->info.media->med_type != INSTALLED_SVC)
			continue;
		/*
		 *  Determine whether the entire service, or any part
		 *  the service needs to be removed.  If the entire
		 *  service needs to be removed, continue, since there
		 *  no reason to look at it further.
		 */
		if (gen_rm_svc(mod) != 0)
			continue;	/* service was entirely removed */

		/*
		 *  Determine whether the entire service, or any part
		 *  the service needs to be moved.  If the entire
		 *  service needs to be moved, continue, since there is
		 *  no reason to look at it further.
		 */
		if (gen_mv_svc(mod, s_newproductmod) != 0)
			continue;	/* service was entirely moved */

		/*
		 *  If the service is entirely new,  add it and continue.
		 */
		if ((mod->info.media->med_flags & NEW_SERVICE) &&
		    !(mod->info.media->med_flags & BUILT_FROM_UPGRADE) &&
		    has_view(s_newproductmod, mod) == SUCCESS) {
			(void) load_view(s_newproductmod, mod);
			gen_add_svc(mod);
			continue;
		}

		if ((mod->info.media->med_flags & NEW_SERVICE) &&
		    (mod->info.media->med_flags & BUILT_FROM_UPGRADE))
			continue;

		if (has_view(s_newproductmod, mod) == SUCCESS) {
			(void) load_view(s_newproductmod, mod);
			modify_existing_svc(mod);
		}
	}

	/* generate admin scripts */
	scriptwrite(fp, LEVEL0|CONTINUE, build_admin_file, "NAME", "dflt",
	    "INSTANCE", "overwrite", "BASEDIR", "default", (char *)0);
	scriptwrite(fp, LEVEL0|CONTINUE, build_admin_file, "NAME", "root",
	    "INSTANCE", "overwrite", "BASEDIR", "/", (char *)0);
	scriptwrite(fp, LEVEL0|CONTINUE, build_admin_file, "NAME", "usr",
	    "INSTANCE", "overwrite", "BASEDIR", "/usr", (char *)0);
	scriptwrite(fp, LEVEL0|CONTINUE, build_admin_file, "NAME", "opt",
	    "INSTANCE", "overwrite", "BASEDIR", "/opt", (char *)0);
	scriptwrite(fp, LEVEL0|CONTINUE, build_admin_file, "NAME", "un.root",
	    "INSTANCE", "unique", "BASEDIR", "/", (char *)0);
	scriptwrite(fp, LEVEL0|CONTINUE, build_admin_file, "NAME", "un.usr",
	    "INSTANCE", "unique", "BASEDIR", "/usr", (char *)0);
	scriptwrite(fp, LEVEL0|CONTINUE, build_admin_file, "NAME", "un.opt",
	    "INSTANCE", "unique", "BASEDIR", "/opt", (char *)0);

	(void) strcpy(newver, s_newproduct->p_version);

	/*
	 * Determine the zonelist "-O zonelist=<...>" for all pkgs
	 * in the global and non-global zones.
	 */

	if ((g_is_swm != 1) && z_zones_are_implemented()) {
		i = 1;
		for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
			if (mod->info.media->med_type == INSTALLED &&
			    mod->info.media->med_type != INSTALLED_SVC &&
			    mod->info.media->med_flags & BASIS_OF_UPGRADE &&
				streq(mod->info.media->med_dir, "/") &&
			    has_view(s_newproductmod, mod) == SUCCESS) {
					modzlist[0] = mod;
			} else if (mod->info.media->med_type == INSTALLED &&
			    mod->info.media->med_type != INSTALLED_SVC &&
			    mod->info.media->med_flags & BASIS_OF_UPGRADE &&
				!streq(mod->info.media->med_dir, "/") &&
			    has_view(s_newproductmod, mod) == SUCCESS) {
					modzlist[i++] = mod;
			}
		}
		modzlist[i] = NULL;
		(void) load_local_view(s_newproductmod);
		determine_zonelist(modzlist, s_newproductmod);
	}

	/*
	 * Generate all commands to save old files and remove
	 * defunct packages.
	 */

	scriptwrite(fp, LEVEL0, print_rmpkg_msg, (char *)0);

	for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
		if (mod->info.media->med_type == INSTALLED &&
		    mod->info.media->med_flags & BASIS_OF_UPGRADE) {
			(void) load_view(s_newproductmod, mod);
			(void) walklist(mod->sub->info.prod->p_packages,
		    save_and_rm, (caddr_t)mod);
			remove_patches(mod);
		} else if (mod->info.media->med_type == INSTALLED_SVC) {
			if ((mod->info.media->med_flags & BUILT_FROM_UPGRADE) &&
			    !(mod->info.media->med_flags & BASIS_OF_UPGRADE))
				continue;
			if ((mod->info.media->med_flags & SVC_TO_BE_REMOVED) &&
			    !(mod->info.media->med_flags & BASIS_OF_UPGRADE))
				continue;
			if (mod->info.media->med_flags & NEW_SERVICE)
				continue;
			if (mod->info.media->med_flags & BASIS_OF_UPGRADE &&
			    mod->info.media->med_upg_to != NULL &&
			    has_view(s_newproductmod,
			    mod->info.media->med_upg_to) == SUCCESS) {
				(void) load_view(s_newproductmod,
				    mod->info.media->med_upg_to);
			} else
				(void) load_view(s_newproductmod, mod);
			(void) walklist(mod->sub->info.prod->p_packages,
			    save_and_rm, (caddr_t)mod);
			if (mod->info.media->med_flags & SVC_TO_BE_REMOVED)
				scriptwrite(fp, LEVEL0|CONTINUE,
					rm_template_dir,
					"SVC", mod->info.media->med_volume,
					(char *)0);
			if (!(mod->info.media->med_flags & SPLIT_FROM_SERVER))
				remove_patches(mod);
		}
	}

	/*
	 * Generate commands to update INST_RELEASE file for those installables
	 * that need it.  The giant boolean expression logic is the same
	 * boolean expression used when determining which installables need
	 * a new .clustertoc.  The expression is explained a few blocks
	 * of code below this point.  We update the INST_RELEASE file here for
	 * packages that read the file (and have appropriate private contracts
	 * to do so).
	 */

	for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
		if ((mod->info.media->med_type == INSTALLED &&
		    mod->info.media->med_flags & BASIS_OF_UPGRADE) ||
		    (mod->info.media->med_type == INSTALLED_SVC &&
		    (mod->info.media->med_flags & BUILT_FROM_UPGRADE ||
			mod->info.media->med_flags & NEW_SERVICE) &&
		    (!(mod->info.media->med_flags & SPLIT_FROM_SERVER)) &&
		    !(mod->info.media->med_flags & BUILT_FROM_UPGRADE &&
			mod->info.media->med_upg_from->info.media->med_flags &
			SPLIT_FROM_SERVER)) &&
			has_view(s_newproductmod, mod) == SUCCESS) {
			(void) load_view(s_newproductmod, mod);
			if (mod->sub->info.prod->p_zonename == NULL) {
				scriptwrite(fp, LEVEL0|CONTINUE,
				    echo_INST_RELEASE,
				    "INST_REL_PATH",
				    inst_release_path(s_newproduct),
				    "OS",
				    s_newproduct->p_name,
				    "VERSION",
				    s_newproduct->p_version,
				    "ROOT",
				    mod->info.media->med_dir,
				    "REVISION",
				    s_newproduct->p_rev, (char *)0);
			} else {
				scriptwrite(fp, LEVEL0|CONTINUE,
				zone_echo_INST_RELEASE,
				"INST_REL_PATH",
				inst_release_path(s_newproduct),
				"OS",
				s_newproduct->p_name,
				"VERSION",
				s_newproduct->p_version,
				"ROOT",
				mod->info.media->med_dir,
				"ZONENAME",
				mod->sub->info.prod->p_zonename,
				"REVISION",
				s_newproduct->p_rev, (char *)0);
			}
		}
	}

	/*
	 *  Generate all pkgadd commands for new packages.
	 */
	for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
		if (mod->info.media->med_type == INSTALLED &&
		    mod->info.media->med_flags & BASIS_OF_UPGRADE &&
		    has_view(s_newproductmod, mod) == SUCCESS) {
			(void) load_view(s_newproductmod, mod);
			(void) walklist(s_newproductmod->info.prod->p_packages,
			    pkgadd_or_spool, (caddr_t)mod);
		} else if (mod->info.media->med_type == INSTALLED_SVC) {
			if ((mod->info.media->med_flags & BUILT_FROM_UPGRADE) &&
			    !(mod->info.media->med_flags & BASIS_OF_UPGRADE))
				continue;
			if ((mod->info.media->med_flags & SVC_TO_BE_REMOVED) &&
			    !(mod->info.media->med_flags & BASIS_OF_UPGRADE))
				continue;
			if (mod->info.media->med_flags & BASIS_OF_UPGRADE &&
			    has_view(s_newproductmod,
			    mod->info.media->med_upg_to) == SUCCESS) {
				(void) load_view(s_newproductmod,
				    mod->info.media->med_upg_to);
				(void) walklist(
				    s_newproductmod->info.prod->p_packages,
				    pkgadd_or_spool, (caddr_t)mod);
			} else if (has_view(s_newproductmod, mod) == SUCCESS) {
				(void) load_view(s_newproductmod, mod);
				(void) walklist(
				    s_newproductmod->info.prod->p_packages,
				    pkgadd_or_spool, (caddr_t)mod);
			}
		}
	}

	/*
	 * Generate all commands to restore permissions
	 */
	for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
		if (mod->info.media->med_type == INSTALLED) {
			(void) walklist(mod->sub->info.prod->p_packages,
				restore_perm, (caddr_t)mod);
		} else if (mod->info.media->med_type == INSTALLED_SVC) {
			if ((mod->info.media->med_flags & BUILT_FROM_UPGRADE) &&
			    !(mod->info.media->med_flags & BASIS_OF_UPGRADE))
				continue;
			if ((mod->info.media->med_flags & SVC_TO_BE_REMOVED) &&
			    !(mod->info.media->med_flags & BASIS_OF_UPGRADE))
				continue;
			if (!(mod->info.media->med_flags & NEW_SERVICE)) {
				(void) walklist(mod->sub->info.prod->p_packages,
				    restore_perm, (caddr_t)mod);
			}
		}
	}

	if (start_perm_printed) {
		scriptwrite(fp, LEVEL0|CONTINUE, end_perm_restores, (char *)0);
	}

	/*
	 * Generate all commands to remove replaced packages
	 * from package database.  Also, generate commands
	 * to remove patch directories for patch packages that
	 * were overwritten by pkgadd.
	 */

	for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
		if (mod->info.media->med_type == INSTALLED) {
		    (void) walklist(mod->sub->info.prod->p_packages,
			    walk_pkgrm_f, (caddr_t)mod);
		} else if (mod->info.media->med_type == INSTALLED_SVC) {
			if ((mod->info.media->med_flags & BUILT_FROM_UPGRADE) &&
			    !(mod->info.media->med_flags & BASIS_OF_UPGRADE))
				continue;
			if ((mod->info.media->med_flags & SVC_TO_BE_REMOVED) &&
			    !(mod->info.media->med_flags & BASIS_OF_UPGRADE))
				continue;
			if (!(mod->info.media->med_flags & NEW_SERVICE)) {
				(void) walklist(mod->sub->info.prod->p_packages,
				    walk_pkgrm_f, (caddr_t)mod);
			}
		}
	}

	/*
	 * generate commands to copy CLUSTER and .clustertoc files to
	 * local and service directories.
	 */
	/*
	 * Here's some explanation of the mondo boolean expression
	 * that follows:  environments that need to have their
	 * .clustertoc, CLUSTER, and INST_RELEASE files updated are:
	 * (1) installed environments that have been upgraded,
	 * (2) services that are entirely new or have been built by
	 *	by an upgrade, but with the following exceptions:
	 *	   -  the environment should not be split from the
	 *		server (the server's update will catch it)
	 *	   -  if the service is built from an upgrade of
	 *		an existing service, and the service from
	 *		which the upgrade is being done is split from
	 *		the server
	 * In addition, any environment getting CLUSTER, .clustertoc,
	 * and INST_RELEASE files must have a view of the new media.
	 *
	 * Notice that at this point the INST_RELEASE file has already
	 * been updated (a block of code a few screens up has already
	 * done this, using the same giant boolean expression found below),
	 * so we don't need to do it again.
	 */
	for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
		if ((mod->info.media->med_type == INSTALLED &&
		    mod->info.media->med_flags & BASIS_OF_UPGRADE) ||
		    (mod->info.media->med_type == INSTALLED_SVC &&
		    (mod->info.media->med_flags & BUILT_FROM_UPGRADE ||
			mod->info.media->med_flags & NEW_SERVICE) &&
		    (!(mod->info.media->med_flags & SPLIT_FROM_SERVER)) &&
		    !(mod->info.media->med_flags & BUILT_FROM_UPGRADE &&
			mod->info.media->med_upg_from->info.media->med_flags &
			SPLIT_FROM_SERVER)) &&
		    has_view(s_newproductmod, mod) == SUCCESS) {
			(void) load_view(s_newproductmod, mod);
			/*
			 * first, look for a selected metacluster.
			 * If none, look for a required metacluster.
			 */
			for (cmod = s_newproductmod->sub; cmod != NULL;
			    cmod = cmod->next) {
				if (cmod->type == METACLUSTER &&
				    cmod->info.mod->m_status == SELECTED)
					break;
			}
			if (cmod == NULL) {
				for (cmod = s_newproductmod->sub;
				    cmod != NULL;
				    cmod = cmod->next)
					if (cmod->type == METACLUSTER &&
					    cmod->info.mod->m_status ==
					    REQUIRED)
						break;
			}
			if (cmod) {
				if (mod->sub->info.prod->p_zonename == NULL) {
					scriptwrite(fp, LEVEL0|CONTINUE,
					    write_CLUSTER,
						"CLUSTER_PATH",
					    cluster_path(s_newproduct),
						"ROOT",
					    mod->info.media->med_dir,
						"CLUSTER",
					    cmod->info.mod->m_pkgid,
						(char *)0);
				} else {
					scriptwrite(fp, LEVEL0|CONTINUE,
					    zone_write_CLUSTER,
						"CLUSTER_PATH",
					    cluster_path(s_newproduct),
						"CLUSTER",
					    cmod->info.mod->m_pkgid,
						"ZONENAME",
					    mod->sub->info.prod->p_zonename,
						(char *)0);
				}
			}
			if (mod->sub->info.prod->p_zonename == NULL) {
				scriptwrite(fp, LEVEL0|CONTINUE,
				    write_clustertoc,
					"CLUSTERTOC_PATH",
				    clustertoc_path(s_newproduct),
					"ROOT", mod->info.media->med_dir,
					"TOC",
				    get_clustertoc_path(s_newproductmod),
				    (char *)0);
			}

			if (mod->sub->info.prod->p_zonename == NULL) {
				gen_locales_installed(fp, s_newproduct,
				    mod->info.media->med_dir);

				upg_write_platform_file(fp,
				    mod->info.media->med_dir,
					s_newproduct, mod->sub->info.prod);
			}
		}
	}

	for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
		if (mod->info.media->med_type == INSTALLED_SVC) {
			if ((mod->info.media->med_flags & BASIS_OF_UPGRADE) &&
			    !(mod->info.media->med_flags &
			    BUILT_FROM_UPGRADE) &&
			    has_view(s_newproductmod,
			    mod->info.media->med_upg_to) == SUCCESS) {
				(void) load_view(s_newproductmod,
				    mod->info.media->med_upg_to);
				gen_inetboot_files(mod);
			} else if ((mod->info.media->med_flags & NEW_SERVICE) &&
			    !(mod->info.media->med_flags &
				    BUILT_FROM_UPGRADE) &&
			    has_view(s_newproductmod, mod) == SUCCESS) {
				(void) load_view(s_newproductmod, mod);
				gen_inetboot_files(mod);
			} else if ((mod->info.media->med_flags & NEW_SERVICE) &&
			    (mod->info.media->med_flags &
				    BUILT_FROM_UPGRADE)) {
				continue;
			} else if (has_view(s_newproductmod, mod) == SUCCESS) {
				(void) load_view(s_newproductmod, mod);
				gen_inetboot_files(mod);
			}
		}
	}

	/* for each client, set up vfstab and inetboot entry */
	/* only upgrade clients with BASIS_OF_UPGRADE flag set */
	if ((g_is_swm != 1) && is_server()) {
		(void) snprintf(service, sizeof (service), "%s_%s",
		    s_newproduct->p_name, s_newproduct->p_version);
		/* find all clients */
		for (mod = get_media_head(); mod != NULL; mod = mod->next) {
			if (mod->info.media->med_type == INSTALLED &&
			    !streq(mod->info.media->med_dir, "/") &&
			    mod->sub->info.prod->p_zonename == NULL &&
			    mod->info.media->med_flags & BASIS_OF_UPGRADE) {
				(void) strcpy(clientarch,
				    mod->sub->info.prod->p_arches->a_arch);
				karch = (char *)strchr(clientarch, '.');
				*karch++ = '\0';
				if (!streq(mod->info.media->med_volume,
				    service)) {
					/*
					 * The vfstab needs the kvm entries
					 * removed if upgrade is from a
					 * pre-KBI to a post-KBI system,
					 * since they are no longer used.
					 */
					if (is_KBI_service(s_newproduct) &&
					    ! is_KBI_service(
						    mod->sub->info.prod))
						scriptwrite(fp,
							LEVEL0|CONTINUE,
							sed_vfstab_rm_kvm,
							"CLIENTROOT",
						mod->sub->info.prod->p_rootdir,
							(char *)0);
					scriptwrite(fp, LEVEL0|CONTINUE,
						sed_vfstab,
						"CLIENTROOT",
						mod->sub->info.prod->p_rootdir,
						"OLD",
						mod->info.media->med_volume,
						"NEW", service,
						"ARCH", clientarch,
						"KARCH", karch, (char *)
						0);

					/*
					 * update tftpboot entries for each
					 * client
					 */
					upgrade_clients_inetboot(mod,
					    s_newproduct);
				}

				scriptwrite(fp, LEVEL0,
				    touch_client_reconfigure, "CLIENTROOT",
				    mod->sub->info.prod->p_rootdir,
				    (char *)0);
			}
		}
	}

	if (g_is_swm == 1) {
		for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
			if (mod->info.media->med_type == INSTALLED_SVC &&
			    !svc_unchanged(mod->info.media) ||
			    has_view(s_newproductmod, mod) == SUCCESS) {
					(void) load_view(s_newproductmod, mod);
					gen_share_commands(mod);
			}
		}
	}

	if (s_localmedia->info.media->med_flags & BASIS_OF_UPGRADE) {
		if (!is_server() &&
		    s_localmedia->sub->info.prod->p_zonename
		    == NULL) {
			scriptwrite(fp, LEVEL0|CONTINUE, echo_softinfo,
			    "SERVICE_PATH", softinfo_services_path(),
			    "OS", s_newproduct->p_name,
			    "VERSION", s_newproduct->p_version,
			    "REVISION", s_newproduct->p_rev, (char *)0);
		}

			if (install_boot_fcn)
				(*install_boot_fcn)(fp);
			scriptwrite(fp, LEVEL0, touch_reconfig, (char *)0);
	}

	for (mod = get_media_head(); mod != NULL; mod = mod->next)  {
		if (mod->info.media->med_type == INSTALLED &&
		    mod->info.media->med_flags & BASIS_OF_UPGRADE) {
			scriptwrite(fp, LEVEL0, rm_tmp, "ZONENAME",
			    GZONE(mod->sub->info.prod->p_zonename),
			    (char *)0);
		}
	}

	scriptwrite(fp, LEVEL0|CONTINUE, remove_restart_files, (char *)0);

	scriptwrite(fp, LEVEL0|CONTINUE, print_cleanup_msg,
	    "UPGRADE_LOG", upgrade_log_path(s_newproduct),
	    "UPGRADE_CLEANUP", upgrade_cleanup_path(),
	    (char *)0);

	scriptwrite(fp, LEVEL0|CONTINUE, exit_ok, (char *)0);

	if (!GetSimulation(SIM_EXECUTE) || (get_trace_level() > 0)) {
		/*
		 * Now plug in the value of action_count for total_actions
		 */
		(void) fclose(fp);
		(void) snprintf(buf, sizeof (buf), "mv %s %sTMP;"
		    "sed -e 's/^total_actions=0$/total_actions=%d/' %sTMP > %s;"
		    "rm %sTMP", scriptpath, scriptpath, action_count,
		    scriptpath, scriptpath, scriptpath);
		if (system(buf))
			return (-1);
	}
	return (0);
}

/*
 * set_umount_script_fcn()
 * Parameters:
 *	scriptfcn	-
 * Return:
 *	none
 * Status:
 *	public
 */
void
set_umount_script_fcn(int (*scriptfcn)(FILE *fp, int do_root),
    void (*installbootfcn)(FILE *fp))
{
	script_fcn = scriptfcn;
	install_boot_fcn = installbootfcn;
}

/*
 * generate_swm_script()
 * Parameters:
 *	scriptpath	-
 * Return:
 *	none
 * Status:
 *	void
 */
void
generate_swm_script(char *scriptpath)
{
	Module *mod;
	Module *prodmod;

#ifdef SW_LIB_LOGGING
	sw_lib_log_hook("generate_swm_script");
#endif

	g_is_swm = 1;
	g_swmscriptpath = scriptpath;

	for (mod = get_media_head(); mod != NULL; mod = mod->next) {
		if (mod->info.media->med_type != INSTALLED_SVC &&
					mod->info.media->med_type != INSTALLED)
			for (prodmod = mod->sub; prodmod != NULL;
						prodmod = prodmod->next)
				(void) write_script(prodmod, TRUE);
	}
}

/* ******************************************************************** */
/*			LIBRARY SUPPORT FUNCTIONS			*/
/* ******************************************************************** */

/* ******************************************************************** */
/*			INTERNAL SUPPORT FUNCTIONS			*/
/* ******************************************************************** */

/*
 * gen_inetboot_files()
 * Parameters:
 *	mod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static void
gen_inetboot_files(Module * mod)
{
	Arch *ap;
	char arch_buf[30], *karch;
	char bootd[10];

	for (ap = s_newproduct->p_arches; ap != NULL; ap = ap->a_next) {
		if (!(ap->a_selected))
			continue;
		(void) strcpy(arch_buf, ap->a_arch);
		karch = (char *)strchr(arch_buf, '.');
		*karch++ = '\0';
		if (strcmp(arch_buf, "i386") == 0)
			(void) strcpy(bootd, "rplboot");
		else
			(void) strcpy(bootd, "tftpboot");

		if ((mod->info.media->med_flags & SPLIT_FROM_SERVER) &&
		    strcmp(get_default_inst(), arch_buf) == 0)
			scriptwrite(fp, LEVEL0, cp_shared_inetboot,
			    "KARCH", karch,
			    "SVCPROD", s_newproduct->p_name,
			    "SVCVER", s_newproduct->p_version,
			    "BOOTDIR", bootd, (char *)0);
		else
			scriptwrite(fp, LEVEL0, cp_svc_inetboot,
			    "KARCH", karch,
			    "ARCH", arch_buf,
			    "SVCPROD", s_newproduct->p_name,
			    "SVCVER", s_newproduct->p_version,
			    "BOOTDIR", bootd, (char *)0);
		scriptwrite(fp, LEVEL0|CONTINUE, cp_inetboot, "KARCH", karch,
		    "SVCPROD", s_newproduct->p_name,
		    "SVCVER", s_newproduct->p_version,
		    "BOOTDIR", bootd, (char *)0);
	}
}

/*
 *
 * Parameters:
 *	prod
 * Return:
 *	none
 * Status:
 *	private
 */
static void
gen_softinfo(Product * prod)
{
	Arch *ap;
	char temparch[ARCH_LENGTH];
	struct softinfo_merge_entry *smep, *holdsmep;

	scriptwrite(fp, LEVEL0, start_softinfo,
	    "SERVICE_PATH", softinfo_services_path(),
	    "OS", s_newproduct->p_name,
	    "VER", s_newproduct->p_version,
	    "REVISION", s_newproduct->p_rev, (char *)0);

	isa_handled_clear();
	for (ap = prod->p_arches; ap != NULL; ap = ap->a_next) {
		if (ap->a_selected) {
			extract_isa(ap->a_arch, temparch);
			if (!isa_handled(temparch))
				scriptwrite(fp, LEVEL0|CONTINUE, usr_softinfo,
					"OS", s_newproduct->p_name,
					"VER", s_newproduct->p_version,
					"ARCH", temparch, (char *)0);
		}
	}

	for (ap = s_newproduct->p_arches; ap != NULL; ap = ap->a_next) {
		if (ap->a_selected) {
			extract_isa(ap->a_arch, temparch);
			if (!isa_handled(temparch))
				scriptwrite(fp, LEVEL0|CONTINUE, usr_softinfo,
					"OS", s_newproduct->p_name,
					"VER", s_newproduct->p_version,
					"ARCH", temparch, (char *)0);
		}
	}

	(void) walklist(prod->p_packages, gen_softinfo_merge_chain,
	    (caddr_t)s_newproduct);
	smep_cur = softinfo_merge_chain;

	(void) walklist(s_newproduct->p_packages, walk_root_kvm_softinfo,
	    (caddr_t)s_newproduct);

	/* Now print out the remainder of the softinfo_merge chain */
	while (smep_cur != NULL) {
		_gen_root_kvm_softinfo(smep_cur->cur_mi, s_newproduct);
		smep_cur = smep_cur->next;
	}

	/* free the softinfo merge chain */
	smep = softinfo_merge_chain;
	while (smep) {
		holdsmep = smep->next;
		free(smep);
		smep = holdsmep;
	}

	softinfo_merge_chain = NULL;

	upg_write_plat_softinfo(fp, s_newproduct, prod);

	gen_softinfo_locales(fp, s_newproduct, prod);

	scriptwrite(fp, LEVEL0, end_softinfo, (char *)0);
}

/*
 * returns non-zero if service removed entirely.  Returns zero if
 * none of the service is removed, or if only part of it is removed.
 */
static int
gen_rm_svc(Module *mod)
{
#ifdef SERVICE_REMOVAL_SUPPORTED
	Arch	*ap, *ap2;
	Module	*mod_upg_to;
	char	isa[ARCH_LENGTH];
#endif

	if ((mod->info.media->med_flags & SVC_TO_BE_REMOVED) &&
	    !(mod->info.media->med_flags & BASIS_OF_UPGRADE)) {
		remove_entire_svc(mod);
		return (1);
	}
#ifdef SERVICE_REMOVAL_SUPPORTED
	mod_upg_to = mod->info.media->med_upg_to;

	if (mod_upg_to == NULL)
		mod_upg_to = mod;
	isa_handled_clear();
	for (ap = mod->sub->info.prod->p_arches; ap; ap = ap->a_next) {
		if (!ap->a_loaded)
			continue;
		extract_isa(ap->a_arch, isa);
		/*
		 *  If this ISA is not selected in med_upg_to media,
		 *  remove the ISA's support entirely from this service.
		 */
		if (!isa_is_selected(mod_upg_to->sub->info.prod, isa)) {
			if (!isa_handled(isa))
				remove_isa_support(mod, isa);
			continue;
		}

		/*
		 *  The ISA is still supported, but is the platform group
		 *  (karch) still supported?  If not, remove it.
		 */

		for (ap2 = mod_upg_to->sub->info.prod->p_arches; ap2;
			    ap2 = ap2->a_next) {
			if (streq(ap->a_arch, ap2->a_arch) &&
			    !ap2->a_selected) {
				remove_platform_support(mod, ap->a_arch);
				break;
			}
		}
	}
#endif
	return (0);
}

static void
remove_entire_svc(Module *mod)
{
	char name[MAXPATHLEN];
	char *cp;

	scriptwrite(fp, LEVEL0, rm_service, "PRODVER",
	    mod->info.media->med_volume, (char *)0);
	action_count++;
	(void) strcpy(name, mod->info.media->med_volume);
	cp = (char *)strchr(name, '_');
	if (cp == NULL)
		return;
	*cp++ = '\0';
	scriptwrite(fp, LEVEL1, rm_inetboot, "SVCPROD", name, "SVCVER", cp,
	    (char *)0);
}

/*
 * gen_mv_svc() - Generate the script commands necessary to move
 *	service.
 *
 * Parameters:
 *	mod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static int
gen_mv_svc(Module *mod, Module *prodmod)
{
	Arch	*ap, *newarchlist;
	Module	*mod_upg_to;
	char	isa[ARCH_LENGTH];
	char *newvolptr, *cp;
	char name[MAXPATHLEN];

	mod_upg_to = mod->info.media->med_upg_to;
	if (!(mod->info.media->med_flags & BASIS_OF_UPGRADE) ||
	    svc_unchanged(mod->info.media) ||
	    !mod_upg_to || mod_upg_to == mod)
		return (0);	/* nothing to move */

	if (has_view(prodmod, mod_upg_to) == SUCCESS)
		(void) load_view(prodmod, mod_upg_to);
	else
		return (0);	/* SHOULDN'T HAPPEN */

	newvolptr = mod->info.media->med_upg_to->info.media->med_volume;
	newarchlist = mod->info.media->med_upg_to->sub->info.prod->p_arches;

	/* move ISA-neutral parts of service */
	scriptwrite(fp, LEVEL0|CONTINUE, mv_whole_svc,
	    "OLD", mod->info.media->med_volume, "NEW", newvolptr, (char *)0);
	/*
	 * remove the old service's entries from the /etc/dfs/dfstab
	 * and add the new ones.
	 */
	scriptwrite(fp, LEVEL0|CONTINUE, rm_svc_dfstab,
	    "NAME", mod->info.media->med_volume, (char *)0);

	isa_handled_clear();
	for (ap = mod->sub->info.prod->p_arches; ap != NULL; ap = ap->a_next) {
		if (fullarch_is_selected(
		    mod->info.media->med_upg_to->sub->info.prod, ap->a_arch)) {
			extract_isa(ap->a_arch, isa);
			if (!isa_handled(isa)) {
				scriptwrite(fp, LEVEL0|CONTINUE, mv_isa_svc,
				    "OLD", mod->info.media->med_volume,
				    "NEW", newvolptr,
				    "PARCH", isa, (char *)0);
				if (strcmp(get_default_inst(), isa) == 0) {
					scriptwrite(fp, LEVEL0|CONTINUE,
						mv_svc_link, "OLD",
						mod->info.media->med_volume,
						"NEW", newvolptr,
						"PARCH", isa, (char *)0);
				}

				scriptwrite(fp, LEVEL0|CONTINUE,
					add_usr_svc_dfstab,
					"NAME", newvolptr,
					"ISA", isa,
					(char *)0);
				/*
				 * This piece of code will update any
				 * Solaris_2.x/usr entries still left in the
				 * dfstab. Things like usr/openwin.
				 */
				scriptwrite(fp, LEVEL0|CONTINUE, sed_dfstab_usr,
					"NEW", newvolptr, "ISA", isa,
					"OLD", mod->info.media->med_volume,
					(char *)0);
			}
			/*
			 * If this is an upgrade from a pre-KIB to post-KBI
			 * service we need to remove the oldkvm service
			 * from the media. If this is an upgrade to a
			 * pre-KBI service the KVM service needs to be moved.
			 */
			if (is_KBI_service(s_newproduct) &&
			    (! is_KBI_service(mod->sub->info.prod))) {
				scriptwrite(fp, LEVEL1, rm_kvm_svc,
				    "OLD", mod->info.media->med_volume,
				    "ARCH", ap->a_arch, (char *)0);
				action_count++;
			} else if (! is_KBI_service(s_newproduct))
				scriptwrite(fp, LEVEL1, mv_kvm_svc,
				    "OLD", mod->info.media->med_volume,
				    "NEW", newvolptr,
				    "ARCH", ap->a_arch, (char *)0);
		}
	}

	scriptwrite(fp, LEVEL0|CONTINUE, move_files_in_contents,
	    "OLD", mod->info.media->med_volume, "NEW", newvolptr,
	    "ROOT", (mod->info.media->med_flags & SPLIT_FROM_SERVER) ? "" :
	    mod_upg_to->sub->info.prod->p_rootdir,  (char *)0);

	/*
	 * find all arches that are new in the new service, set up
	 * links to them, if this is not a KBI service. The KVM links
	 * are not used for post-KBI systems.
	 */
	if (!is_KBI_service(s_newproduct)) {
		for (ap = newarchlist; ap != NULL; ap = ap->a_next)
			if (ap->a_selected && !archinlist(ap->a_arch,
			    mod->sub->info.prod->p_arches))
				scriptwrite(fp, LEVEL1, add_kvm_svc, "NEW",
				    newvolptr, "ARCH", ap->a_arch,
				    (char *)0);
	}

	/* remove the old service's inetboot */
	(void) strcpy(name, mod->info.media->med_volume);
	cp = (char *)strchr(name, '_');
	*cp++ = '\0';
	scriptwrite(fp, LEVEL0|CONTINUE,
	    rm_inetboot, "SVCPROD", name, "SVCVER", cp, (char *)0);

	/* remove the old softinfo file and generate the new one */
	scriptwrite(fp, LEVEL0|CONTINUE, rm_softinfo,
	    "OLD", mod->info.media->med_volume,
	    "SERVICE_PATH", softinfo_services_path(), (char *)0);
	gen_softinfo(mod->sub->info.prod);

	/* If this is a KBI service there is no need to add the kvm */
	/* entries in the dfstab, since they are no longer used. */
	if (!is_KBI_service(s_newproduct))
		for (ap = newarchlist; ap != NULL; ap = ap->a_next)
			if (ap->a_selected)
				scriptwrite(fp, LEVEL0|CONTINUE,
				    add_kvm_svc_dfstab, "NAME", newvolptr,
				    "ARCH", ap->a_arch, (char *)0);
	return (1);
}

/*
 *
 * Parameters:
 *	mod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static void
modify_existing_svc(Module *mod)
{
	Arch *ap, *newarchlist;
	char *newvolptr, *cp;
	char name[MAXPATHLEN];
	char isa[ARCH_LENGTH];

	newvolptr = mod->info.media->med_volume;
	newarchlist = s_newproduct->p_arches;

	add_new_isa_svc(mod);
	/*
	 * find all arches that are new in the new service, set up
	 * links to them, only if this is not a KBI service. For
	 * post-KBI systems these links are meaningless.
	 */
	if (!is_KBI_service(s_newproduct))
		for (ap = newarchlist; ap != NULL; ap = ap->a_next)
			if (ap->a_selected && !archinlist(ap->a_arch,
			    mod->sub->info.prod->p_arches)) {
				if (mod->info.media->med_flags &
				    SPLIT_FROM_SERVER &&
				    strcmp(get_default_arch(), ap->a_arch) == 0)
					scriptwrite(fp, LEVEL0|CONTINUE,
					    link_kvm_svc, "NEW", newvolptr,
					    "ARCH", ap->a_arch, (char *)0);
				else
					scriptwrite(fp, LEVEL0|CONTINUE,
					    add_kvm_svc, "NEW", newvolptr,
					    "ARCH", ap->a_arch, (char *)0);

			}
	/* remove the service's inetboot file and links */
	(void) strcpy(name, mod->info.media->med_volume);
	cp = (char *)strchr(name, '_');
	*cp++ = '\0';

	/* remove the old softinfo file and generate the new one */
	scriptwrite(fp, LEVEL0|CONTINUE, rm_softinfo,
	    "OLD", mod->info.media->med_volume,
	    "SERVICE_PATH", softinfo_services_path(), (char *)0);
	gen_softinfo(mod->sub->info.prod);

	/*
	 * remove the old service's entries from the /etc/dfs/dfstab
	 * and add the new ones.
	 */
	scriptwrite(fp, LEVEL0|CONTINUE, rm_svc_dfstab, "NAME",
	    mod->info.media->med_volume, (char *)0);
	isa_handled_clear();
	for (ap = mod->sub->info.prod->p_arches; ap != NULL; ap = ap->a_next) {
		if (ap->a_selected) {
			extract_isa(ap->a_arch, isa);
			if (!isa_handled(isa)) {
				scriptwrite(fp, LEVEL0|CONTINUE,
					add_usr_svc_dfstab, "NAME", newvolptr,
					"ISA", isa, (char *)0);
			}
		}
	}
	for (ap = newarchlist; ap != NULL; ap = ap->a_next) {
		if (ap->a_selected) {
			extract_isa(ap->a_arch, isa);
			if (!isa_handled(isa)) {
				scriptwrite(fp, LEVEL0|CONTINUE,
					add_usr_svc_dfstab, "NAME", newvolptr,
					"ISA", isa, (char *)0);
			}
		}
	}
	/*
	 * If the service being added is pre-KBI then add the dfstab
	 * entries to the server. Else do nothing since the post-KBI
	 * service do not use the kvm links.
	 */
	if (!is_KBI_service(s_newproduct)) {
		isa_handled_clear();
		for (ap = mod->sub->info.prod->p_arches; ap != NULL;
		    ap = ap->a_next) {
			if (ap->a_selected && !isa_handled(ap->a_arch))
				scriptwrite(fp, LEVEL0|CONTINUE,
				    add_kvm_svc_dfstab, "NAME", newvolptr,
				    "ARCH", ap->a_arch, (char *)0);
		}
		for (ap = newarchlist; ap != NULL; ap = ap->a_next) {
			if (ap->a_selected && !isa_handled(ap->a_arch))
				scriptwrite(fp, LEVEL0|CONTINUE,
				    add_kvm_svc_dfstab, "NAME", newvolptr,
				    "ARCH", ap->a_arch, (char *)0);
		}
	}
}

/*
 * archinlist()
 * Parameters:
 *	arch	 -
 *	archlist -
 * Return:
 *
 * Status:
 *	private
 */
static int
archinlist(char *arch, Arch *archlist)
{
	Arch *ap;

	for (ap = archlist; ap != NULL; ap = ap->a_next)
		if (strcmp(arch, ap->a_arch) == 0)
			return (1);
	return (0);
}

/*
 * gen_add_svc()
 * Parameters:
 *	mod
 * Return:
 * Status:
 *	private
 */
static void
gen_add_svc(Module *mod)
{
	Arch *ap, *newarchlist;
	char *newvolptr;
	char isa[ARCH_LENGTH];

	newvolptr = mod->info.media->med_volume;
	newarchlist = s_newproduct->p_arches;
	if (mod->info.media->med_flags & SPLIT_FROM_SERVER)
		scriptwrite(fp, LEVEL0|CONTINUE, link_varsadm_usr,
		    "SVC", mod->info.media->med_volume, (char *)0);

	else {
		scriptwrite(fp, LEVEL0|CONTINUE, add_varsadm_usr,
		    "SVC", mod->info.media->med_volume,
		    "POST_KBI",
		    is_KBI_service(s_newproduct) ? "postKBI" : "preKBI",
		    (char *)0);
	}

	add_new_isa_svc(mod);

	/*
	 * find all arches that are new in the new service, set up
	 * links to them, only if this is not a KBI service. In a
	 * post-KBI service the kvm entries are not used.
	 */
	if (!is_KBI_service(s_newproduct))
		for (ap = newarchlist; ap != NULL; ap = ap->a_next) {
			if (ap->a_selected) {
				if (mod->info.media->med_flags &
				    SPLIT_FROM_SERVER &&
				    strcmp(get_default_arch(),
					ap->a_arch) == 0)
					scriptwrite(fp, LEVEL0|CONTINUE,
					    link_kvm_svc,
					    "NEW", newvolptr, "ARCH",
					    ap->a_arch, (char *)0);
				else {
					scriptwrite(fp, LEVEL0|CONTINUE,
					    add_kvm_svc, "NEW", newvolptr,
					    "ARCH", ap->a_arch, (char *)0);
				}
			}
		}
	gen_softinfo(mod->sub->info.prod);
	/*
	 * add the service's entries to /etc/dfs/dfstab
	 */
	isa_handled_clear();
	for (ap = newarchlist; ap != NULL; ap = ap->a_next)
		if (ap->a_selected) {
			extract_isa(ap->a_arch, isa);
			if (!isa_handled(isa))
				scriptwrite(fp, LEVEL0|CONTINUE,
					add_usr_svc_dfstab,
					"NAME", newvolptr,
					"ISA", isa,
					(char *)0);
		}
	/*
	 * find all arches that are new in the new service, set up
	 * dfstab entries for them, only if this is not a KBI
	 * service. Again, in the post-KBI services the kvm links are
	 * not use therefore the dfstab entries are not needed.
	 */
	if (!is_KBI_service(s_newproduct))
		for (ap = newarchlist; ap != NULL; ap = ap->a_next)
			if (ap->a_selected) {
				scriptwrite(fp, LEVEL0|CONTINUE,
				    add_kvm_svc_dfstab, "NAME", newvolptr,
				    "ARCH", ap->a_arch, (char *)0);
			}
}

/*
 * save_and_rm()
 * Parameters:
 *	node	-
 * Return:
 * Status:
 *	private
 */
static int
save_and_rm(Node *node, caddr_t val)
{
	Modinfo *mi;
	Module 	*mod;

	mi = (Modinfo *)(node->data);
	/*LINTED [alignment ok]*/
	mod = (Module *)val;
	_save_and_rm(mi, mod);
	while ((mi = next_inst(mi)) != NULL)
		_save_and_rm(mi, mod);
	return (0);
}

/*
 * _save_and_rm()
 * Parameters:
 *	mi	-
 *	mod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static void
_save_and_rm(Modinfo *mi, Module *mod)
{
	struct filediff *diffp;
	char	*cp;
	char	*p;
	char	pathhold[MAXPATHLEN];
	Node	*node;
	Modinfo *tmpmi;

	char	*diff_missing = "DIFF_MISSING";
	char	*diff_type = "DIFF_TYPE";
	char	*diff_slink_target = "DIFF_SLINK_TARGET";
	char	*diff_hlink_target = "DIFF_HLINK_TARGET";
	char	*err;
	char	arg3[MAXPATHLEN], arg4[MAXPATHLEN];
	char	*zonename = mod->sub->info.prod->p_zonename;

	diffp = mi->m_filediff;
	while (diffp != NULL) {
		/*
		 * This huge condition is determining if we should process
		 * the files on the filediff list. This condition is:
		 * if the replacing package is TO_BE_PKGADDED or
		 * if the action is not TO_BE_PRESERVED and
		 * if the contents of the package are not going away
		 * then process the filediff list.
		 */
		if ((diffp->replacing_pkg != NULL &&
		    (diffp->replacing_pkg->m_status == SELECTED ||
			diffp->replacing_pkg->m_status == REQUIRED) &&
		    diffp->replacing_pkg->m_action == TO_BE_PKGADDED) ||
		/* 2nd condition */
		    (mi->m_action != TO_BE_PRESERVED &&
			!(mi->m_flags & CONTENTS_GOING_AWAY))) {
			err = NULL;
			(void) strcpy(arg3, "0");
			(void) strcpy(arg4, "0");

			if (diffp->diff_flags & DIFF_MISSING)
				err = diff_missing;
			else if (diffp->diff_flags & DIFF_TYPE) {
				err = diff_type;
				(void) sprintf(arg3, "%c", diffp->actual_type);
				(void) sprintf(arg4, "%c", diffp->exp_type);

			} else if (diffp->diff_flags & DIFF_SLINK_TARGET) {
				err = diff_slink_target;
				if (diffp->linkptr != NULL) {
					(void) snprintf(arg3, sizeof (arg3),
						"%s", diffp->linkptr);
				}
				if (diffp->link_found != NULL) {
					(void) snprintf(arg4, sizeof (arg4),
						"%s", diffp->link_found);
				}
			} else if (diffp->diff_flags & DIFF_HLINK_TARGET) {
				err = diff_hlink_target;
				if (diffp->linkptr != NULL) {
					(void) snprintf(arg3, sizeof (arg3),
						"%s", diffp->linkptr);
				}
			}

			if (err != NULL) {
				(void) snprintf(pathhold, sizeof (pathhold),
					"%s/%s", mod->sub->info.prod->p_rootdir,
				    diffp->component_path);
				canoninplace(pathhold);
				scriptwrite(fp, LEVEL0|CONTINUE, log_file_diff,
				    "ERR", err, "FILE", pathhold,
				    "ARG3", arg3, "ARG4", arg4,
					"ZONENAME", GZONE(zonename),
				    (char *)0);
			}

			if ((diffp->diff_flags & DIFF_CONTENTS) ||
			    (err == diff_type && diffp->actual_type == 'd')) {
				scriptwrite(fp, LEVEL0|CONTINUE, rename_file,
				    "DIR", mod->sub->info.prod->p_rootdir,
				    "FILE", diffp->component_path,
				    "VER", mod->info.media->med_volume +
				    strlen(s_newproduct->p_name) + 1,
				    "ZONENAME", GZONE(zonename),
				    (char *)0);
			}
		}
		diffp = diffp->diff_next;

	}

	if (mi->m_pkg_hist != NULL && mi->m_pkg_hist->prod_rm_list) {
		cp = mi->m_pkg_hist->prod_rm_list;
		while ((p = split_name(&cp)) != NULL) {
			scriptwrite(fp, LEVEL0, do_prodrm,
			    "UUID", p,
			    "ZONENAME", GZONE(zonename),
			    (char *)0);
		}
	}

	if (mi->m_pkg_hist != NULL && mi->m_shared == NOTDUPLICATE) {
		cp = mi->m_pkg_hist->deleted_files;
		if (cp != NULL) {
			scriptwrite(fp, LEVEL0,
			    ((zonename == NULL) ? start_rmlist :
				    zone_start_rmlist),
				"ZONENAME", GZONE(zonename),
				(char *)0);
			while ((p = split_name(&cp)) != NULL) {
				(void) snprintf(pathhold,
					sizeof (pathhold),
					"/%s/%s", getrealbase(mi), p);
				/* convert path to canonical form */
				canoninplace(pathhold);
				scriptwrite(fp, LEVEL1,
				    ((zonename == NULL) ? addto_rmlist :
				    zone_addto_rmlist),
				    "FILE", pathhold,
				    "ZONENAME", GZONE(zonename),
				    (char *)0);
			}
			scriptwrite(fp, LEVEL0,
			    ((zonename == NULL) ?
			    end_rmlist : zone_end_rmlist),
			    "ZONENAME", GZONE(zonename),
			    (char *)0);
			scriptwrite(fp, LEVEL1, do_removef,
			    "ROOT", mod->sub->info.prod->p_rootdir,
			    "PKG", mi->m_pkginst,
			    "ZONENAME", GZONE(zonename),
			    (char *)0);
			action_count++;
		}
	}

	if (((mi->m_pkg_hist && mi->m_pkg_hist->needs_pkgrm) ||
	    mi->m_flags & DO_PKGRM) &&
	    mi->m_shared == NOTDUPLICATE &&
	    (mi->m_action == TO_BE_REMOVED ||
	    mi->m_action == TO_BE_REPLACED)) {
		tmpmi = mi;
		while ((node = tmpmi->m_next_patch) != NULL) {
			tmpmi = (Modinfo *)(node->data);
			if (zonename == NULL || !streq(ZLIST(mi->m_zonelist),
			    NZLIST)) {
				scriptwrite(fp, LEVEL1, do_pkgrm,
				    "ROOT", mod->sub->info.prod->p_rootdir,
				    "PKG", tmpmi->m_pkginst,
				    "ZONENAME", GZONE(zonename),
				    "ZONELIST", ZLIST(mi->m_zonelist),
				    (char *)0);
				action_count++;
			}
		}
		if (zonename == NULL || !streq(ZLIST(mi->m_zonelist),
		    NZLIST)) {
			scriptwrite(fp, LEVEL1, do_pkgrm,
			    "ROOT", mod->sub->info.prod->p_rootdir,
			    "PKG", mi->m_pkginst,
			    "ZONENAME", GZONE(zonename),
			    "ZONELIST", ZLIST(mi->m_zonelist),
			    (char *)0);
			action_count++;
		}
	}

	/* remove any patch packages */
	if (mi->m_shared == SPOOLED_NOTDUP) {
		if (mi->m_action == TO_BE_REMOVED ||
		    mi->m_action == TO_BE_REPLACED) {
			if (mi->m_instdir != NULL) {
				if (zonename == NULL) {
					scriptwrite(fp, LEVEL1,
					    remove_template,
					    "DIR", mi->m_instdir,
					    (char *)0);
				}
				action_count++;
			}
		} else {
			if (mi->m_action == TO_BE_PRESERVED &&
			    mod->info.media->med_flags & BASIS_OF_UPGRADE &&
			    !(mod->info.media->med_flags &
				    BUILT_FROM_UPGRADE)) {
				if (mi->m_instdir != NULL &&
				    mod->info.media->med_upg_to != NULL &&
					zonename == NULL) {
					scriptwrite(fp, LEVEL0|CONTINUE,
					    move_template,
					    "OLDDIR", mi->m_instdir,
					    "NEWSVC",
			mod->info.media->med_upg_to->info.media->med_volume,
					    "PKG", mi->m_pkgid,
					    "VER", mi->m_version,
					    "ARCH", mi->m_arch, (char *)0);
				}
			}
#ifdef SPOOLED_PATCHES_SUPPORTED
			while ((node = mi->m_next_patch) != NULL) {
				mi = (Modinfo *)(node->data);
				scriptwrite(fp, LEVEL1, do_rm_fr,
				    "DIR", mi->m_instdir,
				    "ZONENAME", GZONE(zonename),
				    (char *)0);
				action_count++;
			}
#endif
		}
	}
	if (((!mi->m_pkg_hist || !mi->m_pkg_hist->needs_pkgrm) &&
	    !(mi->m_flags & DO_PKGRM)) &&
	    mi->m_shared == NOTDUPLICATE &&
	    (mi->m_action == TO_BE_REMOVED || mi->m_action == TO_BE_REPLACED)) {
		tmpmi = mi;
		while ((node = tmpmi->m_next_patch) != NULL) {
			tmpmi = (Modinfo *)(node->data);
			if (zonename == NULL || !streq(ZLIST(mi->m_zonelist),
			    NZLIST)) {
				scriptwrite(fp, LEVEL1, do_pkgrm,
				    "ROOT", mod->sub->info.prod->p_rootdir,
				    "PKG", mi->m_pkginst,
				    "ZONENAME", GZONE(zonename),
				    "ZONELIST", ZLIST(mi->m_zonelist),
				    (char *)0);
				action_count++;
			}
		}
	}
}

/*
 * remove_patches()
 * Parameters:
 *	mi	-
 *	mod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static void
remove_patches(Module *mod)
{
	struct patch	*p;

	for (p = mod->sub->info.prod->p_patches; p != NULL; p = p->next)
		if (p->removed) {
			scriptwrite(fp, LEVEL1, remove_patch,
			    "ROOT", mod->sub->info.prod->p_rootdir,
			    "PATCHID", p->patchid,
			    "ZONENAME",
			    GZONE(mod->sub->info.prod->p_zonename),
			    (char *)0);
			action_count++;
		}
}

/*
 * restore_perm()
 * Parameters:
 *	np	-
 *	data	-
 * Return:
 * Status:
 *	private
 */
static int
restore_perm(Node *np, caddr_t data)
{
	Modinfo *mi;
	Module 	*mod;

	mi = (Modinfo *)(np->data);
	/*LINTED [alignment ok]*/
	mod = (Module *)data;
	_restore_perm(mi, mod);
	while ((mi = next_inst(mi)) != NULL)
		_restore_perm(mi, mod);
	return (0);
}

/*
 * _restore_perm()
 * Parameters:
 * Return:
 *	none
 * Status:
 *	private
 */
static void
_restore_perm(Modinfo *mi, Module *mod)
{
	struct filediff *diffp;
	struct statvfs stvfs;
	char path[PATH_MAX + 1];
	char *rootdir;
	char *zonename = mod->sub->info.prod->p_zonename;

	if (mi->m_action == TO_BE_REPLACED) {
		rootdir = get_rootdir();
		for (diffp = mi->m_filediff; diffp != NULL;
		    diffp = diffp->diff_next) {
			if (!(diffp->diff_flags &
			    (DIFF_PERM | DIFF_UID | DIFF_GID)))
				continue;
			if (!start_perm_printed) {
				scriptwrite(fp, LEVEL0|CONTINUE,
				    start_perm_restores, (char *)0);
				start_perm_printed = 1;
			}
			if (diffp->diff_flags & DIFF_PERM) {
				scriptwrite(fp, LEVEL1|CONTINUE,
				    chmod_file,
				    "DIR",
				    mod->sub->info.prod->p_rootdir,
				    "FILE", diffp->component_path,
				    "MODE", cvtperm(diffp->act_mode),
				    "ZONENAME", GZONE(zonename),
				    (char *)0);
			}

			/* Group and owner IDs don't make sense on PCFS. */
			(void) snprintf(path, sizeof (path), "%s/%s%s",
			    rootdir ? rootdir : "",
			    mod->sub->info.prod->p_rootdir,
			    diffp->component_path);
			if (statvfs(path, &stvfs) == 0 &&
			    streq(stvfs.f_basetype, "pcfs"))
				continue;

			if (diffp->diff_flags & DIFF_UID) {
				scriptwrite(fp, LEVEL1|CONTINUE,
					chown_file,
				    "DIR",
				    mod->sub->info.prod->p_rootdir,
				    "FILE", diffp->component_path,
				    "OWNER", cvtuid(diffp->act_uid),
				    "ZONENAME", GZONE(zonename),
				    (char *)0);
			}
			if (diffp->diff_flags & DIFF_GID) {
				scriptwrite(fp, LEVEL1|CONTINUE,
				    chgrp_file,
				    "DIR",
				    mod->sub->info.prod->p_rootdir,
				    "FILE", diffp->component_path,
				    "GROUP", cvtgid(diffp->act_gid),
				    "ZONENAME", GZONE(zonename),
				    (char *)0);
			}
		}
	}
}

/*
 * walk_pkgrm_f()
 * Parameters:
 *	node	-
 *	val	-
 * Return:
 * Status:
 *	private
 */
static int
walk_pkgrm_f(Node *node, caddr_t val)
{
	Modinfo *mi;
	Module 	*mod;

	mi = (Modinfo *)(node->data);
	/*LINTED [alignment ok]*/
	mod = (Module *)val;
	_walk_pkgrm_f(mi, mod);
	while ((mi = next_inst(mi)) != NULL)
		_walk_pkgrm_f(mi, mod);
	return (0);
}

/*
 * _walk_pkgrm_f()
 * Parameters:
 *	mi	-
 *	mod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static void
_walk_pkgrm_f(Modinfo *mi, Module *mod)
{
	char *zonename = mod->sub->info.prod->p_zonename;

	if (mi->m_pkg_hist && !mi->m_pkg_hist->needs_pkgrm &&
	    !(mi->m_flags & DO_PKGRM) &&
	    mi->m_shared == NOTDUPLICATE &&
	    mi->m_action == TO_BE_REMOVED) {
		if (zonename == NULL || !streq(ZLIST(mi->m_zonelist), NZLIST)) {
			scriptwrite(fp, LEVEL1, do_pkgrm_f,
			    "ROOT", mod->sub->info.prod->p_rootdir,
			    "PKG", mi->m_pkginst,
			    "ZONENAME", GZONE(zonename),
			    "ZONELIST", ZLIST(mi->m_zonelist),
			    (char *)0);
		}
		action_count++;
	}
}

/*
 * walk_root_kvm_softinfo()
 * Parameters:
 *	np	-
 *	data	-
 * Return:
 *	0
 * Status:
 *	private
 */
static int
walk_root_kvm_softinfo(Node * np, caddr_t data)
{
	Modinfo *mi;
	Product	*prod;

	mi = (Modinfo *)(np->data);
	/*LINTED [alignment ok]*/
	prod = (Product *)data;
	_walk_root_kvm_softinfo(mi, prod);
	while ((mi = next_inst(mi)) != NULL)
		_walk_root_kvm_softinfo(mi, prod);
	return (0);
}

static void
_walk_root_kvm_softinfo(Modinfo * mi, Product *prod)
{
	struct softinfo_merge_entry *matchsmep, *smep;

	if (mi->m_shared == NULLPKG ||
	    (mi->m_sunw_ptype != PTYPE_KVM && mi->m_sunw_ptype != PTYPE_ROOT))
		return;

	/*
	 *  See if this package is on the already-printed part of the
	 *  softinfo_merge_chain
	 */
	matchsmep = softinfo_merge_chain;
	for (matchsmep = softinfo_merge_chain; matchsmep != smep_cur;
	    matchsmep = matchsmep->next) {
		if (matchsmep->new_mi == mi)
			return; /* already handled */
	}
	for (matchsmep = smep_cur; matchsmep != NULL;
	    matchsmep = matchsmep->next) {
		if (matchsmep->new_mi == mi) {
			smep = smep_cur;
			while (smep != matchsmep) {
				_gen_root_kvm_softinfo(smep->cur_mi, prod);
				smep = smep->next;
			}
			_gen_root_kvm_softinfo(smep->cur_mi, prod);
			smep_cur = smep->next;
			return;
		}
	}

	if (mi->m_status == UNSELECTED ||
	    (mi->m_sunw_ptype == PTYPE_ROOT &&
		mi->m_shared != SPOOLED_NOTDUP &&
		mi->m_shared != SPOOLED_DUP &&
		mi->m_action != TO_BE_SPOOLED))
		return;

	_gen_root_kvm_softinfo(mi, prod);
}

/*
 * _gen_root_kvm_softinfo()
 * Parameters:
 *	mi	-
 *	prod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static void
_gen_root_kvm_softinfo(Modinfo *mi, Product *prod)
{
	char rootsize[10];
	char fullarch[20];
	char mapname[MAXPATHLEN + 1];

	/*
	 * For post-KBI systems there is no reason to add the KVM_ROOT
	 * information into the softinfo file. This is due to the fact
	 * that post-KBI systems handle KBI packages just like and
	 * other USR package.
	 */
	if (mi->m_sunw_ptype == PTYPE_KVM && ! is_KBI_service(s_newproduct)) {
		scriptwrite(fp, LEVEL0|CONTINUE, kvm_softinfo,
		    "ARCH", mi->m_arch,
		    "OS", prod->p_name, "VER", prod->p_version, (char *)0);
	} else if (mi->m_sunw_ptype == PTYPE_ROOT) {
		if (mi->m_deflt_fs[ROOT_FS] + mi->m_deflt_fs[VAR_FS] == 0) {
			(void) snprintf(mapname, sizeof (mapname),
				"%s%s/%s/pkgmap", get_rootdir(),
			    mi->m_instdir, mi->m_pkg_dir);
			calc_pkg_space(mapname, mi, prod);
		}
		(void) snprintf(rootsize, sizeof (rootsize), "%ld",
		    mi->m_deflt_fs[ROOT_FS] + mi->m_deflt_fs[VAR_FS]);
		if (strrchr(mi->m_arch, '.') == 0)
			(void) snprintf(fullarch, sizeof (fullarch),
				"%s.all", mi->m_arch);
		else
			(void) strcpy(fullarch, mi->m_arch);
		if (mi->m_instdir)
			scriptwrite(fp, LEVEL0|CONTINUE, root_softinfo,
			    "ARCH", fullarch,
			    "PATH", mi->m_instdir,
			    "PKG", mi->m_pkgid,
			    "SIZE", rootsize,
			    "VERSION", mi->m_version, (char *)0);
	}
}

/*
 * gen_softinfo_merge_chain()
 * Parameters:
 *	np	-
 *	data	-
 * Return:
 *	0
 * Status:
 *	private
 */
static int
gen_softinfo_merge_chain(Node * np, caddr_t data)
{
	Modinfo *mi, *j;
	Product	*prod;

	mi = (Modinfo *)(np->data);
	/*LINTED [alignment ok]*/
	prod = (Product *)data;

	/* Check the header and it patches to see if they need to be */
	/* added. */
	if (mi->m_status != UNSELECTED) {
		_gen_softinfo_merge_chain(mi, prod);
		for (j = next_patch(mi); j != NULL; j = next_patch(j))
			_gen_softinfo_merge_chain(j, prod);
	}

	/*
	 * Step throuch the instances and the patches to see if they
	 * need to be added to the chain.
	 * The check of unselected is because the patches are not
	 * selected, but the rule is that if a package is selected that
	 * implicitly its patches are selected.
	 */
	while ((mi = next_inst(mi)) != NULL)
		if (mi->m_status != UNSELECTED)
			for (j = mi; j != NULL; j = next_patch(j))
				_gen_softinfo_merge_chain(j, prod);

	return (0);
}

/*
 * _gen_softinfo_merge_chain()
 * Parameters:
 *	mi	-
 *	prod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static void
_gen_softinfo_merge_chain(Modinfo *mi, Product *prod)
{
	Arch_match_type		match;
	Modinfo			*mnew;
	struct softinfo_merge_entry	*sme, **smepp;

	/* If the package is null or not to be preserved then do */
	/* nothing */
	if (mi->m_shared == NULLPKG || mi->m_action == TO_BE_REMOVED ||
	    (mi->m_status == LOADED && mi->m_action != TO_BE_PRESERVED))
		return;
	if (mi->m_sunw_ptype == PTYPE_ROOT &&
	    mi->m_shared != SPOOLED_NOTDUP &&
	    mi->m_shared != SPOOLED_DUP &&
	    mi->m_action != TO_BE_SPOOLED)
		return;
	if (mi->m_sunw_ptype == PTYPE_KVM || mi->m_sunw_ptype == PTYPE_ROOT) {
		mnew = find_new_package(prod, mi->m_pkgid, mi->m_arch, &match);
		if (mnew == NULL &&
		    (mi->m_action == TO_BE_REPLACED ||
		    mi->m_action == TO_BE_REMOVED))
			return;
		sme = (struct softinfo_merge_entry *)xmalloc((size_t)
		    sizeof (struct softinfo_merge_entry));
		/*
		 *  Note that mnew might be NULL.  That's OK.  The
		 *  code that reads this list will expect that.
		 */
		sme->new_mi = mnew;
		sme->cur_mi = mi;
		for (smepp = &softinfo_merge_chain;
		    *smepp != NULL; smepp = &((*smepp)->next));
		*smepp = sme;
		sme->next = NULL;
	}
}

/*
 * getrealbase()
 * Parameters:
 *	mi	-
 * Return:
 * Status:
 *	private
 */
static char *
getrealbase(Modinfo * mi)
{
	if (mi->m_instdir != NULL)
		return (mi->m_instdir);
	else
		return (mi->m_basedir);
}

/*
 * pkgadd_or_spool()
 * Parameters:
 *	np	-
 *	data	-
 * Return:
 *	0
 * Status:
 *	private
 */
static int
pkgadd_or_spool(Node * np, caddr_t data)
{
	Modinfo *mi;
	Module 	*mod;

	mi = (Modinfo *)(np->data);
	/*LINTED [alignment ok]*/
	mod = (Module *)data;
	_pkgadd_or_spool(mi, mod);
	while ((mi = next_inst(mi)) != NULL)
		_pkgadd_or_spool(mi, mod);
	return (0);
}

/*
 * _pkgadd_or_spool()
 * Parameters:
 *	mi	-
 *	mod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static void
_pkgadd_or_spool(Modinfo * mi, Module * mod)
{
	char *adminfile;
	char pkginst[MAXPKGNAME_LENGTH];
	char *realbase;
	char *zonename = mod->sub->info.prod->p_zonename;

	if (!mi->m_pkgadd_processed &&
	    ((mi->m_status == SELECTED || mi->m_status == REQUIRED) &&
	    (mi->m_action == TO_BE_SPOOLED ||
	    mi->m_action == TO_BE_PKGADDED))) {
		(void) strcpy(pkginst, mi->m_pkg_dir);
		realbase = getrealbase(mi);
		if (mi->m_action == TO_BE_PKGADDED) {
			adminfile = NULL;
			if (mi->m_flags & INSTANCE_ALREADY_PRESENT) {
				if (strcmp(realbase, "/") == 0)
					adminfile = "root";
				else if (strcmp(realbase, "/usr") == 0)
					adminfile = "usr";
				else if (strcmp(realbase, "/opt") == 0)
					adminfile = "opt";
				else
					adminfile = newadmin(realbase,
					    OVERWRITE);
			} else {
				if (strcmp(realbase, "/") == 0)
					adminfile = "un.root";
				else if (strcmp(realbase, "/usr") == 0)
					adminfile = "un.usr";
				else if (strcmp(realbase, "/opt") == 0)
					adminfile = "un.opt";
				else
					adminfile = newadmin(realbase,
					    UNIQUE);
			}
			if (mi->m_flags & IS_VIRTUAL_PKG) {
				scriptwrite(fp, LEVEL1, do_virtual_pkgadd,
				    "ROOT", mod->sub->info.prod->p_rootdir,
				    "PKG", mi->m_pkgid,
				    "ARCH", mi->m_arch,
				    "PKGDIR", pkginst,
				    "SPOOL", s_newproduct->p_pkgdir,
				    "ADMIN", adminfile,
				    "VIRTUAL_PKG_LOG", pkgs_tobeadded_path(),
				    "ZONENAME", GZONE(zonename),
				    "ZONELIST", ZLIST(mi->m_zonelist),
				    (char *)0);
			} else {
				if (!copyright_printed) {
					scriptwrite(fp, LEVEL0, print_copyright,
					    "SPOOL", s_newproduct->p_pkgdir,
					    "PKG", pkginst, (char *)0);
					copyright_printed = 1;
				}
				scriptwrite(fp, LEVEL1,
				    do_local_pkgadd, "ROOT",
					mod->sub->info.prod->p_rootdir,
				    "SPOOL", s_newproduct->p_pkgdir,
				    "ADMIN", adminfile,
				    "PKG", pkginst,
				    "ZONENAME", GZONE(zonename),
				    "ZONELIST", ZLIST(mi->m_zonelist),
				    (char *)0);
			}
			mi->m_pkgadd_processed = B_TRUE;
			action_count++;
		} else if (mi->m_action == TO_BE_SPOOLED) {
			if (mi->m_flags & IS_VIRTUAL_PKG) {
				scriptwrite(fp, LEVEL1, spool_virtual_pkg,
				    "SPOOLDIR", mi->m_instdir,
				    "PKG", mi->m_pkgid,
				    "ARCH", mi->m_arch,
				    "PKGDIR", pkginst,
				    "MEDIA", s_newproduct->p_pkgdir,
				    "VIRTUAL_PKG_LOG", pkgs_tobeadded_path(),
				    "ZONELIST", ZLIST(mi->m_zonelist),
				    (char *)0);
			} else {
				scriptwrite(fp, LEVEL1,
				    spool_local_pkg,
				    "SPOOLDIR", mi->m_instdir,
				    "PKG", pkginst,
				    "MEDIA", s_newproduct->p_pkgdir,
				    "ZONENAME", GZONE(zonename),
				    "ZONELIST", ZLIST(mi->m_zonelist),
				    (char *)0);
			}
			mi->m_pkgadd_processed = B_TRUE;
			action_count++;
		}
	}
}

/*
 * Description: Construct the  zonelist portion of "-O=<zonelist...>"
 *              for any package that needs it based on a comparison
 *              of pkgabbrev's in the global and non-global zones.
 *              Attach it to the correct module for later detection
 *              by the functions that create the upgrade_script.
 *
 * Parameters:
 *	modzlist - An array of global and non-global Modules.
 *	newmod   - The Module representing the media.
 * Return:
 *	none
 * Status:
 *	private
 */

static void
determine_zonelist(Module **modzlist, Module *newmod)
{
	int i;
	Modinfo *mm, *gm, *zm;
	Node *n, *n2, *n3;
	List *zl, *gl, *ml;

	/* Load the view of the global module */
	(void) load_view(newmod, modzlist[0]);

	/* Global zone list of packages */
	gl = modzlist[0]->sub->info.prod->p_packages;

	/* List of packages on the media */
	ml = newmod->info.prod->p_packages;

	assert(gl != NULL);
	assert(ml != NULL);

	/*
	 * Find the package entry in the media module and modify it with a
	 * string representing the "-O zonelist=zonename" option/argument
	 * if packages don't exist in all zones. This string is only used
	 * for pkgadd script generation.
	 */

	/* For all the packages in the media module */

	for (n3 = ml->list->next; n3 != NULL && n3 != ml->list;
	    n3 = n3->next) {

		mm = (Modinfo *) n3->data;

		if (mm == NULL) {
			continue;
		}

		/* For all packages in the global zone */
		for (n = gl->list->next; n != NULL && n != gl->list;
		    n = n->next) {
			gm = (Modinfo *) n->data;

			if (gm == NULL ||
			    gm->m_found_in_zone ||
			    gm->m_shared == NULLPKG ||
			    mm->m_pkgid == NULL ||
			    gm->m_pkgid == NULL ||
			    !streq(mm->m_pkgid, gm->m_pkgid)) {
				continue;
			}

			/* Found in the global zone */
			add_to_zonelist(GLOBALZONE);
			gm->m_found_in_zone = B_TRUE;
			break;
		}

		/* For all non-global zones */
		for (i = 1; modzlist[i] != NULL; i++) {

			zl = modzlist[i]->sub->info.prod->p_packages;

			assert(zl != NULL);

			/* For all packages in a non global zone */
			for (n2 = zl->list->next; n2 != NULL && n2 != zl->list;
				n2 = n2->next) {
				zm = (Modinfo *) n2->data;

				/*
				 * pkgid could be NULL if a
				 * var/sadm/pkg/PKG/pkginfo
				 * file doesn't exist but the
				 * package exists in the media.
				 */

				if (zm == NULL ||
				    zm->m_shared == NULLPKG ||
				    mm->m_pkgid == NULL ||
				    zm->m_pkgid == NULL ||
				    !streq(mm->m_pkgid, zm->m_pkgid)) {
					continue;
				}

				/* Found in a non-global zone */
				add_to_zonelist(
				    modzlist[i]->sub->info.prod->p_zonename);
				break;
			}
		}


		/*
		 * If package is found on the media generate the
		 * list of zones to install. If a package exists
		 * in a zone and not on the media then skip it
		 * since it won't be installed anyway.
		 */

		generate_zonelist(mm, i);
	}

	/*
	 * Now we search for the packages on a global zone that
	 * were not found in the media. A pkg may exist in the global
	 * and not a non-global zone. These second set of loops are
	 * done for pkgrm script generation.
	 */

	/* For all packages in the global zone */
	for (n = gl->list->next; n != NULL && n != gl->list; n = n->next) {

		gm = (Modinfo *) n->data;

		if (gm == NULL || gm->m_shared == NULLPKG) {
			continue;
		}

		add_to_zonelist(GLOBALZONE);

		for (i = 1; modzlist[i] != NULL; i++) {

			zl = modzlist[i]->sub->info.prod->p_packages;

			assert(zl != NULL);

			/* for all packages in a non global zones */
			for (n2 = zl->list->next; n2 != NULL && n2 != zl->list;
				n2 = n2->next) {
				zm = (Modinfo *) n2->data;

				if (zm == NULL ||
				    zm->m_shared == NULLPKG ||
				    zm->m_found_in_zone ||
				    gm->m_pkginst == NULL ||
				    zm->m_pkginst == NULL ||
				    !streq(gm->m_pkginst, zm->m_pkginst)) {
					continue;
				}

				/* Found in a non-global zone */
				add_to_zonelist(
				    modzlist[i]->sub->info.prod->p_zonename);

				zm->m_found_in_zone = B_TRUE;
				break;
			}
		}

		generate_zonelist(gm, i);
	}

	/*
	 * Now we search for the packages on a non-global zone that
	 * were not found in the global zone.
	 */

	for (i = 1; modzlist[i] != NULL; i++) {

		(void) load_view(newmod, modzlist[i]);

		zl = modzlist[i]->sub->info.prod->p_packages;

		assert(zl != NULL);

		/* for all packages in a non global zones */
		for (n = zl->list->next; n != NULL && n != zl->list;
			n = n->next) {
			zm = (Modinfo *) n->data;

			if (zm == NULL ||
			    zm->m_found_in_zone ||
			    zm->m_shared == NULLPKG ||
			    zm->m_pkginst == NULL) {
				continue;
			}

			/* Found in a non-global zone */
			if (modzlist[i]->sub->info.prod->p_zonename != NULL) {
				zm->m_zonelist = xstrdup(
				    modzlist[i]->sub->info.prod->p_zonename);
			}
		}
	}
}

/*
 * Description: Add zonename's to an array of zonenames for each
 *              package entry in a module representing a media,
 *              global or non-global Module.
 *
 * Parameters:
 *	zonename   - The name of the zone.
 *
 * Return:
 *	none
 *
 * Status:
 *	private
 *
 * Side effects:
 *	Sets global variable num_pkgs_in_zone and g_zonelist.
 */

static void
add_to_zonelist(char *zonename)
{
	assert(zonename != NULL);

	if (g_zonelist == NULL) {
		g_zonelist = (char **)xcalloc(sizeof (char **));
	} else {
		g_zonelist = (char **)realloc(g_zonelist,
		    sizeof (char **)*(num_pkgs_in_zone+1));
	}

	g_zonelist[num_pkgs_in_zone++] = xstrdup(zonename);
}

/*
 * Description: Construct the final <zonelist> string. If a package
 *              is in the global and all the non-global zones then
 *              no <zonelist> string is constructed.
 *
 * Parameters:
 *	mod              - The Module representing the media.
 *	tot_num_of_zones - The total number of zones on the system including
 *                     the global zone.
 * Return:
 *	none
 *
 * Status:
 *	private
 *
 * Side effects:
 *	Free's memory for global variable g_zonelist and initializes
 *  num_pkgs_in_zone to 0.
 */

static void
generate_zonelist(Modinfo *mod, int tot_num_of_zones)
{
	int i = 0;
	int zonelist_len = 0;
	char *zonelist = NULL;

	mod->m_zonelist = NULL;

	/* It's a new package and should be applied to all zones */
	if (num_pkgs_in_zone == 0) {
		return;
	}

	if (num_pkgs_in_zone < tot_num_of_zones) {
		for (i = 0; i < num_pkgs_in_zone; i++) {
			assert(g_zonelist[i] != NULL);
			zonelist_len = strlen(g_zonelist[i]) + 1 + zonelist_len;
		}

		zonelist = xmalloc(zonelist_len);

		(void) strlcpy(zonelist, g_zonelist[0], zonelist_len);

		for (i = 1; i < num_pkgs_in_zone; i++) {
			(void) strlcat(zonelist, " ", zonelist_len);
			(void) strlcat(zonelist, g_zonelist[i], zonelist_len);
			free(g_zonelist[i]);
		}

		mod->m_zonelist = xstrdup(zonelist);
		free(zonelist);
	}

	free(g_zonelist);
	g_zonelist = NULL;
	num_pkgs_in_zone = 0;
}

/*
 * newadmin()
 * Parameters:
 *	basedir	 -
 *	instance -
 * Return:
 * Status:
 *	private
 */
static char *
newadmin(char *basedir, enum instance_type instance)
{
	struct admin_file *afp;

	for (afp = adminfile_head; afp != NULL; afp = afp->next) {
		if (strcmp(basedir, afp->basedir) == 0 &&
		    afp->inst_type == instance)
			return (afp->admin_name);
	}
	afp = (struct admin_file *) xmalloc(sizeof (struct admin_file) +
					strlen(basedir));
	(void) strcpy(afp->basedir, basedir);
	(void) sprintf(afp->admin_name, "%d", admin_seq++);
	afp->inst_type = instance;
	afp->next = adminfile_head;
	adminfile_head = afp;
	scriptwrite(fp, LEVEL0|CONTINUE, build_admin_file,
			"NAME", afp->admin_name,
			"INSTANCE",
			(instance == UNIQUE ? "unique" : "overwrite"),
			"BASEDIR",
			basedir,
			(char *)0);
	return (afp->admin_name);
}

/*
 * gen_share_commands()
 * Parameters:
 *	mod	-
 * Return:
 *	none
 * Status:
 *	private
 */
static void
gen_share_commands(Module *mod)
{
	Arch *ap, *newarchlist;
	char *newvolptr;
	char	isa[ARCH_LENGTH];

	newvolptr = mod->info.media->med_volume;
	newarchlist = s_newproduct->p_arches;
	/*
	 * share the service
	 */
	isa_handled_clear();
	for (ap = newarchlist; ap != NULL; ap = ap->a_next)
		if (ap->a_selected) {
			extract_isa(ap->a_arch, isa);
			if (!isa_handled(isa))
				scriptwrite(fp, LEVEL0|CONTINUE,
				    share_usr_svc_dfstab,
				    "NAME", newvolptr, "PARCH", isa,
				    (char *)0);
		}
	/*
	 * find all arches that are new in the new service
	 * and share them, only if this is not a KBI
	 * service. In post-KBI services there is no need since the
	 * KVM entries are not used.
	 */
	if (!is_KBI_service(s_newproduct))
		for (ap = newarchlist; ap != NULL; ap = ap->a_next)
			if (ap->a_selected) {
				scriptwrite(fp, LEVEL0|CONTINUE,
				    share_kvm_svc_dfstab,
				    "NAME", newvolptr,
				    "ARCH", ap->a_arch, (char *)0);
			}

}

/*
 * cvtperm()
 * Parameters:
 *	mode
 * Return:
 * Status:
 *	private
 */
static char *
cvtperm(mode_t mode)
{
	(void) sprintf(ascii_number, "%lo", mode);
	return (ascii_number);
}

/*
 * cvtuid()
 * Parameters:
 *	uid	-
 * Return:
 * Status:
 *	private
 */
static char *
cvtuid(uid_t uid)
{
	(void) sprintf(ascii_number, "%ld", uid);
	return (ascii_number);
}

/*
 * cvtgid()
 * Parameters:
 *	gid	-
 * Return:
 * Status:
 *	private
 */
static char *
cvtgid(gid_t gid)
{
	(void) sprintf(ascii_number, "%ld", gid);
	return (ascii_number);
}

static void
add_new_isa_svc(Module *mod)
{
	Arch *ap, *newarchlist;
	char isa[ARCH_LENGTH];

	newarchlist = s_newproduct->p_arches;

	/*
	 * find all ISA's that are new in the new service, set up
	 * links to them.
	 */
	isa_handled_clear();
	for (ap = newarchlist; ap != NULL; ap = ap->a_next) {
		if (ap->a_selected) {
			extract_isa(ap->a_arch, isa);
			if (!isa_is_loaded(mod->sub->info.prod, isa) &&
			    !isa_handled(isa)) {
				if (mod->info.media->med_flags &
				    SPLIT_FROM_SERVER &&
				    strcmp(get_default_inst(), isa) == 0)
					scriptwrite(fp, LEVEL0|CONTINUE,
					    link_usr_svc,
					    "SVC", mod->info.media->med_volume,
					    "ISA", isa, (char *)0);
				else
					scriptwrite(fp, LEVEL0|CONTINUE,
					    add_usr_svc,
					    "SVC", mod->info.media->med_volume,
					    "ISA", isa, (char *)0);
			}
		}
	}
}

/*
 * get_product()
 *
 */
static Product *
get_product(void)
{
	Product		*theProd;

	if (s_localmedia->info.media->med_flags & BASIS_OF_UPGRADE)
		theProd = s_newproduct;
	else
		theProd = s_localmedia->sub->info.prod;
	return (theProd);
}

/*
 * build_upgrade_debug_callback()
 *	Look for the debugging callback script.  If it exists, return
 *	the shell script code that'll call the callback.  If it doesn't
 *	exist, return a comment.  Whatever is returned is returned via
 *	a static buffer.
 */
static char *
build_upgrade_debug_callback(void)
{
	static char cmdbuf[1024];
	struct stat statbuf;

	if (stat(DEBUG_CALLBACK_FILE, &statbuf) < 0) {
		/* Any error, we assume it's not there */
		return ("# Callback not found");
	} else {
		(void) snprintf(cmdbuf, 1024, "sh %s", DEBUG_CALLBACK_FILE);
		return (cmdbuf);
	}
}


/*
 * *_path()
 *	These functions will return the path to a specific file based on
 *	the product passed in as the argument. The path generation is based
 *	on the pre or port_KBI status of the product. Pre-KBI uses the old
 *	var/sadm directory tree and the post-KBI systems use the new
 *	var/sadm structure.
 *
 * Parameters:
 *	prod	- the product to be used in the comparson
 * Return:
 *	a file path to the given file
 * Status:
 *	private
 */

char *
upgrade_script_path(Product *prod)
{
	if (is_KBI_service(prod))
		return ("/var/sadm/system/admin/upgrade_script");
	else
		return ("/var/sadm/install_data/upgrade_script");
}
char *
upgrade_log_path(Product *prod)
{
	if (is_KBI_service(prod))
		return ("/var/sadm/system/logs/upgrade_log");
	else
		return ("/var/sadm/install_data/upgrade_log");
}
static char *
upgrade_restart_path(void)
{
	if (is_KBI_service(get_product()))
		return ("/var/sadm/system/admin/upgrade_restart");
	else
		return ("/var/sadm/install_data/upgrade_restart");
}
static char *
upgrade_cleanup_path(void)
{
	if (is_KBI_service(get_product()))
		return ("/var/sadm/system/data/upgrade_cleanup");
	else
		return ("/var/sadm/install_data/upgrade_cleanup");
}
static char *
upgrade_failedpkgs_path(void)
{
	if (is_KBI_service(get_product()))
		return ("/var/sadm/system/data/upgrade_failed_pkgadds");
	else
		return ("/var/sadm/install_data/upgrade_failed_pkgadds");
}

static char *
locales_installed_path(void)
{
	if (is_KBI_service(get_product()))
		return ("/var/sadm/system/data/locales_installed");
	else
		return ("/var/sadm/install_data/locales_installed");
}

static char *
inst_release_path(Product *prod)
{
	if (is_KBI_service(prod))
		return ("/var/sadm/system/admin/INST_RELEASE");
	else
		return ("/var/sadm/softinfo/INST_RELEASE");
}
static char *
softinfo_services_path(void)
{
	if (is_KBI_service(get_product()))
		return ("/var/sadm/system/admin/services/");
	else
		return ("/var/sadm/softinfo/");
}

static char *
pkgs_tobeadded_path(void)
{
	if (is_KBI_service(get_product()))
		return ("/var/sadm/system/data/packages_to_be_added");
	else
		return ("/var/sadm/install_data/packages_to_be_added");
}

static char *
cluster_path(Product *prod)
{
	if (is_KBI_service(prod))
		return ("/var/sadm/system/admin/CLUSTER");
	else
		return ("/var/sadm/install_data/CLUSTER");
}
static char *
clustertoc_path(Product *prod)
{
	if (is_KBI_service(prod))
		return ("/var/sadm/system/admin/.clustertoc");
	else
		return ("/var/sadm/install_data/.clustertoc");
}

/*
 * gen_locales_installed()
 *	Create the upgrade_script stanza that will create the locales_installed
 *	file on this system.  This file will contain a list of the selected
 *	locales.
 *
 * Parameters:
 *	fp	- FILE pointer for writing the upgrade_script stanza
 *	prod	- The product being installed
 *	root	- The path to the root of the filesystem being upgraded
 *	path	- The name of the file to be created
 *
 * Return:
 *	none
 * Status:
 *	public
 */
static void
gen_locales_installed(FILE *fp, Product *prod, char *root)
{
	Module *loc, *geo;
	char *locstr, *geostr;
	int size;

	/*
	 * Build the string for the LOCALES= line.  The string will look like
	 * this:   LOCALES=fr,de,es
	 */
	locstr = xstrdup("");
	for (loc = prod->p_locale; loc; loc = loc->next) {
		if (loc->info.locale->l_selected) {
			/* Resize the string to take the new locale */
			size = strlen(loc->info.locale->l_locale) + 1 +
			    strlen(locstr) + 1;
			locstr = (char *)xrealloc(locstr, size);
			/* Add the new locale (and a comma if necessary) */
			if (locstr[0]) {
				(void) strcat(locstr, ",");
			}
			(void) strcat(locstr, loc->info.locale->l_locale);
		}
	}

	/*
	 * Build the string for the GEOS= line.  The string will look like
	 * this:  GEOS=W_Europe,E_Europe
	 */
	geostr = xstrdup("");
	for (geo = prod->p_geo; geo; geo = geo->next) {
		if (geo->info.geo->g_selected) {
			/* Resize the string to take the new geo */
			size = strlen(geo->info.geo->g_geo) + 1 +
				strlen(geostr) + 1;
			geostr = (char *)xrealloc(geostr, size);
			/* Add the new geo (and a comma if necessary */
			if (geostr[0]) {
				(void) strcat(geostr, ",");
			}
			(void) strcat(geostr, geo->info.geo->g_geo);
		}
	}

	if (prod->p_zonename == NULL) {
		scriptwrite(fp, LEVEL0|CONTINUE, echo_locales_installed,
		    "LOCALES_INSTALLED_PATH", locales_installed_path(),
		    "GEOS", geostr,
		    "LOCALES", locstr,
		    "ROOT", root, (char *)0);
	}
	free(locstr);
}

static void
gen_softinfo_locales(FILE *fp, Product *prod1, Product *prod2)
{
	isa_handled_clear();
	merge_softinfo_locales(fp, prod1);
	merge_softinfo_locales(fp, prod2);
}

static void
merge_softinfo_locales(FILE *fp, Product *prod)
{
	char	buf[MAXPATHLEN + 1];
	Module	*modp, *subp;

	for (modp = prod->p_locale; modp; modp = modp->next) {
		if (!modp->info.locale->l_selected)
			continue;
		for (subp = modp->sub; subp; subp = subp->next) {
			if (subp->info.mod->m_arch) {
				(void) snprintf(buf, sizeof (buf),
				    "%s:%s",
				    subp->info.mod->m_arch,
				    modp->info.locale->l_locale);
				if (!isa_handled(buf))
					scriptwrite(fp, LEVEL0|CONTINUE,
					    locale_softinfo,
					    "ARCH", subp->info.mod->m_arch,
					    "LOC", modp->info.locale->l_locale,
					    (char *)0);
			}
		}
	}
}

/*
 * upgrade_clients_inetboot()
 * Parameters:
 *	mod	- the media of the client
 * Return:
 *	none
 * Status:
 *	private
 */
static void
upgrade_clients_inetboot(Module * mod, Product *newproduct)
{
	Arch *ap;
	char arch_buf[30], *karch;
	char bootd[10];
	StringList	*host;
	char		*ip_addr;
	char		searchFile[MAXPATHLEN];
	ulong		addr;

	for (ap = mod->sub->info.prod->p_arches; ap != NULL;
	    ap = ap->a_next) {
		if (!(ap->a_selected))
			continue;
		(void) strcpy(arch_buf, ap->a_arch);
	}

	karch = (char *)strchr(arch_buf, '.');
	*karch++ = '\0';
	if (karch == NULL)
		return;

	if (strcmp(arch_buf, "i386") == 0)
		(void) strcpy(bootd, "rplboot");
	else
		(void) strcpy(bootd, "tftpboot");

	for (host = mod->info.media->med_hostname; host != NULL;
	    host = host->next) {
		if ((ip_addr = name2ipaddr(host->string_ptr)) == NULL)
			continue;

		if (strcmp(arch_buf, "i386") == 0) {
			(void) snprintf(searchFile, sizeof (searchFile),
				"%s.inetboot", ip_addr);
		} else {
			addr = inet_addr(ip_addr);
			(void) sprintf(searchFile, "%08X", htonl(addr));
		}
		scriptwrite(fp, LEVEL0, upgrade_client_inetboot,
		    "KARCH", karch,
		    "SVCPROD", newproduct->p_name,
		    "SVCVER", newproduct->p_version,
		    "SEARCHFILE", searchFile,
		    "BOOTDIR", bootd, (char *)0);
	}
}
