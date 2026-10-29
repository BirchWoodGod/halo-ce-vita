# File Share with friends (design)

Date: 2026-10-29. Branch `feat/fileshare-friends`, from `release-1.1.1`
(ddf7ce86). The owner's ask (Oct 18, confirmed Oct 25 and Oct 29): connect
File Share with friends, Halo 3's way: browse a friend's File Share (the map
variants and game variants they mark shared), download one with a prompt,
and "send to friend" from File Share. Online security is the owner's first
concern, so everything here keeps the friends' trust model
(`docs/superpowers/specs/2026-10-18-friends-design.md`) and adds nothing a
stranger can reach.

## The rules (agreed)

- **Friends only.** Every file-share message is a friends' pair message:
  sealed with the pair's key for one direction and signed by the sender's
  friend identity. Only an accepted friend's key opens one; a blocked
  player is no friend (their messages are not opened at all), a stranger
  has no pair key with this machine, a party member who is not a friend is
  refused, and a removed friend's tombstone answers nothing of it.
- **Variants only.** Two kinds: Forge map variants (`.hmv`,
  `port/linux/game/forge_files.h`) and game variants (the Xbox's game
  types, the 0x68-byte `struct game_variant` of `blam.lst`). Never a map,
  a resource map, a mod, a save or any other file: the kind is a byte with
  two values, the receiver never takes a file name from the sender, and
  what is saved is re-encoded by this machine's own writers.
- **A size cap.** A map variant at most 65,536 bytes (the format's own
  `FORGE_FILE_MAXIMUM_SIZE`), a game variant exactly 104 bytes. An offer
  outside it is refused before any byte of the file is asked for; a chunk
  past it fails the transfer.
- **Every received file through the existing validator before it is
  saved.** A map variant through `forge_file_decode` (every check of the
  Forge design, the one Forge itself reads files with), then written by
  `forge_store_save` (which checks the layout again, writes a `.tmp`, reads
  it back and checks it before it renames it in) under a name this machine
  makes from the file's hash (`forge_file_name_received`: `r0123abcd.hmv`).
  A game variant through the game variant checks: its name 1 to 11
  printable characters and terminated, its flags none, its engine one of the
  five, and `game_engine_variant_cleanup` (what every network game applies
  to a host's variant) changing nothing; then written by the game's own
  `playlist_profile_new` and `playlist_profile_save` (their checksum and
  their `SaveMeta.xbx`). A file that fails is dropped, never written, and
  the sender told it failed.
- **The player is asked before anything is saved.** A download from a
  friend's File Share starts only after the player says DOWNLOAD to the
  question naming the file, its kind, its map, its size and the friend. A
  friend's "send to friend" brings an offer (its name, kind, map, size,
  the friend) and nothing else: the game asks the player at the menus
  (Accept or Decline, the overlay's question, as a friend's invite is
  asked), and not one byte of the file is asked for before Accept. Nothing
  is kept on disk until the whole file has arrived, its SHA-256 is the one
  the signed offer named, and the validator passed it.
- **Nothing sent or offered while the player appears offline or has File
  Share sharing off.** With Appear offline on, this machine sends no
  file-share message of any kind (its asks, answers and transfers stop),
  and does not answer a friend's; with Share with friends
  (`friends.fileshare`) off, it answers a friend's browse and download with
  "not sharing", takes no offer, and sends none. A friend who appears
  offline (or whose presence says offline) is never asked or sent anything.
- **Rate limits** on every message, per friend and in all (below).

## What is reused

- **The friends' channel** (`port/linux/src/p2p_friends.c`): the brokers
  (`p2p_signal.c`), the pair keys (X25519 of the two identities, HMAC per
  direction), ChaCha20-Poly1305, the Ed25519 signature of every message
  with the label `hcev-friends-1`, the epoch and counter replay window,
  the outgoing queue that keeps the brokers' reserve for joiners and hosts.
  File share is `p2p_fileshare.inc`, part of `p2p_friends.c` as the party
  (`p2p_party.inc`) is: its messages are new pair message types (14 to 20),
  read by the same `pair_heard` (key tried, sender = the key that opened it,
  recipient = this machine, signature, replay) before anything of theirs
  is looked at.
