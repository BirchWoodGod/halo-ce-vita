/*
POSIX_HTTPS.C

One HTTPS GET for the update check (update_check.h), and the Vita
updater's downloads (posix_https_download, below; port/vita/host/
vita_update_download.c): Mbed TLS
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

posix_https_download streams a file to disk with the same TLS and limits
on headers; it follows a redirect only where its caller's allow-list says
(the updater's: GitHub's release-asset hosts, vita_update_verify.h), takes
a body only by Content-Length, at most the size given (exactly the signed
size for halo.vpk), resumes with Range, and has 30 s without a byte as its
deadline instead of a whole-request one.

posix_tls_* (at the end) carry internet play's brokers over TLS
(p2p_signal.c's fallback from their plain port): the same Mbed TLS,
non-blocking, the handshake on a thread of its own, against the authorities
of broker_roots.h.

Built with the host's ABI, as the other posix_*.c (and on the Vita with
the SDK's: tools/vita_build.py, with port/vita/include/mbedtls_vita_config.h;
on Linux with port/linux/src/mbedtls_linux_config.h).
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

#include "broker_roots.h"
#include "update_roots.h"
#ifdef POSIX_HTTPS_TEST_ROOTS
/* (tests only, never a game build: run_update_download_test.sh's own
authority, posix_https_test_roots_pem) */
#include POSIX_HTTPS_TEST_ROOTS
#endif

#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define CONNECT_TIMEOUT_SECONDS 8
#define TIMEOUT_SECONDS 15
#define HEADER_MAXIMUM 16384
#define HOST_MAXIMUM 254
/* (a release file's redirect to GitHub's storage is ~900 characters) */
#define PATH_MAXIMUM 2048

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
#ifdef POSIX_HTTPS_TEST_ROOTS
	roots_ready = roots_ready && mbedtls_x509_crt_parse(&roots, (const unsigned char *)posix_https_test_roots_pem,
		sizeof(posix_https_test_roots_pem)) == 0;
#endif
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
		int list[1], errors[1];
		int count = 1, error_count = 1, none = 0, result;
		time_t now = time(NULL);

		if (now >= deadline)
			return 0;
		list[0] = socket;
		errors[0] = socket;
		/* (a second at a time: the deadline is in whole seconds) */
		if (writing)
			result = posix_socket_select(NULL, &none, list, &count, errors, &error_count, 1, 0, 0);
		else
			result = posix_socket_select(list, &count, NULL, &none, NULL, &none, 1, 0, 0);
		if (result < 0)
			return 0;
		/* port: a connect that failed is in the error set, as Winsock has it
		(and not writable: once its error is read, it would look so) */
		if (writing && result > 0 && error_count > 0)
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

/* ---------- the connection made */

