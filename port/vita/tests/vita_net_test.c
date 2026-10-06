/*
VITA_NET_TEST.C

A desktop test of port/vita/host/vita_net.c (included whole, its static
helpers too) over mock_scenet.c, a sceNet with the Vita's semantics on
Linux sockets. It makes the platform layer's calls in the order the game
makes them for a split screen or solo multiplayer game (a host and its own
client on one machine, over 127.0.0.1):

- network_connection_new (networking/network_connection.c) for the server:
  a TCP endpoint bound to 5150 and listening, a UDP endpoint bound to 5150,
  both non-blocking; for the client: a TCP endpoint and a UDP endpoint
  bound to 5151 (create_socket in transport_endpoint_winsock.c sets
  SO_BROADCAST, SO_REUSEADDR and the buffer sizes);
- network_connection_connect: connect_endpoint on the client's UDP
  endpoint (a datagram connect to the server), then on its TCP endpoint
  (would-block, a select for writeable, connected);
- the server's poll and accept; the lobby's messages over the connection
  (the part that worked on the Vita);
- in the game, the client's update every 16 ms (network_game_globals.c):
  network_connection_write unreliable with the server's address, which is
  write_to_endpoint: a sendto, with a destination, on the connected UDP
  socket; the server's network_connection_idle reads it with recvfrom
  (read_from_endpoint). The host counts a client's updates; with none it
  stalls 128 ticks later and drops the client 2 s after that
  (game_time.c, network_game_server_stalled_on_client): the Vita's
  "forcibly removing client system ... due to timeout in-game", 6 s in.

Run port/vita/tests/run_vita_net_test.sh. MOCK_SCENET_LINUX_SENDTO=1 gives
the mock Linux's sendto (what Vita3K does), to see the difference.
*/

#include "../host/vita_net.c"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

extern int mock_log_quiet;
extern int mock_scenet_sendto_calls, mock_scenet_eisconn_refusals;

/* mock_scenet.c's */
extern int mock_scenet_epoll_creates;

static int failures, checks;

static void check(int condition, const char *what, long value, long expected)
{
	checks++;
	if (condition)
	{
		printf("PASS %s\n", what);
	}
	else
	{
		failures++;
		printf("FAIL %s (got %ld, expected %ld; last error %d)\n", what, value, expected, posix_socket_last_error());
	}
}

/* a Winsock sockaddr_in: 16-bit family, port and address in network order */
static void winsock_address_of(unsigned char out[16], unsigned int address_host_order, unsigned short port)
{
	winsock_address(out, address_host_order, port);
}

