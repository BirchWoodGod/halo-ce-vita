/*
BROKER_TLS_TEST.C

Internet play's TLS client for the brokers (posix_tls_*, posix_https.c), as
p2p_signal.c drives it: non-blocking, the handshake polled while its
thread does the work. Built by run_broker_tls_test.sh with AddressSanitizer
and UBSan, against Mbed TLS built with the Linux build's configuration,
calloc and free wrapped to count the heap Mbed TLS holds. One case a run
(the authorities are loaded once a process, with the authorities' file of
its first session):

  mqtt HOST PORT CA        the test broker's TLS port (mqtt_test_broker.py):
                           handshake, CONNECT (MQTT 5), CONNACK, SUBSCRIBE,
                           SUBACK; then three sessions at once. Says the
                           handshake's time and longest step, and the heap:
                           the authorities, a session open, a handshake's
                           peak
  refused HOST PORT CA WHY the handshake must fail, its error holding WHY
                           (CA "-": the authorities built in alone)
  cancel PORT CA           a server that never answers: the handshake stays
                           under way; closed meanwhile, its thread must free
                           it (the heap back to what it was) and stop
  stream PORT CA           an echo server that reads only after a while: the
                           sends fill the socket and wait (posix_tls_send's
                           0, the same bytes given first again); 16 MB must
                           come back whole, in order, once
  fuzz COUNT               a server of random bytes (some framed as TLS
                           records, a ServerHello's start) over a socket
                           pair, COUNT times: every handshake fails, cleanly
  internet HOST PORT TOPIC [plain]  a public broker's TLS port (or plain):
                           CONNECT, SUBSCRIBE to TOPIC (read only: nothing
                           is published), and the retained messages it
                           sends in 10 s counted
HOST is the name the certificate must have; the connection goes to
127.0.0.1 unless the case is internet.
*/

#include "posix.h"

#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* ---------- the heap Mbed TLS holds (calloc and free wrapped) */

void *__real_calloc(size_t count, size_t size);
void __real_free(void *pointer);

enum { TRACKED = 1 << 16 };
static pthread_mutex_t track_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { void *pointer; size_t size; } tracked[TRACKED];
static long live_bytes, peak_bytes;

void *__wrap_calloc(size_t count, size_t size)
{
	void *pointer = __real_calloc(count, size);

	if (pointer)
	{
		size_t slot = ((size_t)pointer >> 4) & (TRACKED - 1);

		pthread_mutex_lock(&track_lock);
		while (tracked[slot].pointer)
			slot = (slot + 1) & (TRACKED - 1);
		tracked[slot].pointer = pointer;
		tracked[slot].size = count * size;
		live_bytes += (long)(count * size);
		if (live_bytes > peak_bytes)
			peak_bytes = live_bytes;
		pthread_mutex_unlock(&track_lock);
	}
	return pointer;
}

void __wrap_free(void *pointer)
{
	if (pointer)
	{
		size_t slot = ((size_t)pointer >> 4) & (TRACKED - 1);
		size_t probes;

		pthread_mutex_lock(&track_lock);
		for (probes = 0; probes < TRACKED && tracked[slot].pointer; probes++, slot = (slot + 1) & (TRACKED - 1))
		{
			if (tracked[slot].pointer == pointer)
			{
				size_t next = (slot + 1) & (TRACKED - 1);

				live_bytes -= (long)tracked[slot].size;
				tracked[slot].pointer = NULL;
				/* (the run after it put back where a lookup finds it) */
				while (tracked[next].pointer)
				{
					void *moved = tracked[next].pointer;
					size_t moved_size = tracked[next].size;

					tracked[next].pointer = NULL;
					{
						size_t home = ((size_t)moved >> 4) & (TRACKED - 1);

						while (tracked[home].pointer)
							home = (home + 1) & (TRACKED - 1);
						tracked[home].pointer = moved;
						tracked[home].size = moved_size;
					}
					next = (next + 1) & (TRACKED - 1);
				}
				break;
			}
		}
		pthread_mutex_unlock(&track_lock);
	}
	__real_free(pointer);
}

