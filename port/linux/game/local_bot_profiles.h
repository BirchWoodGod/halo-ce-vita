/* SPDX-License-Identifier: GPL-3.0-only
 * Optional local bot profiles by kaboggs. */
#ifndef LOCAL_BOT_PROFILES_H
#define LOCAL_BOT_PROFILES_H
#include <stddef.h>
struct network_player;
struct player_datum;
/* Mutations run on the game/session thread, before play or during teardown. */
void local_bot_profiles_begin(unsigned int active_mask);
void local_bot_profiles_reset(void);
void local_bot_profiles_restore(int index, struct network_player *player);
void local_bot_profiles_profile(int index, struct network_player *player);
int local_bot_profiles_kind(int index);
const wchar_t *local_bot_profiles_display_name(const struct player_datum *player);
#endif
