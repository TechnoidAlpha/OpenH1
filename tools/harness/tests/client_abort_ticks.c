#include "harness.h"
#undef match_assert
#define match_assert(file,line,condition) CHECK(condition,"%s:%d",file,line)

/* update_client_local_ticks in a network client's game whose host went down:
network_game_abort has made the connection local, and the host's queues,
which a client never started, are not valid (a datum_get of one asserted
and returned NULL, written to) */
enum { _game_connection_local, _game_connection_network_client };
struct player_action { byte bytes[32]; };
struct player_action_collection { struct player_action actions[4]; };
struct server_update { long count; };
struct queues { boolean valid; };
static struct { boolean initialized; struct queues *queues; } update_server_globals;
static int connection = _game_connection_local;
static int built, handled, updates, server_updates;

static int game_connection(void) { return connection; }
static void update_client_build_client_update(struct player_action_collection *collection)
{ memset(collection, 0, sizeof(*collection)); built++; }
static void update_server_handle_client_update(long machine_index, struct player_action *actions)
{
 (void)machine_index; (void)actions;
 CHECK(update_server_globals.initialized && update_server_globals.queues->valid, "the host's queues asked for while not valid");
 handled++;
}
static void update_server_next_update(void) { updates++; }
static void update_server_build_server_update(long machine_index, struct server_update *update, long *update_number)
{
 (void)machine_index; (void)update; (void)update_number;
 CHECK(update_server_globals.queues->valid, "a server update built from queues not valid");
 server_updates++;
}
#include "under_test.inc"

int main(int argc,char **argv)
{
 struct queues queues; char const *case_name=argc==2?argv[1]:"";
 update_server_globals.queues=&queues; update_server_globals.initialized=TRUE;
 CASE("local") { queues.valid=TRUE; update_client_local_ticks(3); CHECK(built==1 && handled==1 && updates==3 && server_updates==3,"a local game's ticks were not run"); }
 else CASE("client-aborted") { queues.valid=FALSE; update_client_local_ticks(3); CHECK(!handled && !updates && !server_updates,"ticks ran on queues not valid"); }
 else CASE("uninitialized") { update_server_globals.initialized=FALSE; queues.valid=FALSE; update_client_local_ticks(1); CHECK(!handled,"ticks ran with no queues"); }
 else return 2;
 return 0;
}
