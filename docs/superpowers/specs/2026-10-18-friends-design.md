# Friends for Halo CE Vita (design)

Date: 2026-10-18. Branch `feat/friends`, from `integration/1.1.1-insider.9`
(8e4f3be2). The owner's ask: "Could we do a friends list maybe?", with the
approved plan: friend codes, a Halo 3-style FRIENDS list (lobby panel and a
FRIENDS screen) with presence, Join and Invite, privacy and security first,
the list stored on the Vita, and the settings panel's Friends page for the
original menus. PSN friends are out of scope.

## What is reused

- The p2p layer's crypto (`p2p_crypto.c`, Monocypher): Ed25519 signing,
  X25519, ChaCha20-Poly1305, SHA-256, HMAC-SHA256.
- The signalling brokers (`p2p_signal.c`): every broker of `brokers.txt` at
  once, MQTT 5 or 3.1.1, plain or TLS, the publish buckets. Friends add two
  subscriptions and a queue of publishes; nothing about JOIN/ACCEPT, codes or
  the server browser changes.
- Joining: a friend's game is joined by its invite link
  (`halo://join/<host key hash><token>`), the same way a pasted link, a code
  or the server browser joins (`p2p_join_invite`): the host's JOIN/ACCEPT,
  its player limit, its version check and every host-side check stay where
  they are. Friends never carry a password or bypass one (below).
- The Halo 3 menu kit (`menus/halo3`, `menu_functions.c`, `menu_tags.c`) and
  the settings panel (`vita_settings.c`).

## Identity and the friend code

The p2p layer's session key is made new each run, on purpose: a public
game's host is not linkable from one run to the next. Friends need a key
that lasts, so each Vita gets one more Ed25519 key, the **friend identity**:
a 32-byte seed made once (`posix_random_bytes`) and kept in the save area
(`friends/identity.dat`). It signs every friends message; its X25519 form
(`p2p_ed25519_public`'s scalar, `p2p_ed25519_to_x25519` of the public key)
does the key agreement. It is never used for games, so playing in public
links nothing to the friends identity, and the friends identity reveals
nothing about a run's games.

The **friend code** is 12 characters of the codes' alphabet (32 letters and
digits, no 0 1 I O), shown `ABCD-EFGH-JKLM`: the first 60 bits of
SHA-256("hcev-friend-code-1" | Ed25519 public key). 60 bits make a key
with the same code out of reach (2^60 key generations), so a code is a
commitment to its key: whoever accepts a request must hold the key whose
hash the code is. The 8-character codes of hosted games stay as they are;
the lengths tell the two apart.

## Topics (what the brokers see)

Two subscriptions per Vita, both under the port's prefix
(`P2P_SIGNAL_PREFIX`: `hcev` on a Vita; the Linode broker's ACL already
takes `hcev/#`):

- the **inbox** `hcev/f1/i/<32 hex>`: HMAC-SHA256(code, "inbox"), where
  requests from people who know only the code arrive;
- the **mailbox** `hcev/f1/m/<32 hex>`: HMAC-SHA256(Ed25519 key,
  "hcev-friends-1 mailbox"), where friends (and the answer to a request)
  write. Only someone who holds the full public key (a friend, or someone
  whose request was answered) can compute it.

No message is retained; the broker keeps nothing. Every publish is QoS 0
through every ready broker (the receiver drops duplicates by counter), and
spends from the broker's bucket with the unproven reserve kept, so friends
never starve a joiner's or host's signalling.

## Messages

Every payload is an envelope: `'F'`, version 1, a kind byte, then the
kind's sealing. The envelope's three bytes are the AEAD's additional data.

| kind | where | sealing | key |
|---|---|---|---|
| `R` request | inbox | nonce (12), ciphertext, tag (16) | HMAC(SHA-256("hcev-friends-1 code" \| code), "seal") |
| `S` box | mailbox | ephemeral X25519 key (32), nonce, ciphertext, tag | HMAC(X25519(eph, recipient), "hcev-friends-1 box" \| eph \| recipient Ed25519 key) |
| `P` pair | mailbox | nonce, ciphertext, tag | the pair's key for this direction (below) |

The plaintext of each: type (1), sender's Ed25519 key (32), recipient (32:
the recipient's Ed25519 key; zero for presence, which goes to every friend;
SHA-256("hcev-friends-1 code" | code) for a request), epoch (8), counter
(4), the body, then an Ed25519 signature (64) of
"hcev-friends-1" | everything before it, by the sender's friend identity.

