/*
P2P_RESOLVER_CACHE.C

The last good IPv4 address of each name internet play looks up: its
signalling brokers (brokers.txt, network.signalling_brokers), STUN servers
(network.stun_servers) and relays (relays.txt, network.relays). A Vita's
resolver fails now and then for seconds at a time (halo.log: "cannot look up
the signalling broker ..." for all three at once); while it does, p2p.c's
p2p_resolve answers with the address the name had the last time it was looked
up, so that the brokers are still reached. A lookup that works always wins:
the cache only stands in for one that failed.

Only the names in those settings are kept: p2p.c allows each as it reads
them (p2p_resolver_cache_allow), and nothing else is stored or answered, so
nothing heard from the network (a peer's addresses, a listing) can ever be
put here. The cache is kept in memory and, unless network.resolver_cache_file
is empty, in a small text file beside config.toml (on the Vita
ux0:data/haloce-vita/data/dns_cache.txt), one name a line:

	broker.emqx.io 34.243.217.54 1791481094

(the name, its address and the Unix time it was looked up). The file is
checked line by line when read: a line that is not a plain host name, a
public or private unicast address and a time within the last
P2P_RESOLVER_CACHE_MAXIMUM_AGE seconds is left out. Deleting it is always
safe.

No locks here: p2p.c calls these under p2p_lock.
*/

#include "p2p_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cache_entry
{
	char host[P2P_RESOLVER_HOST_SIZE];
	/* network byte order */
	unsigned long address;
	/* Unix seconds it was looked up */
	unsigned long time;
};

static struct
{
	struct cache_entry entries[P2P_RESOLVER_CACHE_ENTRIES];
	int count;
	char allowed[P2P_RESOLVER_CACHE_ENTRIES][P2P_RESOLVER_HOST_SIZE];
	int allowed_count;
} cache;

/* a host name as kept: lower case; 0 if it is not one (letters, digits, "-"
and ".", 1 to P2P_RESOLVER_HOST_SIZE - 1 characters, not starting with "-"
or ".") */
static int host_clean(const char *host, char *clean)
{
	int length;

	if (!host || !host[0] || host[0] == '-' || host[0] == '.')
		return 0;
	for (length = 0; host[length]; length++)
	{
		char character = host[length];

		if (length >= P2P_RESOLVER_HOST_SIZE - 1)
			return 0;
		if (character >= 'A' && character <= 'Z')
			character = (char)(character - 'A' + 'a');
		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-' ||
			character == '.'))
		{
			return 0;
		}
		clean[length] = character;
	}
	clean[length] = 0;
	return 1;
}

/* dotted decimal, exactly four parts of 0 to 255, into network byte order;
0 if it is not that */
static int parse_address(const char *text, int length, unsigned long *address)
{
	unsigned char bytes[4];
	int part = 0, index = 0;

	while (part < 4)
	{
		int value = 0, digits = 0;

		while (index < length && text[index] >= '0' && text[index] <= '9' && digits < 4)
		{
			value = value * 10 + (text[index++] - '0');
			digits++;
		}
		if (!digits || digits > 3 || value > 255)
			return 0;
		bytes[part++] = (unsigned char)value;
		if (part < 4)
		{
			if (index >= length || text[index] != '.')
				return 0;
			index++;
		}
	}
	if (index != length)
		return 0;
	/* (as the sockets' in_addr holds it: the first byte first in memory) */
	*address = 0;
	memcpy(address, bytes, 4);
	return 1;
}

/* whether a name is an address already (nothing to keep for it) */
static int host_is_address(const char *host)
{
	unsigned long address;

	return parse_address(host, (int)strlen(host), &address);
}

int p2p_resolver_cache_usable_address(unsigned long address)
{
	const unsigned char *bytes = (const unsigned char *)&address;

	/* (this network, loopback, link-local, multicast and the reserved
	addresses above it: no broker is at any of them) */
	return bytes[0] != 0 && bytes[0] != 127 && bytes[0] < 224 && !(bytes[0] == 169 && bytes[1] == 254);
}

static struct cache_entry *find(const char *clean)
{
	int index;

	for (index = 0; index < cache.count; index++)
	{
		if (!strcmp(cache.entries[index].host, clean))
			return &cache.entries[index];
	}
	return NULL;
}

void p2p_resolver_cache_reset(void)
{
	memset(&cache, 0, sizeof(cache));
}

void p2p_resolver_cache_allow(const char *host)
{
	char clean[P2P_RESOLVER_HOST_SIZE];
	int index;

	if (!host_clean(host, clean) || host_is_address(clean))
		return;
	for (index = 0; index < cache.allowed_count; index++)
	{
		if (!strcmp(cache.allowed[index], clean))
			return;
	}
	if (cache.allowed_count < P2P_RESOLVER_CACHE_ENTRIES)
		memcpy(cache.allowed[cache.allowed_count++], clean, sizeof(clean));
}