- **File Share and Forge's files** (`port/linux/src/forge_store.c`,
  `port/linux/game/forge_files.c`): the list, the loader, the validator, the
  atomic writer, the received-file name.
- **The game variants' own code** (`source/saved games/playlist_profile.c`,
  `game_engine_variant_cleanup`), as the GAME TYPES editor uses it.
- **Map sharing's transfer ideas** (`map_share_protocol.c`): the receiving
  side keeps the file in memory only, checks the whole file's SHA-256
  against what the sender said, asks for data in windows the receiver
  controls, and gives up on a stall. Map sharing itself rides a joined
  game's connection to its host (TCP, the tunnel's KCP stream, the ad hoc
  bridge); browsing a friend's File Share happens at the menus with no game
  between the two machines, so the bytes ride the friends' channel instead.
  The relay (`port/relay`) carries only a game session's tunnel packets
  between that session's two peers and is not involved.
- **The lobby path** (the Forge late-join layout transfer,
  `forge_net_protocol.h`, and the game settings' variant): a friend who
  joins a Forge or map-variant game already receives the host's map variant
  through `forge_file_decode`, and every joiner the game variant through
  `game_engine_variant_cleanup`, to play them. Keeping them is not added:
  the lobby path has no question to the player and no size or kind beyond
  the game's own, and adding one would be a second, in-game path into File
  Share; the friend asks for the file through File Share instead (one path,
  the one with the prompt).
- **The menus and the settings panel**: the Halo 3 File Share
  (`menus/halo3/fileshare.xml`) and its Classic+ look
  (`menus/classic/fileshare.xml`), the FRIENDS screen's options, the
  invite question at the menus (`friends_game.c`), and the settings panel's
  Forge and File Share page and Friends page (`vita_settings.c`) for the
  original menus.

## Messages

Pair messages (kind `P`) between two friends, as presence and invites are:
envelope `'F'`, version 1, `'P'`, nonce, ChaCha20-Poly1305 under the pair's
key for that direction; inside, type, sender's key, recipient's key (this
machine: never zero), epoch, counter, the body, and the sender's Ed25519
signature of `"hcev-friends-1"` and all before it. Every body starts with an
id of 8 random bytes: the asker's for an ask or a download (echoed by the
answers), the sender's for an offer sent unasked.

| type | name | from | body after the id |
|---|---|---|---|
| 14 | FILES_ASK | browser | (none) |
| 15 | FILES_LIST | owner | page (1), pages (1), count (1), then count entries: kind (1), file id (8), size (4), map (1+31), title (1+31) |
| 16 | FILES_GET | browser | file id (8) |
| 17 | FILES_OFFER | owner / sender | flags (1: 1 sent unasked), kind (1), size (4), SHA-256 (32), map (1+31), title (1+31) |
| 18 | FILES_PULL | receiver | first chunk (2), count (1: 1 to 8) |
| 19 | FILES_CHUNK | owner / sender | chunk index (2), data (1 to 288 bytes) |
| 20 | FILES_END | either | reason (1) |

- A **file id** is the first 8 bytes of the file's SHA-256; a list entry's
  is the file as it is now, and the offer carries the whole SHA-256.
- **Kinds**: 1 map variant, 2 game variant. Any other byte: the message is
  malformed (dropped).
- **Texts** are printable ASCII (`'?'` for anything else), bounded, cleaned
  as every friends text is; they are for the lists only (a saved file keeps
  its own title, from inside it).
- **Chunks**: the file cut in 288-byte pieces, the last one shorter. A chunk
  must be the size its index says (288, or the remainder), for the transfer
  its id names, from the friend it is with.
- **END reasons**: 0 saved, 1 declined, 2 cancelled, 3 not sharing, 4 not
  shared any more (or changed), 5 busy, 6 too large, 7 refused by the
  validator or its SHA-256 wrong, 8 too soon.
