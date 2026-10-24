/* Vita bot integration additions by kaboggs.
 * Keep ChaoticSPARTAN1 and KodeBreaker16 and their full scoreboard names.
 */
#include "cseries/cseries.h"
#include "cseries/cseries_windows.h"
#include "game/players.h"
#include "bots.h"
#include "local_bot_profiles.h"
#include "local_bot_profile_rules.h"
#include <stdlib.h>
#include <string.h>

static struct local_bot_profile_rules profiles;
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
void local_bot_profiles_begin(int count)
{
    const char *value=getenv("HALO_LOCAL_BOT_GHOST_CHANCE");
    int chance=value?atoi(value):0;
    uint32_t seed=(uint32_t)system_milliseconds()^(++serial*0x85ebca6bu);
    if (chance<0 || chance>100) chance=0;
    local_bot_profiles_make(&profiles,count,chance,seed);
    ready=chance>0 && count>0 && count<=15;
}

/* kaboggs: local_bot_profiles_kind - 1 ChaoticSPARTAN1, 2 KodeBreaker16. */
int local_bot_profiles_kind(int index)
{
    return ready && index>=0 && index<15?profiles.kind[index]:0;
}

/* kaboggs: local_bot_profiles_profile - team rules still choose the visible team color. */
void local_bot_profiles_profile(int index, struct network_player *player)
{
    const char *name, *preference;
    int i, kind;
    if (!ready || index<0 || index>=15 || !player) return;
    kind=profiles.kind[index];
    name=kind==1?"ChaoticSP1":kind==2?"KodeBrkr16":guests[profiles.guest[index]];
    for (i=0; i<(int)NUMBEROF(player->name)-1 && name[i]; ++i) player->name[i]=(wchar_t)(unsigned char)name[i];
    player->name[i]=0;
    preference=getenv("HALO_LOCAL_BOT_COLORS");
    if (!preference || atoi(preference)) player->primary_color_index=(short)profiles.color[index];
}

/* kaboggs: local_bot_profiles_display_name - retain full names beyond the wire profile limit. */
const wchar_t *local_bot_profiles_display_name(const struct player_datum *player)
{
    int index, kind;
    if (!player) return L"";
    index=(int)player->network_player_data.machine_index-BOTS_FIRST_MACHINE;
    kind=local_bot_profiles_kind(index);
    return kind==1?L"ChaoticSPARTAN1":kind==2?L"KodeBreaker16":player->name;
}
