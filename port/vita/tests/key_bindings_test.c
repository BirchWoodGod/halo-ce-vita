/*
KEY_BINDINGS_TEST.C

A desktop test of the PC builds' keyboard and mouse bindings
(port/linux/src/key_bindings.c, port/linux/include/halo_key_bindings.h):
the Halo MCC defaults, inputs' names both ways, a binding's text read and
written, the swap when a key another action has is bound, clearing, the
conflicts of a hand-written file, and the migration of an earlier
version's config.toml (its defaults made this version's, the player's own
kept, a default giving up a key the player's binding has).

The keys' names stand in for SDL's here (a few, by scancode). Run
port/vita/tests/run_key_bindings_test.sh.
*/

#include "../../linux/include/halo_key_bindings.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

/* (SDL's scancodes and names, the ones these tests use) */
static const struct
{
	int scancode;
	const char *name;
} keys[] =
{
	{ 4, "A" }, { 6, "C" }, { 7, "D" }, { 8, "E" }, { 9, "F" }, { 10, "G" }, { 11, "H" }, { 20, "Q" }, { 21, "R" },
	{ 22, "S" }, { 26, "W" }, { 27, "X" }, { 29, "Z" }, { 40, "Return" }, { 41, "Escape" }, { 42, "Backspace" },
	{ 43, "Tab" }, { 44, "Space" }, { 49, "\\" }, { 58, "F1" }, { 224, "Left Ctrl" }, { 225, "Left Shift" },
	{ 30, "1" }, { 25, "V" },
};

static int test_scancode_from_name(const char *name)
{
	size_t index;

	for (index = 0; index < sizeof(keys) / sizeof(keys[0]); index++)
	{
		if (!strcasecmp(keys[index].name, name))
			return keys[index].scancode;
	}
	return 0;
}

static const char *test_scancode_name(int scancode)
{
	size_t index;

	for (index = 0; index < sizeof(keys) / sizeof(keys[0]); index++)
	{
		if (keys[index].scancode == scancode)
			return keys[index].name;
	}
	return "";
}

static int text_is(int action, const struct halo_bindings *bindings, const char *expected)
{
	char text[128];

	halo_bind_format(bindings->inputs[action], HALO_BIND_SLOTS, text, sizeof(text));
	if (strcmp(text, expected))
		printf("  (%s is \"%s\", not \"%s\")\n", halo_bind_action_key(action), text, expected);
	return !strcmp(text, expected);
}

static void test_defaults(void)
{
	static const struct
	{
		int action;
		const char *text;
	} mcc[] =
	{
		{ HALO_BIND_MOVE_FORWARD, "W" }, { HALO_BIND_MOVE_BACKWARD, "S" }, { HALO_BIND_STRAFE_LEFT, "A" },
		{ HALO_BIND_STRAFE_RIGHT, "D" }, { HALO_BIND_JUMP, "Space" }, { HALO_BIND_CROUCH, "Left Ctrl, C" },
		{ HALO_BIND_FIRE, "Mouse Left" }, { HALO_BIND_ZOOM, "Mouse Right, Z" },
		{ HALO_BIND_THROW_GRENADE, "G" }, { HALO_BIND_MELEE, "F, Mouse 4" }, { HALO_BIND_RELOAD, "R" },
		{ HALO_BIND_SWITCH_WEAPON, "Tab, Wheel" }, { HALO_BIND_SWITCH_GRENADE, "H, Mouse Middle" },
		{ HALO_BIND_ACTION, "E" }, { HALO_BIND_FLASHLIGHT, "Q" }, { HALO_BIND_SCOREBOARD, "Backspace" },
	};
	struct halo_bindings bindings;
	size_t index;
	int first, second;
	char what[96];

	halo_bind_defaults(&bindings);
	for (index = 0; index < sizeof(mcc) / sizeof(mcc[0]); index++)
	{
		snprintf(what, sizeof(what), "default: %s is %s", halo_bind_action_key(mcc[index].action), mcc[index].text);
		check(text_is(mcc[index].action, &bindings, mcc[index].text), what);
	}
	check(sizeof(mcc) / sizeof(mcc[0]) == HALO_BIND_ACTION_COUNT, "default: every action has one");
	check(halo_bind_first_conflict(&bindings, &first, &second) < 0, "default: no key on two actions");
	/* (E and R apart: the Xbox's X is both) */
	check(bindings.inputs[HALO_BIND_ACTION][0] != bindings.inputs[HALO_BIND_RELOAD][0], "default: E and R apart");
	/* (Escape, F1 and the arrows are the menus' and pause's: never bound) */
	check(halo_bind_owner(&bindings, 41, -1, NULL) < 0 && halo_bind_owner(&bindings, 58, -1, NULL) < 0,
		"default: Escape and F1 are no action's");
	check(halo_bind_owner(&bindings, 27, -1, NULL) < 0, "default: X is free (the debug camera's Black is no key's)");
	check(halo_bind_action_of_key("throw_grenade") == HALO_BIND_THROW_GRENADE && halo_bind_action_of_key("nope") < 0,
		"action of key");
}

