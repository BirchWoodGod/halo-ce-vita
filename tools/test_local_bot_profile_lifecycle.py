# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual profile module and session callbacks with host stubs.

No commercial data, SDK, network or physical console is required. The stubs
model the consumed fields, not the Vita's ABI; target/device checks stay separate.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


def definition(path, name):
    text = (ROOT / path).read_text(encoding='utf-8')
    match = re.search(r'(?:static\s+)?void\s+' + name + r'\s*\([^;{}]*\)\s*\{', text)
    assert match, name
    depth = 1
    at = match.end()
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[match.start():at]


def test_profiles_do_not_escape_local_session(tmp_path):
    cc = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
    if not cc:
        pytest.skip('C compiler required')
    include = tmp_path / 'include'
    headers = {
        'cseries/cseries.h': '#include "model.h"\n',
        'cseries/cseries_windows.h': '#include "model.h"\n',
        'game/players.h': '#include "model.h"\n',
        'networking/network_game_globals.h': '#include "model.h"\n',
        'bots.h': '#include "model.h"\n',
        'model.h': r'''
#ifndef PROFILE_TEST_MODEL_H
#define PROFILE_TEST_MODEL_H
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#define NONE (-1)
#define FALSE 0
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 128
#define BOTS_FIRST_MACHINE 113
#define MAXIMUM_NETWORK_PLAYER_COUNT 16
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
typedef int boolean;
struct network_player { wchar_t name[12]; short primary_color_index;
    char machine_index,controller_index; short team_index; int valid; };
struct player_datum { wchar_t name[12]; int local_player_index;
    struct network_player network_player_data; };
struct network_game_server { struct {struct network_player players[16];} game; };
int system_milliseconds(void);
int network_game_is_splitscreen_local(void);
int bots_machine_is_bot(long machine);
int network_player_is_valid(const struct network_player *player);
#endif
''',
    }
    for name, text in headers.items():
        path = include / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
    callbacks = '\n'.join([
        definition('source/networking/network_server_manager.c', 'network_game_server_clear_local_bot_profiles'),
        definition('source/networking/network_server_manager.c', 'network_game_server_countdown_started'),
        definition('source/networking/network_game_globals.c', 'network_game_accept_remote_connections'),
    ])
    harness = tmp_path / 'lifecycle.c'
    harness.write_text(r'''
#include "model.h"
#include "local_bot_profiles.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static struct network_game_server server;
static struct network_player originals[2];
static struct {int accept_remote_connections;} bss_004566dc;
static struct network_game_server *global_network_game_server=&server;
static int network_game_server_countdown_slowed[128];
static int p2p_calls;
#define csmemset memset
int system_milliseconds(void) {return 12345;}
int network_game_is_splitscreen_local(void) {
    return global_network_game_server && !bss_004566dc.accept_remote_connections;
}
int bots_machine_is_bot(long machine) {return machine>=113 && machine<128;}
int network_player_is_valid(const struct network_player *p) {return p->valid;}
static void setting(const char *name,const char *value) {
#ifdef _WIN32
    assert(!_putenv_s(name,value?value:""));
#else
    assert(!(value?setenv(name,value,1):unsetenv(name)));
#endif
}
static void baseline(void) {
    struct network_player *p=&server.game.players[0];
    assert(!memcmp(p->name,originals[0].name,sizeof(p->name)));
    assert(p->primary_color_index==originals[0].primary_color_index);
    p=&server.game.players[1];
    assert(!memcmp(p->name,originals[1].name,sizeof(p->name)));
    assert(p->primary_color_index==originals[1].primary_color_index);
}
static void p2p_set_game_accepts_remote(int accepts) {
    if (accepts) {baseline();assert(local_bot_profiles_kind(0)==0);}
    ++p2p_calls;
}
''' + callbacks + r'''
int main(void) {
    int i,round;
    struct player_datum view={0};
    struct network_player scratch;
    memset(&server,0,sizeof(server));
    for(i=0;i<2;++i) {
        struct network_player *p=&server.game.players[i];
        wcscpy(p->name,i?L"Second":L"First");
        p->primary_color_index=(short)(i+3);p->machine_index=(char)(113+i);p->valid=1;
        originals[i]=*p;
    }
    setting("HALO_LOCAL_BOT_GHOST_CHANCE",NULL);
    network_game_server_countdown_started(&server);baseline();
    assert(!local_bot_profiles_kind(0));
    setting("HALO_LOCAL_BOT_GHOST_CHANCE","100");
    for(round=0;round<20;++round) {
        network_game_server_countdown_started(&server);
        assert(local_bot_profiles_kind(0)+local_bot_profiles_kind(1)==3);
        for(i=0;i<2;++i) {
            int kind=local_bot_profiles_kind(i);
            assert(server.game.players[i].primary_color_index==(kind==1?6:12));
            view.local_player_index=NONE;
            view.network_player_data=server.game.players[i];
            wcscpy(view.name,server.game.players[i].name);
            assert(!wcscmp(local_bot_profiles_display_name(&view),kind==1?L"ChaoticSPARTAN1":L"KodeBreaker16"));
        }
        server.game.players[0].team_index=7;
        network_game_accept_remote_connections(1);baseline();
        assert(server.game.players[0].team_index==7);
        assert(!local_bot_profiles_kind(0) && !local_bot_profiles_kind(1));
        network_game_server_countdown_started(&server);baseline();
        network_game_accept_remote_connections(0);
    }
    assert(p2p_calls==40);
    /* A direct mode change must also suppress all readers before cleanup. */
    network_game_server_countdown_started(&server);
    bss_004566dc.accept_remote_connections=1;
    assert(!local_bot_profiles_kind(0));
    wcscpy(view.name,L"Online");view.local_player_index=NONE;view.network_player_data.machine_index=113;
    assert(local_bot_profiles_display_name(&view)==view.name);
    scratch=originals[0];local_bot_profiles_profile(0,&scratch);
    assert(!memcmp(&scratch,&originals[0],sizeof(scratch)));
    network_game_server_clear_local_bot_profiles(&server);baseline();
    bss_004566dc.accept_remote_connections=0;
    /* Sparse bot machine slots retain one roll per present profile. */
    local_bot_profiles_begin((1u<<2)|(1u<<14));
    assert(local_bot_profiles_kind(2)+local_bot_profiles_kind(14)==3);
    assert(!local_bot_profiles_kind(0) && !local_bot_profiles_kind(13));
    view.local_player_index=0;
    assert(local_bot_profiles_display_name(&view)==view.name);
    view.local_player_index=NONE;view.network_player_data.machine_index=0;
    assert(local_bot_profiles_display_name(&view)==view.name);
    assert(!local_bot_profiles_kind(-1) && !local_bot_profiles_kind(15));
    local_bot_profiles_begin(1u<<15);assert(!local_bot_profiles_kind(2));
    setting("HALO_LOCAL_BOT_GHOST_CHANCE","100junk");
    local_bot_profiles_begin(3);assert(!local_bot_profiles_kind(0));
    setting("HALO_LOCAL_BOT_GHOST_CHANCE","-1");
    local_bot_profiles_begin(3);assert(!local_bot_profiles_kind(0));
    setting("HALO_LOCAL_BOT_GHOST_CHANCE","101");
    local_bot_profiles_begin(3);assert(!local_bot_profiles_kind(0));
    setting("HALO_LOCAL_BOT_GHOST_CHANCE","100");
    setting("HALO_LOCAL_BOT_COLORS","0");
    network_game_server_countdown_started(&server);
    for(i=0;i<2;++i) assert(server.game.players[i].primary_color_index==originals[i].primary_color_index);
    network_game_server_clear_local_bot_profiles(&server);baseline();
    global_network_game_server=NULL;
    local_bot_profiles_begin(3);assert(!local_bot_profiles_kind(0));
    return 0;
}
''', encoding='utf-8')
    output = tmp_path / ('lifecycle.exe' if os.name == 'nt' else 'lifecycle')
    subprocess.run([cc, '-std=c99', '-D_POSIX_C_SOURCE=200809L', '-DHALO_VITA=1', '-DHALO_LINUX=1',
                    '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(include),
                    '-I' + str(ROOT / 'port/linux/game'), str(harness),
                    str(ROOT / 'port/linux/game/local_bot_profiles.c'), '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True, timeout=15)
