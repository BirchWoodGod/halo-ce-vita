#!/bin/sh
# The Vita updater's tests (port/vita/include/vita_update_verify.h,
# vita_update_install.h), each built 32-bit as the Vita and the Linux game
# are, with AddressSanitizer and UBSan, with a throwaway signing key made
# here by tools/release/make_update_key.py (never a real one):
#   update_install_test.c     the signature, manifest, version, title, size,
#                             URL, redirect, name and param.sfo checks; again
#                             built with the placeholder key (refuses all)
#   update_install_fuzz.c     every truncation and UPDATE_FUZZ_ITERATIONS
#                             (default 30000) changes of a signed manifest,
#                             random inputs to every reader; with
#                             UPDATE_FUZZ_SECONDS=n also n seconds of
#                             libFuzzer (64-bit; clang)
#   update_install_harness.c  the whole path (download, checks, unpacking,
#                             install, undo) against update_install_server.py
#                             on 127.0.0.1, over plain HTTP and over TLS with
#                             an authority made here (built into this test
#                             only: POSIX_HTTPS_TEST_ROOTS), releases signed
#                             by tools/release/sign_update.py (so the Python
#                             signer and Monocypher's check agree), folders
#                             standing for ux0:data/haloce-vita and
#                             ux0:app/HCEV00001; the data folder is checked
#                             untouched and the game's folder as it was after
#                             every refusal
#   CC  the compiler (default gcc)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/update_install_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
cc=${CC:-gcc}
mkdir -p "$out"
server_pids=
cleanup() {
	rc=$?
	for pid in $server_pids; do kill "$pid" 2>/dev/null || true; done
	test_out_done $rc
}
trap cleanup EXIT
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
mono=$root/port/third_party/monocypher
zlib=$root/port/third_party/zlib
flags="-m32 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -I$root/port/vita/include -I$root/port/linux/src -I$mono"
export ASAN_OPTIONS=detect_leaks=1:exitcode=99 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=98
status=0
fail() { echo "FAIL $*"; status=1; }
pass() { echo "PASS $*"; }

# ---------- the keys (throwaway, in the test's folder)
python3 "$root/tools/release/make_update_key.py" --private "$out/keys/test.key" > /dev/null
python3 "$root/tools/release/make_update_key.py" --private "$out/keys/other.key" > /dev/null
seed=$(grep -v '^#' "$out/keys/test.key")
public=$(sed -n 's/^# public key: //p' "$out/keys/test.key")
printf '#define HALO_UPDATE_PUBLIC_KEY "%s"\n#define HALO_TEST_SEED "%s"\n' "$public" "$seed" > "$out/test_key.h"
keyflags="-include $out/test_key.h"

echo "--- update_install_test"
verify_sources="$root/port/vita/host/vita_update_verify.c $root/port/linux/src/update_check.c $mono/monocypher.c $mono/monocypher-ed25519.c"
$cc $flags $sanitize $keyflags "$here/update_install_test.c" $verify_sources -o "$out/update_install_test"
"$out/update_install_test" || status=1
echo "--- update_install_test (placeholder key)"
$cc $flags $sanitize -DHALO_TEST_PLACEHOLDER "$here/update_install_test.c" $verify_sources -o "$out/update_install_placeholder"
"$out/update_install_placeholder" || status=1

echo "--- update_install_fuzz"
$cc $flags $sanitize $keyflags "$here/update_install_fuzz.c" $verify_sources -o "$out/update_install_fuzz"
"$out/update_install_fuzz" || status=1
seconds=${UPDATE_FUZZ_SECONDS:-0}
if [ "$seconds" != 0 ]; then
	found=${UPDATE_FUZZ_OUT:-$out/found}
	mkdir -p "$found/corpus"
	clang -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -DUPDATE_FUZZ_LIBFUZZER -g -O1 \
		-I"$root/port/vita/include" -I"$root/port/linux/src" -I"$mono" $keyflags "$here/update_install_fuzz.c" \
		$verify_sources -o "$out/update_install_libfuzzer"
	"$out/update_install_libfuzzer" -max_total_time="$seconds" -max_len=4096 -print_final_stats=1 \
		-artifact_prefix="$found/" "$found/corpus" > "$found/fuzz.log" 2>&1 || status=1
	grep -E 'stat::number_of_executed_units|cov:.*ft:' "$found/fuzz.log" | tail -2 || true
	ls "$found" | grep -E '^(crash|leak|timeout|oom)-' && status=1
