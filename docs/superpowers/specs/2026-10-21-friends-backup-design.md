# Friend code backup (design)

Date: 2026-10-21. Branch `feat/friends-backup`, from `sec/review-followups`
(691650eab, itself on `1.1.1-insider.20`, 6f4efacf; `fix/party-replay-drops`
is already in insider.20). Code: `port/linux/src/p2p_friends_backup.inc`
(part of `p2p_friends.c`), the crypto in `p2p_crypto.c`, the menus in
`menu_functions.c` (Halo 3 FRIENDS) and `vita_settings.c` (the settings
panel). The friends design this builds on:
`2026-10-18-friends-design.md`.

## The problem

Each Vita has one friend identity: an Ed25519 seed made once and kept in
`ux0:data/haloce-vita/friends/identity.dat`, its friend code the hash of
its key. The owner's rule: one friend code per Vita, friends see the
profile name. A player who loses the memory card or reinstalls loses the
code and every friendship (each friend would have to add the new code).

## What the player does

**Back up** (Halo 3 FRIENDS: the FRIEND CODE BACKUP row, then BACK UP
FRIEND CODE; the settings panel: Multiplayer > Friends > Friend code backup,
Cross): a file name (default `friends-backup`), a password, the password
again, then "with the friends list" or "the friend code alone". The file is
`ux0:data/haloce-vita/<name>.hfb` (on a PC, in the save folder). It holds
the identity, and the list if chosen. The screens say: anyone with the file
and its password can be you; keep both safe.

**Restore** (FRIEND CODE BACKUP, then RESTORE FRIEND CODE; the panel's
Square): the file's name, its password. The file is opened and checked
first (a wrong password or a damaged file is said, nothing changes). Then
the Halo 3 question, CONFIRM or CANCEL: "Your friend code ABCD-... becomes
WXYZ-... (n friends in the backup)". Confirmed: the identity it replaces is
kept in `friends/replaced-<date>/` (its identity.dat, friends.dat and
marks.dat, as they were), then the backup's is this Vita's: the same friend
code; the friends list too if the file has one. No restart: the brokers'
topics follow the new code at the next pass.

Passwords: 8 to 64 printable ASCII characters (what the Vita's keyboard and
the PC text box type); spaces at either end are ignored, since the Halo 3
menus' typing drops them and the panel's keeps them. Names: letters,
digits, `-` and `_`, 32 at most; `.hfb` is added. No path can be typed.

## The file

`BACKUP_FILE_SIZE` bytes (about 24 KB) whatever the list holds, so its
size says nothing of the friends.

