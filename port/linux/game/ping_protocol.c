/*
PING_PROTOCOL.C

The scoreboard's pings' rules (ping_protocol.h).
*/

#include "ping_protocol.h"

#include <string.h>

uint16_t ping_entry_value(
	long milliseconds)
{
	if (milliseconds < 0)
		return (uint16_t)PING_UNKNOWN;
	return (uint16_t)(milliseconds > PING_MAXIMUM_MILLISECONDS ? PING_MAXIMUM_MILLISECONDS : milliseconds);
}

int ping_entries_read(
	void const *entries,
	int count,
	uint16_t *table,
	int players)
{
	uint16_t read[256];
	uint8_t named[256];
	int index;

	if (!entries || !table || players <= 0 || players > 256 || count < 0 || count > players)
		return -1;
	memset(named, 0, sizeof(named));
	for (index = 0; index < players; index++)
		read[index] = (uint16_t)PING_UNKNOWN;
	for (index = 0; index < count; index++)
	{
		struct ping_entry entry;

		/* (copied: the entries may sit at any byte of a datagram) */
		memcpy(&entry, (uint8_t const *)entries + index * sizeof(entry), sizeof(entry));
		if (entry.player_index >= players || named[entry.player_index] || entry.pad ||
			(entry.milliseconds > PING_MAXIMUM_MILLISECONDS && entry.milliseconds != PING_UNKNOWN))
		{
			return -1;
		}
		named[entry.player_index] = 1;
		read[entry.player_index] = entry.milliseconds;
	}
	memcpy(table, read, (size_t)players * sizeof(*table));
	return count;
}

int ping_message_due(
	struct ping_receiver *receiver,
	uint32_t now)
{
	/* (a clock that went back: as if long ago) */
	if (receiver->taken && now - receiver->last < PING_MINIMUM_INTERVAL_MILLISECONDS)
		return 0;
	receiver->last = now;
	receiver->taken = 1;
	return 1;
}