static int udp_endpoint(unsigned short port)
{
	unsigned char address[16];
	int value = -1;
	int socket = posix_socket(SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
	int length = sizeof(value);

	/* create_socket */
	posix_socket_setsockopt(socket, 0xffff, 0x0020 /* SO_BROADCAST */, &value, sizeof(value));
	value = 1;
	posix_socket_setsockopt(socket, 0xffff, 0x0004 /* SO_REUSEADDR */, &value, sizeof(value));
	if (posix_socket_getsockopt(socket, 0xffff, 0x1001, &value, &length) == 0 && value < 256 * 1024)
	{
		value = 256 * 1024;
		posix_socket_setsockopt(socket, 0xffff, 0x1001, &value, sizeof(value));
	}
	length = sizeof(value);
	if (posix_socket_getsockopt(socket, 0xffff, 0x1002, &value, &length) == 0 && value < 256 * 1024)
	{
		value = 256 * 1024;
		posix_socket_setsockopt(socket, 0xffff, 0x1002, &value, sizeof(value));
	}
	winsock_address_of(address, 0, port);
	check(posix_socket_bind(socket, address, 16) == 0, "udp endpoint bind", posix_socket_last_error(), 0);
	check(posix_socket_set_nonblocking(socket, 1) == 0, "udp endpoint non-blocking", posix_socket_last_error(), 0);
	return socket;
}

static int tcp_endpoint(void)
{
	int value = 1;
	int socket = posix_socket(SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);

	posix_socket_setsockopt(socket, 0xffff, 0x0004, &value, sizeof(value));
	posix_socket_set_nodelay(socket);
	return socket;
}

static void test_error_mapping(void)
{
	static const struct { unsigned int raw; int winsock; const char *name; } table[] = {
		{ 0x80410123u, 10035, "EAGAIN/EWOULDBLOCK -> WSAEWOULDBLOCK" },
		{ 0x80410124u, 10036, "EINPROGRESS -> WSAEINPROGRESS" },
		{ 0x80410125u, 10037, "EALREADY -> WSAEALREADY" },
		{ 0x80410128u, 10040, "EMSGSIZE -> WSAEMSGSIZE" },
		{ 0x80410130u, 10048, "EADDRINUSE -> WSAEADDRINUSE" },
		{ 0x80410134u, 10052, "ENETRESET -> WSAENETRESET" },
		{ 0x80410135u, 10053, "ECONNABORTED -> WSAECONNABORTED" },
		{ 0x80410136u, 10054, "ECONNRESET -> WSAECONNRESET" },
		{ 0x80410137u, 10055, "ENOBUFS -> WSAENOBUFS" },
		{ 0x80410138u, 10056, "EISCONN -> WSAEISCONN" },
		{ 0x80410139u, 10057, "ENOTCONN -> WSAENOTCONN" },
		{ 0x8041013Au, 10058, "ESHUTDOWN -> WSAESHUTDOWN" },
		{ 0x8041013Cu, 10060, "ETIMEDOUT -> WSAETIMEDOUT" },
		{ 0x8041013Du, 10061, "ECONNREFUSED -> WSAECONNREFUSED" },
		{ 0x8041010Du, 10013, "EACCES -> WSAEACCES" },
		{ 0x80410116u, 10022, "EINVAL -> WSAEINVAL" },
		/* (not errno numbers: what Winsock would say) */
		{ 0x80410120u, 10054, "EPIPE (peer gone) -> WSAECONNRESET" },
		{ 0x804101C9u, 10055, "ENOLIBMEM (pool full) -> WSAENOBUFS" },
		{ 0x804101C8u, 10093, "ENOTINIT -> WSANOTINITIALISED" },
	};
	unsigned int index;

	for (index = 0; index < sizeof(table) / sizeof(table[0]); index++)
	{
		int result = answer((int)table[index].raw);

		check(result == -1 && posix_socket_last_error() == table[index].winsock, table[index].name,
			posix_socket_last_error(), table[index].winsock);
	}
	check(answer(7) == 7 && posix_socket_last_error() == 0, "success clears the error", posix_socket_last_error(), 0);
}

static void test_game_sequence(void)
{
	unsigned char address[16], from[16];
	char buffer[2048];
	int server_tcp, server_udp, client_tcp, client_udp, accepted;
	int sockets[4], count, empty = 0, from_length, result, update, received = 0, sent = 0, epolls;
	unsigned int server_ip;

	/* network_connection_new: the server's, then the client's */
	server_tcp = tcp_endpoint();
	winsock_address_of(address, 0, 5150);
	check(posix_socket_bind(server_tcp, address, 16) == 0, "server tcp bind :5150", 0, 0);
	check(posix_socket_set_nonblocking(server_tcp, 1) == 0, "server tcp non-blocking", 0, 0);
	check(posix_socket_listen(server_tcp, 128) == 0, "server listen", 0, 0);
	server_udp = udp_endpoint(5150);
	client_tcp = tcp_endpoint();
	client_udp = udp_endpoint(5151);

	/* network_connection_connect: the datagram endpoint, then the stream */
	winsock_address_of(address, 0x7f000001u, 5150);
	check(posix_socket_connect(client_udp, address, 16) == 0, "client udp connect 127.0.0.1:5150", 0, 0);
	posix_socket_set_nonblocking(client_tcp, 1);
	result = posix_socket_connect(client_tcp, address, 16);
	check(result == -1 && posix_socket_last_error() == 10035, "client tcp connect would-block (WSAEWOULDBLOCK)",
		posix_socket_last_error(), 10035);
	sockets[0] = client_tcp;
	count = 1;
	check(posix_socket_select(NULL, &empty, sockets, &count, NULL, &empty, 1, 0, 0) == 1 && count == 1,
		"client tcp select writeable (connected)", count, 1);

	/* the server's poll_endpoint_set and accept_endpoint (a non-blocking
	listener's zero-timeout poll accepts ahead, without an epoll: a pending
	connection is not peekable; the next accept hands it out) */
	usleep(20000);
	sockets[0] = server_tcp;
	sockets[1] = server_udp;
	count = 2;
	epolls = mock_scenet_epoll_creates;
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 0, 0) == 1 && count == 1 &&
		sockets[0] == server_tcp, "server listener readable (zero timeout), the idle udp not", count, 1);
	check(mock_scenet_epoll_creates == epolls, "zero-timeout poll of a listener: no epoll",
		mock_scenet_epoll_creates - epolls, 0);
	sockets[0] = server_tcp;
	count = 1;
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 0, 0) == 1 && count == 1,
		"server listener still readable (its connection accepted ahead)", count, 1);
	sockets[0] = server_tcp;
	count = 1;
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 100000, 0) == 1,
		"server listener readable (timed select)", count, 1);
	from_length = 16;
	accepted = posix_socket_accept(server_tcp, from, &from_length);
	check(accepted >= 0, "server accept", accepted, 0);
	check(from[4] == 127 && from[7] == 1, "accepted from 127.0.0.1", from[4], 127);
	posix_socket_set_nonblocking(accepted, 1);
	posix_socket_set_nodelay(accepted);
	sockets[0] = server_tcp;
	count = 1;
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 0, 0) == 0 && count == 0,
		"server listener not readable once accepted (zero timeout)", count, 0);
	from_length = 16;
	check(posix_socket_accept(server_tcp, from, &from_length) == -1 && posix_socket_last_error() == 10035,
		"no second connection: accept is WSAEWOULDBLOCK", posix_socket_last_error(), 10035);

	/* the lobby: messages both ways over the connection */
	check(posix_socket_send(client_tcp, "join request", 12, 0) == 12, "lobby: client sends on the connection", 0, 12);
	usleep(20000);
	sockets[0] = accepted;
	sockets[1] = server_udp;
	count = 2;
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 0, 0) == 1 && count == 1 &&
		sockets[0] == accepted, "zero-timeout poll: the connection with bytes waiting is readable, the idle udp not",
		count, 1);
	check(posix_socket_recv(accepted, buffer, sizeof(buffer), 0) == 12, "lobby: server receives it", 0, 12);
	sockets[0] = accepted;
	count = 1;
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 0, 0) == 0 && count == 0,
		"zero-timeout poll: a drained connection is not readable", count, 0);
	check(posix_socket_recv(accepted, buffer, sizeof(buffer), 0) == -1 && posix_socket_last_error() == 10035,
		"lobby: an empty connection is WSAEWOULDBLOCK", posix_socket_last_error(), 10035);
	check(posix_socket_send(accepted, "machine accepted", 16, 0) == 16, "lobby: server sends", 0, 16);
	usleep(20000);
	check(posix_socket_recv(client_tcp, buffer, sizeof(buffer), 0) == 16, "lobby: client receives it", 0, 16);

	/* the game: the client's update every 16 ms to the server's address
	(get_endpoint_address of its connection: getpeername), which the server
	reads from its datagram endpoint */
	from_length = 16;
	check(posix_socket_getpeername(client_tcp, from, &from_length) == 0, "client knows the server's address", 0, 0);
	server_ip = (unsigned int)from[4] << 24 | from[5] << 16 | from[6] << 8 | from[7];
	for (update = 0; update < 30; update++)
	{
		unsigned char destination[16];

		memset(buffer, update, 64);
		winsock_address_of(destination, server_ip, 5150);
		if (posix_socket_sendto(client_udp, buffer, 64, 0, destination, 16) == 64)
			sent++;
		else if (update == 0)
			printf("     first client update sendto failed: Winsock error %d\n", posix_socket_last_error());
		usleep(2000);
		if (update == 0)
		{
			int poll_sockets[1], poll_count = 1;

			poll_sockets[0] = server_udp;
			check(posix_socket_select(poll_sockets, &poll_count, NULL, &empty, NULL, &empty, 0, 0, 0) == 1,
				"zero-timeout poll: a datagram waiting is readable", poll_count, 1);
		}
		for (;;)
		{
			from_length = 16;
			result = posix_socket_recvfrom(server_udp, buffer, sizeof(buffer), 0, from, &from_length);
			if (result < 0)
				break;
			if (result == 64 && from[4] == 127 && from[7] == 1 && (from[2] << 8 | from[3]) == 5151)
				received++;
		}
	}
	check(sent == 30, "game: the client's 30 updates are sent (sendto with the server's address)", sent, 30);
	check(received == 30, "game: the server receives all 30 from 127.0.0.1:5151", received, 30);

	/* the server's datagrams to the client (write_to_endpoint, an unconnected
	socket) and the client's read of its connected one (read_endpoint) */
	winsock_address_of(address, 0x7f000001u, 5151);
	check(posix_socket_sendto(server_udp, "server datagram", 15, 0, address, 16) == 15, "game: server sendto client", 0, 15);
	usleep(5000);
	check(posix_socket_recv(client_udp, buffer, sizeof(buffer), 0) == 15, "game: client recv on its connected socket", 0, 15);

	/* a datagram to some other address from the connected socket is still
	refused (BSD cannot, and the game never does) */
	if (!getenv("MOCK_SCENET_LINUX_SENDTO"))
	{
		winsock_address_of(address, 0x7f000001u, 6000);
		result = posix_socket_sendto(client_udp, "elsewhere", 9, 0, address, 16);
		check(result == -1 && posix_socket_last_error() == 10056, "connected udp: another destination stays WSAEISCONN",
			posix_socket_last_error(), 10056);
	}

	/* the host goes away: the client's read says the connection closed */
	posix_socket_close(accepted);
	usleep(20000);
	sockets[0] = client_tcp;
	count = 1;
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 0, 0) == 1,
		"closed connection reads ready (zero timeout)", count, 1);
	sockets[0] = client_tcp;
	count = 1;
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 100000, 0) == 1,
		"closed connection reads ready", count, 1);
	check(posix_socket_recv(client_tcp, buffer, sizeof(buffer), 0) == 0, "closed connection reads 0", 0, 0);

	posix_socket_close(client_tcp);
	posix_socket_close(client_udp);
	posix_socket_close(server_udp);
	posix_socket_close(server_tcp);
}