static long live(void)
{
	long value;

	pthread_mutex_lock(&track_lock);
	value = live_bytes;
	pthread_mutex_unlock(&track_lock);
	return value;
}

static long peak_reset(void)
{
	long value;

	pthread_mutex_lock(&track_lock);
	value = peak_bytes;
	peak_bytes = live_bytes;
	pthread_mutex_unlock(&track_lock);
	return value;
}

/* ---------- helpers */

static int failures;

static void check(int condition, const char *what)
{
	printf("%s %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition)
		failures++;
}

static unsigned long milliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)now.tv_sec * 1000UL + (unsigned long)(now.tv_nsec / 1000000);
}

static void sleep_ms(int count)
{
	struct timespec wait = { count / 1000, (long)(count % 1000) * 1000000L };

	nanosleep(&wait, NULL);
}

/* a non-blocking connection to address:port (network order), made: its
socket, or -1 */
static int connect_to(unsigned int address, int port)
{
	struct
	{
		unsigned short family, port;
		unsigned int address;
		unsigned char zero[8];
	} where;
	int socket = posix_socket(2, 1, 0);
	unsigned long started = milliseconds();

	if (socket < 0)
		return -1;
	posix_socket_set_nonblocking(socket, 1);
	memset(&where, 0, sizeof(where));
	where.family = 2;
	where.port = (unsigned short)((port >> 8) | ((port & 0xff) << 8));
	where.address = address;
	if (posix_socket_connect(socket, &where, sizeof(where)) < 0)
	{
		int list[1] = { socket }, count = 1, none = 0, error = 0, length = sizeof(error);

		while (posix_socket_select(NULL, &none, list, &count, NULL, &none, 0, 100000, 0) == 0)
		{
			if (milliseconds() - started > 8000)
				return posix_socket_close(socket), -1;
			list[0] = socket, count = 1;
		}
		if (posix_socket_getsockopt(socket, 0xffff, 0x1007, &error, &length) == 0 && error)
			return posix_socket_close(socket), -1;
	}
	return socket;
}

static unsigned int loopback(void)
{
	return 0x0100007Fu;
}

/* polls a handshake until it ends or timeout ms pass: its result */
static int handshake(int handle, char *text, int size, int timeout)
{
	unsigned long started = milliseconds();
	int result;

	while ((result = posix_tls_handshake(handle, text, size)) == 0 && milliseconds() - started < (unsigned long)timeout)
		sleep_ms(5);
	return result;
}

/* a whole send (waiting while the socket is full) */
static int send_all(int handle, const unsigned char *data, int size)
{
	unsigned long started = milliseconds();

	while (size > 0)
	{
		int sent = posix_tls_send(handle, data, size);

		if (sent < 0 || milliseconds() - started > 10000)
			return 0;
		if (!sent)
		{
			sleep_ms(2);
			continue;
		}
		data += sent;
		size -= sent;
	}
	return 1;
}

static int put_string(unsigned char *out, const char *text)
{
	int size = (int)strlen(text);

	out[0] = (unsigned char)(size >> 8);
	out[1] = (unsigned char)size;
	memcpy(out + 2, text, (size_t)size);
	return size + 2;
}

/* MQTT 5's CONNECT and a SUBSCRIBE (3.1.1's with mqtt311) to topic */
static int mqtt_hello(unsigned char *out, const char *client, const char *topic, int mqtt311)
{
	unsigned char body[256];
	int size = 0, total = 0;

	size += put_string(body, "MQTT");
	body[size++] = mqtt311 ? 4 : 5;
	body[size++] = 0x02;
	body[size++] = 0;
	body[size++] = 30;
	if (!mqtt311)
		body[size++] = 0;
	size += put_string(body + size, client);
	out[total++] = 0x10;
	out[total++] = (unsigned char)size;
	memcpy(out + total, body, (size_t)size);
	total += size;
	size = 0;
	body[size++] = 0;
	body[size++] = 1;
	if (!mqtt311)
		body[size++] = 0;
	size += put_string(body + size, topic);
	body[size++] = 0;
	out[total++] = 0x82;
	out[total++] = (unsigned char)size;
	memcpy(out + total, body, (size_t)size);
	return total + size;
}

