/*
NETVERSION_FETCH.H

The signed crossplay version table's fetch and keeping (netversion_table.h,
netversion_fetch.c): the last good table kept on the memory card (the
Vita's ux0:data/haloce-vita/netversions.txt and .sig; the PC's save folder)
taken at start, then the owner's newest fetched from GitHub over the update
check's HTTPS (its pinned authorities, a redirect only to GitHub's release
storage, a few KB at most) at start and once a day while internet play is
on, in the background. A table is taken only when its signature is the
owner's, its form strict and its version higher than the one in effect;
anything else is logged and ignored, and the built-in list stands.

Never asked of the network: with network.crossplay_updates false (the
Vita's settings panel: Vita and PC, PC versions list), internet play off
(network.online; the Vita's Connection not Online), ad hoc play, no
address, a PC's update.auto false, a dedicated server, or a build that
does not play as a Vita (no crossplay wire).
*/

#ifndef HALO_NETVERSION_FETCH_H
#define HALO_NETVERSION_FETCH_H

/* the kept table taken (once), then the thread that fetches when due and
allowed started (once: it looks every 10 s, so a connection switched to
Online is seen within that); returns at once */
void netversion_fetch_start(void);

#endif