/* url's host looked up, connected to and (https) its certificate checked
in TLS's handshake, by connection->deadline; 1, else 0 with why in error
(the caller frees connection and config either way: connection_close) */
static int connection_open(const struct url *url, struct connection *connection, mbedtls_ssl_config *config,
	char *error, int error_size)
{
	struct
	{
		unsigned short family, port;
		unsigned int address;
		unsigned char zero[8];
	} address;
	posix_ulong ip;

	if (url->tls)
	{
		pthread_once(&roots_once, roots_load);
		if (!crypto_ready || !roots_ready)
		{
			snprintf(error, (size_t)error_size, "TLS not available");
			return 0;
		}
	}
	ip = posix_resolve_ipv4(url->host);
	if (!ip)
	{
		char why[64];

		posix_resolve_error(why, sizeof(why));
		snprintf(error, (size_t)error_size, "cannot look up %s (%s)", url->host, why);
		return 0;
	}
	connection->socket = posix_socket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
	if (connection->socket < 0)
	{
		snprintf(error, (size_t)error_size, "no socket (%d)", posix_socket_last_error());
		return 0;
	}
	posix_socket_set_nonblocking(connection->socket, 1);
	memset(&address, 0, sizeof(address));
	address.family = 2;
	address.port = (unsigned short)(url->port >> 8 | (url->port & 0xff) << 8);
	address.address = ip;
	if (posix_socket_connect(connection->socket, &address, sizeof(address)) < 0)
	{
		int code = posix_socket_last_error();

		time_t connect_deadline = time(NULL) + CONNECT_TIMEOUT_SECONDS;

		if (connect_deadline > connection->deadline)
			connect_deadline = connection->deadline;
		if ((code != WSAEWOULDBLOCK && code != WSAEINPROGRESS) || !socket_wait(connection->socket, 1, connect_deadline))
		{
			snprintf(error, (size_t)error_size, "cannot connect to %s (%d)", url->host, code);
			return 0;
		}
		/* port: a refused connection is writable too: its own error says so */
		{
			int socket_error = 0, length = (int)sizeof(socket_error);

			if (posix_socket_getsockopt(connection->socket, 0xffff /* SOL_SOCKET */, 0x1007 /* SO_ERROR */, &socket_error,
				&length) == 0 && socket_error)
			{
				snprintf(error, (size_t)error_size, "cannot connect to %s (%d)", url->host, socket_error);
				return 0;
			}
		}
	}
	if (url->tls)
	{
		int code;

		if (mbedtls_ssl_config_defaults(config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
			MBEDTLS_SSL_PRESET_DEFAULT) != 0)
		{
			snprintf(error, (size_t)error_size, "TLS set-up failed");
			return 0;
		}
		mbedtls_ssl_conf_min_tls_version(config, MBEDTLS_SSL_VERSION_TLS1_2);
		mbedtls_ssl_conf_authmode(config, MBEDTLS_SSL_VERIFY_REQUIRED);
		mbedtls_ssl_conf_ca_chain(config, &roots, NULL);
		mbedtls_ssl_conf_rng(config, random_bytes, NULL);
		if (mbedtls_ssl_setup(&connection->ssl, config) != 0 || mbedtls_ssl_set_hostname(&connection->ssl, url->host) != 0)
		{
			snprintf(error, (size_t)error_size, "TLS set-up failed");
			return 0;
		}
		mbedtls_ssl_set_bio(&connection->ssl, connection, bio_send, bio_receive, NULL);
		while ((code = mbedtls_ssl_handshake(&connection->ssl)) != 0)
		{
			if (code == MBEDTLS_ERR_SSL_WANT_READ || code == MBEDTLS_ERR_SSL_WANT_WRITE)
				continue;
			{
				unsigned int flags = mbedtls_ssl_get_verify_result(&connection->ssl);
				char why[128];

				if (code == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags != (unsigned int)-1 && flags)
				{
					mbedtls_x509_crt_verify_info(why, sizeof(why), "", flags);
					why[strcspn(why, "\n")] = 0;
					snprintf(error, (size_t)error_size, "%s's certificate refused: %s", url->host, why);
				}
				else
				{
					mbedtls_strerror(code, why, sizeof(why));
					snprintf(error, (size_t)error_size, "TLS handshake with %s failed: %s", url->host, why);
				}
			}
			return 0;
		}
		connection->tls = 1;
	}
	return 1;
}

static void connection_close(struct connection *connection, mbedtls_ssl_config *config)
{
	if (connection->tls)
		mbedtls_ssl_close_notify(&connection->ssl);
	mbedtls_ssl_free(&connection->ssl);
	mbedtls_ssl_config_free(config);
	if (connection->socket >= 0)
		posix_socket_close(connection->socket);
	connection->socket = -1;
	connection->tls = 0;
}

/* ---------- the request */

int posix_https_get(const char *url_text, const char *user_agent, char *body, int capacity, char *error, int error_size)
{
	struct url url;
	struct connection connection;
	mbedtls_ssl_config config;
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
	if (!connection_open(&url, &connection, &config, error, error_size))
		goto done;
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
	connection_close(&connection, &config);
	free(raw);
	return result;
}

/* ---------- a file downloaded (posix_https_download) */

#define IDLE_TIMEOUT_SECONDS 30
#define REDIRECT_MAXIMUM 5
#define URL_MAXIMUM (8 + HOST_MAXIMUM + 7 + PATH_MAXIMUM + 1)

enum
{
	DOWNLOAD_FAILED = 0,
	DOWNLOAD_DONE = 1,
	DOWNLOAD_CANCELLED = -1,
	DOWNLOAD_REDIRECT = 2,
};

/* a decimal number (1-10 digits, nothing else) of the length bytes at text:
1, else 0 */
static int read_decimal(const char *text, size_t length, unsigned long long *value)
{
	size_t index;

	*value = 0;
	if (!length || length > 10)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (text[index] < '0' || text[index] > '9')
			return 0;
		*value = *value * 10 + (unsigned long long)(text[index] - '0');
	}
	return 1;
}

/* "bytes <first>-<last>/<total>": 1, else 0 */
static int read_content_range(const char *text, size_t length, unsigned long long *first, unsigned long long *last,
	unsigned long long *total)
{
	const char *dash, *slash, *end = text + length;

	if (length < 6 || memcmp(text, "bytes ", 6))
		return 0;
	text += 6;
	dash = memchr(text, '-', (size_t)(end - text));
	slash = dash ? memchr(dash, '/', (size_t)(end - dash)) : NULL;
	return dash && slash && read_decimal(text, (size_t)(dash - text), first) &&
		read_decimal(dash + 1, (size_t)(slash - dash - 1), last) && read_decimal(slash + 1, (size_t)(end - slash - 1), total);
}

