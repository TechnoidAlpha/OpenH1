/*
BOT_CONTROL.C

The host's control connection to its bots, and its command line
(bot_control.h; docs/bot_control.md).

The connection: off unless the host turns it on (bots.control, or the game
started with --bot-control). On, the game listens on 127.0.0.1 alone, at
bots.control_port (25190; the next nine if that one is taken), and writes
bot_control.txt next to config.toml: the port and a token of 32 random
bytes, new each time it opens. A request is one line, the token, a space
and a command of bots_command.c's; the answer is "ok" or "failed" on a line
of its own, then the command's lines, and the connection closes. Requests
are read and answered on the main thread between frames (bot_control_frame),
so a command sees the game as its frames do; what it changes waits for the
next tick (bots_command.c). A dedicated server has no such connection: its
admins' RCON and HTTP take "bots <command>" with their own sign-in.

The command line: `goath1 bots <command> [--json]` reads bot_control.txt
(beside this run's config.toml: --config and HALO_CONFIG_FILE as the game
takes them), sends the command and prints the answer. `goath1 bots watch`
asks for the events since the last it printed four times a second, and the
bots' state every --every seconds (5), each as one line of JSON, until
--seconds have passed (none: until it is stopped). With --http <host:port>
it signs in to a dedicated server's HTTP API (--password, or
HALO_BOTS_PASSWORD) and runs the same commands as "bots <command>" there.
*/

#include "platform.h"
#include "bot_control.h"
#include "port_config.h"
#include "posix.h"
#include "server_json.h"
#include "server_mode.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
/* port/linux/game/bots_command.c's (its boolean, a byte) */
unsigned char bots_command_run(char const *line, char *reply, long reply_size);

enum
{
	BOT_CONTROL_DEFAULT_PORT = 25190,
	BOT_CONTROL_PORTS_TRIED = 10,
	BOT_CONTROL_CONNECTIONS = 8,
	BOT_CONTROL_REQUEST_SIZE = 512,
	BOT_CONTROL_REPLY_SIZE = 64 * 1024,
	/* a connection that sends no whole line in this long is closed */
	BOT_CONTROL_SECONDS = 5,
	BOT_CONTROL_TOKEN_BYTES = 32,
	/* Winsock's: the call would block */
	BOT_CONTROL_WOULD_BLOCK = 10035,
};

#define BOT_CONTROL_FILE "bot_control.txt"

/* (the game's free takes no NULL: its debug memory asserts) */
#define bot_control_free(pointer) do { if (pointer) free(pointer); } while (0)

#if HALO_SERVER_BUILD

/* ---------- helpers */

static void bot_control_address(unsigned char *address, unsigned long ip, long port)
{
	memset(address, 0, 16);
	address[0] = 2;
	address[2] = (unsigned char)(port >> 8);
	address[3] = (unsigned char)port;
	address[4] = (unsigned char)(ip >> 24);
	address[5] = (unsigned char)(ip >> 16);
	address[6] = (unsigned char)(ip >> 8);
	address[7] = (unsigned char)ip;
}

static void bot_control_file_path(char *path, size_t size)
{
	char folder[1024];

	config_folder(folder, sizeof(folder));
	snprintf(path, size, "%s" BOT_CONTROL_FILE, folder);
}

static int bot_control_token_equal(const char *a, const char *b)
{
	size_t length = strlen(b), index;
	unsigned char difference = 0;

	if (strlen(a) != length)
		return 0;
	for (index = 0; index < length; index++)
		difference |= (unsigned char)(a[index] ^ b[index]);
	return difference == 0;
}

/* ---------- the connection (the game's) */

struct bot_control_connection
{
	int socket;
	char in[BOT_CONTROL_REQUEST_SIZE];
	int in_length;
	char *out;
	int out_length;
	int out_sent;
	Uint64 opened;
};

static struct
{
	int checked;
	unsigned long changes;
	int listening;
	int listener;
	long port;
	char token[BOT_CONTROL_TOKEN_BYTES * 2 + 1];
	struct bot_control_connection connections[BOT_CONTROL_CONNECTIONS];
	char reply[BOT_CONTROL_REPLY_SIZE];
} bot_control;

