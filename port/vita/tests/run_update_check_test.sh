#!/bin/sh
# The update check's tests (port/linux/src/update_check.h), each built
# 32-bit as the Vita and the Linux game are, with AddressSanitizer and UBSan:
#   update_check_test.c   versions, tags, channels, the releases' JSON, the
#                         cache file (update_check.c)
#   update_check_fuzz.c   update_check_cases/ and made-up inputs, every
#                         truncation, then UPDATE_FUZZ_ITERATIONS (default
#                         30000) changes from a fixed seed; with
#                         UPDATE_FUZZ_SECONDS=n also n seconds of libFuzzer
#                         (64-bit; clang), what it finds in UPDATE_FUZZ_OUT
#   update_https_test.c   the HTTPS request (posix_https.c, Mbed TLS,
#                         posix_net.c) against update_test_server.py on
#                         127.0.0.1, plain and over TLS with a certificate of
#                         its own (refused); UPDATE_TEST_INTERNET=1 also asks
#                         api.github.com once, over TLS checked against the
#                         authorities built in
#   CC  the compiler (default gcc)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/update_check_test.$$
cc=${CC:-gcc}
mkdir -p "$out"
server_pids=
cleanup() {
	for pid in $server_pids; do kill "$pid" 2>/dev/null || true; done
	rm -rf "$out"
}
trap cleanup EXIT
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
flags="-m32 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -I$root/port/linux/src"
export ASAN_OPTIONS=detect_leaks=1:exitcode=99 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=98
status=0

echo "--- update_check_test"
$cc $flags $sanitize "$here/update_check_test.c" "$root/port/linux/src/update_check.c" -o "$out/update_check_test"
"$out/update_check_test" || status=1

echo "--- update_check_fuzz"
$cc $flags $sanitize "$here/update_check_fuzz.c" "$root/port/linux/src/update_check.c" -o "$out/update_check_fuzz"
"$out/update_check_fuzz" "$here/update_check_cases" || status=1
seconds=${UPDATE_FUZZ_SECONDS:-0}
if [ "$seconds" != 0 ]; then
	found=${UPDATE_FUZZ_OUT:-$out/found}
	mkdir -p "$found/corpus"
	clang -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -DUPDATE_FUZZ_LIBFUZZER -g -O1 \
		-I"$root/port/linux/src" "$here/update_check_fuzz.c" "$root/port/linux/src/update_check.c" -o "$out/update_libfuzzer"
	"$out/update_libfuzzer" -max_total_time="$seconds" -max_len=300000 -print_final_stats=1 -artifact_prefix="$found/" \
		"$found/corpus" "$here/update_check_cases" > "$found/fuzz.log" 2>&1 || status=1
	grep -E 'stat::number_of_executed_units|cov:.*ft:' "$found/fuzz.log" | tail -2 || true
	ls "$found" | grep -E '^(crash|leak|timeout|oom)-' && status=1
fi

echo "--- update_https_test"
# (Mbed TLS as the Linux build has it: its default configuration, no
# sanitizers, built once into the test's folder)
mkdir -p "$out/mbedtls"
ls "$root"/port/third_party/mbedtls/library/*.c | xargs -P "$(nproc)" -I{} sh -c \
	'$0 -m32 -O1 -w -I"$1/include" -I"$1/library" -c "{}" -o "$2/$(basename "{}" .c).o"' \
	"$cc" "$root/port/third_party/mbedtls" "$out/mbedtls"
$cc $flags $sanitize -I"$root/port/third_party/mbedtls/include" "$here/update_https_test.c" \
	"$root/port/linux/src/posix_https.c" "$root/port/linux/src/posix_net.c" "$root/port/linux/src/update_check.c" \
	"$out"/mbedtls/*.o -lpthread -o "$out/update_https_test"
# (the server's own certificate: no authority the update check trusts)
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=127.0.0.1" \
	-addext "subjectAltName=IP:127.0.0.1" -keyout "$out/key.pem" -out "$out/cert.pem" 2>/dev/null
python3 "$here/update_test_server.py" "$out/port" "$out/requests" & server_pids="$server_pids $!"
python3 "$here/update_test_server.py" "$out/tls_port" "$out/tls_requests" --tls "$out/cert.pem" "$out/key.pem" &
server_pids="$server_pids $!"
for i in $(seq 50); do [ -s "$out/port" ] && [ -s "$out/tls_port" ] && break; sleep 0.1; done
internet=
[ "${UPDATE_TEST_INTERNET:-0}" = 1 ] && internet=internet
"$out/update_https_test" "http://127.0.0.1:$(cat "$out/port")" "https://127.0.0.1:$(cat "$out/tls_port")" $internet ||
	status=1
# (nothing reached the TLS server's request log: refused before sending)
if [ -s "$out/tls_requests" ]; then
	echo "FAIL a request was sent to the server whose certificate was refused"
	status=1
fi
if grep -q COOKIE "$out/requests" 2>/dev/null || grep -qv " HaloCEVita/test$" "$out/requests"; then
	echo "FAIL a request carried a cookie or another User-Agent"
	status=1
fi

[ $status = 0 ] && echo PASS || echo FAIL
exit $status