/* the size of the file at path, 0 when there is none */
static unsigned long long file_size(const char *path)
{
	FILE *file = fopen(path, "rb");
	long size;

	if (!file)
		return 0;
	size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
	fclose(file);
	return size > 0 ? (unsigned long long)size : 0;
}

/* one request for url: DOWNLOAD_DONE, _CANCELLED, _FAILED (why in error) or
_REDIRECT with where in next (URL_MAXIMUM bytes) */
static int download_once(const char *url_text, const char *user_agent, const char *path, posix_ulong expected_size,
	posix_ulong maximum, posix_https_progress_proc progress, void *context, char *next, char *error, int error_size)
{
	struct url url;
	struct connection connection;
	mbedtls_ssl_config config;
	char request[PATH_MAXIMUM + HOST_MAXIMUM + 512], range[48] = "", host[HOST_MAXIMUM + 8];
	char header[HEADER_MAXIMUM + 1];
	unsigned char buffer[16384];
	size_t header_size = 0;
	long header_end = -1;
	unsigned long long have = 0, content_length = 0, total, received = 0;
	unsigned long long range_first = 0, range_last = 0, range_total = 0;
	int status = 0, has_length = 0, chunked = 0, has_range = 0, request_length, result = DOWNLOAD_FAILED;
	const char *location = NULL;
	size_t location_length = 0;
	FILE *file = NULL;

	if (!url_parse(url_text, &url, error, error_size))
		return DOWNLOAD_FAILED;
	/* (what is there already: kept and resumed when its size is known) */
	if (expected_size)
	{
		have = file_size(path);
		if (have == expected_size)
			return DOWNLOAD_DONE;
		if (have > expected_size)
			have = 0;
	}
	memset(&connection, 0, sizeof(connection));
	connection.socket = -1;
	connection.deadline = time(NULL) + TIMEOUT_SECONDS;
	mbedtls_ssl_init(&connection.ssl);
	mbedtls_ssl_config_init(&config);
	if (!connection_open(&url, &connection, &config, error, error_size))
		goto done;
	connection.deadline = time(NULL) + IDLE_TIMEOUT_SECONDS;
	if (have)
		snprintf(range, sizeof(range), "Range: bytes=%llu-\r\n", have);
	if (url.port != (url.tls ? 443 : 80))
		snprintf(host, sizeof(host), "%s:%u", url.host, (unsigned int)url.port);
	else
		snprintf(host, sizeof(host), "%s", url.host);
	request_length = snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: application/octet-stream\r\n"
		"Accept-Encoding: identity\r\n%sConnection: close\r\n\r\n", url.path, host, user_agent, range);
	if (request_length <= 0 || request_length >= (int)sizeof(request) ||
		!connection_write(&connection, request, (size_t)request_length))
	{
		snprintf(error, (size_t)error_size, "cannot send the request");
		goto done;
	}
	/* the headers (and whatever of the body came with them) */
	while (header_end < 0)
	{
		int count;
		char *end;

		if (header_size >= HEADER_MAXIMUM)
		{
			snprintf(error, (size_t)error_size, "headers too large");
			goto done;
		}
		count = connection_read(&connection, header + header_size, HEADER_MAXIMUM - header_size);
		if (count <= 0)
		{
			snprintf(error, (size_t)error_size, count < 0 && time(NULL) >= connection.deadline ? "timed out" :
				"connection lost");
			goto done;
		}
		header_size += (size_t)count;
		header[header_size] = 0;
		end = strstr(header, "\r\n\r\n");
		if (end)
			header_end = (long)(end - header) + 4;
	}
	if (strncmp(header, "HTTP/1.", 7) || sscanf(header + 8, " %3d", &status) != 1)
	{
		snprintf(error, (size_t)error_size, "not an HTTP response");
		goto done;
	}
	{
		const char *line = strstr(header, "\r\n") + 2, *end = header + header_end - 2;

		while (line < end)
		{
			const char *line_end = strstr(line, "\r\n");
			size_t length = (size_t)(line_end - line), value_length;
			const char *value;

			if (header_is(line, length, "content-length"))
			{
				value = header_value(line, length, &value_length);
				if (has_length || !read_decimal(value, value_length, &content_length))
				{
					snprintf(error, (size_t)error_size, "bad Content-Length");
					goto done;
				}
				has_length = 1;
			}
			else if (header_is(line, length, "transfer-encoding"))
				chunked = 1;
			else if (header_is(line, length, "location"))
			{
				if (location)
				{
					snprintf(error, (size_t)error_size, "two Locations");
					goto done;
				}
				location = header_value(line, length, &location_length);
			}
			else if (header_is(line, length, "content-range"))
			{
				value = header_value(line, length, &value_length);
				if (has_range || !read_content_range(value, value_length, &range_first, &range_last, &range_total))
				{
					snprintf(error, (size_t)error_size, "bad Content-Range");
					goto done;
				}
				has_range = 1;
			}
			line = line_end + 2;
		}
	}
	if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
	{
		if (!location || !location_length || location_length >= URL_MAXIMUM)
		{
			snprintf(error, (size_t)error_size, "a redirect without a usable Location");
			goto done;
		}
		memcpy(next, location, location_length);
		next[location_length] = 0;
		result = DOWNLOAD_REDIRECT;
		goto done;
	}
	if (status == 200)
		have = 0;
	else if (status == 206 && have)
	{
		/* (exactly the rest asked for) */
		if (!has_range || range_first != have || range_last + 1 != range_total || !has_length ||
			content_length != range_total - have)
		{
			snprintf(error, (size_t)error_size, "the server sent another part than the one asked for");
			goto done;
		}
	}
	else
	{
		snprintf(error, (size_t)error_size, "the server answered %d", status);
		goto done;
	}
	if (chunked || !has_length)
	{
		snprintf(error, (size_t)error_size, chunked ? "a chunked download" : "no Content-Length");
		goto done;
	}
	total = have + content_length;
	if (total > maximum)
	{
		snprintf(error, (size_t)error_size, "the file is too large (%llu bytes)", total);
		goto done;
	}
	if (expected_size && total != expected_size)
	{
		snprintf(error, (size_t)error_size, "the file is %llu bytes, not %lu", total, (unsigned long)expected_size);
		goto done;
	}
	file = fopen(path, have ? "r+b" : "wb");
	if (!file || (have && fseek(file, (long)have, SEEK_SET) != 0))
	{
		snprintf(error, (size_t)error_size, "cannot write %s", path);
		goto done;
	}
	/* the body: what came with the headers, then the rest as it comes */
	{
		size_t already = header_size - (size_t)header_end;

		if (already > content_length)
			already = (size_t)content_length;
		if (already && fwrite(header + header_end, 1, already, file) != already)
		{
			snprintf(error, (size_t)error_size, "cannot write %s (is the memory card full?)", path);
			goto done;
		}
		received = already;
	}
	if (progress && progress(context, (posix_ulong)(have + received), (posix_ulong)total))
	{
		result = DOWNLOAD_CANCELLED;
		goto done;
	}
	while (received < content_length)
	{
		unsigned long long left = content_length - received;
		int count = connection_read(&connection, (char *)buffer, left < sizeof(buffer) ? (size_t)left : sizeof(buffer));

		if (count <= 0)
		{
			snprintf(error, (size_t)error_size, count < 0 && time(NULL) >= connection.deadline ? "timed out" :
				"connection lost");
			goto done;
		}
		connection.deadline = time(NULL) + IDLE_TIMEOUT_SECONDS;
		if (fwrite(buffer, 1, (size_t)count, file) != (size_t)count)
		{
			snprintf(error, (size_t)error_size, "cannot write %s (is the memory card full?)", path);
			goto done;
		}
		received += (unsigned long long)count;
		if (progress && progress(context, (posix_ulong)(have + received), (posix_ulong)total))
		{
			result = DOWNLOAD_CANCELLED;
			goto done;
		}
	}
	if (fclose(file) != 0)
	{
		file = NULL;
		snprintf(error, (size_t)error_size, "cannot write %s (is the memory card full?)", path);
		goto done;
	}
	file = NULL;
	result = DOWNLOAD_DONE;