fi

echo "--- update_install_harness (build)"
mkdir -p "$out/mbedtls" "$out/zlib"
ls "$root"/port/third_party/mbedtls/library/*.c | xargs -P "$(nproc)" -I{} sh -c \
	'$0 -m32 -O1 -w -I"$1/include" -I"$1/library" -c "{}" -o "$2/$(basename "{}" .c).o"' \
	"$cc" "$root/port/third_party/mbedtls" "$out/mbedtls"
for name in adler32 crc32 inffast inflate inftrees uncompr zutil; do
	$cc -m32 -O1 -w -DZ_PREFIX -Dz_errmsg=z_port_errmsg -c "$zlib/$name.c" -o "$out/zlib/$name.o"
done
# (the tests' authority, and localhost's certificate from it; a second
# certificate of its own, from no authority)
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=Halo Update Test CA" \
	-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" \
	-keyout "$out/ca_key.pem" -out "$out/ca.pem" 2>/dev/null
openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -subj "/CN=localhost" \
	-keyout "$out/tls_key.pem" -out "$out/tls.csr" 2>/dev/null
printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n' > "$out/tls.ext"
openssl x509 -req -in "$out/tls.csr" -CA "$out/ca.pem" -CAkey "$out/ca_key.pem" -CAcreateserial -days 2 \
	-extfile "$out/tls.ext" -out "$out/tls.pem" 2>/dev/null
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=localhost" \
	-addext "subjectAltName=DNS:localhost" -keyout "$out/self_key.pem" -out "$out/self.pem" 2>/dev/null
python3 - "$out/ca.pem" "$out/test_roots.h" <<'EOF'
import sys
lines = open(sys.argv[1]).read().strip().splitlines()
with open(sys.argv[2], "w") as out:
    out.write("static const char posix_https_test_roots_pem[] =\n")
    for line in lines:
        out.write('\t"%s\\n"\n' % line)
    out.write(";\n")
EOF
harness_sources="$here/update_install_harness.c $root/port/vita/host/vita_update_install.c $root/port/linux/src/posix_https.c
	$root/port/linux/src/posix_net.c $verify_sources"
printf '#define POSIX_HTTPS_TEST_ROOTS "%s"\n' "$out/test_roots.h" > "$out/test_roots_define.h"
$cc $flags $sanitize $keyflags -include "$out/test_roots_define.h" -DUPDATE_INSTALL_TEST_HOOKS \
	-I"$root/port/third_party/mbedtls/include" -I"$zlib" $harness_sources "$out"/zlib/*.o "$out"/mbedtls/*.o -lpthread \
	-o "$out/harness"
$cc $flags $sanitize -DUPDATE_INSTALL_TEST_HOOKS -I"$root/port/third_party/mbedtls/include" -I"$zlib" $harness_sources \
	"$out"/zlib/*.o "$out"/mbedtls/*.o -lpthread -o "$out/harness_placeholder"

# ---------- the releases (tools/release/sign_update.py, as the owner signs)
mkdir -p "$out/releases" "$out/templates"
python3 - "$root" "$out" <<'EOF'
import hashlib, os, shutil, struct, subprocess, sys, zipfile
root, out = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(root, "tools/release"))
import update_signing as common

def sfo(title):
    keys = [b"APP_VER", b"TITLE", b"TITLE_ID"]
    values = [b"01.10", b"Halo CE", title.encode()]
    key_table = 20 + 16 * len(keys)
    key_blob = b"".join(k + b"\0" for k in keys)
    key_blob += b"\0" * (-len(key_blob) % 4)
    data_table = key_table + len(key_blob)
    entries, data, key_offset = b"", b"", 0
    for key, value in zip(keys, values):
        value += b"\0"
        size = (len(value) + 3) & ~3
        entries += struct.pack("<HHIII", key_offset, 0x0204, len(value), size, len(data))
        data += value + b"\0" * (size - len(value))
        key_offset += len(key) + 1
    return struct.pack("<4sIIII", b"\0PSF", 0x101, key_table, data_table, len(keys)) + entries + key_blob + data

def vpk(path, version, title="HCEV00001", extra=()):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("sce_sys/param.sfo", sfo(title))
        archive.writestr("eboot.bin", ("NEW EBOOT " + version + "\n").encode() * 3000)
        archive.writestr(zipfile.ZipInfo("sce_sys/livearea/"), b"")
        archive.writestr("sce_sys/livearea/contents/template.xml", b"<livearea/>\n")
        archive.writestr("lang/es.txt", ("es " + version + "\n").encode() * 500)
        info = zipfile.ZipInfo("shaders.pak")
        info.compress_type = zipfile.ZIP_STORED
        archive.writestr(info, os.urandom(70000))
        for name, data in extra:
            archive.writestr(zipfile.ZipInfo(name), data)

def sign(folder, version, key="test.key"):
    subprocess.run([sys.executable, os.path.join(root, "tools/release/sign_update.py"), "--key",
                    os.path.join(out, "keys", key), "--vpk", os.path.join(folder, "halo.vpk"), "--version", version,
                    "--out", folder], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def release(version, key="test.key", title="HCEV00001", extra=()):
    folder = os.path.join(out, "releases", "v" + version)
    os.makedirs(folder)
    vpk(os.path.join(folder, "halo.vpk"), version, title, extra)
    sign(folder, version, key)
    return folder

def sign_text(folder, text):
    seed = common.read_private_key(os.path.join(out, "keys", "test.key"))
    open(os.path.join(folder, "halo-update.txt"), "wb").write(text.encode())
    open(os.path.join(folder, "halo-update.txt.sig"), "w").write(common.ed25519.sign(seed, text.encode()).hex() + "\n")

good = release("1.1.0-beta.4")
release("1.1.0-beta.2")                                  # older than the running build
replay = os.path.join(out, "releases", "v1.1.0-beta.5")  # beta.4's files under another tag
shutil.copytree(good, replay)
tampered = release("1.1.0-beta.6")                       # the VPK changed after signing
data = bytearray(open(os.path.join(tampered, "halo.vpk"), "rb").read())
data[len(data) // 2] ^= 1
open(os.path.join(tampered, "halo.vpk"), "wb").write(data)
release("1.1.0-beta.7", key="other.key")                 # another key's signature
changed = release("1.1.0-beta.8")                        # the manifest changed after signing
text = open(os.path.join(changed, "halo-update.txt")).read()
open(os.path.join(changed, "halo-update.txt"), "w").write(text.replace("title HCEV00001", "title HCEV00002"))
release("1.1.0-beta.9", extra=[("../escape.txt", b"out")])  # a name leaving the folder
release("1.1.0-beta.10", extra=[("SCE_SYS/Package/work.bin", b"license")])  # a system file (in another case)
big = release("1.1.0-beta.11")                           # signed, but 70 MB
size, sha = common.file_digest(os.path.join(big, "halo.vpk"))
sign_text(big, common.manifest_text("1.1.0-beta.11", "HCEV00001", 70000000, sha))
release("1.1.0-beta.12", title="OTHER0001")              # another game's VPK, signed
nosig = release("1.1.0-beta.13")                         # no signature published
os.remove(os.path.join(nosig, "halo-update.txt.sig"))
release("1.1.0-beta.14", extra=[("menus2/x.xml", b"<x/>")])  # (the commit failure's)
lying = release("1.1.0-beta.15")                         # the manifest's SHA-512 is another file's
size, sha = common.file_digest(os.path.join(lying, "halo.vpk"))
sign_text(lying, common.manifest_text("1.1.0-beta.15", "HCEV00001", size, "0" * 128))

# the game's folder and the data folder as a Vita has them
app = os.path.join(out, "templates", "app")
for name, data in [("eboot.bin", b"OLD EBOOT\n" * 1000), ("sce_sys/param.sfo", sfo("HCEV00001")),
                   ("lang/es.txt", b"es old\n"), ("oldonly.txt", b"a file the new VPK lacks\n"),
                   ("shaders.pak", b"old shaders"), ("sce_sys/package/work.bin", b"the install's license"),
                   ("sce_sys/livearea/contents/template.xml", b"<old/>")]:
    os.makedirs(os.path.dirname(os.path.join(app, name)), exist_ok=True)
    open(os.path.join(app, name), "wb").write(data)
data_folder = os.path.join(out, "templates", "data")
for name, data in [("settings.txt", b"HALO_UPDATE_CHANNEL=experimental\n"), ("config.toml", b"[game]\n"),
                   ("maps/bloodgulch.map", os.urandom(4096)), ("saves/campaign.sav", b"save"),
                   ("movies/intro.mp4", b"movie"), ("update_check.txt", b"cache")]:
    os.makedirs(os.path.dirname(os.path.join(data_folder, name)), exist_ok=True)
    open(os.path.join(data_folder, name), "wb").write(data)
EOF

python3 "$here/update_install_server.py" "$out/releases" "$out/port" "$out/requests" & server_pids="$server_pids $!"
python3 "$here/update_install_server.py" "$out/releases" "$out/tls_port" "$out/tls_requests" --tls "$out/tls.pem" \
	"$out/tls_key.pem" & server_pids="$server_pids $!"
python3 "$here/update_install_server.py" "$out/releases" "$out/self_port" "$out/self_requests" --tls "$out/self.pem" \
	"$out/self_key.pem" & server_pids="$server_pids $!"
for i in $(seq 50); do [ -s "$out/port" ] && [ -s "$out/tls_port" ] && [ -s "$out/self_port" ] && break; sleep 0.1; done
http="http://127.0.0.1:$(cat "$out/port")"
https="https://localhost:$(cat "$out/tls_port")"
self="https://localhost:$(cat "$out/self_port")"

# ---------- scenarios
# tree DIR: one line per file, its path and SHA-256 (the updater's own
# folder left out)
tree() { (cd "$1" && find . -type f ! -path './update/*' | LC_ALL=C sort | xargs -r sha256sum); }
app_before=$(tree "$out/templates/app")
data_before=$(tree "$out/templates/data")
# scenario NAME: fresh copies of the folders; $data, $app
scenario() {
	data=$out/s/$1/data
	app=$out/s/$1/app
	mkdir -p "$out/s/$1"
	cp -r "$out/templates/data" "$data"
	cp -r "$out/templates/app" "$app"
}
# run ACTION BASE VERSION [more]: the harness's result line in $result, all of it in $log
run() {
	action=$1 base=$2 version=$3
	shift 3
	log=$("$out/harness" "$action" "$data" "$app" "$base" "$version" "$@" 2>&1) || { echo "$log"; fail "$action crashed"; }
	result=$(printf '%s\n' "$log" | sed -n 's/^result //p' | tail -1)
}
expect() { # CODE WHAT [text in the result]
	case "$result" in
	"$1"*) if [ -n "$3" ] && ! printf '%s' "$result" | grep -q -- "$3"; then echo "$log"; fail "$2: $result"; else pass "$2 ($result)"; fi ;;
	*) echo "$log"; fail "$2: $result" ;;
	esac
}
unchanged() { # WHAT: the game's and the data folder as they were, nothing staged left
	[ "$(tree "$app")" = "$app_before" ] || { tree "$app"; fail "$1: the game's folder changed"; }
	[ "$(tree "$data")" = "$data_before" ] || fail "$1: the data folder changed"
	[ ! -e "$data/update/stage" ] && [ ! -e "$data/update/backup" ] && [ ! -e "$data/update/journal.txt" ] ||
		fail "$1: staging, backup or journal left"
	[ ! -e "$app/.halo_update_probe" ] || fail "$1: the probe file left"
}
no_download() { [ ! -e "$data/update/halo.vpk.part" ] || fail "$1: the download left"; }

echo "--- update_install_harness"
scenario good
run install "$http/dl" 1.1.0-beta.4
expect "0 " "a signed update installs"
grep -q "NEW EBOOT 1.1.0-beta.4" "$app/eboot.bin" || fail "the new eboot.bin is not in place"
grep -q "es 1.1.0-beta.4" "$app/lang/es.txt" || fail "the new lang/es.txt is not in place"
[ -f "$app/oldonly.txt" ] && [ "$(cat "$app/sce_sys/package/work.bin")" = "the install's license" ] ||
	fail "a file the VPK lacks, or the install's own, was touched"
[ "$(tree "$data")" = "$data_before" ] || fail "the install changed the data folder"
[ -z "$(ls -A "$data/update")" ] || { ls -R "$data/update"; fail "the install left files in update/"; }
unzip -p "$out/releases/v1.1.0-beta.4/halo.vpk" shaders.pak | cmp -s - "$app/shaders.pak" || fail "shaders.pak differs"
printf '%s\n' "$log" | grep -q "signed by the update key" || fail "no log line of the signature"

scenario redirect
run install "$http/redirect2" 1.1.0-beta.4
expect "0 " "followed loopback redirects (two hops) install"
scenario evil
run install "$http/evil" 1.1.0-beta.4
expect "-1 " "a redirect to another host refused" "not allowed (evil.example)"
unchanged "evil redirect"

scenario older
run install "$http/dl" 1.1.0-beta.2
expect "-1 " "an older signed version refused (no downgrade)" "not newer"
unchanged older; no_download older
scenario replay
run install "$http/dl" 1.1.0-beta.5
expect "-1 " "a manifest replayed under a newer tag refused" "not 1.1.0-beta.5"
unchanged replay
scenario tampered
run install "$http/dl" 1.1.0-beta.6
expect "-1 " "a VPK changed after signing refused" "not the file the manifest signs"
unchanged tampered; no_download tampered
scenario lying
run install "$http/dl" 1.1.0-beta.15
expect "-1 " "a VPK whose SHA-512 is not the manifest's refused" "not the file the manifest signs"
unchanged lying; no_download lying
scenario wrongkey
run install "$http/dl" 1.1.0-beta.7
expect "-1 " "another key's signature refused" "not the update key"
unchanged wrongkey
scenario changed
run install "$http/dl" 1.1.0-beta.8
expect "-1 " "a manifest changed after signing refused" "not the update key"
unchanged changed
scenario escape
run install "$http/dl" 1.1.0-beta.9
expect "-1 " "a VPK name leaving the folder refused" "name not taken"
unchanged escape
[ ! -e "$out/s/escape/escape.txt" ] && [ ! -e "$data/escape.txt" ] && [ ! -e "$data/update/escape.txt" ] ||
	fail "a file was written outside the staging folder"
scenario system
run install "$http/dl" 1.1.0-beta.10
expect "-1 " "a VPK writing sce_sys/package (in any case) refused" "name not taken"
unchanged system
scenario big
run install "$http/dl" 1.1.0-beta.11
expect "-1 " "a signed manifest over 64 MB refused" "not taken"
unchanged big
grep -q "v1.1.0-beta.11/halo.vpk" "$out/requests" && fail "the 70 MB VPK was asked for"
scenario title
run install "$http/dl" 1.1.0-beta.12
expect "-1 " "another game's signed VPK refused" "title OTHER0001"
unchanged title
scenario nosig
run install "$http/dl" 1.1.0-beta.13
expect "-1 " "a release without a signature refused" "answered 404"
unchanged nosig
scenario offline
run install "http://127.0.0.1:1/dl" 1.1.0-beta.4
expect "-6 " "no server: no connection" "cannot connect"
unchanged offline
scenario notag
run install "$http/dl" 1.1.0-rc.1
expect "-1 " "a version that is no tag refused"
unchanged notag

scenario readonly
chmod 555 "$app"
run install "$http/dl" 1.1.0-beta.4
chmod 755 "$app"
expect "-4 " "a game's folder it cannot write: no access (unsafe homebrew)"
unchanged readonly
scenario nospace
run nospace "$http/dl" 1.1.0-beta.4
expect "-5 " "too little space refused" "MB free"
unchanged nospace; no_download nospace
scenario cancel
run cancel "$http/dl" 1.1.0-beta.4
expect "-2 " "stopped during the download"
unchanged cancel; no_download cancel
scenario placeholder
log=$("$out/harness_placeholder" install "$data" "$app" "$http/dl" 1.1.0-beta.4 2>&1)
result=$(printf '%s\n' "$log" | sed -n 's/^result //p')
expect "-3 " "the placeholder key's build installs nothing" "no update key in this build"
unchanged placeholder

scenario resume
run install "$http/cut" 1.1.0-beta.4
expect "0 " "a download cut short resumes and installs"
grep "/cut/v1.1.0-beta.4/halo.vpk range=bytes=" "$out/requests" > /dev/null || fail "the second request had no Range"
scenario twice
run prepare "$http/dl" 1.1.0-beta.4
expect "0 prepared" "prepared, then put off (the download kept)"
[ -f "$data/update/halo.vpk.part" ] || fail "the download was not kept"
unchanged "put off"
before=$(grep -c "/dl/v1.1.0-beta.4/halo.vpk " "$out/requests")
run install "$http/dl" 1.1.0-beta.4
expect "0 " "the kept download installs without downloading again"
[ "$(grep -c "/dl/v1.1.0-beta.4/halo.vpk " "$out/requests")" = "$before" ] || fail "the VPK was downloaded again"

scenario tamper
run tamper "$http/dl" 1.1.0-beta.4
expect "-1 " "a staged file changed after its check refused" "changed after it was checked"
unchanged tamper
scenario commitfail
printf 'a file where the VPK has a folder' > "$app/menus2"
app_saved=$app_before
app_before=$(tree "$app")
run install "$http/dl" 1.1.0-beta.14
expect "-1 " "an install failing midway" "cannot"
printf '%s\n' "$log" | grep -q "as they were" || fail "no log line of the undo"
unchanged "failed install undone"
app_before=$app_saved
scenario powercut
run powercut "$http/dl" 1.1.0-beta.4 3
expect "-1 " "an install stopped after 3 moves (a power cut)"
[ -f "$data/update/journal.txt" ] || fail "no journal after the power cut"
[ "$(tree "$app")" != "$app_before" ] || fail "the power cut moved nothing (the test tests nothing)"
run recover "$http/dl" 1.1.0-beta.4
expect "3" "the next start puts the 3 files back"
unchanged "power cut undone"
run recover "$http/dl" 1.1.0-beta.4
expect "0" "nothing to undo after that"
scenario lateeboot
run powercut "$http/dl" 1.1.0-beta.4 5
grep -q "NEW EBOOT" "$app/eboot.bin" || fail "eboot.bin is not the last file moved"
run recover "$http/dl" 1.1.0-beta.4
expect "5" "after all moves but the clean-up: put back"
unchanged "late power cut undone"

# ---------- the download alone: TLS, redirects, framing
d=$out/s/download
mkdir -p "$d"
data=$d app=$d
run download x x "$https/dl/v1.1.0-beta.4/halo.vpk" "$d/tls.vpk" 0
expect "1 " "over TLS with a certificate of the tests' authority"
cmp -s "$d/tls.vpk" "$out/releases/v1.1.0-beta.4/halo.vpk" || fail "the TLS download differs"
run download x x "$self/dl/v1.1.0-beta.4/halo.vpk" "$d/self.vpk" 0
expect "0 " "a certificate of no authority refused" "certificate refused"
[ -s "$out/self_requests" ] && fail "a request reached the server whose certificate was refused"
run download x x "$https/redirect/v1.1.0-beta.4/halo.vpk" "$d/r.vpk" 0
expect "0 " "https redirected to plain http refused" "not allowed"
run download x x "$https/evil/v1.1.0-beta.4/halo.vpk" "$d/e.vpk" 0
expect "0 " "https redirected to another host refused" "not allowed (evil.example)"
run download x x "$http/chunked/v1.1.0-beta.4/halo.vpk" "$d/c.vpk" 0
expect "0 " "a chunked download refused" "chunked"
run download x x "$http/big/v1.1.0-beta.4/halo.vpk" "$d/b.vpk" 0
expect "0 " "over 64 MB refused before the body" "too large"
size=$(stat -c %s "$out/releases/v1.1.0-beta.4/halo.vpk")
run download x x "$http/dl/v1.1.0-beta.4/halo.vpk" "$d/s.vpk" 1000
expect "0 " "another size than the signed one refused" "not 1000"
head -c 1000 "$out/releases/v1.1.0-beta.4/halo.vpk" > "$d/w.vpk"
run download x x "$http/wrong-range/v1.1.0-beta.4/halo.vpk" "$d/w.vpk" "$size"
expect "0 " "a resume answered with another part refused" "another part"
head -c 1000 "$out/releases/v1.1.0-beta.4/halo.vpk" > "$d/ok.vpk"
run download x x "$http/dl/v1.1.0-beta.4/halo.vpk" "$d/ok.vpk" "$size"
expect "1 " "a partial file resumed"
cmp -s "$d/ok.vpk" "$out/releases/v1.1.0-beta.4/halo.vpk" || fail "the resumed file differs"

if grep -v "agent=HaloCEVita/test$" "$out/requests" "$out/tls_requests" | grep -q .; then
	fail "a request carried another User-Agent"
fi

[ $status = 0 ] && echo PASS || echo FAIL
exit $status
