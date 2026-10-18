/*
NETWORK_CONNECTION.H

The connection the game plays over, switched live at the menus
(network_connection.c): Same Wi-Fi, Ad hoc or Online, the settings panel's
Connection, which the Halo 3 lobbies' NETWORK sets too.
*/

#ifndef __NETWORK_CONNECTION_H
#define __NETWORK_CONNECTION_H

/* (HALO_NETWORK_*'s order, system_link_shortcut.h) */
enum
{
	NETWORK_CONNECTION_WIFI,
	NETWORK_CONNECTION_ADHOC,
	NETWORK_CONNECTION_ONLINE,
	NUMBER_OF_NETWORK_CONNECTIONS
};

/* network_connection_status's */
enum
{
	NETWORK_STATUS_READY,
	/* connecting, or switching to another connection */
	NETWORK_STATUS_SWITCHING,
	/* no Wi-Fi, or Online failed (why said) */
	NETWORK_STATUS_FAILED,
};

/* the connection chosen (the setting), and the one running now (they
differ until the menus with no network game let it switch) */
short network_connection_wanted(void);
short network_connection_running(void);
/* the connection chosen, as the settings panel chooses it (kept as it
keeps it); switched at the next safe frame */
void network_connection_choose(short which);
/* the lobbies' status line: "Online", "Connecting...", "Same Wi-Fi", "Ad
hoc", "No Wi-Fi" or why Online failed (in the language chosen); a
NETWORK_STATUS_* */
int network_connection_status(char *text, int size);
/* (each frame, the main loop) the connection switched to the one chosen
when it may be */
void network_connection_update(boolean main_menu_loaded);

#endif
