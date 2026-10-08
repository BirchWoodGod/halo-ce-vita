/*
MAIN.C

halo-relay: the relay (relay.c) on one UDP port of a Linux machine. See
README.md for running it, and triage/relay-status.md for what it guards
against.
*/

#define _GNU_SOURCE

#include "relay.h"

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum
{
	DEFAULT_PORT = 47320,
	/* a summary in the log this often (milliseconds) */
	SUMMARY_INTERVAL = 10 * 60 * 1000,
};

static volatile sig_atomic_t stopping;
static volatile sig_atomic_t summary_wanted;
static int relay_socket = -1;
static int quiet;

static void on_stop(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}

static void on_summary(int signal_number)
{
	(void)signal_number;
	summary_wanted = 1;
}

static uint64_t now_milliseconds(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}

static void log_out(void *context, const char *line)
{
	(void)context;
	if (!quiet)
	{
		/* (no time stamp: journald and the shell add their own) */
		printf("%s\n", line);
		fflush(stdout);
	}
}

static void send_out(void *context, uint32_t address, uint16_t port, const uint8_t *data, size_t size)
{
	struct sockaddr_in to;

	(void)context;
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = address;
	to.sin_port = port;
	/* (a full buffer drops it, as the network may) */
	sendto(relay_socket, data, size, MSG_DONTWAIT, (struct sockaddr *)&to, sizeof(to));
}

static void summary(const struct relay *relay)
{
	const struct relay_statistics *statistics = &relay->statistics;

	printf("summary: %d allocations open; %llu opened, %llu paired; %llu packets (%llu B) relayed, %llu dropped "
		"over the rates, %llu cookies, %llu refused, %llu ignored\n", relay_open_allocations(relay),
		(unsigned long long)statistics->allocations_opened, (unsigned long long)statistics->allocations_ready,
		(unsigned long long)statistics->packets, (unsigned long long)statistics->bytes,
		(unsigned long long)statistics->dropped, (unsigned long long)statistics->cookies,
		(unsigned long long)statistics->refused, (unsigned long long)statistics->ignored);
	fflush(stdout);
}

static void usage(FILE *file)
{
	fprintf(file,
		"usage: halo-relay [options]\n"
		"  --bind ADDRESS        the IPv4 address to listen on (default 0.0.0.0)\n"
		"  --port PORT           the UDP port (default %d)\n"
		"  --allocations N       allocations at once (default 256, at most %d)\n"
		"  --per-address N       allocations one IPv4 address may be in (default 16)\n"
		"  --rate-kbit N         an allocation's rate, kbit/s both ways together (default 4000)\n"
		"  --burst-kb N          an allocation's burst, KB (default 256)\n"
		"  --packet-rate N       an allocation's packets a second (default 1000)\n"
		"  --max-mb N            an allocation's megabytes in all, 0 for no limit (default 1024)\n"
		"  --total-mbit N        every allocation's rate together, Mbit/s, 0 for no limit (default 0)\n"
		"  --lifetime-min N      an allocation's longest life, minutes (default 360)\n"
		"  --idle-sec N          an allocation both of whose machines are silent this long closes (default 60)\n"
		"  --quiet               log nothing but the summaries\n",
		DEFAULT_PORT, RELAY_MAXIMUM_ALLOCATIONS);
}

static int number(const char *text, unsigned long long maximum, unsigned long long *value)
{
	char *end;

	errno = 0;
	*value = strtoull(text, &end, 10);
	return !errno && *text && !*end && *value <= maximum;
}

