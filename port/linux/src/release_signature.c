/*
RELEASE_SIGNATURE.C

A release's SHA256SUMS, its signature by the release key and the
download's SHA-256 checked before the self-updater installs it
(release_signature.h says what is refused; updater.c downloads them;
tools/release_sign.py signs them; Ed25519 is p2p_crypto.c's, Monocypher's).
What the release gives is untrusted until its signature checks: every size
is bounded and every line read within it.
*/

#include "release_signature.h"
#include "p2p_internal.h"

#include <stdio.h>
#include <string.h>

/* OpenH1's release keys, as port/assets/release-key.pub has them (64 hex
digits; tools/test_release_signature.py checks that they agree): a
signature by any of them is the release's. A new key is added here a
release before the old one's signatures stop. */
static const char *const release_keys[] =
{
	"38e00053ccb1c7fced2553635fec4c5598ac4531092ff82787b662dc8e129c74",
};

#define RELEASE_KEY_COUNT ((int)(sizeof(release_keys) / sizeof(release_keys[0])))

static int release_hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

/* size bytes from twice as many hex digits; 0 if one is not a digit */
static int release_hex_bytes(const char *text, int size, unsigned char *bytes)
{
	int index;

	for (index = 0; index < size; index++)
	{
		int high = release_hex_digit(text[index * 2]);
		int low = high < 0 ? -1 : release_hex_digit(text[index * 2 + 1]);

		if (low < 0)
			return 0;
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

static void release_error(char *error, int error_size, const char *message)
{
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", message);
}

/* SHA256SUMS.sig's signature: 128 hex digits, then nothing but white space;
1 when it is so */
static int release_signature_read(const char *text, int size, unsigned char *signature)
{
	int index;

	if (size < P2P_SIGNATURE_SIZE * 2 || size > RELEASE_SIGNATURE_MAXIMUM_SIZE ||
		!release_hex_bytes(text, P2P_SIGNATURE_SIZE, signature))
	{
		return 0;
	}
	for (index = P2P_SIGNATURE_SIZE * 2; index < size; index++)
	{
		if (text[index] != '\n' && text[index] != '\r' && text[index] != ' ' && text[index] != '\t')
			return 0;
	}
	return 1;
}

/* whether SHA256SUMS lists the asset with this SHA-256: its line,
"<64 hex digits>  <name>" (or " *<name>", sha256sum's binary mark), with
\n or \r\n after it. 1: listed so; 0: not listed; -1: listed with another
SHA-256 */
static int release_sums_match(const char *sums, int size, const char *asset, const unsigned char *digest)
{
	int asset_length = (int)strlen(asset);
	int offset = 0;
	int listed = 0;

	while (offset < size)
	{
		const char *line = sums + offset;
		const char *end = memchr(line, '\n', (size_t)(size - offset));
		int length = end ? (int)(end - line) : size - offset;
		unsigned char listed_digest[RELEASE_DIGEST_SIZE];

		offset += length + 1;
		if (length && line[length - 1] == '\r')
			length--;
		if (length != RELEASE_DIGEST_SIZE * 2 + 2 + asset_length || line[RELEASE_DIGEST_SIZE * 2] != ' ' ||
			(line[RELEASE_DIGEST_SIZE * 2 + 1] != ' ' && line[RELEASE_DIGEST_SIZE * 2 + 1] != '*') ||
			memcmp(line + RELEASE_DIGEST_SIZE * 2 + 2, asset, (size_t)asset_length) ||
			!release_hex_bytes(line, RELEASE_DIGEST_SIZE, listed_digest))
		{
			continue;
		}
		/* (every line of the name must agree) */
		if (!p2p_equal(listed_digest, digest, RELEASE_DIGEST_SIZE))
			return -1;
		listed = 1;
	}
	return listed;
}

enum release_verdict release_check_keys(const char *sums, int sums_size, const char *signature, int signature_size,
	const char *asset, const unsigned char *digest, int required, const unsigned char (*keys)[RELEASE_KEY_SIZE],
	int key_count, char *error, int error_size)
{
	enum release_verdict verdict;
	int match;

	release_error(error, error_size, "");
	if (sums && (sums_size <= 0 || sums_size > RELEASE_SUMS_MAXIMUM_SIZE))
	{
		release_error(error, error_size, "its SHA256SUMS is not a list of checksums");
		return _release_refused;
	}
	if (signature)
	{
		unsigned char bytes[P2P_SIGNATURE_SIZE];
		int index;

		if (!sums)
		{
			release_error(error, error_size, "it has a signature, but no SHA256SUMS for it to sign");
			return _release_refused;
		}
		if (!release_signature_read(signature, signature_size, bytes))
		{
			release_error(error, error_size, "its SHA256SUMS.sig is not a signature");
			return _release_refused;
		}
		for (index = 0; index < key_count; index++)
		{
			if (p2p_ed25519_verify(keys[index], sums, sums_size, bytes))
				break;
		}
		if (index == key_count)
		{
			release_error(error, error_size, "its SHA256SUMS is not signed by OpenH1's release key");
			return _release_refused;
		}
		verdict = _release_signed;
	}
	else if (required)
	{
		release_error(error, error_size, "it is not signed (no SHA256SUMS.sig), and this version installs only "
			"signed releases");
		return _release_refused;
	}
	else if (!sums)
	{
		release_error(error, error_size, "it has no SHA256SUMS nor signature: taken on HTTPS alone");
		return _release_unsigned;
	}
	else
	{
		release_error(error, error_size, "it has no signature (SHA256SUMS.sig): its SHA256SUMS checked, on HTTPS "
			"alone");
		verdict = _release_unsigned;
	}
	match = release_sums_match(sums, sums_size, asset, digest);
	if (match <= 0)
	{
		char message[300];

		snprintf(message, sizeof(message), match < 0 ? "the download is not the %s its SHA256SUMS lists" :
			"its SHA256SUMS does not list %s", asset);
		release_error(error, error_size, message);
		return _release_refused;
	}
	return verdict;
}

enum release_verdict release_check(const char *sums, int sums_size, const char *signature, int signature_size,
	const char *asset, const unsigned char *digest, int required, const char *test_key, char *error, int error_size)
{
	unsigned char keys[RELEASE_KEY_COUNT + 1][RELEASE_KEY_SIZE];
	int count = 0, index;

	for (index = 0; index < RELEASE_KEY_COUNT; index++)
	{
		if (strlen(release_keys[index]) == RELEASE_KEY_SIZE * 2 &&
			release_hex_bytes(release_keys[index], RELEASE_KEY_SIZE, keys[count]))
		{
			count++;
		}
	}
	/* (a test's key beside them, when it is one) */
	if (test_key && strlen(test_key) == RELEASE_KEY_SIZE * 2 &&
		release_hex_bytes(test_key, RELEASE_KEY_SIZE, keys[count]))
	{
		count++;
	}
	return release_check_keys(sums, sums_size, signature, signature_size, asset, digest, required,
		(const unsigned char (*)[RELEASE_KEY_SIZE])keys, count, error, error_size);
}
