/*
P2P_FLAME.INC (included by p2p_friends.c, whose state, lock and cards it
shares)

The tester's flaming helmet: a blue flame on the head of a player whose
machine's friend identity is on the owner's signed tester list
(tester_list.h), seen by everyone in the game, and drawn by no machine that
has not checked it itself.

- The claim: a wearer's player joins with network_player.icon_index
  NETWORK_PLAYER_FLAME_CLAIM (players.h), a field the game leaves unread
  (NONE always, before this) and the record carries whole; a hint to ask,
  never trusted. Builds before it carry it and read nothing of it. Never on
  the crossplay wire (the cards are not either).
- The proof: every machine that sees a claim asks the claimant's machine
  (on the friend cards' channel, p2p.c's packet type 7, relayed by the host
  as cards are: CARD_FLAME_ASK, a fresh nonce) and the claimant answers
  (CARD_FLAME_ANSWER) with its friend identity's key and an Ed25519
  signature of "hcev-flame-1 claim", its machine, the asker's, the nonce,
  the key and its player's name as the game's record has it. The asker
  draws the flame only once the key is on its own newest valid tester list
  (not expired), the signature is the key's over its own nonce (no replay:
  a nonce is asked once), the machines and the name it has, and no other
  player of the game proved it with that key, nor is it this machine's own.
- The host relays a flame answer only once it checked it itself (the list,
  the signature with the sender's machine as the host reads it and the
  claimant's name): a modified host can still pass anything on, which is why
  every machine checks again; it cannot make a proof for a key it does not
  hold, nor replay one (the nonce is the asker's).
- What a tester can do with their own key (lend their flame, as by giving
  away their identity.dat) only a new list takes back: one key lights one
  player a game.
- Builds before this one drop the two kinds as malformed (card_heard): their
  host relays none (its joiners see only the host's own flame and their own,
  none the host's: an older host has no flame), their joiners ask nothing
  and answer nothing.

(debug, not in release builds) HALO_TEST_FLAME_FORCE=1: claims and answers
as if listed (a player who forced the bit: nobody may draw it);
HALO_TEST_FLAME_FORGE_HOST=1: a modified host's, every player's claim set
(players.h's network_game_server side) and every ask for another machine
answered by the host itself, with a listed key it does not hold, its relays
unchecked; HALO_TEST_TESTERS_SHIPPED: the shipped list's path.
*/

static int flame_test_setting(const char *name)
{
#ifdef P2P_FRIENDS_TEST
	/* (p2p_friends_test.c's machines, each its own) */
	if (!strcmp(name, "HALO_TEST_FLAME_FORCE"))
		return friends->test_flame_force;
	if (!strcmp(name, "HALO_TEST_FLAME_FORGE_HOST"))
		return friends->test_flame_forge;
#endif
#ifndef HALO_RELEASE
	const char *value = getenv(name);

	return value && value[0] == '1';
#else
	(void)name;
	return 0;
#endif
}

static int flame_kind(int kind)
{
	return kind == CARD_FLAME_ASK || kind == CARD_FLAME_ANSWER;
}

/* a list's file read whole (at most the largest list, one byte more to see
it is larger): its size, or -1 */
static int flame_file_read(const char *path, char *bytes, int capacity)
{
	FILE *file = fopen(path, "rb");
	size_t count;

	if (!file)
		return -1;
	count = fread(bytes, 1, (size_t)capacity, file);
	fclose(file);
	return (int)count;
}

