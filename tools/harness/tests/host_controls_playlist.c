#include "harness.h"

/* Host Controls' PLAYLIST (host_controls_menu.inc's host_playlist and
host_playlist_index): the host's own playlist and its next for a game this
machine hosts, the server's rotation and index for a dedicated server's
admin. Each once ended in itself, and a host's PLAYLIST tab hung the game
(an endless loop, the compiler's of the call). A case that does not return
within a few seconds, or overflows its stack, fails. */

#include <signal.h>
#include <unistd.h>

struct server_rotation { long count; };
struct network_admin_state { long rotation_index; };

/* ---- the fake world: this host's playlist, and a server's an admin sees */

static struct server_rotation host_rotation = { 3 }, server_rotation = { 5 };
static boolean remote;

static boolean host_remote(void) { return remote; }
static struct server_rotation const *network_admin_rotation(void) { return &server_rotation; }
static struct server_rotation const *network_host_game_playlist(void) { return &host_rotation; }
static long network_host_game_playlist_index(void) { return 1; }

static boolean network_admin_remote_state(struct network_admin_state *state)
{
	if (!remote)
		return FALSE;
	state->rotation_index = 4;
	return TRUE;
}

#include "under_test.inc"

/* (an endless loop, or an endless recursion's overflowed stack) */
static void hung(int signal_number)
{
	static char const message[] = "check failed: the playlist never came (it ends in itself)\n";

	(void)signal_number;
	if (write(2, message, sizeof(message) - 1) < 0)
		_exit(1);
	_exit(1);
}

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";
	static char alternate[65536];
	stack_t stack;
	struct sigaction action;

	stack.ss_sp = alternate;
	stack.ss_size = sizeof(alternate);
	stack.ss_flags = 0;
	sigaltstack(&stack, NULL);
	memset(&action, 0, sizeof(action));
	action.sa_handler = hung;
	action.sa_flags = SA_ONSTACK;
	sigemptyset(&action.sa_mask);
	sigaction(SIGALRM, &action, NULL);
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGBUS, &action, NULL);
	alarm(5);
	if (!strcmp(test, "host"))
	{
		remote = FALSE;
		CHECK(host_playlist() == &host_rotation, "a host's PLAYLIST: its own playlist");
		CHECK(host_playlist_index() == 1, "a host's PLAYLIST: its own next");
	}
	else if (!strcmp(test, "admin"))
	{
		remote = TRUE;
		CHECK(host_playlist() == &server_rotation, "an admin's PLAYLIST: the server's rotation");
		CHECK(host_playlist_index() == 4, "an admin's PLAYLIST: the server's index");
	}
	else
	{
		fprintf(stderr, "unknown case %s\n", test);
		return 2;
	}
	return 0;
}
