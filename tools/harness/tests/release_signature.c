/*
RELEASE_SIGNATURE.C (test)

The self-updater's check of a release's download (port/linux/src/
release_signature.c, the whole file: under_test.inc): its SHA256SUMS
signed by the release key, a bad signature, a missing one while signatures
are optional and once they are required, another key's, the download's
SHA-256 against its line, and a signature OpenSSL made (tools/
release_sign.py's) checked as Monocypher's; on the real SHA-256 and Ed25519
(port/linux/src/p2p_crypto.c, Monocypher). test_release_signature.py takes
the code.
*/

#include "harness.h"

#include <stdint.h>

/* ---------- the crypto, as whole files */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
/* (compiled quickly: each negative control builds it again) */
#pragma clang optimize off
#include "monocypher.inc"
/* (both files have a hash_reduce of their own) */
#define hash_reduce ed25519_hash_reduce
#include "monocypher_ed25519.inc"
#undef hash_reduce
#pragma clang optimize on
#pragma clang diagnostic pop
#undef FOR
#undef COPY
#undef ZERO
#undef WIPE_CTX
#undef WIPE_BUFFER
#undef MIN

#include "p2p_crypto.inc"

void posix_random_bytes(void *buffer, posix_ulong size)
{
	memset(buffer, 0, size);
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + index * 2, "%02x", bytes[index]);
}

#include "under_test.inc"
#include "config.inc"

/* ---------- the fake world */

/* the test's release keys (tools/harness/tests/test_release_signature.py:
SHA-256 of a phrase each), and a release's sums */
#define TEST_SEED "8a543fbaacb9aaa064eeb749f9cb3c3fc4c0591525d068c0d96f7472708f1c01"
#define OTHER_SEED "86c556326346f528048138fa6180b750571a9cbaa30338625345b38c7d159682"
#define ASSET "OpenH1-Linux.zip"

static unsigned char test_key[1][RELEASE_KEY_SIZE];
static unsigned char other_key[1][RELEASE_KEY_SIZE];
static unsigned char both_keys[2][RELEASE_KEY_SIZE];
static unsigned char test_seed[32], other_seed[32];
static unsigned char digest[RELEASE_DIGEST_SIZE];
static char sums[1024];
static char signature[200];
static char error[300];

static void from_hex(const char *text, unsigned char *bytes, int size)
{
	int index;

	for (index = 0; index < size; index++)
	{
		unsigned int value;

		sscanf(text + index * 2, "%2x", &value);
		bytes[index] = (unsigned char)value;
	}
}

/* the sums of a release whose ASSET is the bytes "the download", its
signature by seed (with what follows the 128 digits) into signature */
static void release(const unsigned char *seed, const unsigned char *key, const char *after)
{
	char hex[RELEASE_DIGEST_SIZE * 2 + 1];
	unsigned char bytes[P2P_SIGNATURE_SIZE];

	p2p_sha256("the download", 12, digest);
	p2p_hex(digest, RELEASE_DIGEST_SIZE, hex);
	snprintf(sums, sizeof(sums),
		"0000000000000000000000000000000000000000000000000000000000000000  OpenH1-Android.apk\n"
		"%s  " ASSET "\n"
		"1111111111111111111111111111111111111111111111111111111111111111  OpenH1-Windows.zip\n", hex);
	p2p_ed25519_sign(seed, key, sums, (int)strlen(sums), bytes);
	p2p_hex(bytes, P2P_SIGNATURE_SIZE, signature);
	strcat(signature, after);
}

static enum release_verdict check(const char *with_sums, const char *with_signature, int required,
	const unsigned char (*keys)[RELEASE_KEY_SIZE], int key_count)
{
	return release_check_keys(with_sums, with_sums ? (int)strlen(with_sums) : 0, with_signature,
		with_signature ? (int)strlen(with_signature) : 0, ASSET, digest, required, keys, key_count, error,
		sizeof(error));
}

#define TEST_KEYS ((const unsigned char (*)[RELEASE_KEY_SIZE])test_key), 1

