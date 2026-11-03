/*
CE_INSTALLER.H

From ChupathingyCE (https://github.com/ChupathingyCE/chupathingyce, pull
request #313, by Milenko), under GPL-3.0-or-later; OpenH1's changes to it
are marked "OpenH1:".

Custom Edition's maps out of its installer (ce_installer.c).
*/

#ifndef __HALO_LINUX_CE_INSTALLER_H
#define __HALO_LINUX_CE_INSTALLER_H

#include "cabinet.h"

/* whether the file is Custom Edition's installer (halocesetup_en_1.00.exe):
a Windows program with a cabinet of maps inside */
int ce_installer_is_installer(const char *path);

/* writes the installer's maps that destination (OpenH1: the custom_maps
folder itself, not <destination>/maps_ce) lacks there; as
pc_disc_extract_maps (pc_disc.h) */
int ce_installer_extract_maps(const char *installer_path, const char *destination, cabinet_progress_proc progress,
	void *context, char *error, int error_size);

#endif