/* the shipped list (the game's package: the Vita's app0:, beside the PC's
executable) and one dropped in the save folder */
static void flame_list_paths(char *shipped, char *dropped, int size)
{
	const char *test = NULL;

#ifndef HALO_RELEASE
	test = getenv("HALO_TEST_TESTERS_SHIPPED");
#endif
#if defined(HALO_VITA) && !defined(P2P_FRIENDS_TEST)
	snprintf(shipped, (size_t)size, "app0:" TESTER_LIST_FILE);
	snprintf(dropped, (size_t)size, "ux0:data/haloce-vita/" TESTER_LIST_FILE);
#elif defined(P2P_FRIENDS_TEST)
	snprintf(shipped, (size_t)size, "%s/shipped-" TESTER_LIST_FILE, platform_save_root());
	snprintf(dropped, (size_t)size, "%s/" TESTER_LIST_FILE, platform_save_root());
#else
	config_folder(shipped, size);
	snprintf(shipped + strlen(shipped), (size_t)(size - (int)strlen(shipped)), TESTER_LIST_FILE);
	snprintf(dropped, (size_t)size, "%s/" TESTER_LIST_FILE, platform_save_root());
#endif
	if (test && test[0])
		snprintf(shipped, (size_t)size, "%s", test);
}

/* (friends_lock held) the tester list, read once: the newest that checks
of the shipped one and the one dropped in the save folder */
static void flame_load(void)
{
	static char text[TESTER_LIST_MAXIMUM_SIZE + 1];
	char paths[2][PATH_SIZE], error[96];
	unsigned char key[32];
	int which;

	if (friends->flame_loaded)
		return;
	friends->flame_loaded = 1;
	memset(&friends->flame_list, 0, sizeof(friends->flame_list));
	tester_list_public_key(key);
	flame_list_paths(paths[0], paths[1], PATH_SIZE);
	for (which = 0; which < 2; which++)
	{
		int size = flame_file_read(paths[which], text, (int)sizeof(text));
		int result;

		if (size < 0)
			continue;
		result = tester_list_newer(text, (size_t)size, key, &friends->flame_list, error, (int)sizeof(error));
		if (result == TESTER_LIST_OK)
			platform_log("flame: tester list %s taken: version %lu, %d testers", paths[which],
				friends->flame_list.version, friends->flame_list.count);
		else
			platform_log("flame: tester list %s not taken: %s", paths[which], error);
	}
}

/* (friends_lock held) this machine's identity is on the list, unexpired */
static int flame_own_listed(void)
{
	load();
	flame_load();
	return friends->has_identity && tester_list_find(&friends->flame_list, friends->key, unix_now()) >= 0;
}

/* (friends_lock held) this machine wears the flame: listed (or the tests'
forced claim) and the setting on */
static int flame_wearing(void)
{
#ifdef HALO_DEDICATED_SERVER
	return 0;
#else
	if (!setting_on("player.flaming_helmet", "HALO_FLAMING_HELMET"))
		return 0;
	return flame_own_listed() || (flame_test_setting("HALO_TEST_FLAME_FORCE") && friends->has_identity);
#endif
}

/* what an answer's signature covers */
static int flame_signed_data(int from, int to, const unsigned char *nonce, const unsigned char *key,
	const unsigned char *name, unsigned char *data)
{
	static const char label[] = "hcev-flame-1 claim";
	int at = 0;

	memcpy(data, label, sizeof(label) - 1);
	at += (int)sizeof(label) - 1;
	data[at++] = (unsigned char)from;
	data[at++] = (unsigned char)to;
	memcpy(data + at, nonce, NONCE_LENGTH);
	at += NONCE_LENGTH;
	memcpy(data + at, key, P2P_KEY_SIZE);
	at += P2P_KEY_SIZE;
	memcpy(data + at, name, FLAME_NAME_SIZE);
	at += FLAME_NAME_SIZE;
	return at;
}