static void test_names(void)
{
	int inputs[HALO_BIND_SLOTS];
	char text[128], unknown[64];

	check(halo_bind_input_from_name("Mouse Left") == HALO_BIND_MOUSE + 1, "name: Mouse Left");
	check(halo_bind_input_from_name("mouse right") == HALO_BIND_MOUSE + 3, "name: case aside");
	check(halo_bind_input_from_name("Wheel Up") == HALO_BIND_WHEEL_UP && halo_bind_input_from_name("Wheel") ==
		HALO_BIND_WHEEL, "name: the wheel");
	check(halo_bind_input_from_name("Mouse 7") == HALO_BIND_MOUSE + 7, "name: Mouse 7");
	check(halo_bind_input_from_name("Comma") == 54, "name: Comma (SDL's \",\" cannot be in a list)");
	check(halo_bind_input_from_name("left ctrl") == 224, "name: SDL's");
	check(halo_bind_input_from_name("Nonsense") == HALO_BIND_NONE && halo_bind_input_from_name("") == HALO_BIND_NONE,
		"name: unknown");
	halo_bind_input_name(HALO_BIND_MOUSE + 4, text, sizeof(text));
	check(!strcmp(text, "Mouse 4"), "name of Mouse 4");
	halo_bind_input_name(49, text, sizeof(text));
	check(!strcmp(text, "\\"), "name of backslash");

	check(halo_bind_parse(" f ,Mouse 4,  ", inputs, HALO_BIND_SLOTS, unknown, sizeof(unknown)) == 2 &&
		inputs[0] == 9 && inputs[1] == HALO_BIND_MOUSE + 4 && inputs[2] == HALO_BIND_NONE && !unknown[0],
		"parse: spaces and a trailing comma");
	check(halo_bind_parse("G, Bogus, G, Q", inputs, HALO_BIND_SLOTS, unknown, sizeof(unknown)) == 2 && inputs[0] == 10 &&
		inputs[1] == 20 && !strcmp(unknown, "Bogus"), "parse: an unknown name skipped and said, a repeat counted once");
	check(halo_bind_parse("A, C, D, E, F", inputs, HALO_BIND_SLOTS, NULL, 0) == HALO_BIND_SLOTS, "parse: at most the slots");
	check(halo_bind_parse("", inputs, HALO_BIND_SLOTS, NULL, 0) == 0 && inputs[0] == HALO_BIND_NONE, "parse: none");
	halo_bind_parse("Tab, Wheel, Left Shift", inputs, HALO_BIND_SLOTS, NULL, 0);
	halo_bind_format(inputs, HALO_BIND_SLOTS, text, sizeof(text));
	check(!strcmp(text, "Tab, Wheel, Left Shift"), "format: back as written");
}