static int bot_control_wanted(void)
{
	return config_boolean("bots.control") || config_command_line_flag("--bot-control");
}

static void bot_control_close(struct bot_control_connection *connection)
{
	if (connection->socket >= 0)
		posix_socket_close(connection->socket);
	bot_control_free(connection->out);
	memset(connection, 0, sizeof(*connection));
	connection->socket = -1;
}

static void bot_control_stop(void)
{
	char path[1100];
	int index;

	for (index = 0; index < BOT_CONTROL_CONNECTIONS; index++)
		bot_control_close(&bot_control.connections[index]);
	if (bot_control.listening)
		posix_socket_close(bot_control.listener);
	bot_control.listening = 0;
	bot_control_file_path(path, sizeof(path));
	remove(path);
	platform_log("bot control: closed");
}

static void bot_control_start(void)
{
	unsigned char bytes[BOT_CONTROL_TOKEN_BYTES];
	unsigned char address[16];
	long first = config_integer("bots.control_port");
	long port;
	char path[1100];
	FILE *file;
	int index;

	for (index = 0; index < BOT_CONTROL_CONNECTIONS; index++)
		bot_control.connections[index].socket = -1;
	if (first <= 0 || first > 65535 - BOT_CONTROL_PORTS_TRIED)
		first = BOT_CONTROL_DEFAULT_PORT;
	bot_control.listener = posix_socket(2, 1, 6);
	if (bot_control.listener < 0)
	{
		platform_log("bot control: no socket (error %d)", posix_socket_last_error());
		return;
	}
	for (port = first; port < first + BOT_CONTROL_PORTS_TRIED; port++)
	{
		bot_control_address(address, 0x7F000001UL, port);
		if (posix_socket_bind(bot_control.listener, address, 16) == 0)
			break;
	}
	if (port >= first + BOT_CONTROL_PORTS_TRIED || posix_socket_listen(bot_control.listener, 8) != 0)
	{
		platform_log("bot control: ports %ld to %ld of 127.0.0.1 are all taken (bots.control_port)", first,
			first + BOT_CONTROL_PORTS_TRIED - 1);
		posix_socket_close(bot_control.listener);
		return;
	}
	posix_socket_set_nonblocking(bot_control.listener, 1);
	posix_random_bytes(bytes, sizeof(bytes));
	for (index = 0; index < BOT_CONTROL_TOKEN_BYTES; index++)
		snprintf(bot_control.token + index * 2, 3, "%02x", bytes[index]);
	bot_control.port = port;
	bot_control.listening = 1;
	bot_control_file_path(path, sizeof(path));
	file = fopen(path, "wb");
	if (file)
	{
		fprintf(file, "port %ld\ntoken %s\n", port, bot_control.token);
		fclose(file);
	}
	else
		platform_log("bot control: %s cannot be written: the command line will not find the game", path);
	platform_log("bot control: listening on 127.0.0.1:%ld (its token in %s)", port, path);
}

/* the request's line answered into the connection's output */
static void bot_control_answer(struct bot_control_connection *connection)
{
	char *space = strchr(connection->in, ' ');
	unsigned char succeeded = 0;
	int length;

	bot_control.reply[0] = 0;
	if (space)
		*space = 0;
	if (!space || !bot_control_token_equal(connection->in, bot_control.token))
		snprintf(bot_control.reply, sizeof(bot_control.reply), "bots: the token is not this game's (%s)\n",
			BOT_CONTROL_FILE);
	else
		succeeded = bots_command_run(space + 1, bot_control.reply, sizeof(bot_control.reply));
	length = (int)strlen(bot_control.reply);
	connection->out = malloc((size_t)length + 16);
	if (!connection->out)
	{
		bot_control_close(connection);
		return;
	}
	connection->out_length = snprintf(connection->out, (size_t)length + 16, "%s\n%s", succeeded ? "ok" : "failed",
		bot_control.reply);
	connection->out_sent = 0;
}