done:
	if (file)
		fclose(file);
	connection_close(&connection, &config);
	return result;
}

int posix_https_download(const char *url, const char *user_agent, const char *path, posix_ulong expected_size,
	posix_ulong maximum, posix_https_redirect_proc redirect_allowed, posix_https_progress_proc progress, void *context,
	char *error, int error_size)
{
	char current[URL_MAXIMUM], next[URL_MAXIMUM];
	int redirects;

	error[0] = 0;
	if (!url || !path || !user_agent || strlen(user_agent) > 128 || strpbrk(user_agent, "\r\n") ||
		strlen(url) >= sizeof(current))
	{
		snprintf(error, (size_t)error_size, "bad arguments");
		return 0;
	}
	snprintf(current, sizeof(current), "%s", url);
	for (redirects = 0;; redirects++)
	{
		int result = download_once(current, user_agent, path, expected_size, maximum, progress, context, next, error,
			error_size);

		if (result != DOWNLOAD_REDIRECT)
			return result;
		if (redirects >= REDIRECT_MAXIMUM)
		{
			snprintf(error, (size_t)error_size, "too many redirects");
			return 0;
		}
		/* (only where the caller allows: never a host taken on trust) */
		if (!redirect_allowed || !redirect_allowed(current, next))
		{
			char shown[80];
			const char *host = strstr(next, "://");

			snprintf(shown, sizeof(shown), "%.79s", host ? host + 3 : next);
			shown[strcspn(shown, "/?")] = 0;
			snprintf(error, (size_t)error_size, "a redirect to a place not allowed (%s)", shown);
			return 0;
		}
		memcpy(current, next, sizeof(current));
	}
}