- The largest message (a full chunk) is 512 bytes, P2P_FRIENDS_PAYLOAD_ROOM:
  the friends' bound on a message's body grows from 278 to 308 bytes for
  it. A build before this one decodes none of the new types (they are
  dropped as malformed, after its signature and replay checks); only an
  offer sent unasked ever reaches one (it never asks, so it is never
  answered), and an offer is 140 bytes, inside its room.

No network version changes: nothing here is a game message, and the
friends' envelope and topics are as they were.

## The conversations

Browse (A looks at B's File Share):

    A                                   B
    FILES_ASK (id)              ->
                                <-      FILES_LIST (id, page 0 of n) ... (n pages)
                                        or FILES_END (id, not sharing / too soon)

Download (A chose a file of the list and said DOWNLOAD):

    FILES_GET (id2, file id)    ->
                                <-      FILES_OFFER (id2, kind, size, SHA-256, map, title)
                                        or FILES_END (id2, not shared / busy / ...)
    FILES_PULL (id2, 0, 8)      ->      (A checks the offer against the list entry first:
                                <-       the same kind, file id and size, within the cap)
                                        FILES_CHUNK (id2, 0..7)
    FILES_PULL (id2, 8, 8)      ->
    ...
    [all chunks, SHA-256 the offer's, the validator passes, saved]
    FILES_END (id2, saved)      ->      (or "refused by the validator")

Send to friend (B chose a file and a friend, A):

                                <-      FILES_OFFER (id3, sent unasked, ...)
    [A's player asked at the menus: Accept / Decline]
    FILES_PULL (id3, 0, 8)      ->      (Accept)  ... as a download
    FILES_END (id3, declined)   ->      (Decline, or the question unanswered in 2 minutes)

The receiver drives the transfer: it asks for at most 8 chunks at a time,
and again for the ones still missing after 4 s with none new; the sender
sends only the chunks asked for, of a file it offered, while the outgoing
queue has room to spare. Either side may END a transfer at any time.

## Limits

- One download at a time on a machine; two uploads at once, one per friend.
- A friend's file-share messages: 48 at once, one more each 40 ms (checked
  before the signature's work); past that, dropped.
- Asks: one each 5 s sent per friend; one answered each 10 s per friend.
  Downloads asked: one each 5 s per friend. Offers sent unasked: one each
  15 s per friend; heard: one each 15 s per friend, four waiting at most
  (a fifth dropped), each waiting 2 minutes at most.
- Pulls: 8 chunks at most each, a friend's served 6 at once and one more
  each 500 ms; the chunks are queued only while 24 of the outgoing queue's
  64 places are free (presence, invites and the party keep theirs).
- A transfer with nothing new for 20 s, or longer than 3 minutes in all,
  fails (both sides drop it; the receiver keeps nothing).
- Shared files: 32 at most (the marks), listed in at most 8 pages.
- A friend removed or blocked, internet play stopped, friends off, Appear
  offline or Share with friends turned off: every transfer with them (or
  all) stops at once, its memory wiped and freed.

## The game's side

The p2p module knows no file: the game's main loop (`fileshare_friends.c`,
called by `friends_game.c` each frame) does the work that needs the game,
and hands it over:

- **The local list**: this machine's map variants (`forge_store_list`, every
  map's) and its player's own game variants (the game's own 27 are never
  shared), each marked shared or not (the marks: `friends/fileshare.dat`,
  32 at most, written and read back as `friends.dat` is: "HFSM", version,
  count, records, SHA-256; every name checked as File Share checks it).
  The shared ones are read and hashed, through the same loaders the game
  uses (`forge_store` and `forge_file_decode`; `playlist_profile_get_from_path`
  and its checksum), and only a file that passes is listed. Built again
  when the marks change, when a friend asks and the list is older than 10
  s, and when File Share opens.
- **Loads**: a friend's FILES_GET of a shared file, or the player's send,
  asks the game for its bytes; the game reads them through the same loader,
  checks that they still hash to what was listed (or, for a send, that they
  pass the validator), and hands them over. Sending a file that does not
  pass is refused here.
- **Received files**: the bytes, once whole and hashed right, go to the
  game's loop, which runs the validator and saves (above), and tells the
  p2p module how it went (the sender is told).
- **The questions**: a friend's offer is asked at the menus, out of any
  level and lobby (it waits up to 2 minutes; the lists show it meanwhile),
  as a friend's invite is (HALO_TEST_FILESHARE_ANSWER for the tests).

## Threats and mitigations

| threat | mitigation |
|---|---|
| A stranger sends files or reads the list | Every file-share message is a pair message: only a friend's key opens one, and its signature must be that friend's identity; a stranger, a blocked player (not a friend, never opened), a non-friend party member and a tombstone are refused. |
| A forged sender (one friend posing as another, or a relayed copy) | The sender inside must be the key that opened the envelope, the recipient this machine, the signature the sender's (as for presence); a flipped byte fails the AEAD. |
| Replays (an old offer, chunk or END sent again) | The friend's epoch and counter window drops it; an id must name a live transfer with that friend; a chunk already had is ignored. |
| A malicious file (a crafted `.hmv` or game variant) | Kind is one of two; size capped before anything is asked; the whole file hashed against the signed offer; the validator (`forge_file_decode`, the game variant checks and `game_engine_variant_cleanup`) before saving; saved by this machine's own writers, under a name it makes; never a map or any other file. |
| Path tricks (`../`, a resource map's name) | No file name ever comes from the sender. A map variant goes to its map's folder only if the map name is `[a-z0-9_]` (the validator's check), as `rXXXXXXXX.hmv`; a game variant gets a new saved game folder from the game. |
| Unwanted files | Nothing is asked for or kept without the player's answer to a question naming the friend and the file; an offer unanswered in 2 minutes is declined. |
| Flooding (CPU, memory, the brokers) | A friend's messages budgeted before the signature work; one download, two uploads; the receiver's window; queued chunks only while the outgoing queue has room; the brokers' bucket and reserve as for every friends message; each transfer's memory at most 64 KB, freed when it ends. |
| Presence leaks | Appear offline sends nothing of file share (not even an answer); only friends who show online are asked or sent anything. |
| Sharing more than meant | Browsing lists only files the player marked; Share with friends off answers "not sharing"; sending is one file, to one friend, chosen each time. |
| Broker sees content | Everything is end-to-end sealed for the pair; topics are the friends' mailbox hashes. A broker sees message sizes and timing (a transfer's burst), as it does for presence. |

Not protected (stated): a friend's copy is theirs (they can share it on);
traffic analysis at a broker can tell that two mailboxes exchange a burst
of messages; a malicious friend can make the receiver's player see an
offer with a misleading title (the question shows the friend's name, and
the file is validated whatever its title).

## UI

- **Halo 3 / Online menus** (`menus/halo3/fileshare.xml`): Start on a file
  opens SHARING: SHARE WITH FRIENDS (or STOP SHARING) and SEND TO A FRIEND
  (the friends online, A sends). The details say whether a file is shared;
  the status line says what a send came to. On the FRIENDS screen,
  Triangle on a friend online adds FILE SHARE: their shared files, with
  their kind, map and size; A asks DOWNLOAD? first.
- **Classic+** (`menus/classic/fileshare.xml`): the same functions in the
  Xbox look (the same widget names), Start on a file.
- **The settings panel** (the original menus): Multiplayer > Friends > a
  friend online: Square... (see vita_settings.c), File Share's rows on the
  Forge and File Share page: Share with friends (the setting), each map
  variant's shared mark (left and right), Send to a friend, and a friend's
  File Share from the Friends page. The offer question is the overlay's own
  in every menu style.

## Tests

- `port/vita/tests/p2p_friends_test.c` (three machines and a broker in one
  process): browse, download, send, decline, every refusal (non-friend,
  blocked, appearing offline, sharing off, oversized, wrong kind, a chunk
  of the wrong size, a wrong SHA-256, a forged sender, a replayed chunk),
  rate limits.
- `port/vita/tests/p2p_friends_fuzz.c`: the new message types in every
  fuzzed pair message, and the marks file.
- `run_netns_online_test.sh` modes `fsbrowse`, `fssend`, `fsrefuse`,
  `fsbad`: Vitas-as-PC in network namespaces (two or three copies).