int p2p_resolver_cache_allowed(const char *host)
{
	char clean[P2P_RESOLVER_HOST_SIZE];
	int index;

	if (!host_clean(host, clean))
		return 0;
	for (index = 0; index < cache.allowed_count; index++)
	{
		if (!strcmp(cache.allowed[index], clean))
			return 1;
	}
	return 0;
}

int p2p_resolver_cache_store(const char *host, unsigned long address, unsigned long now)
{
	char clean[P2P_RESOLVER_HOST_SIZE];
	struct cache_entry *entry;
	int changed;

	if (!host_clean(host, clean) || host_is_address(clean) || !p2p_resolver_cache_allowed(clean) ||
		!p2p_resolver_cache_usable_address(address))
	{
		return 0;
	}
	entry = find(clean);
	if (!entry)
	{
		if (cache.count < P2P_RESOLVER_CACHE_ENTRIES)
			entry = &cache.entries[cache.count++];
		else
		{
			/* (the one looked up longest ago gives way) */
			int index, oldest = 0;

			for (index = 1; index < cache.count; index++)
			{
				if (cache.entries[index].time < cache.entries[oldest].time)
					oldest = index;
			}
			entry = &cache.entries[oldest];
		}
		memset(entry, 0, sizeof(*entry));
		memcpy(entry->host, clean, sizeof(clean));
	}
	/* (written again only when the address changed, or a day after it was
	last written: the memory card is not written on every lookup) */
	changed = entry->address != address || now >= entry->time + 86400UL;
	entry->address = address;
	if (changed)
		entry->time = now;
	return changed;
}

int p2p_resolver_cache_lookup(const char *host, unsigned long now, unsigned long *address, unsigned long *stored_time)
{
	char clean[P2P_RESOLVER_HOST_SIZE];
	struct cache_entry *entry;

	if (!host_clean(host, clean) || !p2p_resolver_cache_allowed(clean))
		return 0;
	entry = find(clean);
	if (!entry || !entry->address)
		return 0;
	/* (too old: the broker may have moved long since) */
	if (now && entry->time && (now > entry->time + P2P_RESOLVER_CACHE_MAXIMUM_AGE || entry->time > now + 86400UL))
		return 0;
	*address = entry->address;
	if (stored_time)
		*stored_time = entry->time;
	return 1;
}

int p2p_resolver_cache_load(const char *text, unsigned long now)
{
	int taken = 0;

	while (text && *text)
	{
		const char *end = text + strcspn(text, "\r\n");
		char line[P2P_RESOLVER_HOST_SIZE + 48];
		int length = (int)(end - text);

		if (length > 0 && length < (int)sizeof(line) && text[0] != '#')
		{
			char host[P2P_RESOLVER_HOST_SIZE];
			char *space, *second;
			unsigned long address, stamp;
			char *stamp_end;

			memcpy(line, text, (size_t)length);
			line[length] = 0;
			space = strchr(line, ' ');
			second = space ? strchr(space + 1, ' ') : NULL;
			if (space && second)
			{
				*space = 0;
				stamp = strtoul(second + 1, &stamp_end, 10);
				if (host_clean(line, host) && !host_is_address(host) &&
					parse_address(space + 1, (int)(second - space - 1), &address) &&
					p2p_resolver_cache_usable_address(address) && second[1] >= '0' && second[1] <= '9' &&
					!*stamp_end && (!now || (stamp + P2P_RESOLVER_CACHE_MAXIMUM_AGE >= now && stamp <= now + 86400UL)) &&
					!find(host) && cache.count < P2P_RESOLVER_CACHE_ENTRIES)
				{
					struct cache_entry *entry = &cache.entries[cache.count++];

					memcpy(entry->host, host, sizeof(host));
					entry->address = address;
					entry->time = stamp;
					taken++;
				}
			}
		}
		text = end;
		while (*text == '\r' || *text == '\n')
			text++;
	}
	return taken;
}

int p2p_resolver_cache_save(char *text, int size)
{
	int length, index;

	if (size <= 0)
		return 0;
	length = snprintf(text, (size_t)size,
		"# The last good address of each broker, STUN server and relay internet play\n"
		"# looked up, used only while looking one up fails. Written by the game; safe\n"
		"# to delete.\n");
	for (index = 0; index < cache.count && length < size; index++)
	{
		const unsigned char *bytes = (const unsigned char *)&cache.entries[index].address;

		length += snprintf(text + length, (size_t)(size - length), "%s %u.%u.%u.%u %lu\n", cache.entries[index].host,
			bytes[0], bytes[1], bytes[2], bytes[3], cache.entries[index].time);
	}
	return length < size ? length : size - 1;
}