void bot_control_frame(void)
{
	Uint64 now;
	int index;

	if (halo_server_mode())
		return;
	/* (opened or closed as the setting changes) */
	if (!bot_control.checked || bot_control.changes != config_changes())
	{
		int wanted = bot_control_wanted();

		bot_control.checked = 1;
		bot_control.changes = config_changes();
		if (wanted && !bot_control.listening)
			bot_control_start();
		else if (!wanted && bot_control.listening)
			bot_control_stop();
	}
	if (!bot_control.listening)
		return;
	now = SDL_GetTicks();
	for (;;)
	{
		unsigned char address[16];
		int address_length = sizeof(address);
		int socket = posix_socket_accept(bot_control.listener, address, &address_length);
		struct bot_control_connection *free_connection = NULL;

		if (socket < 0)
			break;
		for (index = 0; index < BOT_CONTROL_CONNECTIONS && !free_connection; index++)
		{
			if (bot_control.connections[index].socket < 0)
				free_connection = &bot_control.connections[index];
		}
		/* (this machine's alone: the listener is bound to 127.0.0.1) */
		if (!free_connection || address[4] != 127)
		{
			posix_socket_close(socket);
			continue;
		}
		posix_socket_set_nonblocking(socket, 1);
		memset(free_connection, 0, sizeof(*free_connection));
		free_connection->socket = socket;
		free_connection->opened = now;
	}
	for (index = 0; index < BOT_CONTROL_CONNECTIONS; index++)
	{
		struct bot_control_connection *connection = &bot_control.connections[index];

		if (connection->socket < 0)
			continue;
		if (!connection->out)
		{
			int count = posix_socket_recv(connection->socket, connection->in + connection->in_length,
				(int)sizeof(connection->in) - 1 - connection->in_length, 0);
			char *end;

			if (count == 0 || (count < 0 && posix_socket_last_error() != BOT_CONTROL_WOULD_BLOCK))
			{
				bot_control_close(connection);
				continue;
			}
			if (count > 0)
				connection->in_length += count;
			connection->in[connection->in_length] = 0;
			end = strchr(connection->in, '\n');
			if (end)
			{
				*end = 0;
				if (end > connection->in && end[-1] == '\r')
					end[-1] = 0;
				bot_control_answer(connection);
			}
			else if (connection->in_length >= (int)sizeof(connection->in) - 1 ||
				now - connection->opened > BOT_CONTROL_SECONDS * 1000)
			{
				bot_control_close(connection);
				continue;
			}
		}
		if (connection->socket >= 0 && connection->out)
		{
			int count = posix_socket_send(connection->socket, connection->out + connection->out_sent,
				connection->out_length - connection->out_sent, 0);

			if (count > 0)
				connection->out_sent += count;
			if (connection->out_sent >= connection->out_length ||
				(count < 0 && posix_socket_last_error() != BOT_CONTROL_WOULD_BLOCK) ||
				now - connection->opened > BOT_CONTROL_SECONDS * 4000)
			{
				bot_control_close(connection);
			}
		}
	}
}

/* ---------- the command line */

static struct
{
	int json;
	long port;
	char token[BOT_CONTROL_TOKEN_BYTES * 2 + 1];
	char http[256];
	char password[256];
	char session[128];
	double every;
	double seconds;
	int from_start;
} bot_control_cli;

/* a socket connected to 127.0.0.1 (or http's host), -1 for none */
static int bot_control_connect(unsigned long ip, long port)
{
	unsigned char address[16];
	int socket = posix_socket(2, 1, 6);

	if (socket < 0)
		return -1;
	bot_control_address(address, ip, port);
	if (posix_socket_connect(socket, address, 16) != 0)
	{
		posix_socket_close(socket);
		return -1;
	}
	return socket;
}

/* everything the other end sends until it closes (10 seconds at most),
malloc'd and NUL terminated; NULL for nothing */
static char *bot_control_receive(int socket)
{
	size_t capacity = 4096, length = 0;
	char *text = malloc(capacity);
	Uint64 deadline = SDL_GetTicks() + 10000;

	if (!text)
		return NULL;
	for (;;)
	{
		int reads[1] = { socket }, read_count = 1;
		int count;

		if (SDL_GetTicks() > deadline)
			break;
		posix_socket_select(reads, &read_count, NULL, NULL, NULL, NULL, 0, 200000, 0);
		if (!read_count)
			continue;
		if (capacity - length < 2048)
		{
			char *grown = realloc(text, capacity * 2);

			if (!grown)
				break;
			text = grown;
			capacity *= 2;
		}
		count = posix_socket_recv(socket, text + length, (int)(capacity - length - 1), 0);
		if (count <= 0)
			break;
		length += (size_t)count;
	}
	text[length] = 0;
	return text;
}