| Offset | Size | Header (the AEAD's additional data) |
|---|---|---|
| 0 | 4 | `HFBK` |
| 4 | 1 | version (1) |
| 5 | 1 | key derivation (1: Argon2id) |
| 6 | 1 | flags (1: the friends list is in it) |
| 7 | 1 | 0 |
| 8 | 4 | Argon2id memory, KiB blocks (8192) |
| 12 | 4 | Argon2id passes (5) |
| 16 | 16 | salt (random) |
| 32 | 24 | nonce (random) |
| 56 | 4 | the contents' size |
| 60 | n | the contents, XChaCha20-Poly1305 |
| 60+n | 16 | its tag |

The contents: `HFBP`, version 1, the flags again, two zeros, the seed (32),
its public key (32), the epoch of the run that wrote it (u64), when it was
written (u64), friends.dat's size and marks.dat's (u32s), then those files'
own bytes (each with its SHA-256), zeros to the end. Integers big-endian.

## Crypto

- Key: Argon2id (Monocypher's `crypto_argon2`, in the tree, the same
  library that does the game's Ed25519 and the lobby passwords), 8 MiB,
  five passes, one lane: at least OWASP's Argon2id floor (7 MiB, five
  passes). About a second on a Vita (the lobby's 4 MiB and four passes take
  half a second), and the same for each guess of an attacker. The work area
  is malloc'd; no memory says so ("try from the main menu").
- Encryption: XChaCha20-Poly1305 (`crypto_aead_lock`), a random 24-byte
  nonce, the 60-byte header as additional data: any change to the header
  (the flags, the parameters, the salt) or the contents fails the tag.
- A file may ask the key for 8 to 32 MiB and 3 to 32 passes, memory times
  passes no less than 8 MiB times 5: a file asking for less is refused (a
  tampered file cannot make a guess cheaper for its maker, but a weak file
  should never be taken as one of ours), one asking for more than a Vita
  spares is refused before any memory is taken.
- Never kept: the password (copied once into the job, wiped once the key is
  made), the key (wiped at once), the contents (wiped when the restore is
  confirmed, cancelled or fails). Never logged: the password, the seed (the
  logs say the file's path, the code, the counts, the outcome). The test
  steps' log masks the password too. The menus wipe what was typed
  (`halo_text_input_text` for a password typing, the IME's own buffer).

## Checks, in order (nothing changes before the last)

1. Name and password (before any work).
2. The file's size, magic, version (newer: "from a newer version"), key
   derivation, flags, the key's parameters, the contents' size (cut, grown,
   foreign: "damaged, or not a backup").
3. Argon2id, then the tag: a wrong password and an altered file are the
   same answer ("Wrong password, or the file was changed").
4. The contents: their magic and version, the flags as the header's, the
   seed giving the key the file names (and a key with an X25519 form), the
   sizes, zeros past them.
5. Confirmed: the identity, list and marks read into a state of their own,
   as `load` reads the files (every count, name and hash; each key once; the
   identity's own key never in its list).
6. The replaced identity's files written to `friends/replaced-<date>/`
   (a failure stops here), then friends.dat, marks.dat, identity.dat (list
   first, identity last; the old ones written back if one fails). A save of
   the old list that read it before is not written after
   (`files_generation`, checked under the file lock).
7. The live state takes the new identity under `friends_lock`: the old
   identity's friends are first sent an offline presence and its party is
   left (told); its invites, party, requests seen, inbound queue are let go;
   the new identity's presence goes out at the next pass, asking for
   replies.

## Two machines with one identity

The identity is a key, not a device: whoever has the file and the password
has the friend code. Nothing in friends binds it to a Vita (F-05 of the
security review binds a friend *card* in a game to the game's machine slot
and its asker's nonce; that is untouched, and a restored identity answers
cards as any other, signed with its key). So restoring onto another Vita
needs no re-binding, and there is nothing to weaken.

What two Vitas with one identity online at once do (decided): **the newest
run is the one friends hear.** Each run of the identity has an epoch (Unix
time, newer than the identity's last); a restore gives the identity a run
newer than the file's and than the restoring Vita's own. Friends keep each
friend's newest epoch and drop messages of an older one as replays (as they
always did). So:

- Vita A has the code; Vita B restores A's backup: B's run is newer; A's
  friends see B (its profile name), and A's presence and invites are
  dropped as replays while A keeps running.
- If A starts again later, its new run is newer: friends hear A, and B's
  messages are dropped until B starts again. Starting a Vita takes the code
  over.
- Messages to the identity (presence, invites, requests) reach both: both
  subscribe its topics and both hold the key.

That is what the owner's rule asks: one friend code per Vita, used on one
at a time. The restore screens say so ("Use it on one Vita at a time:
friends see the one that started last"). Refusing a second machine would
need a server or a broker-side lock; the brokers are dumb pipes and keep
nothing, so it is not done.

A Vita whose clock is far behind the one that last ran the identity starts
runs older than that one's: friends drop its messages until its clock
passes it (the same holds for a Vita's own identity today).

## What is not protected

- A weak password: the file can be guessed offline at about a second of a
  Vita's work a guess (much less on a PC's GPU: Argon2id's memory blunts but
  does not stop it). Hence 8 characters at least and the advice on screen.
- A stolen file and password: the thief is you, to your friends, until you
  start your Vita again (then your run is the newer). Remove and re-add
  friends from a new identity if the password leaked.
- `friends/replaced-*/` holds an identity unencrypted, as `identity.dat`
  always has: anyone with the memory card has it.
- An older friends list restored: replays of messages sent between the
  backup and the restore could be taken once (no broker retains them, so
  only someone who captured them can replay them).

## Tests

- `port/vita/tests/p2p_friends_test.c` (`run_p2p_friends_test.sh`, plain and
  ASan/UBSan): `test_backup` - names and passwords; the file's size,
  parameters, salt and nonce new each time, no seed/key/list/password in it;
  wrong password, cut file, flipped bytes (contents, tag, salt, flags),
  newer and bad versions (file and contents), another key derivation, key
  parameters out of bounds, a key not the seed's, bytes past the contents;
  16 MiB/3 passes taken; cancel; restore on another machine (the same code,
  the list, identity.dat, the replaced identity kept, a new run keeps it);
  two machines with one identity (the friend sees one friend, the newest
  run, the other's dropped as replays); restored again on its own machine (a
  newer run); a save waiting with the old list not written; the code alone.
- `p2p_friends_fuzz.c` mode 11: a file's header and a backup's contents read
  as a restore reads them.
- `run_netns_online_test.sh friendsbackup`: three copies of the Linux build
  on local brokers in network namespaces: Alpha and Bravo become friends,
  Bravo backs up and quits, Charlie (a new machine) restores Bravo's backup;
  Alpha must see Bravo's code online again as Charlie, Charlie must see
  Alpha, and no log may hold the password.
- The menus: `run_vita_settings_test.sh` (the panel's flow, Spanish too),
  `run_lang_test.sh`.