/* ---------- internet play's brokers over TLS (posix_tls_*; p2p_signal.c)

A broker's session: its socket (p2p_signal.c's, connected and non-blocking,
then this layer's), Mbed TLS's context over it, and a state the handshake's
thread and the caller share under tls_lock:

- handshaking: the handshake's thread steps it (the caller never touches
  its context, only asks how it goes); a session closed meanwhile is marked
  cancelled, and that thread frees it and closes its socket;
- open, or failed: the caller's alone (sends, receives, closes).

The thread runs while a handshake does (made again for the next): each pass
steps every handshake, then waits up to TLS_WAIT_MS for their sockets. Its
public key work (an ECDHE and the chain's signatures: tens of milliseconds
on the Vita) is thus never the caller's, whose thread carries the tunnels.
The authorities are parsed once, at the first session (none of this costs
anything while no broker needs TLS). Mbed TLS's own state shared by threads
(PSA's keys) is guarded by MBEDTLS_THREADING_C (the update check may run at
once). */

#define TLS_WAIT_MS 100
#define TLS_CA_FILE_MAXIMUM 65536
#define TLS_THREAD_STACK (256 * 1024)

enum
{
	_tls_free,
	_tls_handshaking,
	_tls_open,
	_tls_failed,
};

struct tls_session
{
	int state;
	/* closed while handshaking: the handshake's thread frees it */
	int cancelled;
	int socket;
	/* (the handshake's thread's) what its last step waits for */
	int wants_write;
	/* the bytes of a record that could not all go yet (posix_tls_send) */
	int write_pending;
	/* the handshake's time, and its longest step, in milliseconds */
	unsigned long started_ms;
	unsigned long longest_step_ms;
	unsigned long handshake_ms;
	char error[160];
	mbedtls_ssl_context ssl;
};

static pthread_mutex_t tls_lock = PTHREAD_MUTEX_INITIALIZER;
static struct tls_session tls_sessions[POSIX_TLS_MAXIMUM];
static int tls_thread_running;
/* the authorities: every broker's (broker_roots_pem, and ca_file's), and
test.mosquitto.org's (its own beside those); their configurations, which
the sessions share; loaded once (tls_loaded), with ca_file as it was then */
static int tls_loaded, tls_usable;
static char tls_load_error[160];
static mbedtls_x509_crt tls_roots, tls_mosquitto_roots;
static mbedtls_ssl_config tls_config, tls_mosquitto_config;

static unsigned long tls_milliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)now.tv_sec * 1000UL + (unsigned long)(now.tv_nsec / 1000000);
}

/* ca_file's certificates added to chain: 1, else 0 with why in error */
static int tls_add_file(mbedtls_x509_crt *chain, const char *ca_file, char *error, int error_size)
{
	FILE *file = fopen(ca_file, "rb");
	unsigned char *text;
	size_t size;
	int code;

	if (!file)
	{
		snprintf(error, (size_t)error_size, "the brokers' authorities file %s cannot be read", ca_file);
		return 0;
	}
	text = malloc(TLS_CA_FILE_MAXIMUM + 1);
	size = text ? fread(text, 1, TLS_CA_FILE_MAXIMUM + 1, file) : 0;
	fclose(file);
	if (!text || size == 0 || size > TLS_CA_FILE_MAXIMUM)
	{
		free(text);
		snprintf(error, (size_t)error_size, "the brokers' authorities file %s is empty or too large", ca_file);
		return 0;
	}
	/* (PEM: its size with the terminating zero) */
	text[size] = 0;
	code = mbedtls_x509_crt_parse(chain, text, size + 1);
	free(text);
	if (code != 0)
	{
		snprintf(error, (size_t)error_size, "the brokers' authorities file %s is not PEM certificates (%d)", ca_file,
			code);
		return 0;
	}
	return 1;
}

static int tls_configure(mbedtls_ssl_config *config, mbedtls_x509_crt *roots)
{
	if (mbedtls_ssl_config_defaults(config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
		MBEDTLS_SSL_PRESET_DEFAULT) != 0)
	{
		return 0;
	}
	mbedtls_ssl_conf_min_tls_version(config, MBEDTLS_SSL_VERSION_TLS1_2);
	mbedtls_ssl_conf_authmode(config, MBEDTLS_SSL_VERIFY_REQUIRED);
	mbedtls_ssl_conf_ca_chain(config, roots, NULL);
	mbedtls_ssl_conf_rng(config, random_bytes, NULL);
	return 1;
}