/* reads packets for ms milliseconds (or until a SUBACK, if stop): the
CONNACK's reason (-1 none), whether a SUBACK came, PUBLISHes counted */
struct heard
{
	int connack, suback, publishes, closed;
};

static void mqtt_listen(int handle, int plain_socket, struct heard *heard, int ms, int stop)
{
	unsigned char input[65536];
	int size = 0;
	unsigned long started = milliseconds();

	memset(heard, 0, sizeof(*heard));
	heard->connack = -1;
	while (milliseconds() - started < (unsigned long)ms && !(stop && heard->suback))
	{
		char why[128];
		int received = plain_socket >= 0 ?
			posix_socket_recv(plain_socket, input + size, (int)sizeof(input) - size, 0) :
			posix_tls_receive(handle, input + size, (int)sizeof(input) - size, why, sizeof(why));

		if (plain_socket >= 0 && received < 0)
			received = 0;
		else if (plain_socket >= 0 && received == 0)
			received = -1;
		if (received < 0)
		{
			heard->closed = 1;
			break;
		}
		if (!received)
		{
			sleep_ms(5);
			continue;
		}
		size += received;
		for (;;)
		{
			int length = 0, shift = 0, at = 1;

			while (at < size && (input[at] & 128) && shift < 21)
				length |= (input[at++] & 127) << shift, shift += 7;
			if (at >= size)
				break;
			length |= (input[at++] & 127) << shift;
			if (at + length > size)
				break;
			if ((input[0] & 0xF0) == 0x20 && length >= 2)
				heard->connack = input[at + 1];
			else if ((input[0] & 0xF0) == 0x90)
				heard->suback = 1;
			else if ((input[0] & 0xF0) == 0x30)
				heard->publishes++;
			memmove(input, input + at + length, (size_t)(size - at - length));
			size -= at + length;
		}
		if (size == (int)sizeof(input))
			size = 0;
	}
}

/* ---------- the cases */

static int case_mqtt(const char *host, int port, const char *ca)
{
	unsigned char hello[512];
	char text[256];
	struct heard heard;
	long before, open_bytes, roots, peak;
	int socket, handle, result, index;
	int handles[3];

	before = live();
	socket = connect_to(loopback(), port);
	check(socket >= 0, "connected to the test broker's TLS port");
	if (socket < 0)
		return 1;
	peak_reset();
	handle = posix_tls_open(socket, host, ca, text, sizeof(text));
	check(handle >= 0, "a session opened");
	if (handle < 0)
	{
		printf("     (%s)\n", text);
		return 1;
	}
	result = handshake(handle, text, sizeof(text), 10000);
	check(result == 1, "the handshake succeeded");
	printf("     handshake: %s\n", text);
	peak = peak_reset();
	open_bytes = live();
	result = send_all(handle, hello, mqtt_hello(hello, "tls-test", "hcev/3/lobby/s/+", 0));
	check(result, "CONNECT and SUBSCRIBE sent over TLS");
	mqtt_listen(handle, -1, &heard, 5000, 1);
	check(heard.connack == 0, "CONNACK 0 over TLS");
	check(heard.suback, "SUBACK over TLS");
	check(!posix_tls_pending(handle), "nothing left pending");
	posix_tls_close(handle);
	roots = live() - before;
	printf("     heap: the authorities %ld KB (once), a session open %ld KB, a handshake's peak %ld KB above the "
		"authorities\n", roots / 1024, (open_bytes - before - roots) / 1024, (peak - before - roots) / 1024);
	/* three at once, as three brokers over TLS */
	peak_reset();
	for (index = 0; index < 3; index++)
	{
		int each = connect_to(loopback(), port);

		handles[index] = each >= 0 ? posix_tls_open(each, host, ca, text, sizeof(text)) : -1;
	}
	for (index = 0; index < 3; index++)
	{
		result = handles[index] >= 0 ? handshake(handles[index], text, sizeof(text), 10000) : -1;
		if (result != 1)
			break;
	}
	check(index == 3, "three sessions at once shook hands");
	printf("     heap: three sessions open %ld KB, their handshakes' peak %ld KB above the authorities\n",
		(live() - before - roots) / 1024, (peak_reset() - before - roots) / 1024);
	for (index = 0; index < 3; index++)
		posix_tls_close(handles[index]);
	check(live() - before == roots, "every session's heap freed (the authorities kept)");
	posix_tls_close(handle);
	posix_tls_close(-1);
	posix_tls_close(POSIX_TLS_MAXIMUM);
	check(posix_tls_send(handle, "x", 1) < 0 && posix_tls_handshake(99, text, sizeof(text)) < 0,
		"a closed or unknown handle is refused");
	return 0;
}

