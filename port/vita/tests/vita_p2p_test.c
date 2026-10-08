/*
VITA_P2P_TEST.C

Internet play (port/linux/src/p2p*.c, compiled as the Vita build compiles
them: the game's ABI, HALO_VITA) over the Vita's socket layer
(port/vita/host/vita_net.c) on mock_scenet.c, which gives Linux sockets
the Vita's semantics (BSD error numbers, the length byte in addresses, a
connected datagram socket refusing sendto, epoll-based select). Internet
play never ran on the Vita before: this is its first run over those
semantics.

Two processes, a host and a joiner (run_vita_p2p_test.sh), with
port/vita/tests/mqtt_test_broker.py as the signalling broker:

- host: starts internet play on 127.0.0.1, listens (as the game does when
  it hosts a System Link game), prints its short code, and serves a UDP
  socket and a TCP listener on its port 2302;
- joiner: starts internet play on 127.0.0.2, joins the code, and once the
  tunnel reaches the host sends a datagram to the host's port 2302 through
  the stand-in (as the game's broadcast would go: p2p_broadcast_targets);
- the host answers the datagram (back through the tunnel) and connects to
  the joiner's port 2302 through the TCP stand-in for its virtual address
  (as the game's client connects to its host: here the other way round,
  which takes the same path), carried over KCP; they exchange a message.

With "adhoc-host" and "adhoc-join" (run_vita_p2p_test.sh adhoc) there is
no broker: each process joins an ad hoc group through vita_net.c's connect
thread (the network check dialog scripted by the mock) and internet play
runs as ad hoc play (p2p_adhoc.c) over vita_net.c's PDP functions, the
mock's PDP carrying them between the two processes; the same datagram and
stream exchange follows.

Each side prints PASS lines; the script checks both.
*/

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "posix.h"
#include "p2p.h"

/* ---------- what the platform layer gives p2p.c (port_config.c,
xbox_kernel.c, platform.c), here */

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int connected;