static void test_assign(void)
{
	struct halo_bindings bindings;
	int displaced, first, second;

	halo_bind_defaults(&bindings);
	/* G onto melee's first slot: G was the grenade's, which takes F */
	check(halo_bind_assign(&bindings, HALO_BIND_MELEE, 0, 10, &displaced) && displaced == HALO_BIND_THROW_GRENADE,
		"assign: a taken key says whose it was");
	check(text_is(HALO_BIND_MELEE, &bindings, "G, Mouse 4") && text_is(HALO_BIND_THROW_GRENADE, &bindings, "F"),
		"assign: swapped, nothing left unbound");
	check(halo_bind_first_conflict(&bindings, &first, &second) < 0, "assign: still no conflict");
	/* a free key into an empty slot */
	check(halo_bind_assign(&bindings, HALO_BIND_RELOAD, 1, 25, &displaced) && displaced < 0 &&
		text_is(HALO_BIND_RELOAD, &bindings, "R, V"), "assign: a free key in the second slot");
	/* the second slot's key into the first: the two swap */
	check(halo_bind_assign(&bindings, HALO_BIND_RELOAD, 0, 25, &displaced) && text_is(HALO_BIND_RELOAD, &bindings, "V, R"),
		"assign: the action's own key moves");
	/* a key whose action has the old key already: its slot empties */
	halo_bind_defaults(&bindings);
	halo_bind_assign(&bindings, HALO_BIND_ZOOM, 1, 10, &displaced);
	check(text_is(HALO_BIND_ZOOM, &bindings, "Mouse Right, G") && text_is(HALO_BIND_THROW_GRENADE, &bindings, "Z"),
		"assign: the grenade takes zoom's Z");
	halo_bind_assign(&bindings, HALO_BIND_THROW_GRENADE, 1, 29, &displaced);
	check(text_is(HALO_BIND_THROW_GRENADE, &bindings, "Z"), "assign: the same key again changes nothing");
	check(!halo_bind_assign(&bindings, HALO_BIND_FIRE, 0, HALO_BIND_NONE, &displaced) &&
		!halo_bind_assign(&bindings, HALO_BIND_FIRE, 0, 9999, &displaced) &&
		!halo_bind_assign(&bindings, HALO_BIND_ACTION_COUNT, 0, 10, &displaced),
		"assign: none, out of range and no action refused");
	/* clearing */
	halo_bind_defaults(&bindings);
	halo_bind_clear(&bindings, HALO_BIND_MELEE, 0);
	check(text_is(HALO_BIND_MELEE, &bindings, "Mouse 4"), "clear: the later slot moves up");
	/* a hand-written file's conflict */
	halo_bind_defaults(&bindings);
	bindings.inputs[HALO_BIND_FLASHLIGHT][1] = 10;
	check(halo_bind_first_conflict(&bindings, &first, &second) == 10 && first == HALO_BIND_THROW_GRENADE &&
		second == HALO_BIND_FLASHLIGHT, "conflict: G on the grenade and the flashlight");
}

