/* SPDX-License-Identifier: GPL-3.0-only
 * Optional local bot profiles by kaboggs.
 * Keep ChaoticSPARTAN1 and KodeBreaker16 and their full scoreboard names.
 */
#include "cseries/cseries.h"
#include "cseries/cseries_windows.h"
#include "game/players.h"
#include "networking/network_game_globals.h"
#include "bots.h"
#include "local_bot_profiles.h"
#include "local_bot_profile_rules.h"
#include <stdlib.h>
#include <string.h>

static struct local_bot_profile_rules profiles;
static struct network_player original_players[15];
static unsigned int active_mask, original_mask;
static int ready;
static uint32_t serial;
static const char *const guests[]={
    "Sleepy","Dopey","Grumpy","Mopey","Hollywood","Crazy","Saucy","Wheezy",
    "Cupid","Killer","The Big L","Butcher","Darling","Disco","Donut","Hambone",
    "Dasher","Prancer","Noodle","Weasel","Snake","Shadow","Caboose","King",
    "Sneak","The Bear","Ghost","Penguin","Howard","Jack","Whisp","Whicker",
    "Wilshire","Pirate","Walla Walla","Tooth","Goat","Stumpy","Stompy","Winky"
};

/* kaboggs: local_bot_profiles_begin - one roll per local match countdown. */
void local_bot_profiles_reset(void)
{
    ready=0;
    active_mask=original_mask=0;
    memset(&profiles,0,sizeof(profiles));
    memset(original_players,0,sizeof(original_players));
}

void local_bot_profiles_begin(unsigned int present)
{
    struct local_bot_profile_rules rolled;
    const char *value=getenv("HALO_LOCAL_BOT_GHOST_CHANCE");
    char *end=NULL;
    long chance=value?strtol(value,&end,10):0;
    int count=0,slot,index=0;
    uint32_t seed=(uint32_t)system_milliseconds()^(++serial*0x85ebca6bu);
    local_bot_profiles_reset();
    if (!network_game_is_splitscreen_local() || (present & ~0x7fffu)) return;
    if (chance<0 || chance>100 || (value && (!*value || !end || *end))) return;
    for (slot=0;slot<15;++slot) if (present & (1u<<slot)) ++count;
    if (!count || !chance) return;
    local_bot_profiles_make(&rolled,count,(int)chance,seed);
    for (slot=0;slot<15;++slot) if (present & (1u<<slot)) {
        profiles.kind[slot]=rolled.kind[index];
        profiles.guest[slot]=rolled.guest[index];
        profiles.color[slot]=rolled.color[index++];
    }
    active_mask=present;
    ready=1;
}

/* kaboggs: local_bot_profiles_kind - 1 ChaoticSPARTAN1, 2 KodeBreaker16. */
int local_bot_profiles_kind(int index)
{
    return ready && network_game_is_splitscreen_local() && index>=0 && index<15 &&
        (active_mask & (1u<<index))?profiles.kind[index]:0;
}

/* kaboggs: local_bot_profiles_profile - team rules still choose the visible team color. */
void local_bot_profiles_profile(int index, struct network_player *player)
{
    const char *name, *preference;
    int i, kind;
    if (!ready || !network_game_is_splitscreen_local() || index<0 || index>=15 ||
        !(active_mask & (1u<<index)) || !player ||
        player->machine_index!=BOTS_FIRST_MACHINE+index) return;
    if (!(original_mask & (1u<<index))) {
        original_players[index]=*player;
        original_mask|=1u<<index;
    }
    kind=profiles.kind[index];
    name=kind==1?"ChaoticSP1":kind==2?"KodeBrkr16":guests[profiles.guest[index]];
    for (i=0; i<(int)NUMBEROF(player->name)-1 && name[i]; ++i) player->name[i]=(wchar_t)(unsigned char)name[i];
    player->name[i]=0;
    preference=getenv("HALO_LOCAL_BOT_COLORS");
    if (!preference || atoi(preference)) player->primary_color_index=(short)profiles.color[index];
}

/* Restore only the fields this extension owns; keep teams and lobby data. */
void local_bot_profiles_restore(int index, struct network_player *player)
{
    if (index<0 || index>=15 || !player || !(original_mask & (1u<<index))) return;
    if (player->machine_index==original_players[index].machine_index) {
        memcpy(player->name,original_players[index].name,sizeof(player->name));
        player->primary_color_index=original_players[index].primary_color_index;
    }
    original_mask&=~(1u<<index);
}

/* kaboggs: local_bot_profiles_display_name - retain full names beyond the wire profile limit. */
const wchar_t *local_bot_profiles_display_name(const struct player_datum *player)
{
    int index, kind;
    if (!player) return L"";
    if (!ready || !network_game_is_splitscreen_local() || player->local_player_index!=NONE ||
        !bots_machine_is_bot(player->network_player_data.machine_index)) return player->name;
    index=(int)player->network_player_data.machine_index-BOTS_FIRST_MACHINE;
    kind=local_bot_profiles_kind(index);
    return kind==1?L"ChaoticSPARTAN1":kind==2?L"KodeBreaker16":player->name;
}