/* ad hoc play's group (posix_adhoc_*, for p2p_adhoc.c) over the mock's PDP:
this machine is 127.0.0.220 (MAC 02:00:7f:00:00:dc), the other machine a
plain UDP socket at 127.0.0.221 */
static void test_adhoc(void)
{
	extern int mock_pdp_creates;
	unsigned char address[6], from[6];
	unsigned char buffer[1500];
	unsigned short port = 0;
	struct sockaddr_in other, to;
	socklen_t other_length = sizeof(other);
	int other_socket, result;
	unsigned long long started;
	static const unsigned char everyone[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	static const unsigned char other_address[6] = { 0x02, 0x00, 127, 0, 0, 221 };

	extern int mock_dialog_result, mock_dialog_terms, mock_dialog_mode;
	extern int mock_gxm_dialog_active, mock_gxm_dialog_switches;
	extern char mock_dialog_group[9];

	check(!posix_adhoc_ready(address), "adhoc: not ready outside a group", adhoc_state, 0);
	/* joining through the dialog (vita_adhoc_connect's thread, run here):
	cancelled, then joined; room 3 is the group HALOCE3 */
	adhoc_mode = 0;
	adhoc_room = 3;
	mock_dialog_result = SCE_COMMON_DIALOG_RESULT_USER_CANCELED;
	adhoc_connect_thread(0, NULL);
	check(adhoc_state == -1 && !posix_adhoc_ready(address), "adhoc: a cancelled dialog leaves no group", adhoc_state, -1);
	check(mock_dialog_terms == 1 && mock_gxm_dialog_switches == 2 && !mock_gxm_dialog_active,
		"adhoc: the dialog was drawn, then stopped and ended", mock_gxm_dialog_switches, 2);
	mock_dialog_result = SCE_COMMON_DIALOG_RESULT_OK;
	adhoc_connect_thread(0, NULL);
	check(adhoc_state == 2 && mock_dialog_mode == SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN &&
		!strcmp(mock_dialog_group, "HALOCE3"), "adhoc: joined room 3's group in connect mode", adhoc_state, 2);
	check(posix_adhoc_ready(address) && !memcmp(address, "\x02\x00\x7f\x00\x00\xdc", 6), "adhoc: ready in a group",
		adhoc_state, 2);
	{
		char status[96];

		vita_adhoc_state(status, sizeof(status));
		check(strstr(status, "in a group") != NULL, "adhoc: the status says so", 0, 0);
		printf("  (status: %s)\n", status);
	}
	check(posix_adhoc_open(2306) == 0 && mock_pdp_creates == 1, "adhoc: PDP port opened", mock_pdp_creates, 1);

	other_socket = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&other, 0, sizeof(other));
	other.sin_family = AF_INET;
	other.sin_port = htons(2306);
	other.sin_addr.s_addr = htonl(0x7F0000DD);
	bind(other_socket, (struct sockaddr *)&other, sizeof(other));

	/* the other machine sends: received with its MAC and port */
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(2306);
	to.sin_addr.s_addr = htonl(0x7F0000DC);
	sendto(other_socket, "beacon", 6, 0, (struct sockaddr *)&to, sizeof(to));
	result = posix_adhoc_receive(from, &port, buffer, sizeof(buffer), 200000);
	check(result == 6 && !memcmp(from, other_address, 6) && port == 2306, "adhoc: receive gives the size, MAC and port",
		result, 6);
	/* nothing there: a wait of about the timeout, then 0 */
	started = vita_host_time_us();
	result = posix_adhoc_receive(from, &port, buffer, sizeof(buffer), 50000);
	check(result == 0 && vita_host_time_us() - started >= 40000, "adhoc: an empty receive waits its timeout, then 0",
		(long)(vita_host_time_us() - started), 50000);
	/* sends to a MAC and to everyone */
	check(posix_adhoc_send(other_address, 2306, "tunnel", 6) == 6, "adhoc: send to a MAC", 0, 6);
	result = (int)recvfrom(other_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&other, &other_length);
	check(result == 6 && !memcmp(buffer, "tunnel", 6), "adhoc: the other machine gets it", result, 6);
	setenv("MOCK_PDP_GROUP", "127.0.0.221", 1);
	check(posix_adhoc_send(everyone, 2306, "everyone", 8) == 8, "adhoc: broadcast", 0, 8);
	result = (int)recvfrom(other_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&other, &other_length);
	check(result == 8, "adhoc: the group gets the broadcast", result, 8);
	check(posix_adhoc_send(other_address, 2306, buffer, SCE_NET_ADHOC_PDP_MFS + 1) == -1,
		"adhoc: a datagram past the PDP's largest fails", 0, -1);
	/* a firmware whose waits do not wait: polling after 20 */
	setenv("MOCK_PDP_NO_WAIT", "1", 1);
	for (result = 0; result < 25; result++)
		posix_adhoc_receive(from, &port, buffer, sizeof(buffer), 100000);
	check(adhoc_polling == 1, "adhoc: receives that do not wait fall back to polling", adhoc_polling, 1);
	unsetenv("MOCK_PDP_NO_WAIT");
	sendto(other_socket, "late", 4, 0, (struct sockaddr *)&to, sizeof(to));
	usleep(10000);
	check(posix_adhoc_receive(from, &port, buffer, sizeof(buffer), 100000) == 4, "adhoc: polling still receives", 0, 4);
	/* the address the game sees: the stand-in without Wi-Fi in ad hoc play */
	setenv("HALO_NET_ADHOC", "true", 1);
	check(adhoc_stand_in_address() == (169u | 254u << 8 | 0u << 16 | 220u << 24), "adhoc: stand-in address 169.254.0.220",
		(long)adhoc_stand_in_address(), (long)(169u | 254u << 8 | 220u << 24));
	unsetenv("HALO_NET_ADHOC");
	check(adhoc_stand_in_address() == 0, "adhoc: no stand-in address outside ad hoc play", (long)adhoc_stand_in_address(), 0);
	posix_adhoc_close();
	close(other_socket);
	adhoc_state = 0;
}

int main(void)
{
	mock_log_quiet = getenv("VITA_NET_TEST_LOG") ? 0 : 1;
	printf("-- error mapping\n");
	test_error_mapping();
	printf("-- the game's calls for a solo multiplayer game\n");
	test_game_sequence();
	printf("-- ad hoc play's group (PDP)\n");
	test_adhoc();
	/* the console's self-test (HALO_NET_SELFTEST / HALO_NET_TRACE), its
	log shown: VITA_NET_TEST_SELFTEST=1 */
	if (getenv("VITA_NET_TEST_SELFTEST"))
	{
		mock_log_quiet = 0;
		printf("-- the self-test\n");
		net_selftest_thread(0, NULL);
	}
	printf("-- %d of %d checks failed (mock sendto calls %d, EISCONN refusals %d)\n", failures, checks,
		mock_scenet_sendto_calls, mock_scenet_eisconn_refusals);
	return failures ? 1 : 0;
}