/* an answer's proof, from a machine to another (the machines as this
machine reads them), for the name the game's record gives the claimant:
1, else 0 and why */
static int flame_proof_valid(const unsigned char *bytes, int from, int to, const unsigned char *name,
	const char **why)
{
	unsigned char data[64 + FLAME_ANSWER_SIZE + FLAME_NAME_SIZE];
	const unsigned char *key = bytes + CARD_HEADER_SIZE;
	int size, listed;

	flame_load();
	listed = tester_list_find(&friends->flame_list, key, unix_now());
	if (listed < 0)
	{
		*why = listed == -2 ? "its tester's entry expired" : "its key is on no tester list this machine has";
		return 0;
	}
	size = flame_signed_data(from, to, bytes + 4, key, name, data);
	if (!p2p_ed25519_verify(key, data, size, bytes + FLAME_ANSWER_SIZE - P2P_SIGNATURE_SIZE))
	{
		*why = "its signature is not the listed key's (for this game's machines, nonce and name)";
		return 0;
	}
	return 1;
}

/* this machine's answer to an ask of a machine (its nonce) */
static void flame_answer(int machine, const unsigned char *nonce)
{
	unsigned char packet[FLAME_ANSWER_SIZE], data[64 + FLAME_ANSWER_SIZE + FLAME_NAME_SIZE];
	unsigned long address;
	int size;

	if (!card_route(machine, &address))
		return;
	packet[0] = CARD_VERSION;
	packet[1] = CARD_FLAME_ANSWER;
	packet[2] = (unsigned char)friends->game_machine;
	packet[3] = (unsigned char)machine;
	memcpy(packet + 4, nonce, NONCE_LENGTH);
	memcpy(packet + CARD_HEADER_SIZE, friends->key, P2P_KEY_SIZE);
	size = flame_signed_data(friends->game_machine, machine, nonce, friends->key, friends->flame_own_name, data);
	p2p_ed25519_sign(friends->seed, friends->key, data, size, packet + FLAME_ANSWER_SIZE - P2P_SIGNATURE_SIZE);
	card_queue(address, packet, FLAME_ANSWER_SIZE);
}

/* (the tests' modified host) an ask for another machine answered by the
host itself: the first listed key (whose private half it does not hold),
signed with its own */
static void flame_forge_answer(int asker, int claimant, const unsigned char *nonce)
{
	unsigned char packet[FLAME_ANSWER_SIZE], data[64 + FLAME_ANSWER_SIZE + FLAME_NAME_SIZE];
	int size;

	if (!friends->machines[asker].address)
		return;
	flame_load();
	packet[0] = CARD_VERSION;
	packet[1] = CARD_FLAME_ANSWER;
	packet[2] = (unsigned char)claimant;
	packet[3] = (unsigned char)asker;
	memcpy(packet + 4, nonce, NONCE_LENGTH);
	memcpy(packet + CARD_HEADER_SIZE, friends->flame_list.count ? friends->flame_list.entries[0].key : friends->key,
		P2P_KEY_SIZE);
	size = flame_signed_data(claimant, asker, nonce, packet + CARD_HEADER_SIZE, friends->machines[claimant].flame_name,
		data);
	p2p_ed25519_sign(friends->seed, friends->key, data, size, packet + FLAME_ANSWER_SIZE - P2P_SIGNATURE_SIZE);
	card_queue(friends->machines[asker].address, packet, FLAME_ANSWER_SIZE);
	platform_log("flame: test: the modified host forged machine %d's flame proof for machine %d", claimant, asker);
}

/* (a host) a flame packet of a joiner's (sender, as this host reads it) for
another machine: an ask relayed; an answer relayed only once it is proven
here, for the sender's claim */
static void flame_relay(int sender, int to, const unsigned char *bytes, int size)
{
	struct card_machine *machine = &friends->machines[sender];
	unsigned char relayed[FLAME_ANSWER_SIZE];
	int forging = flame_test_setting("HALO_TEST_FLAME_FORGE_HOST");

	if (!friends->machines[to].address ||
		(machine->flame_relayed_time && !elapsed(machine->flame_relayed_time, FLAME_RELAY_MACHINE_INTERVAL)) ||
		!budget_left(&friends->card_relay_budget, CARD_RELAY_BUDGET, CARD_RELAY_INTERVAL, 1))
	{
		friends->dropped_rate++;
		return;
	}
	machine->flame_relayed_time = now_stamp();
	if (forging && bytes[1] == CARD_FLAME_ASK && to < CARD_MACHINES)
	{
		flame_forge_answer(sender, to, bytes + 4);
		return;
	}
	if (bytes[1] == CARD_FLAME_ANSWER && !forging)
	{
		const char *why = "";

		if (!machine->flame_claim || !flame_proof_valid(bytes, sender, to, machine->flame_name, &why))
		{
			friends->dropped_forged++;
			platform_log("flame: refused to relay machine %d's flame proof to machine %d: %s", sender, to,
				machine->flame_claim ? why : "its player claims no flame");
			return;
		}
	}
	memcpy(relayed, bytes, (size_t)size);
	relayed[2] = (unsigned char)sender;
	card_queue(friends->machines[to].address, relayed, size);
}

