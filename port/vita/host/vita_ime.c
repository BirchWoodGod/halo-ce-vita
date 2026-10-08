/*
VITA_IME.C

The system's keyboard (the IME dialog) for the settings panel's lines of
text: the Play page's lobby name and password (vita_settings.c). The
dialog is one of the system's common dialogs, drawn over the game's frames
while vgxm_common_dialog(1) holds (vita_gxm.c) and reading the pad itself;
the panel polls it once a frame (vita_ime_poll) and hands the game no
buttons meanwhile. The text goes in and out as UTF-16; the panel keeps only
printable ASCII (what the game's font and the game lists show).
*/

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/system_param.h>

#include <stdio.h>
#include <string.h>

#include "vita_gxm.h"
#include "vita_host.h"

/* (the dialog's text, and its title, as UTF-16) */
#define IME_TEXT_SIZE 64

static SceWChar16 ime_title[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static SceWChar16 ime_initial[IME_TEXT_SIZE];
static SceWChar16 ime_input[IME_TEXT_SIZE + 1];
static int ime_open;

static void ime_log(const char *step, int result)
{
	char line[128];

	snprintf(line, sizeof(line), "ime: %s -> 0x%08x", step, (unsigned int)result);
	vita_host_log(line);
}

static void ascii_to_utf16(SceWChar16 *out, int size, const char *text)
{
	int length = 0;

	while (text && *text && length < size - 1)
		out[length++] = (SceWChar16)(unsigned char)*text++;
	out[length] = 0;
}

int vita_ime_open(const char *title, const char *text, int maximum_length, int password)
{
	static int configured;
	SceImeDialogParam parameters;
	int result;

	if (ime_open)
		return -1;
	if (!configured)
	{
		SceCommonDialogConfigParam configuration;
		int value;

		/* (the system's language and confirm button) */
		configured = 1;
		sceCommonDialogConfigParamInit(&configuration);
		if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &value) >= 0)
			configuration.language = (SceSystemParamLang)value;
		if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &value) >= 0)
			configuration.enterButtonAssign = (SceSystemParamEnterButtonAssign)value;
		sceCommonDialogSetConfigParam(&configuration);
	}
	if (maximum_length < 1 || maximum_length > IME_TEXT_SIZE)
		maximum_length = IME_TEXT_SIZE;
	ascii_to_utf16(ime_title, SCE_IME_DIALOG_MAX_TITLE_LENGTH, title);
	ascii_to_utf16(ime_initial, maximum_length + 1 < IME_TEXT_SIZE ? maximum_length + 1 : IME_TEXT_SIZE, text);
	memset(ime_input, 0, sizeof(ime_input));
	sceImeDialogParamInit(&parameters);
	parameters.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
	parameters.languagesForced = SCE_FALSE;
	parameters.type = SCE_IME_TYPE_BASIC_LATIN;
	parameters.option = 0;
	parameters.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
	parameters.textBoxMode = password ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD : SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
	parameters.title = ime_title;
	parameters.maxTextLength = (SceUInt32)maximum_length;
	parameters.initialText = ime_initial;
	parameters.inputTextBuffer = ime_input;
	result = sceImeDialogInit(&parameters);
	ime_log("sceImeDialogInit", result);
	if (result < 0)
		return -1;
	ime_open = 1;
	vgxm_common_dialog(1);
	return 0;
}

int vita_ime_poll(char *text, int size)
{
	SceImeDialogResult result;
	int status, code, length = 0, index;

	if (!ime_open)
		return -1;
	status = sceImeDialogGetStatus();
	if (status != SCE_COMMON_DIALOG_STATUS_FINISHED && status >= 0)
		return 0;
	vgxm_common_dialog(0);
	memset(&result, 0, sizeof(result));
	code = sceImeDialogGetResult(&result);
	ime_log("sceImeDialogGetResult", code);
	ime_log("sceImeDialogTerm", sceImeDialogTerm());
	ime_open = 0;
	if (status < 0 || code < 0 || result.button != SCE_IME_DIALOG_BUTTON_ENTER)
		return -1;
	for (index = 0; ime_input[index] && length < size - 1; index++)
		if (ime_input[index] >= 0x20 && ime_input[index] <= 0x7E)
			text[length++] = (char)ime_input[index];
	text[length] = 0;
	return 1;
}