Bodies (lengths before text; every text printable ASCII, cleaned as player
names are, and bounded):

- **REQUEST** (type 1, kind R): name (1+16), request nonce (16).
- **ACCEPT** (type 2, kind S): name (1+16), the request's nonce (16),
  friendship id (16, random, the accepter's).
- **PRESENCE** (type 3, kind P): flags (online, want a reply, joinable),
  state (offline, menus, multiplayer, co-op, campaign, Forge), game engine,
  difficulty, players, most players, network version (2), wire (1), name
  (1+16), map (1+32), game type (1+24), and when joinable the invite's host
  key hash (16) and token (16).
- **INVITE** (type 4, kind P): friendship id (16), invite id (8), state,
  engine, difficulty, network version, wire, map (1+32), game type (1+24),
  the invite's host key hash and token (32).
- **REMOVE** (type 5, kind P): friendship id (16).

A decline sends nothing (below). The largest message is about 300 bytes,
under p2p_signal's 540-byte limit.

## Crypto: which key does what

- Each friend identity: Ed25519 seed `s`, public key `E`, X25519 scalar `x`
  and public key `X` (Monocypher's conversion, as the lobby's listings use).
- Pair secret: `X25519(x_a, X_b)` (refused if it is all zero: a
  small-order key), then `pair = HMAC(shared, "hcev-friends-1 pair" |
  min(E_a, E_b) | max(E_a, E_b))`; the key from A to B is
  `HMAC(pair, "dir" | E_a | E_b)`. Static-static: no forward secrecy (a
  stolen seed reads recorded traffic of that pair); stated as a limit.
- A request is sealed with a key from the code, as the game codes' records
  are. Whoever knows the code can read requests sent to it (the sender's
  public key and name), but cannot answer one: an ACCEPT must come from the
  key whose hash is the code.
- An ACCEPT is a sealed box to the requester (who does not know the
  accepter's key yet): an ephemeral X25519 key, so only the requester opens
  it. Its signature binds the requester's key (recipient) and the request's
  nonce, so it cannot be redirected or replayed.
- Every message is signed. A pair message's AEAD already proves the sender
  is the friend; the signature makes it the owner's "signed with each
  Vita's identity key" and binds the counter and recipient.

## Replay protection

- **Epoch**: each run, the identity's epoch becomes max(stored + 1, Unix
  time), written to `identity.dat` before anything is sent. **Counter**:
  each message sent this run, one more.
- A receiver keeps, per friend, the highest epoch seen (in the friends file)
  and, for that epoch, the highest counter and a 64-message window (in
  memory). An older epoch, or a counter already seen or more than 64 behind,
  is dropped. The same message through two brokers is dropped the second
  time by the same rule.
- Requests (from people not yet friends) are held in a 64-entry table of
  (key, epoch, counter); an ACCEPT must name the nonce of a request that is
  still pending (once taken it is gone); a REMOVE names the friendship id,
  so an old REMOVE replayed after the two became friends again does nothing;
  an invite is shown only while it is newer than five minutes on the
  receiver's clock, and once.
- Limit: right after the receiver restarts, a recorded message from the
  friend's *current* run (epoch) newer than the counter last written can be
  replayed once; its effect is a presence or invite the friend did send
  this run, superseded by the next real one. The counter is written with the
  file, and at exit.

## Presence: friends only

- Presence goes to each friend's mailbox, sealed for that friend alone;
  nobody else can read it or know whose it is. It is signed once and sealed
  per friend.
- It is sent on a change (at most every 3 s) and every 60 s; a friend is
  shown offline 150 s after the last one, or at once on an "offline"
  presence (Appear offline turned on, internet play stopped, the game
  quitting).
- On coming online a Vita asks its friends to answer at once (want a reply)
  unless it appears offline: an appear-offline Vita sends nothing at all, so
  even its asking cannot show it is on. It still receives its friends'
  presence, requests and invites.
- **Joinable** (presence carries the invite) only when all hold: this Vita
  hosts a game for the internet; it is public (listed); it has no password;
  it is open with room; the player's "Friends can join" is on; the player
  does not appear offline; the game is on this build's network version and
  wire. A private or password game's invite never goes out in presence. An
  INVITE is the host's own choice, for one friend, which carries the invite
  as sending the link would (a private game included: that is what private
  games' invites are for); with a password it is refused (the password stays
  the way in for everyone else).

## Flows

- **Add**: the player types a code. Pending outgoing request (code, nonce):
  the REQUEST goes to the code's inbox every 30 s for 10 minutes, then
  every 5 minutes while online, for 14 days. Requests to one's own code, to
  a friend's code, or past 16 pending are refused at once.
- **Accept**: the receiver checks the signature, the code (the recipient
  field), the sender not blocked, not self; the request is shown. Accepting
  adds the friend (friendship id), sends ACCEPT to the requester's mailbox,
  and presence. The requester takes an ACCEPT only from the key whose hash
  is the code it typed, for the nonce it sent; then both are friends.
  A request from someone who is already a friend (their list was lost) is
  answered with ACCEPT again: they were agreed to.
- **Decline**: the request goes, and the key is muted for 7 days (requests
  from it dropped unread of anything but the key), so a re-sent request does
  not pop up again. Nothing is sent: a decline must not tell a stranger the
  Vita is online. The requester's side stays "Request sent" until it
  expires or is cancelled.
- **Remove**: the friend goes; a REMOVE (friendship id) is sent, and the key
  is kept as a tombstone for 30 days that answers any message from it with
  REMOVE again (rate limited), so the other side drops the friendship when it
  next comes online.
- **Block**: as Remove, and the key is kept blocked (64 at most): its
  requests, presence and invites are dropped unread, it is never told
  anything again, and it cannot be added. A blocked player can make a new
  identity (a new code); that is a new stranger, rate limited like any.
- **Join**: one press on a joinable friend joins the invite from their last
  presence, then the game's own join (System Link screen or the Halo 3 games
  browser, as JOIN WITH CODE does).