int main(int argc, char **argv)
{
	static const struct option options[] =
	{
		{ "bind", required_argument, NULL, 'b' },
		{ "port", required_argument, NULL, 'p' },
		{ "allocations", required_argument, NULL, 'a' },
		{ "per-address", required_argument, NULL, 'A' },
		{ "rate-kbit", required_argument, NULL, 'r' },
		{ "burst-kb", required_argument, NULL, 'B' },
		{ "packet-rate", required_argument, NULL, 'P' },
		{ "max-mb", required_argument, NULL, 'm' },
		{ "total-mbit", required_argument, NULL, 't' },
		{ "lifetime-min", required_argument, NULL, 'l' },
		{ "idle-sec", required_argument, NULL, 'i' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	struct relay_config config;
	struct relay relay;
	struct sockaddr_in address;
	uint8_t secret[RELAY_KEY_SIZE * 2];
	unsigned long long value, port = DEFAULT_PORT;
	const char *bind_text = "0.0.0.0";
	uint64_t expired_time, summary_time;
	int option, option_index = 0;

	relay_default_config(&config);
	while ((option = getopt_long(argc, argv, "h", options, &option_index)) != -1)
	{
		int good = 1;

		switch (option)
		{
		case 'b': bind_text = optarg; break;
		case 'p': good = number(optarg, 65535, &port) && port; break;
		case 'a': good = number(optarg, RELAY_MAXIMUM_ALLOCATIONS, &value) && value;
			config.maximum_allocations = (int)value; break;
		case 'A': good = number(optarg, RELAY_MAXIMUM_ALLOCATIONS, &value) && value;
			config.maximum_per_address = (int)value; break;
		case 'r': good = number(optarg, 8000000, &value) && value; config.rate = value * 1000 / 8; break;
		case 'B': good = number(optarg, 1000000, &value) && value >= 2; config.burst = value * 1024; break;
		case 'P': good = number(optarg, 1000000, &value) && value; config.packet_rate = (uint32_t)value; break;
		case 'm': good = number(optarg, 1000000000, &value); config.maximum_bytes = value * 1024 * 1024; break;
		case 't': good = number(optarg, 8000, &value); config.total_rate = value * 1000000 / 8; break;
		case 'l': good = number(optarg, 100000, &value) && value; config.lifetime = value * 60 * 1000; break;
		case 'i': good = number(optarg, 100000, &value) && value; config.idle_time = value * 1000; break;
		case 'q': quiet = 1; break;
		case 'h': usage(stdout); return 0;
		default: usage(stderr); return 2;
		}
		if (!good)
		{
			fprintf(stderr, "halo-relay: --%s %s is out of range\n", options[option_index].name, optarg);
			usage(stderr);
			return 2;
		}
	}
	if (optind != argc)
	{
		usage(stderr);
		return 2;
	}
	if (getrandom(secret, sizeof(secret), 0) != (ssize_t)sizeof(secret))
	{
		perror("halo-relay: getrandom");
		return 1;
	}
	if (!relay_initialize(&relay, &config, secret, send_out, log_out, NULL))
	{
		fprintf(stderr, "halo-relay: the settings are out of range, or there is no memory\n");
		return 1;
	}
	memset(secret, 0, sizeof(secret));

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, bind_text, &address.sin_addr) != 1)
	{
		fprintf(stderr, "halo-relay: --bind %s is not an IPv4 address\n", bind_text);
		return 2;
	}
	relay_socket = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (relay_socket < 0 || bind(relay_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		perror("halo-relay: cannot listen");
		return 1;
	}
	{
		int size = 4 << 20;

		setsockopt(relay_socket, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
		setsockopt(relay_socket, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
	}
	signal(SIGINT, on_stop);
	signal(SIGTERM, on_stop);
	signal(SIGUSR1, on_summary);
	signal(SIGPIPE, SIG_IGN);
	printf("halo-relay: listening on %s:%llu (UDP); %d allocations, %d per address, %llu kbit/s and %llu MB each\n",
		bind_text, port, config.maximum_allocations, config.maximum_per_address,
		(unsigned long long)(config.rate * 8 / 1000), (unsigned long long)(config.maximum_bytes / 1024 / 1024));
	fflush(stdout);

	expired_time = summary_time = now_milliseconds();
	while (!stopping)
	{
		struct pollfd wait = { relay_socket, POLLIN, 0 };
		uint64_t now;
		int count;

		if (poll(&wait, 1, 1000) < 0 && errno != EINTR)
		{
			perror("halo-relay: poll");
			break;
		}
		/* (a pass reads what is there, a bounded amount) */
		for (count = 0; count < 4096; count++)
		{
			uint8_t packet[2048];
			struct sockaddr_in from;
			socklen_t from_size = sizeof(from);
			ssize_t size = recvfrom(relay_socket, packet, sizeof(packet), 0, (struct sockaddr *)&from, &from_size);

			if (size < 0)
				break;
			if (from_size != sizeof(from) || from.sin_family != AF_INET)
				continue;
			relay_received(&relay, packet, (size_t)size, from.sin_addr.s_addr, from.sin_port, now_milliseconds());
		}
		now = now_milliseconds();
		if (now - expired_time >= 1000)
		{
			relay_expire(&relay, now);
			expired_time = now;
		}
		if (summary_wanted || now - summary_time >= SUMMARY_INTERVAL)
		{
			summary_wanted = 0;
			summary_time = now;
			summary(&relay);
		}
	}
	summary(&relay);
	relay_release(&relay);
	close(relay_socket);
	return 0;
}