/* the authorities and configurations, once; under tls_lock */
static void tls_load(const char *ca_file)
{
	int good;

	tls_loaded = 1;
	mbedtls_x509_crt_init(&tls_roots);
	mbedtls_x509_crt_init(&tls_mosquitto_roots);
	mbedtls_ssl_config_init(&tls_config);
	mbedtls_ssl_config_init(&tls_mosquitto_config);
	if (psa_crypto_init() != PSA_SUCCESS)
	{
		snprintf(tls_load_error, sizeof(tls_load_error), "TLS not available (no randomness)");
		return;
	}
	/* (each must parse: a root left out would be noticed only when a
	broker moved to it) */
	good = mbedtls_x509_crt_parse(&tls_roots, (const unsigned char *)broker_roots_pem, sizeof(broker_roots_pem)) == 0 &&
		mbedtls_x509_crt_parse(&tls_mosquitto_roots, (const unsigned char *)broker_mosquitto_pem,
		sizeof(broker_mosquitto_pem)) == 0 &&
		mbedtls_x509_crt_parse(&tls_mosquitto_roots, (const unsigned char *)broker_roots_pem,
		sizeof(broker_roots_pem)) == 0;
	if (!good)
	{
		snprintf(tls_load_error, sizeof(tls_load_error), "TLS not available (its authorities did not parse)");
		return;
	}
	if (ca_file && ca_file[0] && (!tls_add_file(&tls_roots, ca_file, tls_load_error, sizeof(tls_load_error)) ||
		!tls_add_file(&tls_mosquitto_roots, ca_file, tls_load_error, sizeof(tls_load_error))))
	{
		return;
	}
	if (!tls_configure(&tls_config, &tls_roots) || !tls_configure(&tls_mosquitto_config, &tls_mosquitto_roots))
	{
		snprintf(tls_load_error, sizeof(tls_load_error), "TLS set-up failed");
		return;
	}
	tls_usable = 1;
}

/* the session's end: its context freed, its socket closed; under tls_lock */
static void tls_free(struct tls_session *session)
{
	mbedtls_ssl_free(&session->ssl);
	if (session->socket >= 0)
		posix_socket_close(session->socket);
	session->socket = -1;
	session->state = _tls_free;
	session->cancelled = 0;
}

