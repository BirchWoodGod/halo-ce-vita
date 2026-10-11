#!/bin/sh
# Internet play's TLS client for the brokers (posix_tls_*, port/linux/src/
# posix_https.c; p2p_signal.c falls back to a broker's TLS port 8883 when its
# plain one fails): broker_tls_test.c, built 32-bit as the Linux game is,
# with AddressSanitizer and UBSan, against Mbed TLS with the Linux build's
# configuration (port/linux/src/mbedtls_linux_config.h: its threading), on
# 127.0.0.1 against
#   mqtt_test_broker.py's TLS port, with a certificate from an authority made
#                here for this run (never a real key; the test's folder only,
#                deleted after a pass): MQTT 5 over TLS, three sessions at
#                once, the heap they hold and the handshake's time;
#   the same certificate under another name, a certificate of its own (from
#                no authority), the built-in authorities alone (not the
#                test's), a server of garbage, one that closes: each refused;
#   a server that never answers: closed while its handshake is under way,
#                freed by the handshake's thread;
#   an echo server slow to read: sends that wait on a full socket, 16 MB
#                back whole;
#   random servers over a socket pair (BROKER_TLS_FUZZ rounds, 300).
# BROKER_TLS_INTERNET=1 also connects to the three public brokers' TLS ports
# (and their plain ones), subscribing read-only to the Vitas' public games'
# slots: nothing is published.
#   CC  the compiler (default gcc)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/broker_tls_test.$$
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
mbed=$root/port/third_party/mbedtls
config='-DMBEDTLS_USER_CONFIG_FILE="mbedtls_linux_config.h"'
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
export ASAN_OPTIONS=detect_leaks=1:exitcode=99 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=98

echo "--- build"
mkdir -p "$out/mbedtls"
ls "$mbed"/library/*.c | xargs -P "$(nproc)" -I{} sh -c \
	'$0 -m32 -O1 -w -I"$1/include" -I"$1/library" -I"$3" "$4" -c "{}" -o "$2/$(basename "{}" .c).o"' \
	"$cc" "$mbed" "$out/mbedtls" "$root/port/linux/src" "$config"
$cc -m32 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE $sanitize -I"$root/port/linux/src" -I"$mbed/include" \
	"$config" "$here/broker_tls_test.c" "$root/port/linux/src/posix_https.c" "$root/port/linux/src/posix_net.c" \
	"$out"/mbedtls/*.o -Wl,--wrap=calloc -Wl,--wrap=free -lpthread -o "$out/broker_tls_test"

# the test's authority, and the broker's certificate from it (its name and
# its address); a certificate of its own, from no authority
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=Halo Broker Test CA" \
	-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" \
	-keyout "$out/ca_key.pem" -out "$out/ca.pem" 2>/dev/null
openssl req -newkey rsa:2048 -nodes -subj "/CN=broker.halo.test" -keyout "$out/broker_key.pem" -out "$out/broker.csr" \
	2>/dev/null
printf 'subjectAltName=DNS:broker.halo.test,IP:127.0.0.1\nbasicConstraints=CA:FALSE\n' > "$out/broker.ext"
openssl x509 -req -in "$out/broker.csr" -CA "$out/ca.pem" -CAkey "$out/ca_key.pem" -CAcreateserial -days 2 \
	-extfile "$out/broker.ext" -out "$out/broker.pem" 2>/dev/null
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 -subj "/CN=broker.halo.test" \
	-addext "subjectAltName=DNS:broker.halo.test" -keyout "$out/self_key.pem" -out "$out/self.pem" 2>/dev/null

# the servers
port_of() { # port_of NAME: the port its server wrote, once it has
	i=0
	while [ ! -s "$out/$1.port" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
	cat "$out/$1.port"
}
free_port() { python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'; }
broker_port=$(free_port); broker_tls_port=$(free_port)
python3 "$here/mqtt_test_broker.py" --host 127.0.0.1 --port "$broker_port" --tls-port "$broker_tls_port" \
	--tls-cert "$out/broker.pem" --tls-key "$out/broker_key.pem" > "$out/broker.log" 2>&1 & server_pids="$server_pids $!"
for mode in garbage silent close; do
	python3 "$here/broker_tls_server.py" --mode $mode --port-file "$out/$mode.port" > "$out/$mode.log" 2>&1 &
	server_pids="$server_pids $!"
done
python3 "$here/broker_tls_server.py" --mode echo --cert "$out/self.pem" --key "$out/self_key.pem" \
	--port-file "$out/self.port" > "$out/self.log" 2>&1 & server_pids="$server_pids $!"
python3 "$here/broker_tls_server.py" --mode echo --cert "$out/broker.pem" --key "$out/broker_key.pem" --delay 2 \
	--port-file "$out/echo.port" > "$out/echo.log" 2>&1 & server_pids="$server_pids $!"
for name in garbage silent close self echo; do port_of $name > /dev/null; done
i=0; while ! grep -q "(tls)" "$out/broker.log" && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done

status=0
run() { # run TITLE ARGS...: one case
	echo "--- $1"; shift
	"$out/broker_tls_test" "$@" || status=1
}
run "MQTT 5 over TLS to the test broker" mqtt broker.halo.test "$broker_tls_port" "$out/ca.pem"
run "its address as the name (the certificate's IP)" mqtt 127.0.0.1 "$broker_tls_port" "$out/ca.pem"
run "another name" refused other.halo.test "$broker_tls_port" "$out/ca.pem" "certificate refused"
run "the built-in authorities alone" refused broker.halo.test "$broker_tls_port" - "certificate refused"
run "a certificate of its own" refused broker.halo.test "$(port_of self)" "$out/ca.pem" "certificate refused"
run "a server of garbage" refused broker.halo.test "$(port_of garbage)" "$out/ca.pem" "TLS handshake failed"
run "a server that closes" refused broker.halo.test "$(port_of close)" "$out/ca.pem" "closed in the TLS handshake"
run "an authorities' file that is not one" refused broker.halo.test "$broker_tls_port" "$out/broker.csr" "not PEM certificates"
run "a missing authorities' file" refused broker.halo.test "$broker_tls_port" "$out/none.pem" "cannot be read"
run "closed while its handshake is under way" cancel "$(port_of silent)" "$out/ca.pem"
run "sends that wait on a full socket" stream "$(port_of echo)" "$out/ca.pem"
run "random servers" fuzz "${BROKER_TLS_FUZZ:-300}"
grep -q "connect tls-test mqtt5 .* tls" "$out/broker.log" || { echo "FAIL the test broker logged no TLS connection"; status=1; }
if [ "${BROKER_TLS_INTERNET:-0}" = 1 ]; then
	for broker in broker.emqx.io broker.hivemq.com test.mosquitto.org; do
		run "$broker over TLS (8883)" internet $broker 8883 'hcev/3/lobby/s/+'
		run "$broker plain (1883)" internet $broker 1883 'hcev/3/lobby/s/+' plain
	done
fi
[ $status = 0 ] && echo "PASS broker TLS" || echo "FAIL broker TLS (in $out)"
exit $status