- **Invite**: while hosting, the host sends INVITE to one friend (at most
  one per friend each 10 s). The friend's Vita shows a pop-up at the menus
  (Accept / Ignore; held while in a level or a lobby, up to five minutes);
  Accept joins as Join does. The Friends lists show it too.

## Rate limits and bounds

- Inbound queue: 64 messages; past it, dropped. Per pass of the p2p thread
  at most 16 handled. Requests (inbox): 10 at once, one more each 2 s;
  sealed boxes (an X25519 each): 10 at once, one more each 500 ms; pair
  messages that no friend's key opens (each is tried with every key): 20 at
  once, one more each 250 ms, then pair messages wait a while. A friend's
  presence: 6 at once, one more each 2 s (their own sends are paced far
  below: this caps a flood); invites: one each 10 s per friend, 8 pending at
  most. Friends' publishes keep the brokers' reserve for joiners' and hosts'
  signalling (they never starve it).
- Dedicated servers have no friends (no identity is made there).
- Every length is checked against the message's size before it is read; a
  message is taken whole or dropped. Names are printable ASCII, 16
  characters at most, cleaned as player names are (no "|", no outer spaces)
  and "?" for anything else; maps 32, game types 24. Keys of small order are
  refused (no X25519 form).
- Lists: friends 100, incoming requests 16, outgoing 16, blocked 64,
  tombstones 32, muted 32.

## Storage

`ux0:data/haloce-vita/friends/` on the Vita, `<save root>/friends/`
elsewhere:

- `identity.dat`: "HFID", version 1, seed (32), epoch (8), SHA-256 of what
  comes before (32).
- `friends.dat`: "HFRL", version 1, the five counts, then friends (key 32,
  name 1+16, friendship id 16, since 8, epoch 8, counter 4), incoming
  requests (key, name, nonce, time), outgoing (code 12, nonce 16, time 8),
  blocked (key, name), tombstones (key, friendship id, time), muted (key,
  time), then SHA-256 of all before.

Both are written as forge_store.c writes a map variant: a `.tmp` file
written whole, read back and validated, the old file renamed `.old`, the new
one renamed in, the old one deleted (the Vita does not rename over a file).
On reading: the size, magic, version, every count against its bound, the
exact length, the hash, every name cleaned, every key usable, no duplicates
and not this Vita's own key; a file that fails is not used (the `.old` one
is tried, else the list starts empty and the bad file is kept as
`.bad` for a report). A lost identity makes a new code; the friends list is
then unusable to its friends until they re-add, which the design states.

## Offline

