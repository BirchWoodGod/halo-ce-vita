/*
VITA_UPDATE.H

The settings panel's update download (vita_update.c): once Check for
updates has found a newer version (update_check.h), pressing it again asks
the player, then downloads the release's signed halo.vpk, checks it and
keeps it in ux0:data/haloce-vita/update/ for the player to install with
VitaShell (the game is a safe homebrew: it cannot install itself).
vita_update_download.h does the work; vita_update_verify.h says what is
checked.
*/

#ifndef VITA_UPDATE_H
#define VITA_UPDATE_H

/* the player's press: with a newer version found and not downloaded yet,
the question, then the download on a thread of its own (the panel's
progress line, circle stops it); with it downloaded, where it is again.
0 when it acted, -1 when there is nothing to download (the caller looks for
a newer version) */
int vita_update_start(void);

/* whether a download is under way */
int vita_update_running(void);

/* the downloaded and verified VPK newer than this build: 1 with its
version and path, else 0 */
int vita_update_ready(char *version, int version_size, char *path, int path_size);

/* at start-up: the kept VPKs checked again on a thread of its own (one
installed since, or failing a check, removed); one to install shows in the
panel and the main menu (update_check_set_ready) */
void vita_update_scan_at_start(void);

#endif