/* a flame packet for this machine, from a machine (the host's reading of
it, or the host itself) */
static void flame_heard(int from, const unsigned char *bytes)
{
	struct card_machine *machine = &friends->machines[from];
	const char *why = "";
	int other;

	if (bytes[1] == CARD_FLAME_ASK)
	{
		/* (not wearing it, or no claim seen for this machine's player: never
		told why) */
		if (!flame_wearing())
			return;
		if (machine->flame_answered_time && !elapsed(machine->flame_answered_time, CARD_ANSWER_INTERVAL / 3))
		{
			friends->dropped_rate++;
			return;
		}
		machine->flame_answered_time = now_stamp();
		flame_answer(from, bytes + 4);
		platform_log("flame: machine %d asked for this machine's flame proof; answered", from);
		return;
	}
	/* an answer: to this machine's ask of that machine, for a claim */
	if (!machine->flame_asking || !p2p_equal(machine->flame_nonce, bytes + 4, NONCE_LENGTH))
	{
		friends->dropped_replays++;
		platform_log("flame: machine %d's flame proof answers no ask of this machine's (a replay?): dropped", from);
		return;
	}
	machine->flame_asking = 0;
	if (!machine->flame_claim)
		why = "its player claims no flame";
	else if (p2p_equal(bytes + CARD_HEADER_SIZE, friends->key, P2P_KEY_SIZE))
		why = "it is this machine's own key";
	else if (!flame_proof_valid(bytes, from, friends->game_machine, machine->flame_name, &why))
		;
	else
	{
		for (other = 0; other < CARD_MACHINES; other++)
			if (other != from && friends->machines[other].flame_verified &&
				p2p_equal(friends->machines[other].flame_key, bytes + CARD_HEADER_SIZE, P2P_KEY_SIZE))
			{
				why = "another player of this game proved it with that key";
				break;
			}
		if (other == CARD_MACHINES)
		{
			int entry = tester_list_find(&friends->flame_list, bytes + CARD_HEADER_SIZE, unix_now());

			machine->flame_verified = 1;
			memcpy(machine->flame_key, bytes + CARD_HEADER_SIZE, P2P_KEY_SIZE);
			platform_log("flame: machine %d's flaming helmet verified (tester %s)", from,
				entry >= 0 ? friends->flame_list.entries[entry].label : "?");
			return;
		}
	}
	friends->dropped_forged++;
	machine->flame_refused = 1;
	platform_log("flame: machine %d's flame claim refused: %s", from, why);
}