static int bot_control_send_all(int socket, const char *text)
{
	int length = (int)strlen(text), sent = 0;

	while (sent < length)
	{
		int count = posix_socket_send(socket, text + sent, length - sent, 0);

		if (count <= 0)
			return 0;
		sent += count;
	}
	return 1;
}

/* the game's port and token from bot_control.txt */
static int bot_control_read_file(char *why, size_t why_size)
{
	char path[1100], line[256];
	FILE *file;

	if (bot_control_cli.port > 0 && bot_control_cli.token[0])
		return 1;
	bot_control_file_path(path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
	{
		snprintf(why, why_size, "no game is listening (%s is not there: start the game with --bot-control, or set "
			"bots.control to true)", path);
		return 0;
	}
	while (fgets(line, sizeof(line), file))
	{
		long port;
		char token[sizeof(bot_control_cli.token)];

		if (sscanf(line, "port %ld", &port) == 1 && bot_control_cli.port <= 0)
			bot_control_cli.port = port;
		else if (sscanf(line, "token %64s", token) == 1 && !bot_control_cli.token[0])
			snprintf(bot_control_cli.token, sizeof(bot_control_cli.token), "%s", token);
	}
	fclose(file);
	if (bot_control_cli.port <= 0 || !bot_control_cli.token[0])
	{
		snprintf(why, why_size, "%s holds no port and token", path);
		return 0;
	}
	return 1;
}

/* an HTTP request to the server (--http), its body; the answer's body,
malloc'd (NULL for none), and its status */
static char *bot_control_http(const char *method, const char *path, const char *body, int *status)
{
	char host[256], request[2048];
	char *colon, *text, *start;
	long port = 8080;
	unsigned long ip;
	int socket;

	*status = 0;
	snprintf(host, sizeof(host), "%s", bot_control_cli.http);
	if (!strncmp(host, "http://", 7))
		memmove(host, host + 7, strlen(host + 7) + 1);
	if (strchr(host, '/'))
		*strchr(host, '/') = 0;
	colon = strrchr(host, ':');
	if (colon)
	{
		port = strtol(colon + 1, NULL, 10);
		*colon = 0;
	}
	ip = posix_resolve_ipv4(host);
	if (!ip)
		return NULL;
	/* (network byte order to the address's) */
	ip = ((ip & 0xFF) << 24) | ((ip & 0xFF00) << 8) | ((ip >> 8) & 0xFF00) | ((ip >> 24) & 0xFF);
	socket = bot_control_connect(ip, port);
	if (socket < 0)
		return NULL;
	snprintf(request, sizeof(request), "%s %s HTTP/1.1\r\nHost: %s:%ld\r\nConnection: close\r\n%s%s%s"
		"Content-Type: application/json\r\nContent-Length: %d\r\n\r\n%s", method, path, host, port,
		bot_control_cli.session[0] ? "Authorization: Bearer " : "", bot_control_cli.session,
		bot_control_cli.session[0] ? "\r\n" : "", (int)strlen(body), body);
	if (!bot_control_send_all(socket, request))
	{
		posix_socket_close(socket);
		return NULL;
	}
	text = bot_control_receive(socket);
	posix_socket_close(socket);
	if (!text)
		return NULL;
	sscanf(text, "HTTP/%*s %d", status);
	start = strstr(text, "\r\n\r\n");
	if (!start)
	{
		bot_control_free(text);
		return NULL;
	}
	memmove(text, start + 4, strlen(start + 4) + 1);
	return text;
}

/* a JSON string's text, its quotes and backslashes escaped */
static void bot_control_json_string(const char *text, char *out, size_t size)
{
	size_t length = 0;

	for (; *text && length + 8 < size; text++)
	{
		if (*text == '"' || *text == '\\')
			out[length++] = '\\';
		if ((unsigned char)*text < 0x20)
			continue;
		out[length++] = *text;
	}
	out[length] = 0;
}

/* a command run on the game: its answer's lines (malloc'd, NUL terminated)
and whether it succeeded; NULL (why, in why) when no game answered */
static char *bot_control_run(const char *line, int *succeeded, char *why, size_t why_size)
{
	*succeeded = 0;
	if (bot_control_cli.http[0])
	{
		char body[1400], escaped[1100], *answer;
		struct server_json_document document;
		int status, lines, index;
		size_t length = 0, capacity;
		char *text;

		if (!bot_control_cli.session[0])
		{
			bot_control_json_string(bot_control_cli.password, escaped, sizeof(escaped));
			snprintf(body, sizeof(body), "{\"password\":\"%s\",\"by\":\"bot control\"}", escaped);
			answer = bot_control_http("POST", "/api/login", body, &status);
			if (!answer || status != 200 || !server_json_parse(&document, answer, (int)strlen(answer), 8))
			{
				snprintf(why, why_size, "%s did not sign us in (HTTP %d: --password, or HALO_BOTS_PASSWORD, is the "
					"server's admin password)", bot_control_cli.http, status);
				bot_control_free(answer);
				return NULL;
			}
			index = server_json_member(&document, 0, "token");
			if (index < 0 || !server_json_get_string(&document, index, bot_control_cli.session,
				sizeof(bot_control_cli.session)))
			{
				snprintf(why, why_size, "%s gave no session", bot_control_cli.http);
				server_json_document_free(&document);
				bot_control_free(answer);
				return NULL;
			}
			server_json_document_free(&document);
			bot_control_free(answer);
		}
		snprintf(body, sizeof(body), "bots %s", line);
		bot_control_json_string(body, escaped, sizeof(escaped));
		snprintf(body, sizeof(body), "{\"line\":\"%s\"}", escaped);
		answer = bot_control_http("POST", "/api/command", body, &status);
		if (!answer || !server_json_parse_tokens(&document, answer, (int)strlen(answer), 8, 65536))
		{
			snprintf(why, why_size, "%s did not answer (HTTP %d)", bot_control_cli.http, status);
			bot_control_free(answer);
			return NULL;
		}
		{
			int ok = 0;
			int token = server_json_member(&document, 0, "ok");

			if (token >= 0)
				server_json_get_boolean(&document, token, &ok);
			*succeeded = ok;
		}
		lines = server_json_member(&document, 0, "lines");
		capacity = strlen(answer) + 64;
		text = malloc(capacity);
		if (!text)
		{
			server_json_document_free(&document);
			bot_control_free(answer);
			return NULL;
		}
		text[0] = 0;
		for (index = 0; lines >= 0; index++)
		{
			int element = server_json_element(&document, lines, index);

			if (element < 0 || !server_json_get_string(&document, element, text + length, (int)(capacity - length - 2)))
				break;
			length += strlen(text + length);
			text[length++] = '\n';
			text[length] = 0;
		}
		if (!length && !*succeeded)
		{
			int error = server_json_member(&document, 0, "error");
			int message = error >= 0 ? server_json_member(&document, error, "message") : -1;

			if (message >= 0 && server_json_get_string(&document, message, text, (int)capacity - 2))
				strcat(text, "\n");
		}
		server_json_document_free(&document);
		bot_control_free(answer);
		return text;
	}
	{
		char request[BOT_CONTROL_REQUEST_SIZE + 128];
		char *answer, *rest;
		int socket;

		if (!bot_control_read_file(why, why_size))
			return NULL;
		socket = bot_control_connect(0x7F000001UL, bot_control_cli.port);
		if (socket < 0)
		{
			snprintf(why, why_size, "no game answers on 127.0.0.1:%ld (is it running, with bot control on?)",
				bot_control_cli.port);
			return NULL;
		}
		snprintf(request, sizeof(request), "%s %s\n", bot_control_cli.token, line);
		if (!bot_control_send_all(socket, request))
		{
			posix_socket_close(socket);
			snprintf(why, why_size, "the game closed the connection");
			return NULL;
		}
		answer = bot_control_receive(socket);
		posix_socket_close(socket);
		if (!answer || !answer[0])
		{
			bot_control_free(answer);
			snprintf(why, why_size, "the game did not answer");
			return NULL;
		}
		rest = strchr(answer, '\n');
		*succeeded = !strncmp(answer, "ok", 2);
		memmove(answer, rest ? rest + 1 : answer + strlen(answer), strlen(rest ? rest + 1 : "") + 1);
		return answer;
	}
}

static void bot_control_help(void)
{
	fputs("goath1 bots <command> [--json]: direct the bots of a game this computer hosts\n"
		"  (the game started with --bot-control, or bots.control on; docs/bot_control.md)\n"
		"\n"
		"  status | bots | places | watch | help\n"
		"  order <who> <attack|defend|hold|follow|push|retreat|regroup|goto|clear> [target]\n"
		"  set <who> <skill|aggression|teamwork|weapon|vehicles|style> <value>   set <who> reset\n"
		"  add [n] [red|blue] | remove [n] | fill <n> | team <who> <red|blue>\n"
		"\n"
		"  --json            one JSON object a line (for scripts and agents)\n"
		"  watch --every S   a snapshot of the bots every S seconds (5; 0 for none)\n"
		"  watch --seconds S stop after S seconds;  watch --from-start: the events kept, then on\n"
		"  --port N --token T            another game's connection than bot_control.txt's\n"
		"  --http HOST:PORT --password P a dedicated server's (its admin password; or HALO_BOTS_PASSWORD)\n"
		"\n"
		"Examples:\n"
		"  goath1 bots status\n"
		"  goath1 bots order red defend red flag\n"
		"  goath1 bots order all red attack blue base\n"
		"  goath1 bots set blue aggression 0.9\n"
		"  goath1 bots watch --json --every 2\n", stdout);
}

/* the events and snapshots, one JSON a line */
static int bot_control_watch(void)
{
	char why[1400];
	long next = 0;
	int succeeded;
	Uint64 started = SDL_GetTicks(), last_snapshot = 0;

	if (!bot_control_cli.from_start)
	{
		char *answer = bot_control_run("status --json", &succeeded, why, sizeof(why));
		char *found = answer ? strstr(answer, "\"next_event\":") : NULL;

		if (!answer)
		{
			fprintf(stderr, "bots: %s\n", why);
			return 2;
		}
		if (found)
			next = strtol(found + 13, NULL, 10);
		bot_control_free(answer);
	}
	for (;;)
	{
		char line[64];
		char *answer, *cursor;
		Uint64 now = SDL_GetTicks();

		if (bot_control_cli.seconds > 0.0 && (double)(now - started) >= bot_control_cli.seconds * 1000.0)
			return 0;
		snprintf(line, sizeof(line), "events %ld --json", next);
		answer = bot_control_run(line, &succeeded, why, sizeof(why));
		if (!answer)
		{
			fprintf(stderr, "bots: %s\n", why);
			return 2;
		}
		/* (each event of the answer's array on its own line: the array's
		elements are the game's event texts) */
		{
			struct server_json_document document;

			if (server_json_parse_tokens(&document, answer, (int)strlen(answer), 16, 262144))
			{
				int events = server_json_member(&document, 0, "events");
				int next_token = server_json_member(&document, 0, "next");
				int index;
				double value;

				for (index = 0; events >= 0; index++)
				{
					int element = server_json_element(&document, events, index);

					if (element < 0)
						break;
					fprintf(stdout, "%.*s\n", document.tokens[element].end - document.tokens[element].start,
						answer + document.tokens[element].start);
				}
				if (next_token >= 0 && server_json_get_number(&document, next_token, &value))
					next = (long)value;
				server_json_document_free(&document);
			}
		}
		bot_control_free(answer);
		if (bot_control_cli.every > 0.0 && (!last_snapshot || (double)(now - last_snapshot) >= bot_control_cli.every * 1000.0))
		{
			last_snapshot = now;
			answer = bot_control_run("bots --json", &succeeded, why, sizeof(why));
			cursor = answer ? strstr(answer, "{\"ok\":true,") : NULL;
			if (cursor)
			{
				char *end = strchr(cursor, '\n');

				if (end)
					*end = 0;
				fprintf(stdout, "{\"event\":\"snapshot\",%s\n", cursor + 11);
			}
			bot_control_free(answer);
		}
		fflush(stdout);
		SDL_Delay(250);
	}
}

void bot_control_command_line(int first_argument)
{
	char argument[1024];
	char line[BOT_CONTROL_REQUEST_SIZE] = "";
	size_t length = 0;
	int index, succeeded;
	int watch = 0;
	char why[1400];
	char *answer;
	const char *password = getenv("HALO_BOTS_PASSWORD");

	memset(&bot_control_cli, 0, sizeof(bot_control_cli));
	bot_control_cli.every = 5.0;
	if (password)
		snprintf(bot_control_cli.password, sizeof(bot_control_cli.password), "%s", password);
	for (index = first_argument; posix_command_line_argument(index, argument, sizeof(argument)); index++)
	{
		char value[1024] = "";
		int has_value = posix_command_line_argument(index + 1, value, sizeof(value));

		if (!strcmp(argument, "--json"))
		{
			bot_control_cli.json = 1;
			continue;
		}
		if (!strcmp(argument, "--from-start"))
		{
			bot_control_cli.from_start = 1;
			continue;
		}
		if (!strcmp(argument, "--port") || !strcmp(argument, "--token") || !strcmp(argument, "--http") ||
			!strcmp(argument, "--password") || !strcmp(argument, "--every") || !strcmp(argument, "--seconds") ||
			!strcmp(argument, "--config"))
		{
			if (!has_value)
			{
				fprintf(stderr, "bots: %s needs a value\n", argument);
				exit(1);
			}
			if (!strcmp(argument, "--port"))
				bot_control_cli.port = strtol(value, NULL, 10);
			else if (!strcmp(argument, "--token"))
				snprintf(bot_control_cli.token, sizeof(bot_control_cli.token), "%s", value);
			else if (!strcmp(argument, "--http"))
				snprintf(bot_control_cli.http, sizeof(bot_control_cli.http), "%s", value);
			else if (!strcmp(argument, "--password"))
				snprintf(bot_control_cli.password, sizeof(bot_control_cli.password), "%s", value);
			else if (!strcmp(argument, "--every"))
				bot_control_cli.every = strtod(value, NULL);
			else if (!strcmp(argument, "--seconds"))
				bot_control_cli.seconds = strtod(value, NULL);
			/* (--config: the game's own, config_path's) */
			index++;
			continue;
		}
		if (!length && !strcmp(argument, "watch"))
		{
			watch = 1;
			continue;
		}
		if (!length && (!strcmp(argument, "--help") || !strcmp(argument, "-h")))
		{
			bot_control_help();
			exit(0);
		}
		/* (a word with a space, quoted: the game's parser keeps it whole) */
		length += (size_t)snprintf(line + length, sizeof(line) - length, strchr(argument, ' ') ? "%s\"%s\"" : "%s%s",
			length ? " " : "", argument);
		if (length >= sizeof(line) - 1)
		{
			fprintf(stderr, "bots: the command is too long\n");
			exit(1);
		}
	}
	if (watch)
		exit(bot_control_watch());
	if (!length)
	{
		bot_control_help();
		exit(0);
	}
	if (bot_control_cli.json && length + 8 < sizeof(line))
		strcat(line, " --json");
	answer = bot_control_run(line, &succeeded, why, sizeof(why));
	if (!answer)
	{
		if (bot_control_cli.json)
		{
			char escaped[1500];

			bot_control_json_string(why, escaped, sizeof(escaped));
			fprintf(stdout, "{\"ok\":false,\"error\":\"%s\"}\n", escaped);
		}
		else
			fprintf(stderr, "bots: %s\n", why);
		exit(2);
	}
	fputs(answer, stdout);
	fflush(stdout);
	bot_control_free(answer);
	exit(succeeded ? 0 : 1);
}

#else

void bot_control_frame(void)
{
}

void bot_control_command_line(int first_argument)
{
	(void)first_argument;
}

#endif
