/*
RELEASE_SIGNATURE.H

A release's download checked before the self-updater installs it
(updater.c; release_signature.c). Each release has SHA256SUMS, the SHA-256
of each of its files ("<64 hex digits>  <name>", as sha256sum writes it),
and, once the release key's secret is in the workflow, SHA256SUMS.sig: the
Ed25519 signature of SHA256SUMS's bytes by OpenH1's release key, as 128 hex
digits (tools/release_sign.py; the public key, port/assets/release-key.pub,
is built in here). The scheme is OpenCE's (its deploy/release-key.pub and
tools/release_sign.sh), with OpenH1's key.

- A signature that is there must be the release key's, and the download's
  SHA-256 must be SHA256SUMS's for its name: otherwise the release is
  refused, whatever RELEASE_SIGNATURE_REQUIRED says.
- A release with no signature is refused while RELEASE_SIGNATURE_REQUIRED
  is 1, as it is from the first signed release on. A build of 0 takes one
  on GitHub's HTTPS alone, with its download still checked against its
  SHA256SUMS when it has one, as the builds before it did.
*/

#ifndef __HALO_LINUX_RELEASE_SIGNATURE_H
#define __HALO_LINUX_RELEASE_SIGNATURE_H

/* whether a release without SHA256SUMS.sig is refused: 1, as every release
is signed (the workflow's OPENH1_RELEASE_KEY secret). A build given
-DRELEASE_SIGNATURE_REQUIRED=0 takes unsigned ones */
#ifndef RELEASE_SIGNATURE_REQUIRED
#define RELEASE_SIGNATURE_REQUIRED 1
#endif

enum
{
	RELEASE_KEY_SIZE = 32,
	RELEASE_DIGEST_SIZE = 32,
	/* the most SHA256SUMS and SHA256SUMS.sig may be */
	RELEASE_SUMS_MAXIMUM_SIZE = 64 * 1024,
	RELEASE_SIGNATURE_MAXIMUM_SIZE = 1024,
};

enum release_verdict
{
	/* not to be installed: the error says why */
	_release_refused,
	/* SHA256SUMS signed by the release key, and the download's SHA-256 its */
	_release_signed,
	/* no signature, taken as signatures are not required (the download
	checked against SHA256SUMS when the release has one): the error says
	what it lacked */
	_release_unsigned,
};

/* the verdict on a release's download: sums, its SHA256SUMS (NULL: the
release has none), signature, its SHA256SUMS.sig (NULL: none), asset, the
download's name in them, digest, the download's SHA-256; required,
RELEASE_SIGNATURE_REQUIRED (a test's own); test_key, a test's public key
(64 hex digits: debug.update_key) trusted beside the release key, or NULL.
With the release key built in */
enum release_verdict release_check(const char *sums, int sums_size, const char *signature, int signature_size,
	const char *asset, const unsigned char *digest, int required, const char *test_key, char *error, int error_size);
/* ... with the keys given (a test's), any of which may have signed */
enum release_verdict release_check_keys(const char *sums, int sums_size, const char *signature, int signature_size,
	const char *asset, const unsigned char *digest, int required, const unsigned char (*keys)[RELEASE_KEY_SIZE],
	int key_count, char *error, int error_size);

#endif