/* (cards_update, friends_lock held) the claims seen, asked about */
static void flame_update(void)
{
	int index;

#ifdef HALO_DEDICATED_SERVER
	/* (a server draws nothing: it asks nothing, relays checked proofs) */
	return;
#endif
	if (!friends->in_game || friends->host.crossplay)
		return;
	for (index = 0; index < CARD_MACHINES; index++)
	{
		struct card_machine *machine = &friends->machines[index];
		unsigned char packet[CARD_HEADER_SIZE];
		unsigned long address;

		if (index == friends->game_machine || !machine->flame_claim || machine->flame_verified ||
			machine->flame_refused)
		{
			continue;
		}
		if (machine->flame_asking)
		{
			if (!elapsed(machine->flame_asked_time, FLAME_ANSWER_TIME))
				continue;
			machine->flame_asking = 0;
			platform_log("flame: machine %d did not answer for its flame proof", index);
		}
		if (machine->flame_tries >= FLAME_TRIES ||
			(machine->flame_asked_time && !elapsed(machine->flame_asked_time, FLAME_ASK_INTERVAL)) ||
			!card_route(index, &address))
		{
			continue;
		}
		machine->flame_asking = 1;
		machine->flame_tries++;
		machine->flame_asked_time = now_stamp();
		posix_random_bytes(machine->flame_nonce, NONCE_LENGTH);
		packet[0] = CARD_VERSION;
		packet[1] = CARD_FLAME_ASK;
		packet[2] = (unsigned char)friends->game_machine;
		packet[3] = (unsigned char)index;
		memcpy(packet + 4, machine->flame_nonce, NONCE_LENGTH);
		card_queue(address, packet, CARD_HEADER_SIZE);
		platform_log("flame: machine %d's player claims the flaming helmet; asked for its proof", index);
	}
}

/* ---------- the game's side (p2p_friends.h) */

int p2p_flame_listed(void)
{
	int listed;

	pthread_mutex_lock(&friends_lock);
	listed = flame_own_listed();
	pthread_mutex_unlock(&friends_lock);
	return listed;
}

int p2p_flame_wearing(void)
{
	int wearing;

	pthread_mutex_lock(&friends_lock);
	wearing = flame_wearing();
	pthread_mutex_unlock(&friends_lock);
	return wearing;
}

int p2p_flame_forging_host(void)
{
	return flame_test_setting("HALO_TEST_FLAME_FORGE_HOST");
}

void p2p_flame_set_players(const int *claims, const unsigned short *names, int count, const unsigned short *own_name)
{
	int index, unit;

	pthread_mutex_lock(&friends_lock);
	for (unit = 0; unit < FLAME_NAME_SIZE / 2; unit++)
	{
		unsigned short value = own_name ? own_name[unit] : 0;

		friends->flame_own_name[2 * unit] = (unsigned char)(value & 0xFF);
		friends->flame_own_name[2 * unit + 1] = (unsigned char)(value >> 8);
	}
	for (index = 0; index < CARD_MACHINES && friends->in_game; index++)
	{
		struct card_machine *machine = &friends->machines[index];
		unsigned char name[FLAME_NAME_SIZE];
		int claim = claims && index < count && claims[index];

		memset(name, 0, sizeof(name));
		for (unit = 0; claim && unit < FLAME_NAME_SIZE / 2; unit++)
		{
			unsigned short value = names[index * (FLAME_NAME_SIZE / 2) + unit];

			name[2 * unit] = (unsigned char)(value & 0xFF);
			name[2 * unit + 1] = (unsigned char)(value >> 8);
		}
		/* (a claim gone, or another player's in its place: what was known of
		it forgotten) */
		if (claim != machine->flame_claim || memcmp(name, machine->flame_name, sizeof(name)))
		{
			machine->flame_claim = claim;
			memcpy(machine->flame_name, name, sizeof(name));
			machine->flame_asking = 0;
			machine->flame_tries = 0;
			machine->flame_asked_time = 0;
			machine->flame_verified = 0;
			machine->flame_refused = 0;
		}
	}
	pthread_mutex_unlock(&friends_lock);
}

int p2p_flame_verified(int machine)
{
	int verified = 0;

	pthread_mutex_lock(&friends_lock);
	if (friends->in_game && machine >= 0 && machine < CARD_MACHINES && machine != friends->game_machine)
		verified = friends->machines[machine].flame_claim && friends->machines[machine].flame_verified;
	pthread_mutex_unlock(&friends_lock);
	return verified;
}