static void setup(void)
{
	from_hex(TEST_SEED, test_seed, 32);
	from_hex(OTHER_SEED, other_seed, 32);
	p2p_ed25519_public(test_seed, test_key[0], NULL);
	p2p_ed25519_public(other_seed, other_key[0], NULL);
	memcpy(both_keys[0], test_key[0], RELEASE_KEY_SIZE);
	memcpy(both_keys[1], other_key[0], RELEASE_KEY_SIZE);
	release(test_seed, test_key[0], "\n");
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	setup();
	if (!strcmp(name, "good"))
	{
		/* (whatever the policy) */
		CHECK(check(sums, signature, 0, TEST_KEYS) == _release_signed, "%s", error);
		CHECK(check(sums, signature, 1, TEST_KEYS) == _release_signed, "%s", error);
		release(test_seed, test_key[0], "\r\n");
		CHECK(check(sums, signature, 1, TEST_KEYS) == _release_signed, "with \\r\\n: %s", error);
		release(test_seed, test_key[0], "");
		CHECK(check(sums, signature, 1, TEST_KEYS) == _release_signed, "with no newline: %s", error);
		/* (a key of rotation's: any of the keys) */
		CHECK(check(sums, signature, 1, (const unsigned char (*)[RELEASE_KEY_SIZE])both_keys, 2) == _release_signed,
			"%s", error);
		/* (sha256sum's binary mark, and \r\n lines) */
		{
			char hex[RELEASE_DIGEST_SIZE * 2 + 1];
			char text[1024];
			unsigned char bytes[P2P_SIGNATURE_SIZE];

			p2p_hex(digest, RELEASE_DIGEST_SIZE, hex);
			snprintf(text, sizeof(text), "0000000000000000000000000000000000000000000000000000000000000000  "
				"OpenH1-Android.apk\r\n%s *" ASSET "\r\n", hex);
			p2p_ed25519_sign(test_seed, test_key[0], text, (int)strlen(text), bytes);
			p2p_hex(bytes, P2P_SIGNATURE_SIZE, signature);
			CHECK(check(text, signature, 1, TEST_KEYS) == _release_signed, "binary mark, \\r\\n: %s", error);
		}
		return 0;
	}
	if (!strcmp(name, "bad"))
	{
		char flipped[200];
		char tampered[1024];

		/* a signature with a bit changed */
		strcpy(flipped, signature);
		flipped[10] = flipped[10] == '0' ? '1' : '0';
		CHECK(check(sums, flipped, 0, TEST_KEYS) == _release_refused, "a changed signature taken");
		CHECK(check(sums, flipped, 1, TEST_KEYS) == _release_refused, "a changed signature taken");
		CHECK(strstr(error, "not signed by"), "%s", error);
		/* sums changed after they were signed */
		strcpy(tampered, sums);
		tampered[0] = '2';
		CHECK(check(tampered, signature, 0, TEST_KEYS) == _release_refused, "changed sums taken");
		/* the download not the one signed for */
		digest[0] ^= 1;
		CHECK(check(sums, signature, 0, TEST_KEYS) == _release_refused, "another download taken");
		CHECK(strstr(error, "is not the"), "%s", error);
		return 0;
	}
	if (!strcmp(name, "malformed"))
	{
		char text[300];

		/* short, junk after it, not hex, too long */
		snprintf(text, sizeof(text), "%.126s\n", signature);
		CHECK(check(sums, text, 0, TEST_KEYS) == _release_refused, "a short signature taken");
		snprintf(text, sizeof(text), "%.128s x\n", signature);
		CHECK(check(sums, text, 0, TEST_KEYS) == _release_refused, "a signature with more taken");
		snprintf(text, sizeof(text), "%.128s", signature);
		text[5] = 'g';
		CHECK(check(sums, text, 0, TEST_KEYS) == _release_refused, "a signature not hex taken");
		CHECK(check(sums, "", 0, TEST_KEYS) == _release_refused, "an empty signature taken");
		/* a signature, but no sums */
		CHECK(check(NULL, signature, 0, TEST_KEYS) == _release_refused, "a signature of no sums taken");
		CHECK(strstr(error, "no SHA256SUMS for it"), "%s", error);
		/* sums too large */
		CHECK(release_check_keys(sums, RELEASE_SUMS_MAXIMUM_SIZE + 1, NULL, 0, ASSET, digest, 0, TEST_KEYS, error,
			sizeof(error)) == _release_refused, "sums too large taken");
		return 0;
	}
	if (!strcmp(name, "missing-optional"))
	{
		/* no signature, while signatures are optional: the sums still checked */
		CHECK(check(sums, NULL, 0, TEST_KEYS) == _release_unsigned, "%s", error);
		CHECK(strstr(error, "no signature"), "%s", error);
		/* nor sums: a release from before they were published */
		CHECK(check(NULL, NULL, 0, TEST_KEYS) == _release_unsigned, "%s", error);
		digest[0] ^= 1;
		CHECK(check(sums, NULL, 0, TEST_KEYS) == _release_refused, "an unsigned release's other download taken");
		return 0;
	}
	if (!strcmp(name, "missing-required"))
	{
		CHECK(check(sums, NULL, 1, TEST_KEYS) == _release_refused, "an unsigned release taken");
		CHECK(strstr(error, "not signed"), "%s", error);
		CHECK(check(NULL, NULL, 1, TEST_KEYS) == _release_refused, "a release of no sums taken");
		/* (the built-in check, required, as a build of RELEASE_SIGNATURE_REQUIRED) */
		CHECK(release_check(sums, (int)strlen(sums), NULL, 0, ASSET, digest, 1, NULL, error, sizeof(error)) ==
			_release_refused, "an unsigned release taken");
		return 0;
	}
	if (!strcmp(name, "wrong-key"))
	{
		/* signed by another key */
		release(other_seed, other_key[0], "\n");
		CHECK(check(sums, signature, 0, TEST_KEYS) == _release_refused, "another key's signature taken");
		CHECK(check(sums, signature, 1, TEST_KEYS) == _release_refused, "another key's signature taken");
		/* no key at all */
		CHECK(check(sums, signature, 0, (const unsigned char (*)[RELEASE_KEY_SIZE])test_key, 0) == _release_refused,
			"a signature taken with no key");
		/* the release key built in is not the test's */
		release(test_seed, test_key[0], "\n");
		CHECK(release_check(sums, (int)strlen(sums), signature, (int)strlen(signature), ASSET, digest, 0, NULL, error,
			sizeof(error)) == _release_refused, "the test key taken for the release key");
		/* a test's key given (debug.update_key) is trusted beside it; one that
		is not 64 hex digits is not a key */
		CHECK(release_check(sums, (int)strlen(sums), signature, (int)strlen(signature), ASSET, digest, 1,
			VECTOR_PUBLIC_KEY, error, sizeof(error)) == _release_signed, "%s", error);
		CHECK(release_check(sums, (int)strlen(sums), signature, (int)strlen(signature), ASSET, digest, 1,
			VECTOR_PUBLIC_KEY "0", error, sizeof(error)) == _release_refused, "a key of 65 digits taken");
		CHECK(release_check(sums, (int)strlen(sums), signature, (int)strlen(signature), ASSET, digest, 1,
			OTHER_SEED, error, sizeof(error)) == _release_refused, "another key given taken for the test's");
		return 0;
	}
	if (!strcmp(name, "listing"))
	{
		char text[1024];
		unsigned char bytes[P2P_SIGNATURE_SIZE];

		/* only the name's own line: not a longer name's, nor one that ends with it */
		snprintf(text, sizeof(text), "%.64s  " ASSET ".old\n%.64s  x" ASSET "\n", strstr(sums, "\n") + 1,
			strstr(sums, "\n") + 1);
		p2p_ed25519_sign(test_seed, test_key[0], text, (int)strlen(text), bytes);
		p2p_hex(bytes, P2P_SIGNATURE_SIZE, signature);
		CHECK(check(text, signature, 0, TEST_KEYS) == _release_refused, "another name's line taken");
		CHECK(strstr(error, "does not list"), "%s", error);
		/* two lines of the name that disagree */
		snprintf(text, sizeof(text), "%s2222222222222222222222222222222222222222222222222222222222222222  " ASSET "\n",
			sums);
		p2p_ed25519_sign(test_seed, test_key[0], text, (int)strlen(text), bytes);
		p2p_hex(bytes, P2P_SIGNATURE_SIZE, signature);
		CHECK(check(text, signature, 0, TEST_KEYS) == _release_refused, "a second line of the name taken");
		return 0;
	}
	if (!strcmp(name, "openssl"))
	{
		/* a signature tools/release_sign.py made with OpenSSL, of these sums,
		by the test key: config.inc's */
		static const char vector_sums[] =
			"0000000000000000000000000000000000000000000000000000000000000000  OpenH1-Android.apk\n"
			"1111111111111111111111111111111111111111111111111111111111111111  OpenH1-Linux.zip\n";
		unsigned char key[1][RELEASE_KEY_SIZE];

		from_hex(VECTOR_PUBLIC_KEY, key[0], RELEASE_KEY_SIZE);
		CHECK(!memcmp(key[0], test_key[0], RELEASE_KEY_SIZE), "OpenSSL's public key of the seed is not Monocypher's");
		memset(digest, 0x11, sizeof(digest));
		CHECK(check(vector_sums, VECTOR_SIGNATURE "\n", 1, (const unsigned char (*)[RELEASE_KEY_SIZE])key, 1) ==
			_release_signed, "%s", error);
		return 0;
	}
	fprintf(stderr, "no case %s\n", name);
	return 2;
}