static int tls_bio_send(void *context, const unsigned char *buffer, size_t length)
{
	struct tls_session *session = context;
	int size = length > 16384 ? 16384 : (int)length;
	int sent = posix_socket_send(session->socket, buffer, size, 0);

	if (sent >= 0)
		return sent;
	if (posix_socket_last_error() == WSAEWOULDBLOCK || posix_socket_last_error() == WSAEINPROGRESS)
		return MBEDTLS_ERR_SSL_WANT_WRITE;
	return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int tls_bio_receive(void *context, unsigned char *buffer, size_t length)
{
	struct tls_session *session = context;
	int size = length > 16384 ? 16384 : (int)length;
	int received = posix_socket_recv(session->socket, buffer, size, 0);

	if (received >= 0)
		return received;
	if (posix_socket_last_error() == WSAEWOULDBLOCK || posix_socket_last_error() == WSAEINPROGRESS)
		return MBEDTLS_ERR_SSL_WANT_READ;
	return MBEDTLS_ERR_NET_RECV_FAILED;
}

/* a handshake's step: under no lock (the session is this thread's while
handshaking); the state it leads to */
static int tls_step(struct tls_session *session)
{
	unsigned long before = tls_milliseconds(), took;
	int code = mbedtls_ssl_handshake(&session->ssl);

	took = tls_milliseconds() - before;
	if (took > session->longest_step_ms)
		session->longest_step_ms = took;
	if (code == MBEDTLS_ERR_SSL_WANT_READ || code == MBEDTLS_ERR_SSL_WANT_WRITE)
	{
		session->wants_write = code == MBEDTLS_ERR_SSL_WANT_WRITE;
		return _tls_handshaking;
	}
	session->handshake_ms = tls_milliseconds() - session->started_ms;
	if (code == 0)
		return _tls_open;
	{
		unsigned int flags = mbedtls_ssl_get_verify_result(&session->ssl);
		char why[128];

		if (code == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags != (unsigned int)-1 && flags)
		{
			mbedtls_x509_crt_verify_info(why, sizeof(why), "", flags);
			why[strcspn(why, "\n")] = 0;
			snprintf(session->error, sizeof(session->error), "its certificate refused: %s", why);
		}
		else if (code == MBEDTLS_ERR_SSL_CONN_EOF || code == MBEDTLS_ERR_NET_RECV_FAILED ||
			code == MBEDTLS_ERR_NET_SEND_FAILED)
		{
			snprintf(session->error, sizeof(session->error), "the connection %s in the TLS handshake",
				code == MBEDTLS_ERR_SSL_CONN_EOF ? "closed" : "broke");
		}
		else
		{
			mbedtls_strerror(code, why, sizeof(why));
			snprintf(session->error, sizeof(session->error), "TLS handshake failed: %s", why);
		}
	}
	return _tls_failed;
}

static void *tls_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		int indices[POSIX_TLS_MAXIMUM];
		int read[POSIX_TLS_MAXIMUM], write[POSIX_TLS_MAXIMUM];
		int count = 0, read_count = 0, write_count = 0, none = 0;
		int index;

		/* the handshakes to step (the cancelled ones freed) */
		pthread_mutex_lock(&tls_lock);
		for (index = 0; index < POSIX_TLS_MAXIMUM; index++)
		{
			struct tls_session *session = &tls_sessions[index];

			if (session->state != _tls_handshaking)
				continue;
			if (session->cancelled)
				tls_free(session);
			else
				indices[count++] = index;
		}
		if (!count)
		{
			tls_thread_running = 0;
			pthread_mutex_unlock(&tls_lock);
			return NULL;
		}
		pthread_mutex_unlock(&tls_lock);
		for (index = 0; index < count; index++)
		{
			struct tls_session *session = &tls_sessions[indices[index]];
			int state = tls_step(session);

			pthread_mutex_lock(&tls_lock);
			if (session->cancelled)
				tls_free(session);
			else
			{
				session->state = state;
				if (state == _tls_handshaking)
				{
					if (session->wants_write)
						write[write_count++] = session->socket;
					else
						read[read_count++] = session->socket;
				}
			}
			pthread_mutex_unlock(&tls_lock);
		}
		/* (the sockets of those still going: their descriptors stay open
		while they are handshaking, as only this thread closes them; a wait
		that fails is a wait all the same, never a loop that spins) */
		if ((read_count || write_count) &&
			posix_socket_select(read, &read_count, write, &write_count, NULL, &none, 0, TLS_WAIT_MS * 1000, 0) < 0)
		{
			read_count = write_count = 0;
			posix_socket_select(NULL, &read_count, NULL, &write_count, NULL, &none, 0, TLS_WAIT_MS * 1000, 0);
		}
	}
}

int posix_tls_open(int socket, const char *host, const char *ca_file, char *error, int error_size)
{
	struct tls_session *session = NULL;
	mbedtls_ssl_config *config;
	int index;

	error[0] = 0;
	if (socket < 0 || !host || !host[0] || strlen(host) > HOST_MAXIMUM)
	{
		snprintf(error, (size_t)error_size, "bad arguments");
		return -1;
	}
	pthread_mutex_lock(&tls_lock);
	if (!tls_loaded)
		tls_load(ca_file);
	if (!tls_usable)
	{
		snprintf(error, (size_t)error_size, "%s", tls_load_error);
		pthread_mutex_unlock(&tls_lock);
		return -1;
	}
	for (index = 0; index < POSIX_TLS_MAXIMUM; index++)
	{
		if (tls_sessions[index].state == _tls_free)
		{
			session = &tls_sessions[index];
			break;
		}
	}
	if (!session)
	{
		snprintf(error, (size_t)error_size, "too many TLS sessions");
		pthread_mutex_unlock(&tls_lock);
		return -1;
	}
	memset(session, 0, sizeof(*session));
	session->socket = -1;
	mbedtls_ssl_init(&session->ssl);
	/* (test.mosquitto.org's own authority for that host alone) */
	config = !strcmp(host, "test.mosquitto.org") ? &tls_mosquitto_config : &tls_config;
	if (mbedtls_ssl_setup(&session->ssl, config) != 0 || mbedtls_ssl_set_hostname(&session->ssl, host) != 0)
	{
		mbedtls_ssl_free(&session->ssl);
		snprintf(error, (size_t)error_size, "TLS set-up failed (out of memory?)");
		pthread_mutex_unlock(&tls_lock);
		return -1;
	}
	mbedtls_ssl_set_bio(&session->ssl, session, tls_bio_send, tls_bio_receive, NULL);
	session->socket = socket;
	session->started_ms = tls_milliseconds();
	session->state = _tls_handshaking;
	if (!tls_thread_running)
	{
		pthread_t thread;
		pthread_attr_t attributes;
		int made;

		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, TLS_THREAD_STACK);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		made = pthread_create(&thread, &attributes, tls_thread, NULL) == 0;
		pthread_attr_destroy(&attributes);
		if (!made)
		{
			/* (the socket stays the caller's) */
			session->socket = -1;
			tls_free(session);
			snprintf(error, (size_t)error_size, "no thread for the TLS handshake");
			pthread_mutex_unlock(&tls_lock);
			return -1;
		}
		tls_thread_running = 1;
	}
	pthread_mutex_unlock(&tls_lock);
	return index;
}