- Internet play off (Connection Same Wi-Fi or Ad hoc): the code and the
  list still show; every friend "Offline"; adding queues the request until
  online; Join and Invite say "Go online first". No broker is reached.
- Online: friends are subscribed while internet play runs (Connection
  Online), as long as there is at least one friend, request or outgoing
  request (no friends: only the inbox, so requests still arrive).
- Internet play switched off live (p2p_shutdown): an offline presence goes
  out first where the buckets allow; the rest see the 150 s timeout.

## Threat model

Protected:
- **Who is friends with whom, and what anyone is doing**, from everyone but
  the two friends. Presence and invites are end-to-end encrypted per pair;
  topics are hashes; the broker sees opaque payloads on opaque topics.
- **Impersonation**: a request answered can only come from the code's key;
  every message is signed by the sender's identity and sealed by the pair
  key; replays are dropped (above).
- **Unwanted contact**: nobody becomes a friend without both agreeing;
  blocked players are dropped unread; strangers' requests are rate limited;
  a decline tells the stranger nothing.
- **Presence leaks**: only friends ever receive presence; Appear offline
  sends nothing (not even the request for replies).
- **Join safety**: Join and Invite use the existing invite join, whose
  checks are unchanged; presence carries an invite only for a public game
  without a password; nothing bypasses a password from the presence path.
- **Malformed input**: every parser is bounded and fuzzed (messages and
  both files).

Not protected (stated):
- A broker (or anyone watching its traffic) sees that one connection
  publishes to a topic another connection subscribes to, and when: traffic
  analysis can link two connections' IP addresses as talking, though not who
  they are or what is said. A hostile broker can drop messages (friends then
  look offline) but cannot read, forge or replay them.
- Someone who knows a code can read the requests sent to it (the senders'
  public keys and chosen names), not answer them, and not see presence.
- No forward secrecy: a stolen identity seed reads recorded pair traffic.
- A blocked player can make a new identity and send one more request (rate
  limited, and blockable again).
- The replay window right after a restart (above).

## Wire changes and the network version

Nothing the game machines send each other changes: not the game's messages,
not the tunnel, not JOIN/ACCEPT, codes or the server browser's listings. The
friends messages travel only on the new `hcev/f1/...` topics, which older
builds never subscribe to, with their own format version byte ("F", 1).
As with the host's ping table (NETCODE.md: a new packet type old builds
never act on), this is no network change: `HALO_PORT_NETWORK_VERSION` stays
18. Presence carries the sender's network version and wire so that Join is
offered only to a game this build can join.

## The broker

No change: the Linode Mosquitto's ACL takes `hcev/#` and `hceu/#`,
`max_packet_size` 4096 is far above 300 bytes, and nothing is retained
(persistence off changes nothing). The public brokers carry the topics as
any other.

## UI

- Halo 3 menus: FRIENDS on the main menu, and Triangle in the lobbies
  (multiplayer, campaign, Forge), open the FRIENDS screen
  (`menus/halo3/friends.xml`): the friend code at the top, ADD FRIEND,
  APPEAR OFFLINE and FRIENDS CAN JOIN rows, then requests, invites, friends
  (online first, each with what they are doing and JOIN when joinable),
  requests sent and blocked players. Cross: the row's action (Accept, Join,
  type a code); Square: the second (Decline, Invite, Ignore, Cancel);
  Triangle: Remove, Block or Unblock after Halo 3's question. A FRIENDS panel
  at the right of each lobby shows who is online and what they are doing.
- Settings panel: Multiplayer > Friends: the code, Add a friend (the
  D-pad's code entry, as Join with a code), Appear offline, Friends can join,
  then the same lists; Cross, Square and Triangle as above. The Multiplayer
  tab is already a screen tall, so the Friends row takes Join with a code's
  place, which the PC menus' Direct Link leaves out (Online, the PC menus,
  not the crossplay wire, whose Vita joins only PCs' games); the Halo 3
  panel's headings give way when its rows would not fit, as the classic
  panel's do.
- Activity lines use ", " before a difficulty ("Co-op: The Pillar of Autumn,
  Legendary"), as the server browser's rules do: the panel's font has no
  middle dot.
- The invite pop-up: the game's question (the Vita overlay's), at the
  menus, Accept or Ignore.
- No new GPU programs; no new art beyond what tools/halo3_menu_art.py
  already makes (the screen reuses its panels, rows and strips).