static int case_refused(const char *host, int port, const char *ca, const char *why)
{
	char text[256];
	int socket = connect_to(loopback(), port), handle, result;
	unsigned long started = milliseconds();

	check(socket >= 0, "connected");
	if (socket < 0)
		return 1;
	handle = posix_tls_open(socket, host, strcmp(ca, "-") ? ca : "", text, sizeof(text));
	if (handle < 0)
	{
		/* (refused before any handshake: the authorities' file) */
		printf("     no session: %s\n", text);
		check(strstr(text, why) != NULL, why);
		posix_socket_close(socket);
		return 0;
	}
	result = handshake(handle, text, sizeof(text), 10000);
	printf("     %s (%lu ms)\n", text, milliseconds() - started);
	check(result == -1, "the handshake failed");
	check(strstr(text, why) != NULL, why);
	check(posix_tls_send(handle, "x", 1) < 0, "a failed session sends nothing");
	posix_tls_close(handle);
	return 0;
}

static int case_cancel(int port, const char *ca)
{
	char text[256];
	long before;
	int socket, handle, index;

	/* (the authorities loaded first, and PSA's key store, which keeps what
	a handshake's first key share made it take: a session whose
	ClientHello went, then closed) */
	socket = connect_to(loopback(), port);
	handle = socket >= 0 ? posix_tls_open(socket, "broker.halo.test", ca, text, sizeof(text)) : -1;
	sleep_ms(300);
	posix_tls_close(handle);
	sleep_ms(400);
	before = live();
	socket = connect_to(loopback(), port);
	handle = socket >= 0 ? posix_tls_open(socket, "broker.halo.test", ca, text, sizeof(text)) : -1;
	check(handle >= 0, "a session opened to a server that never answers");
	for (index = 0; index < 30; index++)
	{
		if (posix_tls_handshake(handle, text, sizeof(text)) != 0)
			break;
		sleep_ms(50);
	}
	check(index == 30, "its handshake stays under way");
	check(posix_tls_send(handle, "x", 1) < 0, "nothing is sent before the handshake");
	posix_tls_close(handle);
	check(posix_tls_handshake(handle, text, sizeof(text)) < 0, "closed: its handle is no more");
	for (index = 0; index < 40 && live() != before; index++)
		sleep_ms(50);
	if (live() != before)
		printf("     the heap: %ld bytes, %ld before\n", live(), before);
	check(live() == before, "its thread freed it (the heap as before)");
	/* every slot reusable */
	for (index = 0; index < POSIX_TLS_MAXIMUM; index++)
	{
		socket = connect_to(loopback(), port);
		handle = socket >= 0 ? posix_tls_open(socket, "broker.halo.test", ca, text, sizeof(text)) : -1;
		if (handle < 0)
			break;
	}
	check(index == POSIX_TLS_MAXIMUM, "every session slot taken again");
	socket = connect_to(loopback(), port);
	check(posix_tls_open(socket, "broker.halo.test", ca, text, sizeof(text)) < 0 && strstr(text, "too many"),
		"one more refused");
	posix_socket_close(socket);
	for (index = 0; index < POSIX_TLS_MAXIMUM; index++)
		posix_tls_close(index);
	for (index = 0; index < 40 && live() != before; index++)
		sleep_ms(50);
	check(live() == before, "all freed again");
	return 0;
}

