/*
VITA_UPDATE.H

The settings panel's update, installed in the game (vita_update.c): once
Check for updates has found a newer version (update_check.h), pressing it
again asks the player, then downloads the release's signed halo.vpk,
checks it, installs it and restarts the game into it
(vita_update_install.h does the work; vita_update_verify.h says what is
checked).
*/

#ifndef VITA_UPDATE_H
#define VITA_UPDATE_H

/* the player's press with an update found: the question, then the work on
a thread of its own (the panel's progress line, circle stops it until the
install starts); -1 when one is under way already */
int vita_update_start(void);

/* whether one is under way */
int vita_update_running(void);

/* at start-up, before the game: an install cut short (a crash, a power cut)
undone, and the game restarted when its files were put back */
void vita_update_recover_at_start(void);

#endif
