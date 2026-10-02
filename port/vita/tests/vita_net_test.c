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

#include <stdio.h>
#include <unistd.h>

extern int mock_log_quiet;
extern int mock_scenet_sendto_calls, mock_scenet_eisconn_refusals;

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
	int sockets[4], count, empty = 0, from_length, result, update, received = 0, sent = 0;
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

	/* the server's poll_endpoint_set and accept_endpoint */
	sockets[0] = server_tcp;
	count = 1;
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 100000, 0) == 1,
		"server listener readable", count, 1);
	from_length = 16;
	accepted = posix_socket_accept(server_tcp, from, &from_length);
	check(accepted >= 0, "server accept", accepted, 0);
	check(from[4] == 127 && from[7] == 1, "accepted from 127.0.0.1", from[4], 127);
	posix_socket_set_nonblocking(accepted, 1);
	posix_socket_set_nodelay(accepted);

	/* the lobby: messages both ways over the connection */
	check(posix_socket_send(client_tcp, "join request", 12, 0) == 12, "lobby: client sends on the connection", 0, 12);
	usleep(20000);
	check(posix_socket_recv(accepted, buffer, sizeof(buffer), 0) == 12, "lobby: server receives it", 0, 12);
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
	check(posix_socket_select(sockets, &count, NULL, &empty, NULL, &empty, 0, 100000, 0) == 1,
		"closed connection reads ready", count, 1);
	check(posix_socket_recv(client_tcp, buffer, sizeof(buffer), 0) == 0, "closed connection reads 0", 0, 0);

	posix_socket_close(client_tcp);
	posix_socket_close(client_udp);
	posix_socket_close(server_udp);
	posix_socket_close(server_tcp);
}

int main(void)
{
	mock_log_quiet = getenv("VITA_NET_TEST_LOG") ? 0 : 1;
	printf("-- error mapping\n");
	test_error_mapping();
	printf("-- the game's calls for a solo multiplayer game\n");
	test_game_sequence();
	printf("-- %d of %d checks failed (mock sendto calls %d, EISCONN refusals %d)\n", failures, checks,
		mock_scenet_sendto_calls, mock_scenet_eisconn_refusals);
	return failures ? 1 : 0;
}