static void test_migrate(void)
{
	char texts[HALO_BIND_ACTION_COUNT][HALO_BIND_TEXT_SIZE];
	unsigned char present[HALO_BIND_ACTION_COUNT];
	unsigned long changed;
	int action;

	/* a file with none (every version before rebinding): every default */
	memset(present, 0, sizeof(present));
	memset(texts, 0, sizeof(texts));
	changed = halo_bind_migrate(texts, present);
	check(changed == (1UL << HALO_BIND_ACTION_COUNT) - 1, "migrate: none there, every action written");
	check(!strcmp(texts[HALO_BIND_ZOOM], "Mouse Right, Z") && !strcmp(texts[HALO_BIND_THROW_GRENADE], "G"),
		"migrate: none there, the MCC defaults");

	/* the old fixed layout written out: all of it is defaults, so the MCC
	ones replace it */
	for (action = 0; action < HALO_BIND_ACTION_COUNT; action++)
		present[action] = 1;
	snprintf(texts[HALO_BIND_MOVE_FORWARD], HALO_BIND_TEXT_SIZE, "W");
	snprintf(texts[HALO_BIND_MOVE_BACKWARD], HALO_BIND_TEXT_SIZE, "S");
	snprintf(texts[HALO_BIND_STRAFE_LEFT], HALO_BIND_TEXT_SIZE, "A");
	snprintf(texts[HALO_BIND_STRAFE_RIGHT], HALO_BIND_TEXT_SIZE, "D");
	snprintf(texts[HALO_BIND_JUMP], HALO_BIND_TEXT_SIZE, "Space, Return");
	snprintf(texts[HALO_BIND_CROUCH], HALO_BIND_TEXT_SIZE, "Left Ctrl, C");
	snprintf(texts[HALO_BIND_FIRE], HALO_BIND_TEXT_SIZE, "Mouse Left");
	snprintf(texts[HALO_BIND_ZOOM], HALO_BIND_TEXT_SIZE, "z, mouse middle");
	snprintf(texts[HALO_BIND_THROW_GRENADE], HALO_BIND_TEXT_SIZE, "Mouse Right, G");
	snprintf(texts[HALO_BIND_MELEE], HALO_BIND_TEXT_SIZE, "F, Backspace, Mouse 4");
	snprintf(texts[HALO_BIND_RELOAD], HALO_BIND_TEXT_SIZE, "E, R");
	snprintf(texts[HALO_BIND_SWITCH_WEAPON], HALO_BIND_TEXT_SIZE, "Tab, Wheel");
	snprintf(texts[HALO_BIND_SWITCH_GRENADE], HALO_BIND_TEXT_SIZE, "X");
	snprintf(texts[HALO_BIND_ACTION], HALO_BIND_TEXT_SIZE, "E, R");
	snprintf(texts[HALO_BIND_FLASHLIGHT], HALO_BIND_TEXT_SIZE, "Q");
	snprintf(texts[HALO_BIND_SCOREBOARD], HALO_BIND_TEXT_SIZE, "F1");
	changed = halo_bind_migrate(texts, present);
	check(!strcmp(texts[HALO_BIND_ZOOM], "Mouse Right, Z") && !strcmp(texts[HALO_BIND_THROW_GRENADE], "G") &&
		!strcmp(texts[HALO_BIND_MELEE], "F, Mouse 4") && !strcmp(texts[HALO_BIND_RELOAD], "R") &&
		!strcmp(texts[HALO_BIND_ACTION], "E") && !strcmp(texts[HALO_BIND_SWITCH_GRENADE], "H, Mouse Middle") &&
		!strcmp(texts[HALO_BIND_SCOREBOARD], "Backspace") && !strcmp(texts[HALO_BIND_JUMP], "Space"),
		"migrate: the old layout's defaults become MCC's");
	check(!(changed & (1UL << HALO_BIND_MOVE_FORWARD)) && !(changed & (1UL << HALO_BIND_FIRE)) &&
		(changed & (1UL << HALO_BIND_ZOOM)), "migrate: only what differs is written");

	/* the player's own kept; a default giving up the key it has */
	for (action = 0; action < HALO_BIND_ACTION_COUNT; action++)
		snprintf(texts[action], HALO_BIND_TEXT_SIZE, "%s", halo_bind_action_default(action));
	snprintf(texts[HALO_BIND_FLASHLIGHT], HALO_BIND_TEXT_SIZE, "G, Mouse Middle");
	snprintf(texts[HALO_BIND_ZOOM], HALO_BIND_TEXT_SIZE, "Z, Mouse Middle");
	present[HALO_BIND_THROW_GRENADE] = 0;
	changed = halo_bind_migrate(texts, present);
	check(!strcmp(texts[HALO_BIND_FLASHLIGHT], "G, Mouse Middle"), "migrate: the player's own binding kept");
	check(!strcmp(texts[HALO_BIND_THROW_GRENADE], ""), "migrate: the new default gives up a key the player bound");
	check(!strcmp(texts[HALO_BIND_SWITCH_GRENADE], "H"), "migrate: a default gives up only the key taken");
	check(!strcmp(texts[HALO_BIND_ZOOM], "Mouse Right, Z"),
		"migrate: an earlier default (Z, Mouse Middle) is a default, not a choice");
	check((changed & (1UL << HALO_BIND_THROW_GRENADE)) && (changed & (1UL << HALO_BIND_SWITCH_GRENADE)) &&
		!(changed & (1UL << HALO_BIND_FLASHLIGHT)), "migrate: the changed actions said");

	/* nothing to do the second time */
	changed = halo_bind_migrate(texts, present);
	present[HALO_BIND_THROW_GRENADE] = 1;
	changed = halo_bind_migrate(texts, present);
	check(changed == 0, "migrate: a migrated file stays as it is");
	check(halo_bind_is_old_default(HALO_BIND_SWITCH_GRENADE, " x ") && !halo_bind_is_old_default(HALO_BIND_FIRE, "G"),
		"old default: case and spaces aside");
}

int main(void)
{
	halo_bind_set_key_names(test_scancode_from_name, test_scancode_name);
	test_defaults();
	test_names();
	test_assign();
	test_migrate();
	printf("%s: %d of %d checks failed\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures ? 1 : 0;
}