static int case_stream(int port, const char *ca)
{
	enum { TOTAL = 16 << 20 };
	unsigned char *out = malloc(TOTAL), *in = malloc(TOTAL);
	char text[256];
	int socket = connect_to(loopback(), port), handle, sent = 0, received = 0, waits = 0;
	unsigned long started = milliseconds();
	int index;

	for (index = 0; index < TOTAL; index++)
		out[index] = (unsigned char)(index * 7 + (index >> 13));
	handle = socket >= 0 ? posix_tls_open(socket, "broker.halo.test", ca, text, sizeof(text)) : -1;
	check(handle >= 0 && handshake(handle, text, sizeof(text), 10000) == 1, "shook hands with the echo server");
	while (received < TOTAL && milliseconds() - started < 60000)
	{
		int count;

		if (sent < TOTAL)
		{
			/* (in pieces as the broker's output is: up to 4096) */
			int piece = TOTAL - sent > 4096 ? 4096 : TOTAL - sent;

			count = posix_tls_send(handle, out + sent, piece);
			if (count < 0)
				break;
			if (!count)
				waits++;
			sent += count;
		}
		count = posix_tls_receive(handle, in + received, TOTAL - received, text, sizeof(text));
		if (count < 0)
			break;
		received += count;
		if (!count && (sent == TOTAL || waits))
			sleep_ms(1);
	}
	printf("     %d bytes sent, %d back, the socket full %d times\n", sent, received, waits);
	check(waits > 0, "the sends waited on a full socket (posix_tls_send's 0)");
	check(received == TOTAL && !memcmp(in, out, TOTAL), "every byte came back once, in order");
	posix_tls_close(handle);
	free(out);
	free(in);
	return 0;
}

static int case_fuzz(int count)
{
	unsigned int seed = 12345;
	int round, refused = 0;
	char text[256];

	for (round = 0; round < count; round++)
	{
		int pair[2], handle, result = 0, step;
		unsigned char buffer[8192];

		if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
			return 1;
		posix_socket_set_nonblocking(pair[0], 1);
		handle = posix_tls_open(pair[0], "fuzz.halo.test", "", text, sizeof(text));
		if (handle < 0)
		{
			close(pair[0]);
			close(pair[1]);
			continue;
		}
		for (step = 0; step < 40 && !result; step++)
		{
			int size = 0, index, length;

			/* (what the client sent, read and dropped) */
			while (recv(pair[1], buffer, sizeof(buffer), MSG_DONTWAIT) > 0)
				;
			seed = seed * 1103515245u + 12345u;
			length = 1 + (int)((seed >> 8) % 600);
			if (seed & 0x30000000u)
			{
				/* a record: a handshake's (or alert's, or anything's) */
				buffer[size++] = (seed & 0x40000000u) ? 0x16 : (unsigned char)(seed >> 3);
				buffer[size++] = 3;
				buffer[size++] = (seed & 0x80000000u) ? 3 : (unsigned char)(seed >> 11);
				buffer[size++] = (unsigned char)(length >> 8);
				buffer[size++] = (unsigned char)length;
				if (seed & 0x08000000u)
				{
					/* a ServerHello's start */
					buffer[size++] = 2;
					buffer[size++] = 0;
					buffer[size++] = (unsigned char)((length - 4) >> 8);
					buffer[size++] = (unsigned char)(length - 4);
					buffer[size++] = 3;
					buffer[size++] = 3;
				}
			}
			for (index = size; index < size + length && index < (int)sizeof(buffer); index++)
			{
				seed = seed * 1103515245u + 12345u;
				buffer[index] = (unsigned char)(seed >> 16);
			}
			if (send(pair[1], buffer, (size_t)(index), MSG_DONTWAIT) < 0)
				break;
			sleep_ms(1);
			result = posix_tls_handshake(handle, text, sizeof(text));
		}
		/* (the server's end closed: the handshake must end) */
		close(pair[1]);
		if (!result)
			result = handshake(handle, text, sizeof(text), 3000);
		refused += result == -1;
		if (result == 1)
			printf("FAIL round %d: a handshake with random bytes succeeded\n", round);
		posix_tls_close(handle);
	}
	printf("     %d of %d random servers refused\n", refused, count);
	check(refused == count, "every random server's handshake failed");
	return 0;
}

