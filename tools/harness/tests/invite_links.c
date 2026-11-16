/*
INVITE_LINKS.C

COPY INVITE's message (port/linux/src/p2p.c's p2p_invite_message) and the
links the game takes (p2p.c's parse_invite, p2p_party.c's
p2p_party_parse_link, and sdl_platform.c's look at the clipboard,
platform_text_has_invite_link): test_invite_links.py takes them from the
sources (under_test.inc). Each case is run by its name:

- party: a party's message, as it is: the join page's address with the
  token after "#party=", then the openh1:// link to paste; both lines lead
  back to the party, and either on the clipboard is seen as a link;
- game: a game's, the same with "#join=";
- forms: every form of either link is still taken (openh1://join/, and
  older builds' halo://join/, a bare code, ?join= and &join= in an address,
  the join page's #join=, an older version's code as one, openh1://party/
  and halo://party/ in any case, ?party=, &party=, #party=), and a wrong
  length is not; a link of an older build's (halo://) makes the message of
  the game's own (openh1://);
- none: text with no link gives no message.
*/

#include "harness.h"

typedef int BOOL;

#include "under_test.inc"

#define HASH "0123456789abcdef0123456789abcdef"
#define TOKEN "00112233445566778899aabbccddeeff"
#define PARTY_TOKEN "fedcba9876543210fedcba9876543210"

static void check_party_token(const char *text, int expected)
{
	unsigned char token[P2P_TOKEN_SIZE];
	int parsed = p2p_party_parse_link(text, token);

	CHECK(parsed == expected, "%s: %d, not %d", text, parsed, expected);
	if (expected)
		CHECK(token[0] == 0xfe && token[15] == 0x10, "%s: the party's token", text);
}

static void check_invite(const char *text, int expected)
{
	unsigned char hash[P2P_KEY_HASH_SIZE], token[P2P_TOKEN_SIZE];
	int parsed = parse_invite(text, hash, token);

	CHECK(parsed == expected, "%s: %d, not %d", text, parsed, expected);
	if (expected > 0)
		CHECK(hash[0] == 0x01 && hash[15] == 0xef && token[0] == 0x00 && token[15] == 0xff, "%s: the invite", text);
}

static void case_party(void)
{
	char message[P2P_INVITE_MESSAGE_SIZE];
	const char *expected = "Join my GOAT H1 party: https://openh1.com/join/#party=" PARTY_TOKEN "\n"
		"Or paste this into GOAT H1's Join by Link: openh1://party/" PARTY_TOKEN;
	const char *line;

	CHECK(p2p_invite_message("openh1://party/" PARTY_TOKEN, message, sizeof(message)), "a party's link has a message");
	CHECK(!strcmp(message, expected), "the message:\n%s", message);
	check_party_token(message, 1);
	CHECK(platform_text_has_invite_link(message), "the message on the clipboard is a link");
	line = "https://openh1.com/join/#party=" PARTY_TOKEN;
	check_party_token(line, 1);
	CHECK(platform_text_has_invite_link(line), "the join page's address alone is a link");
	check_party_token(strchr(message, '\n') + 1, 1);
	/* (the message's own link, or its address, makes it again) */
	CHECK(p2p_invite_message(line, message, sizeof(message)) && !strcmp(message, expected), "again: %s", message);
}

static void case_game(void)
{
	char message[P2P_INVITE_MESSAGE_SIZE];
	const char *expected = "Join my GOAT H1 game: https://openh1.com/join/#join=" HASH TOKEN "\n"
		"Or paste this into GOAT H1's Join by Link: openh1://join/" HASH TOKEN;
	const char *line = "https://openh1.com/join/#join=" HASH TOKEN;

	CHECK(p2p_invite_message("openh1://join/" HASH TOKEN, message, sizeof(message)), "a game's link has a message");
	CHECK(!strcmp(message, expected), "the message:\n%s", message);
	CHECK(strlen(message) < P2P_INVITE_MESSAGE_SIZE - 1, "within its size");
	check_invite(message, 1);
	check_invite(line, 1);
	check_invite(strchr(message, '\n') + 1, 1);
	CHECK(platform_text_has_invite_link(message) && platform_text_has_invite_link(line),
		"on the clipboard, it and its address are links");
	CHECK(p2p_invite_message(HASH TOKEN, message, sizeof(message)) && !strcmp(message, expected),
		"a bare code's: %s", message);
	{
		unsigned char token[P2P_TOKEN_SIZE];

		CHECK(!p2p_party_parse_link(expected, token), "a game's message is no party's");
	}
}

