/* Vita bot integration additions by kaboggs. */
#ifndef KEVIN_BOTS_H
#define KEVIN_BOTS_H
struct network_player;
struct player_datum;
void kevin_bots_begin(int count);
void kevin_bots_profile(int index, struct network_player *player);
int kevin_bots_kind(int index);
const wchar_t *kevin_bots_display_name(const struct player_datum *player);
#endif
