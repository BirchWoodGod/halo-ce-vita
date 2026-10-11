/*
POSIX_HTTPS.C

One HTTPS GET for the update check (update_check.h): Mbed TLS
(port/third_party/mbedtls) over the platform's own sockets (posix.h: vita_net.c
on the Vita, posix_net.c on Linux), so the Vita runs the same TLS as the
Linux harness and the tests, and none of the system's: its SceHttp/SceSsl
are not asked to speak a modern TLS or to know today's certificate
authorities.

- TLS 1.2 or 1.3; the server's certificate must chain to one of the few
  authorities built in (update_roots.h: GitHub's) and name the host, and
  nothing is sent before that is checked.
- A plain HTTP/1.1 GET with Connection: close; the request carries the
  User-Agent given (GitHub refuses requests without one) and nothing about
  the player or the machine; no token.
- No redirect is followed (a 3xx is a failure), whatever host it names.
- The response is read into a buffer of the size given and no further:
  headers at most HEADER_MAXIMUM bytes, the body (by Content-Length, or
  chunked) at most the caller's capacity; anything larger, malformed or cut
  short is a failure.
- Plain http:// is taken only for a loopback address (127.x.x.x), for the
  tests' local server.
- Every wait has a deadline: CONNECT_TIMEOUT_SECONDS to connect,
  TIMEOUT_SECONDS for the whole request. No cookie is kept or sent.

Built with the host's ABI, as the other posix_*.c (and on the Vita with
the SDK's: tools/vita_build.py, with port/vita/include/mbedtls_vita_config.h).
*/

#include "posix.h"

#include "mbedtls/error.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "update_roots.h"

#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define CONNECT_TIMEOUT_SECONDS 8
#define TIMEOUT_SECONDS 15
#define HEADER_MAXIMUM 16384
#define HOST_MAXIMUM 254
#define PATH_MAXIMUM 1024

/* ---------- certificate authorities and randomness */

static pthread_once_t roots_once = PTHREAD_ONCE_INIT;
static mbedtls_x509_crt roots;
static int roots_ready, crypto_ready;

static void roots_load(void)
{
	crypto_ready = psa_crypto_init() == PSA_SUCCESS;
	mbedtls_x509_crt_init(&roots);
	/* (each must parse: a root left out would be noticed only when GitHub
	moved to it) */
	roots_ready = mbedtls_x509_crt_parse(&roots, (const unsigned char *)update_roots_pem, sizeof(update_roots_pem)) ==
		0;
}

static int random_bytes(void *context, unsigned char *buffer, size_t size)
{
	(void)context;
	posix_random_bytes(buffer, (posix_ulong)size);
	return 0;
}

#ifdef MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG
/* (the Vita's: PSA's random numbers from the system's generator,
sceKernelGetRandomNumber, through posix_random_bytes) */
psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *context, uint8_t *output,
	size_t output_size, size_t *output_length)
{
	(void)context;
	posix_random_bytes(output, (posix_ulong)output_size);
	*output_length = output_size;
	return PSA_SUCCESS;
}
#endif

#ifdef MBEDTLS_PLATFORM_MS_TIME_ALT
/* (the Vita's: milliseconds for TLS 1.3's ticket ages, from the clock's
seconds) */
mbedtls_ms_time_t mbedtls_ms_time(void)
{
	return (mbedtls_ms_time_t)time(NULL) * 1000;
}
#endif

/* ---------- the connection */

struct connection
{
	int socket;
	time_t deadline;
	int tls;
	mbedtls_ssl_context ssl;
};

/* waits for the socket to be readable (or writable): 1, or 0 at the
deadline or on an error */
static int socket_wait(int socket, int writing, time_t deadline)
{
	for (;;)
	{
		int list[1];
		int count = 1, none = 0, result;
		time_t now = time(NULL);

		if (now >= deadline)
			return 0;
		list[0] = socket;
		/* (a second at a time: the deadline is in whole seconds) */
		if (writing)
			result = posix_socket_select(NULL, &none, list, &count, NULL, &none, 1, 0, 0);
		else
			result = posix_socket_select(list, &count, NULL, &none, NULL, &none, 1, 0, 0);
		if (result < 0)
			return 0;
		if (result > 0 && count > 0)
			return 1;
	}
}

