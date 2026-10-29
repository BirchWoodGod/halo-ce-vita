/*
VITA_UPDATE_VERIFY.H

What decides whether a downloaded update is kept for the player to install
(the Vita's updater: vita_update.c, vita_update_download.c), with no system
in it, so the tests and the fuzzer build it alone.

A release that the game may download carries three assets beside each other
(https://github.com/BirchWoodGod/halo-ce-vita/releases/download/v<version>/):

  halo.vpk             the game, as VitaShell installs it
  halo-update.txt      the manifest: which version, which file, its size and
                       SHA-512, which title (UPDATE_MANIFEST_* below)
  halo-update.txt.sig  an Ed25519 signature of halo-update.txt's exact bytes,
                       128 hexadecimal digits (and a newline)

The game holds one Ed25519 public key (vita_update_key.h). It downloads the
manifest and its signature, checks the signature with that key, then that
the manifest names the version it asked for (the tag's: an old manifest
replayed under a newer tag is refused), that this version is newer than the
running build (no downgrade), its own title and halo.vpk, and a size it
takes; only then is halo.vpk downloaded (to that size at most), and it is
kept (for the player to install with VitaShell) only if its SHA-512 is the
manifest's. The private key never leaves the owner's machine
(tools/release/README.md).

The signature is checked before anything of the manifest is read; the
manifest is strict text (each line in its place, nothing more) read by
length; every buffer here has a fixed size. Only integers, strings and
structures of them cross to the callers, whatever their ABI.
*/

#ifndef VITA_UPDATE_VERIFY_H
#define VITA_UPDATE_VERIFY_H

#include <stddef.h>

/* the assets, the release's download folder and the repository's host. The
VPK is the release's halo-ce-vita-<version>-vita.vpk (UPDATE_ASSET_VPK_PREFIX
<version> UPDATE_ASSET_VPK_SUFFIX: update_asset_vpk_name), beside the PC
builds' halo-ce-vita-<version>-pc-windows.zip and -pc-linux.tar.gz; the
manifest names which file it signs, and the name of the releases before
1.1.1, halo.vpk (UPDATE_ASSET_VPK), is taken too, which builds before 1.1.1
ask for alone (tools/release/README.md) */
#define UPDATE_ASSET_VPK "halo.vpk"
#define UPDATE_ASSET_VPK_PREFIX "halo-ce-vita-"
#define UPDATE_ASSET_VPK_SUFFIX "-vita.vpk"
#define UPDATE_ASSET_MANIFEST "halo-update.txt"
#define UPDATE_ASSET_SIGNATURE "halo-update.txt.sig"
#define UPDATE_DOWNLOAD_BASE "https://github.com/BirchWoodGod/halo-ce-vita/releases/download"
/* the manifest's first line */
#define UPDATE_MANIFEST_MAGIC "halo-ce-vita-update 1"
/* the manifest and the signature file, at most */
#define UPDATE_MANIFEST_MAXIMUM 1024
#define UPDATE_SIGNATURE_FILE_MAXIMUM 256
/* the largest halo.vpk taken (1.1.0-beta.3's was 4.5 MB) */
#define UPDATE_VPK_MAXIMUM (64UL * 1024 * 1024)

struct update_manifest
{
	char version[64];
	char title[16];
	char file[96];
	unsigned long size;
	unsigned char sha512[64];
};

enum
{
	UPDATE_VERIFY_OK = 0,
	/* this build holds the placeholder key: it installs nothing */
	UPDATE_VERIFY_NO_KEY = -1,
	UPDATE_VERIFY_BAD_SIGNATURE = -2,
	UPDATE_VERIFY_MALFORMED = -3,
	UPDATE_VERIFY_WRONG_VERSION = -4,
	UPDATE_VERIFY_NOT_NEWER = -5,
	UPDATE_VERIFY_WRONG_TITLE = -6,
	UPDATE_VERIFY_WRONG_FILE = -7,
	UPDATE_VERIFY_TOO_LARGE = -8,
};

/* exactly 2 * size hexadecimal digits (either case) at text into bytes:
1, else 0 */
int update_hex_decode(const char *text, size_t length, unsigned char *bytes, size_t size);

/* the public key this build holds (vita_update_key.h, or the tests'
HALO_UPDATE_PUBLIC_KEY): 1, else 0 (the placeholder: no update key in this
build) */
int update_public_key(unsigned char key[32]);

/* a signature file's text (128 hexadecimal digits, then nothing or one
newline, "\n" or "\r\n"): 1, else 0 */
int update_signature_parse(const char *text, size_t size, unsigned char signature[64]);

/* the manifest's text, exactly

	halo-ce-vita-update 1\n
	version <x.y.z, x.y.z-beta.n or x.y.z-rc.n: a tag without its v>\n
	title <9 capitals or digits>\n
	file halo.vpk\n
	size <1-9 digits>\n
	sha512 <128 lowercase hexadecimal digits>\n

(nothing before, between or after): 1, else 0 */
/* the release's VPK of a version, halo-ce-vita-<version>-vita.vpk, into
name: 1, else 0 (no room) */
int update_asset_vpk_name(const char *version, char *name, size_t size);
int update_manifest_parse(const char *text, size_t size, struct update_manifest *manifest);

/* the whole check of a downloaded manifest and its signature file with the
key given: the signature, then the manifest's form, its version (exactly
expected_version, and newer than current_version), its title, its file and
its size; UPDATE_VERIFY_OK with manifest filled, else a code and why in
error (manifest then zeroed) */
int update_manifest_check(const char *text, size_t size, const char *signature_text, size_t signature_size,
	const unsigned char key[32], const char *expected_version, const char *current_version, const char *title,
	struct update_manifest *manifest, char *error, int error_size);

/* an asset's URL: base (UPDATE_DOWNLOAD_BASE; another repository's
https://github.com/<owner>/<repository>/releases/download, for a test
build's hardware test, whose files still need that build's key; or the
tests' loopback http://127.x.x.x...) /v<version>/<asset>; 1, else 0 (a
version that is not a release tag's, an asset not one of the three, a base
of none of those forms, or too long) */
int update_asset_url(char *url, int size, const char *base, const char *version, const char *asset);

/* whether a download that asked from_url may follow a redirect to to_url:
https:// to GitHub's release-asset hosts (release-assets.githubusercontent.com,
objects.githubusercontent.com) on the default port, nothing else; or, for the
tests, from a loopback http://127.x.x.x URL to another one */
int update_redirect_allowed(const char *from_url, const char *to_url);

#endif
