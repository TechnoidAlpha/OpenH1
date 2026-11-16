/*
CLI_INSTALL.H

goath1, GOAT H1's command (goath1 bots ...), on the PATH: halo --install-cli
(cli_install.c).
*/

#ifndef __HALO_LINUX_CLI_INSTALL_H
#define __HALO_LINUX_CLI_INSTALL_H

#include <stddef.h>

/* a goath1 script in folder that runs executable (path: where); 0, why in
why, if it cannot be written or a goath1 there is not one this wrote */
int cli_install_write(const char *folder, const char *executable, char *path, size_t path_size, char *why,
	size_t why_size);
/* the command line's --install-cli [folder] (server_mode.c): answered, and
the process ended (0: installed; 1: not) */
void cli_install_command_line(int first_argument);

#endif