static int case_internet(const char *host, int port, const char *topic, int plain)
{
	struct hostent *entry = gethostbyname(host);
	unsigned char hello[512];
	char text[256];
	struct heard heard;
	unsigned int address;
	int socket, handle = -1, size;

	check(entry && entry->h_addrtype == 2, "the broker's name resolved");
	if (!entry)
		return 1;
	memcpy(&address, entry->h_addr_list[0], 4);
	socket = connect_to(address, port);
	check(socket >= 0, "connected");
	if (socket < 0)
		return 1;
	if (!plain)
	{
		unsigned long started = milliseconds();

		handle = posix_tls_open(socket, host, "", text, sizeof(text));
		check(handle >= 0 && handshake(handle, text, sizeof(text), 10000) == 1, "TLS handshake, its certificate checked");
		printf("     %s (%lu ms)\n", text, milliseconds() - started);
	}
	size = mqtt_hello(hello, "hcev-tlstest", topic, 0);
	if (plain)
		check(posix_socket_send(socket, hello, size, 0) == size, "CONNECT and SUBSCRIBE sent");
	else
		check(send_all(handle, hello, size), "CONNECT and SUBSCRIBE sent over TLS");
	mqtt_listen(handle, plain ? socket : -1, &heard, 10000, 0);
	printf("     CONNACK %d, SUBACK %s, %d retained messages on %s\n", heard.connack, heard.suback ? "yes" : "no",
		heard.publishes, topic);
	check(heard.connack == 0 && heard.suback, "the broker took the connection and the subscription");
	if (plain)
		posix_socket_close(socket);
	else
		posix_tls_close(handle);
	return 0;
}

int main(int count, char **arguments)
{
	const char *name = count > 1 ? arguments[1] : "";

	if (!strcmp(name, "mqtt") && count == 5)
		case_mqtt(arguments[2], atoi(arguments[3]), arguments[4]);
	else if (!strcmp(name, "refused") && count == 6)
		case_refused(arguments[2], atoi(arguments[3]), arguments[4], arguments[5]);
	else if (!strcmp(name, "cancel") && count == 4)
		case_cancel(atoi(arguments[2]), arguments[3]);
	else if (!strcmp(name, "stream") && count == 4)
		case_stream(atoi(arguments[2]), arguments[3]);
	else if (!strcmp(name, "fuzz") && count == 3)
		case_fuzz(atoi(arguments[2]));
	else if (!strcmp(name, "internet") && (count == 5 || count == 6))
		case_internet(arguments[2], atoi(arguments[3]), arguments[4], count == 6);
	else
	{
		fprintf(stderr, "broker_tls_test CASE ...: see broker_tls_test.c\n");
		return 2;
	}
	/* (any handshake's thread done before the leak check) */
	sleep_ms(300);
	return failures ? 1 : 0;
}