/* the session of a handle, NULL if it is none; under tls_lock */
static struct tls_session *tls_session_of(int handle)
{
	if (handle < 0 || handle >= POSIX_TLS_MAXIMUM || tls_sessions[handle].state == _tls_free ||
		tls_sessions[handle].cancelled)
	{
		return NULL;
	}
	return &tls_sessions[handle];
}

int posix_tls_handshake(int handle, char *error, int error_size)
{
	struct tls_session *session;
	int result;

	error[0] = 0;
	pthread_mutex_lock(&tls_lock);
	session = tls_session_of(handle);
	if (!session)
		result = -1, snprintf(error, (size_t)error_size, "no such TLS session");
	else if (session->state == _tls_handshaking)
		result = 0;
	else if (session->state == _tls_failed)
		result = -1, snprintf(error, (size_t)error_size, "%s", session->error);
	else
	{
		result = 1;
		snprintf(error, (size_t)error_size, "%s, %s, %lu ms (its longest step %lu ms)",
			mbedtls_ssl_get_version(&session->ssl), mbedtls_ssl_get_ciphersuite(&session->ssl),
			session->handshake_ms, session->longest_step_ms);
	}
	pthread_mutex_unlock(&tls_lock);
	return result;
}

/* the session of a handle, if open: the caller's alone then (no lock held
while it is used: only the caller closes it) */
static struct tls_session *tls_open_session(int handle)
{
	struct tls_session *session;

	pthread_mutex_lock(&tls_lock);
	session = tls_session_of(handle);
	if (session && session->state != _tls_open)
		session = NULL;
	pthread_mutex_unlock(&tls_lock);
	return session;
}

int posix_tls_send(int handle, const void *buffer, int size)
{
	struct tls_session *session = tls_open_session(handle);
	int length, sent;

	if (!session || size < 0)
		return -1;
	if (!size)
		return 0;
	/* (a record not all sent yet goes first: Mbed TLS takes the same
	length again, its bytes already in the record) */
	length = session->write_pending ? session->write_pending : size;
	if (length > size)
		return -1;
	sent = mbedtls_ssl_write(&session->ssl, buffer, (size_t)length);
	if (sent == MBEDTLS_ERR_SSL_WANT_WRITE || sent == MBEDTLS_ERR_SSL_WANT_READ)
	{
		session->write_pending = length;
		return 0;
	}
	session->write_pending = 0;
	if (sent <= 0 || sent > length)
		return -1;
	return sent;
}

int posix_tls_receive(int handle, void *buffer, int size, char *error, int error_size)
{
	struct tls_session *session = tls_open_session(handle);
	int tries;

	error[0] = 0;
	if (!session || size <= 0)
	{
		snprintf(error, (size_t)error_size, "no such TLS session");
		return -2;
	}
	/* (a TLS 1.3 session ticket is no data: read on, a few times) */
	for (tries = 0; tries < 4; tries++)
	{
		int received = mbedtls_ssl_read(&session->ssl, buffer, (size_t)size);

		if (received > 0)
			return received > size ? -2 : received;
		if (received == MBEDTLS_ERR_SSL_WANT_READ || received == MBEDTLS_ERR_SSL_WANT_WRITE)
			return 0;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
		if (received == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
			continue;
#endif
		if (received == 0 || received == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || received == MBEDTLS_ERR_SSL_CONN_EOF)
			return -1;
		{
			char why[96];

			mbedtls_strerror(received, why, sizeof(why));
			snprintf(error, (size_t)error_size, "TLS: %s", why);
		}
		return -2;
	}
	return 0;
}

int posix_tls_pending(int handle)
{
	struct tls_session *session = tls_open_session(handle);

	return session && (mbedtls_ssl_get_bytes_avail(&session->ssl) || mbedtls_ssl_check_pending(&session->ssl));
}

void posix_tls_close(int handle)
{
	struct tls_session *session;

	pthread_mutex_lock(&tls_lock);
	session = tls_session_of(handle);
	if (session)
	{
		if (session->state == _tls_handshaking)
			session->cancelled = 1;
		else
		{
			/* (its close_notify if it goes at once: the socket is
			non-blocking) */
			if (session->state == _tls_open)
				mbedtls_ssl_close_notify(&session->ssl);
			tls_free(session);
		}
	}
	pthread_mutex_unlock(&tls_lock);
}