void platform_log(const char *format, ...)
{
	char line[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	pthread_mutex_lock(&log_lock);
	printf("  [p2p] %s\n", line);
	fflush(stdout);
	pthread_mutex_unlock(&log_lock);
	if (strstr(line, "Internet play: connected to"))
		connected = 1;
}

const char *platform_data_root(void)
{
	return "/nonexistent";
}

int config_boolean(const char *name)
{
	if (!strcmp(name, "network.online"))
		return !getenv("TEST_ADHOC");
	if (!strcmp(name, "network.adhoc"))
		return getenv("TEST_ADHOC") != NULL;
	if (!strcmp(name, "network.host_public"))
		return 0;
	return 0;
}

long config_integer(const char *name)
{
	(void)name;
	return 0;
}

double config_real(const char *name)
{
	(void)name;
	return 0.0;
}

const char *config_string(const char *name)
{
	if (!strcmp(name, "network.signalling_brokers"))
		return getenv("TEST_BROKER") ? getenv("TEST_BROKER") : "";
	if (!strcmp(name, "network.lobby_name"))
		return "vita test";
	return "";
}

void config_folder(char *path, size_t size)
{
	snprintf(path, size, "./");
}

/* (no brokers' file: TEST_BROKER names the test's broker) */
char *config_file_read(const char *path, size_t *size)
{
	(void)path;
	(void)size;
	return NULL;
}

/* (the resolver cache's file: not written here) */
int config_file_write(const char *path, const char *text, size_t size)
{
	(void)path;
	(void)text;
	(void)size;
	return 1;
}

__attribute__((stdcall)) unsigned long GetTickCount(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

__attribute__((stdcall)) void Sleep(unsigned long milliseconds)
{
	usleep(milliseconds * 1000);
}

/* the desktop's and Discord's, which the Vita has not (vita_stubs.c) */
int posix_command_line_argument(int index, char *buffer, posix_ulong size) { (void)index; (void)buffer; (void)size; return 0; }
int posix_register_url_scheme(const char *scheme, const char *description) { (void)scheme; (void)description; return 0; }
void p2p_discord_update(void) {}
void p2p_discord_set_hosting(const char *secret, int player_count, int maximum_player_count)
{
	(void)secret; (void)player_count; (void)maximum_player_count;
}
int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error, int error_size)
{
	(void)port; (void)preferred_port; (void)external_address; (void)external_port;
	snprintf(error, (size_t)error_size, "no UPnP in the test");
	return 0;
}
void posix_upnp_stop_forwarding_udp(unsigned short external_port) { (void)external_port; }
int posix_user_secret(unsigned char *secret, int size) { (void)secret; (void)size; return 0; }
void p2p_discord_user(char *id, int id_size, char *name, int name_size)
{
	(void)id_size; (void)name_size;
	id[0] = name[0] = 0;
}
/* (the Vita's is a keyed hash of its OpenPSID, vita_stubs.c) */
int posix_hardware_id_source(char *text, int size)
{
	snprintf(text, (size_t)size, "vita-p2p-test");
	return 1;
}

void posix_random_bytes(void *buffer, posix_ulong size)
{
	FILE *file = fopen("/dev/urandom", "rb");

	if (!file || fread(buffer, 1, size, file) != size)
		abort();
	fclose(file);
}

/* ---------- the test */

extern void p2p_initialize(unsigned long local_address);
/* vita_net.c's (vita_host.h) */
int vita_adhoc_connect(int mode, int room);
int vita_adhoc_state(char *text, int size);

/* ad hoc play: in a group first, as the settings panel's "Join ad hoc
group" leaves the Vita */
static int join_group(void)
{
	int tenth;

	if (!getenv("TEST_ADHOC"))
		return 1;
	if (vita_adhoc_connect(0, 1) != 0)
		return 0;
	for (tenth = 0; tenth < 50 && vita_adhoc_state(NULL, 0) != 2; tenth++)
		usleep(100000);
	return vita_adhoc_state(NULL, 0) == 2;
}

static int failures;

static void check(int condition, const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	fflush(stdout);
	if (!condition)
		failures++;
}

static void winsock_address(struct sockaddr_in *address, unsigned long ip, unsigned short port)
{
	/* (Winsock's layout: a 16-bit family, as the game's) */
	memset(address, 0, sizeof(*address));
	*(unsigned short *)address = AF_INET;
	address->sin_port = port;
	address->sin_addr.s_addr = ip;
}

static int bound_socket(int type, unsigned long ip, unsigned short port)
{
	struct sockaddr_in address;
	int result = posix_socket(AF_INET, type, 0);
	int value = 1;

	/* (as the game's endpoints: a port a run before left in TIME_WAIT is
	taken again; Winsock's SOL_SOCKET and SO_REUSEADDR) */
	posix_socket_setsockopt(result, 0xFFFF, 0x0004, &value, sizeof(value));
	winsock_address(&address, ip, port);
	if (result < 0 || posix_socket_bind(result, &address, sizeof(address)) < 0)
		return -1;
	return result;
}

/* waits up to timeout ms for the socket to read */
static int readable(int socket, int timeout)
{
	int read[1] = { socket };
	int read_count = 1, write_count = 0, error_count = 0;

	return posix_socket_select(read, &read_count, NULL, &write_count, NULL, &error_count, timeout / 1000,
		(timeout % 1000) * 1000, 0) > 0 && read_count == 1;
}

static int wait_connected(int seconds)
{
	int tenth;

	for (tenth = 0; tenth < seconds * 10 && !connected; tenth++)
		usleep(100000);
	return connected;
}

static int host(void)
{
	unsigned long local = inet_addr("127.0.0.1");
	int listener, datagrams, stream;
	char code[P2P_CODE_SIZE];
	char buffer[256];
	struct sockaddr_in from;
	int from_length = sizeof(from);
	int size, tenth;
	unsigned long peer_address;
	unsigned short peer_port;

	if (getenv("TEST_ADHOC"))
		check(join_group(), "host: in an ad hoc group");
	p2p_initialize(local);
	/* the game's System Link host: a TCP listener and a UDP socket on 2302 */
	listener = bound_socket(SOCK_STREAM, local, htons(2302));
	datagrams = bound_socket(SOCK_DGRAM, local, htons(2302));
	check(listener >= 0 && datagrams >= 0 && posix_socket_listen(listener, 4) == 0, "host: sockets on 2302");
	/* (as xnet.c tells internet play: the ports peers may reach) */
	p2p_socket_port(listener, 1, 1, htons(2302));
	p2p_socket_port(datagrams, 0, 0, htons(2302));
	if (!getenv("TEST_ADHOC"))
	{
		for (tenth = 0; tenth < 100 && !p2p_hosting_code(code, sizeof(code)); tenth++)
			usleep(100000);
		check(p2p_hosting_code(code, sizeof(code)), "host: hosting, with a code");
		printf("CODE %s\n", code);
		fflush(stdout);
	}
	check(wait_connected(40), "host: the tunnel reached the joiner");
	/* the joiner's datagram, through the tunnel and a stand-in */
	size = readable(datagrams, 15000) ? posix_socket_recvfrom(datagrams, buffer, sizeof(buffer), 0, &from, &from_length) : -1;
	check(size == 11 && !memcmp(buffer, "hello host!", 11), "host: the joiner's datagram arrived");
	if (size < 0)
		return 1;
	/* the stand-in it came from is the joiner's: answer it */
	check(posix_socket_sendto(datagrams, "hello joiner", 12, 0, &from, sizeof(from)) == 12, "host: answered");
	/* and connect to the joiner's port 2302 (its virtual address) over KCP */
	peer_address = from.sin_addr.s_addr;
	peer_port = from.sin_port;
	check(p2p_incoming(0, &peer_address, &peer_port) && (ntohl(peer_address) & 0xFFC00000) == 0x64400000,
		"host: the datagram's source is the joiner's virtual address");
	peer_port = htons(2302);
	check(p2p_outgoing(1, -1, &peer_address, &peer_port) == 1, "host: a TCP stand-in for the joiner's port 2302");
	stream = posix_socket(AF_INET, SOCK_STREAM, 0);
	posix_socket_set_nonblocking(stream, 1);
	{
		struct sockaddr_in target;
		int write[1] = { stream };
		int read_count = 0, write_count = 1, error_count = 0;

		winsock_address(&target, peer_address, peer_port);
		posix_socket_connect(stream, &target, sizeof(target));
		check(posix_socket_select(NULL, &read_count, write, &write_count, NULL, &error_count, 5, 0, 0) == 1,
			"host: the stand-in took the connection");
	}
	check(posix_socket_send(stream, "stream to joiner", 16, 0) == 16, "host: sent on the stream");
	size = readable(stream, 15000) ? posix_socket_recv(stream, buffer, sizeof(buffer), 0) : -1;
	check(size == 16 && !memcmp(buffer, "stream to host!!", 16), "host: the joiner's reply came back over the stream");
	sleep(2);
	return failures;
}

static int joiner(const char *code)
{
	unsigned long local = inet_addr("127.0.0.2");
	int datagrams, listener, accepted = -1;
	unsigned long targets[4];
	unsigned short ports[4];
	char buffer[256];
	int count, size, tenth;

	if (getenv("TEST_ADHOC"))
		check(join_group(), "joiner: in an ad hoc group");
	p2p_initialize(local);
	datagrams = bound_socket(SOCK_DGRAM, local, htons(2302));
	listener = bound_socket(SOCK_STREAM, local, htons(2302));
	check(datagrams >= 0 && listener >= 0 && posix_socket_listen(listener, 4) == 0, "joiner: sockets on 2302");
	p2p_socket_port(listener, 1, 1, htons(2302));
	p2p_socket_port(datagrams, 0, 0, htons(2302));
	if (code)
		check(p2p_join_code(code), "joiner: the code is a code");
	check(wait_connected(40), "joiner: the tunnel reached the host");
	/* the host's port 2302 among the broadcast's stand-ins */
	count = 0;
	for (tenth = 0; tenth < 50 && !count; tenth++)
	{
		count = p2p_broadcast_targets(htons(2302), targets, ports, 4);
		if (!count)
			usleep(100000);
	}
	check(count == 1, "joiner: one stand-in for the host's port 2302");
	if (!count)
		return 1;
	{
		struct sockaddr_in to;

		winsock_address(&to, targets[0], ports[0]);
		check(posix_socket_sendto(datagrams, "hello host!", 11, 0, &to, sizeof(to)) == 11, "joiner: sent a datagram");
	}
	size = readable(datagrams, 15000) ? posix_socket_recv(datagrams, buffer, sizeof(buffer), 0) : -1;
	check(size == 12 && !memcmp(buffer, "hello joiner", 12), "joiner: the host's answer arrived");
	/* the host's connection to this machine's 2302, over KCP */
	if (readable(listener, 15000))
		accepted = posix_socket_accept(listener, NULL, NULL);
	check(accepted >= 0, "joiner: the host's stream connection arrived");
	if (accepted < 0)
		return 1;
	size = readable(accepted, 15000) ? posix_socket_recv(accepted, buffer, sizeof(buffer), 0) : -1;
	check(size == 16 && !memcmp(buffer, "stream to joiner", 16), "joiner: the host's stream message arrived");
	check(posix_socket_send(accepted, "stream to host!!", 16, 0) == 16, "joiner: replied on the stream");
	sleep(3);
	return failures;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "host"))
		return host();
	if (argc >= 3 && !strcmp(argv[1], "join"))
		return joiner(argv[2]);
	if (argc >= 2 && !strcmp(argv[1], "adhoc-host"))
		return setenv("TEST_ADHOC", "1", 1), host();
	if (argc >= 2 && !strcmp(argv[1], "adhoc-join"))
		return setenv("TEST_ADHOC", "1", 1), joiner(NULL);
	fprintf(stderr, "usage: %s host | join CODE\n", argv[0]);
	return 2;
}