static int bio_send(void *context, const unsigned char *buffer, size_t length)
{
	struct connection *connection = context;
	int size = length > 16384 ? 16384 : (int)length;

	for (;;)
	{
		int sent = posix_socket_send(connection->socket, buffer, size, 0);

		if (sent >= 0)
			return sent;
		if (posix_socket_last_error() != WSAEWOULDBLOCK || !socket_wait(connection->socket, 1, connection->deadline))
			return MBEDTLS_ERR_NET_SEND_FAILED;
	}
}

static int bio_receive(void *context, unsigned char *buffer, size_t length)
{
	struct connection *connection = context;
	int size = length > 16384 ? 16384 : (int)length;

	for (;;)
	{
		int received = posix_socket_recv(connection->socket, buffer, size, 0);

		if (received >= 0)
			return received;
		if (posix_socket_last_error() != WSAEWOULDBLOCK)
			return MBEDTLS_ERR_NET_RECV_FAILED;
		if (!socket_wait(connection->socket, 0, connection->deadline))
			return MBEDTLS_ERR_SSL_TIMEOUT;
	}
}

/* all of buffer sent: 1, else 0 */
static int connection_write(struct connection *connection, const char *buffer, size_t length)
{
	while (length)
	{
		int sent = connection->tls ? mbedtls_ssl_write(&connection->ssl, (const unsigned char *)buffer, length) :
			bio_send(connection, (const unsigned char *)buffer, length);

		if (sent == MBEDTLS_ERR_SSL_WANT_READ || sent == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		if (sent <= 0)
			return 0;
		buffer += sent;
		length -= (size_t)sent;
	}
	return 1;
}

/* some bytes: their count, 0 at the end, -1 on an error */
static int connection_read(struct connection *connection, char *buffer, size_t length)
{
	for (;;)
	{
		int received;

		if (!connection->tls)
		{
			received = bio_receive(connection, (unsigned char *)buffer, length);
			return received >= 0 ? received : -1;
		}
		received = mbedtls_ssl_read(&connection->ssl, (unsigned char *)buffer, length);
		if (received == MBEDTLS_ERR_SSL_WANT_READ || received == MBEDTLS_ERR_SSL_WANT_WRITE
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
			|| received == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
#endif
			)
		{
			continue;
		}
		if (received == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
			return 0;
		return received >= 0 ? received : -1;
	}
}

/* ---------- the URL */

struct url
{
	int tls;
	char host[HOST_MAXIMUM + 1];
	unsigned short port;
	char path[PATH_MAXIMUM + 1];
};

static int url_parse(const char *text, struct url *url, char *error, int error_size)
{
	const char *cursor;
	size_t length = 0;

	memset(url, 0, sizeof(*url));
	if (!strncmp(text, "https://", 8))
	{
		url->tls = 1;
		url->port = 443;
		cursor = text + 8;
	}
	else if (!strncmp(text, "http://", 7))
	{
		url->port = 80;
		cursor = text + 7;
	}
	else
	{
		snprintf(error, (size_t)error_size, "not an http(s) URL");
		return 0;
	}
	while ((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= '0' && *cursor <= '9') ||
		*cursor == '.' || *cursor == '-')
	{
		if (length >= HOST_MAXIMUM)
		{
			snprintf(error, (size_t)error_size, "host name too long");
			return 0;
		}
		url->host[length++] = *cursor++;
	}
	url->host[length] = 0;
	if (!length)
	{
		snprintf(error, (size_t)error_size, "no host");
		return 0;
	}
	if (*cursor == ':')
	{
		unsigned long port = 0;
		int digits = 0;

		for (cursor++; *cursor >= '0' && *cursor <= '9' && digits < 6; cursor++, digits++)
			port = port * 10 + (unsigned long)(*cursor - '0');
		if (!digits || port == 0 || port > 65535)
		{
			snprintf(error, (size_t)error_size, "bad port");
			return 0;
		}
		url->port = (unsigned short)port;
	}
	if (*cursor != '/' || strlen(cursor) > PATH_MAXIMUM)
	{
		snprintf(error, (size_t)error_size, "bad path");
		return 0;
	}
	for (length = 0; cursor[length]; length++)
		if ((unsigned char)cursor[length] <= ' ' || (unsigned char)cursor[length] >= 0x7f)
		{
			snprintf(error, (size_t)error_size, "bad path");
			return 0;
		}
	memcpy(url->path, cursor, length + 1);
	/* (plain HTTP only to this machine: the tests' server) */
	if (!url->tls)
	{
		unsigned int a, b, c, d;
		char end;

		if (sscanf(url->host, "%u.%u.%u.%u%c", &a, &b, &c, &d, &end) != 4 || a != 127 || b > 255 || c > 255 || d > 255)
		{
			snprintf(error, (size_t)error_size, "plain http only to a loopback address");
			return 0;
		}
	}
	return 1;
}

/* ---------- the response */

static int header_is(const char *line, size_t length, const char *name)
{
	size_t index, name_length = strlen(name);

	if (length <= name_length || line[name_length] != ':')
		return 0;
	for (index = 0; index < name_length; index++)
	{
		char character = line[index];

		if (character >= 'A' && character <= 'Z')
			character = (char)(character - 'A' + 'a');
		if (character != name[index])
			return 0;
	}
	return 1;
}

/* the header's value (after its colon and spaces), its length in *size */
static const char *header_value(const char *line, size_t length, size_t *size)
{
	const char *value = memchr(line, ':', length) + 1;
	const char *end = line + length;

	while (value < end && (*value == ' ' || *value == '\t'))
		value++;
	while (end > value && (end[-1] == ' ' || end[-1] == '\t'))
		end--;
	*size = (size_t)(end - value);
	return value;
}

/* the chunked body in raw (size bytes) decoded into body: its length, or
-1 if it is malformed, cut short or longer than capacity - 1 */
static long dechunk(const char *raw, size_t size, char *body, int capacity)
{
	size_t at = 0, total = 0;

	for (;;)
	{
		unsigned long chunk = 0;
		int digits = 0;

		while (at < size && digits < 8)
		{
			char character = raw[at];
			int value = character >= '0' && character <= '9' ? character - '0' : character >= 'a' && character <= 'f' ?
				character - 'a' + 10 : character >= 'A' && character <= 'F' ? character - 'A' + 10 : -1;

			if (value < 0)
				break;
			chunk = chunk << 4 | (unsigned long)value;
			digits++;
			at++;
		}
		if (!digits)
			return -1;
		/* (chunk extensions: skipped to the line's end) */
		while (at < size && raw[at] != '\r')
			at++;
		if (size - at < 2 || raw[at + 1] != '\n')
			return -1;
		at += 2;
		if (!chunk)
			break;
		if (chunk > size - at || chunk >= (unsigned long)capacity - total)
			return -1;
		memcpy(body + total, raw + at, chunk);
		total += chunk;
		at += chunk;
		if (size - at < 2 || raw[at] != '\r' || raw[at + 1] != '\n')
			return -1;
		at += 2;
	}
	body[total] = 0;
	return (long)total;
}

/* ---------- the request */

int posix_https_get(const char *url_text, const char *user_agent, char *body, int capacity, char *error, int error_size)
{
	struct url url;
	struct connection connection;
	mbedtls_ssl_config config;
	struct
	{
		unsigned short family, port;
		unsigned int address;
		unsigned char zero[8];
	} address;
	posix_ulong ip;
	char request[PATH_MAXIMUM + HOST_MAXIMUM + 512];
	char *raw = NULL;
	size_t raw_size = 0, raw_capacity;
	int request_length, result = -1;
	long header_end = -1, content_length = -1;
	int chunked = 0, status = 0;

	error[0] = 0;
	if (!body || capacity < 2 || !user_agent || strlen(user_agent) > 128 || strpbrk(user_agent, "\r\n"))
	{
		snprintf(error, (size_t)error_size, "bad arguments");
		return -1;
	}
	body[0] = 0;
	if (!url_parse(url_text, &url, error, error_size))
		return -1;
	memset(&connection, 0, sizeof(connection));
	connection.socket = -1;
	connection.deadline = time(NULL) + TIMEOUT_SECONDS;
	mbedtls_ssl_init(&connection.ssl);
	mbedtls_ssl_config_init(&config);
	if (url.tls)
	{
		pthread_once(&roots_once, roots_load);
		if (!crypto_ready || !roots_ready)
		{
			snprintf(error, (size_t)error_size, "TLS not available");
			goto done;
		}
	}
	ip = posix_resolve_ipv4(url.host);
	if (!ip)
	{
		char why[64];

		posix_resolve_error(why, sizeof(why));
		snprintf(error, (size_t)error_size, "cannot look up %s (%s)", url.host, why);
		goto done;
	}
	connection.socket = posix_socket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
	if (connection.socket < 0)
	{
		snprintf(error, (size_t)error_size, "no socket (%d)", posix_socket_last_error());
		goto done;
	}
	posix_socket_set_nonblocking(connection.socket, 1);
	memset(&address, 0, sizeof(address));
	address.family = 2;
	address.port = (unsigned short)(url.port >> 8 | (url.port & 0xff) << 8);
	address.address = ip;
	if (posix_socket_connect(connection.socket, &address, sizeof(address)) < 0)
	{
		int code = posix_socket_last_error();

		time_t connect_deadline = time(NULL) + CONNECT_TIMEOUT_SECONDS;

		if (connect_deadline > connection.deadline)
			connect_deadline = connection.deadline;
		if ((code != WSAEWOULDBLOCK && code != WSAEINPROGRESS) || !socket_wait(connection.socket, 1, connect_deadline))
		{
			snprintf(error, (size_t)error_size, "cannot connect to %s (%d)", url.host, code);
			goto done;
		}
	}
	if (url.tls)
	{
		int code;

		if (mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
			MBEDTLS_SSL_PRESET_DEFAULT) != 0)
		{
			snprintf(error, (size_t)error_size, "TLS set-up failed");
			goto done;
		}
		mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
		mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
		mbedtls_ssl_conf_ca_chain(&config, &roots, NULL);
		mbedtls_ssl_conf_rng(&config, random_bytes, NULL);
		if (mbedtls_ssl_setup(&connection.ssl, &config) != 0 || mbedtls_ssl_set_hostname(&connection.ssl, url.host) != 0)
		{
			snprintf(error, (size_t)error_size, "TLS set-up failed");
			goto done;
		}
		mbedtls_ssl_set_bio(&connection.ssl, &connection, bio_send, bio_receive, NULL);
		while ((code = mbedtls_ssl_handshake(&connection.ssl)) != 0)
		{
			if (code == MBEDTLS_ERR_SSL_WANT_READ || code == MBEDTLS_ERR_SSL_WANT_WRITE)
				continue;
			{
				unsigned int flags = mbedtls_ssl_get_verify_result(&connection.ssl);
				char why[128];

				if (code == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags != (unsigned int)-1 && flags)
				{
					mbedtls_x509_crt_verify_info(why, sizeof(why), "", flags);
					why[strcspn(why, "\n")] = 0;
					snprintf(error, (size_t)error_size, "%s's certificate refused: %s", url.host, why);
				}
				else
				{
					mbedtls_strerror(code, why, sizeof(why));
					snprintf(error, (size_t)error_size, "TLS handshake with %s failed: %s", url.host, why);
				}
			}
			goto done;
		}
		connection.tls = 1;
	}
	request_length = snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: application/vnd.github+json\r\n"
		"X-GitHub-Api-Version: 2022-11-28\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
		url.path, url.host, user_agent);
	if (request_length <= 0 || request_length >= (int)sizeof(request) ||
		!connection_write(&connection, request, (size_t)request_length))
	{
		snprintf(error, (size_t)error_size, "cannot send the request");
		goto done;
	}
	/* the response: headers and body, read to the connection's end (or
	Content-Length), at most raw_capacity */
	raw_capacity = (size_t)capacity + HEADER_MAXIMUM;
	raw = malloc(raw_capacity + 1);
	if (!raw)
	{
		snprintf(error, (size_t)error_size, "out of memory");
		goto done;
	}
	for (;;)
	{
		int received;

		if (raw_size >= raw_capacity)
		{
			snprintf(error, (size_t)error_size, "response too large");
			goto done;
		}
		received = connection_read(&connection, raw + raw_size, raw_capacity - raw_size);
		if (received < 0)
		{
			snprintf(error, (size_t)error_size, time(NULL) >= connection.deadline ? "timed out" : "connection lost");
			goto done;
		}
		if (received == 0)
			break;
		raw_size += (size_t)received;
		raw[raw_size] = 0;
		if (header_end < 0)
		{
			char *end = strstr(raw, "\r\n\r\n");

			if (!end && raw_size > HEADER_MAXIMUM)
			{
				snprintf(error, (size_t)error_size, "headers too large");
				goto done;
			}
			if (end)
			{
				const char *line = raw;

				header_end = (long)(end - raw) + 4;
				if (header_end > HEADER_MAXIMUM)
				{
					snprintf(error, (size_t)error_size, "headers too large");
					goto done;
				}
				if (strncmp(raw, "HTTP/1.", 7) || sscanf(raw + 8, " %3d", &status) != 1)
				{
					snprintf(error, (size_t)error_size, "not an HTTP response");
					goto done;
				}
				if (status != 200)
				{
					snprintf(error, (size_t)error_size, "the server answered %d", status);
					goto done;
				}
				while (line < end)
				{
					const char *line_end = strstr(line, "\r\n");
					size_t length = (size_t)(line_end - line), value_length;
					const char *value;

					if (header_is(line, length, "content-length"))
					{
						char number[16];

						value = header_value(line, length, &value_length);
						if (!value_length || value_length >= sizeof(number) || content_length >= 0)
						{
							snprintf(error, (size_t)error_size, "bad Content-Length");
							goto done;
						}
						memcpy(number, value, value_length);
						number[value_length] = 0;
						if (strspn(number, "0123456789") != value_length || value_length > 9)
						{
							snprintf(error, (size_t)error_size, "bad Content-Length");
							goto done;
						}
						content_length = atol(number);
						if (content_length >= capacity)
						{
							snprintf(error, (size_t)error_size, "response too large (%ld bytes)", content_length);
							goto done;
						}
					}
					else if (header_is(line, length, "transfer-encoding"))
					{
						value = header_value(line, length, &value_length);
						if (value_length != 7 || strncmp(value, "chunked", 7))
						{
							snprintf(error, (size_t)error_size, "unknown Transfer-Encoding");
							goto done;
						}
						chunked = 1;
					}
					line = line_end + 2;
				}
				if (chunked && content_length >= 0)
				{
					snprintf(error, (size_t)error_size, "both chunked and a length");
					goto done;
				}
				if (!chunked && content_length < 0)
				{
					snprintf(error, (size_t)error_size, "no Content-Length");
					goto done;
				}
			}
		}
		if (header_end >= 0 && content_length >= 0 && (long)raw_size - header_end >= content_length)
			break;
	}
	if (header_end < 0)
	{
		snprintf(error, (size_t)error_size, "response cut short");
		goto done;
	}
	if (chunked)
	{
		long length = dechunk(raw + header_end, raw_size - (size_t)header_end, body, capacity);

		if (length < 0)
		{
			snprintf(error, (size_t)error_size, "bad or cut short chunked body");
			goto done;
		}
		result = (int)length;
	}
	else
	{
		if ((long)raw_size - header_end < content_length)
		{
			snprintf(error, (size_t)error_size, "response cut short");
			goto done;
		}
		memcpy(body, raw + header_end, (size_t)content_length);
		body[content_length] = 0;
		result = (int)content_length;
	}

done:
	/* (nothing of a failed response is left for the caller) */
	if (result < 0)
		memset(body, 0, (size_t)capacity);
	if (connection.tls)
		mbedtls_ssl_close_notify(&connection.ssl);
	mbedtls_ssl_free(&connection.ssl);
	mbedtls_ssl_config_free(&config);
	if (connection.socket >= 0)
		posix_socket_close(connection.socket);
	free(raw);
	return result;
}