static void case_forms(void)
{
	char message[P2P_INVITE_MESSAGE_SIZE];

	/* (a game's) */
	check_invite("openh1://join/" HASH TOKEN, 1);
	check_invite("OPENH1://JOIN/" HASH TOKEN, 1);
	check_invite("halo://join/" HASH TOKEN, 1);
	check_invite("HALO://JOIN/" HASH TOKEN, 1);
	check_invite("  " HASH TOKEN " \n", 1);
	check_invite("join us: https://openh1.com/?join=" HASH TOKEN " tonight", 1);
	check_invite("https://example.org/?x=1&join=" HASH TOKEN, 1);
	check_invite("https://openh1.com/join/#join=" HASH TOKEN, 1);
	check_invite("halo://join/0123456789ab" TOKEN, -1);
	check_invite("openh1://join/" HASH TOKEN "00", 0);
	check_invite("halo://join/" HASH TOKEN "00", 0);
	check_invite("openh1://join/" HASH, 0);
	check_invite("a checksum " HASH TOKEN, 0);
	/* (a party's) */
	check_party_token("openh1://party/" PARTY_TOKEN, 1);
	check_party_token("open OpenH1://Party/" PARTY_TOKEN " now", 1);
	check_party_token("halo://party/" PARTY_TOKEN, 1);
	check_party_token("open HALO://Party/" PARTY_TOKEN " now", 1);
	check_party_token("https://openh1.com/?party=" PARTY_TOKEN, 1);
	check_party_token("https://example.org/?a=b&party=" PARTY_TOKEN, 1);
	check_party_token("https://openh1.com/join/#party=" PARTY_TOKEN, 1);
	check_party_token("openh1://party/" PARTY_TOKEN "00", 0);
	check_party_token("halo://party/" PARTY_TOKEN "00", 0);
	check_party_token("openh1://party/fedcba98", 0);
	check_party_token("openh1://join/" HASH TOKEN, 0);
	check_party_token("halo://join/" HASH TOKEN, 0);
	/* (on the clipboard, either scheme's link is one) */
	CHECK(platform_text_has_invite_link("openh1://join/" HASH TOKEN) &&
		platform_text_has_invite_link("OpenH1://Party/" PARTY_TOKEN), "the game's links on the clipboard");
	CHECK(platform_text_has_invite_link("halo://join/" HASH TOKEN) &&
		platform_text_has_invite_link("HALO://PARTY/" PARTY_TOKEN), "an older build's links on the clipboard");
	/* (an older build's link is taken, and made into the game's own) */
	CHECK(p2p_invite_message("halo://join/" HASH TOKEN, message, sizeof(message)) &&
		strstr(message, "\nOr paste this into GOAT H1's Join by Link: openh1://join/" HASH TOKEN) &&
		!strstr(message, "halo:"), "an older build's game link, as the game's:\n%s", message);
	CHECK(p2p_invite_message("halo://party/" PARTY_TOKEN, message, sizeof(message)) &&
		strstr(message, "\nOr paste this into GOAT H1's Join by Link: openh1://party/" PARTY_TOKEN) &&
		!strstr(message, "halo:"), "an older build's party link, as the game's:\n%s", message);
	CHECK(!platform_text_has_invite_link("https://openh1.com/maps"), "another page of the site is no link");
	CHECK(!platform_text_has_invite_link(HASH TOKEN), "a bare code on the clipboard is no link");
}

static void case_none(void)
{
	char message[P2P_INVITE_MESSAGE_SIZE] = "untouched";

	CHECK(!p2p_invite_message("no link here", message, sizeof(message)), "no link, no message");
	CHECK(!p2p_invite_message("openh1://party/0011", message, sizeof(message)), "a short token, no message");
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "party"))
		case_party();
	else if (argc > 1 && !strcmp(argv[1], "game"))
		case_game();
	else if (argc > 1 && !strcmp(argv[1], "forms"))
		case_forms();
	else if (argc > 1 && !strcmp(argv[1], "none"))
		case_none();
	else
		return 2;
	return 0;
}
