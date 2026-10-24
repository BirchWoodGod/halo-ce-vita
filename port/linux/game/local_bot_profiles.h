/* Vita bot integration additions by kaboggs. */
#ifndef LOCAL_BOT_PROFILES_H
#define LOCAL_BOT_PROFILES_H
struct network_player;
struct player_datum;
void local_bot_profiles_begin(int count);
void local_bot_profiles_profile(int index, struct network_player *player);
int local_bot_profiles_kind(int index);
const wchar_t *local_bot_profiles_display_name(const struct player_datum *player);
#endif
