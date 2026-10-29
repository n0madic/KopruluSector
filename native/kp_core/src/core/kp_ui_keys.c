/* The keyboard as the original's window takes it (plan phase 9C): the accelerator tables (LoadAccelerators 0x43f210,
 * Local.dll's 101 merged with the executable's 113, 102 and 103; SetGameAccelerators 0x43f140, SetAccelerators102
 * 0x43f120, SetDefaultAccelerators 0x43f100), the message pump's TranslateAcceleratorA (PumpWindowMessages
 * 0x4401c0) and MainWindowProc 0x43f600's key and character messages with GetModifierKeyState 0x440060 (atlas P13).
 * The caller gives the window's messages (kp_ui_window_key, kp_ui_window_char); the oracle gives the dispatched
 * events themselves (kp_ui_input_event). */
#include "kp_ui.h"

#include <string.h>

#define FVIRTKEY 0x01          /* ACCEL.fVirt */
#define FSHIFT 0x04
#define FCONTROL 0x08
#define FALT 0x10
#define MODIFIERS (FSHIFT | FCONTROL | FALT)
#define TABLE_GAME 101         /* Local.dll's, merged with the executable's 113 */
#define TABLE_EXE 113
#define TABLE_TEXT 102
#define TABLE_DEFAULT 103
#define ENTRY_BYTES 6
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define MOD_SHIFT 3            /* GetModifierKeyState 0x440060 */
#define MOD_CONTROL 4
#define MOD_ALT 8
#define CHAR_REPEAT 0x100

/* The entries of ACCL's table `id` from `module` (1 the executable, 0 Local.dll) appended to t. */
static void append(KpUiAccelTable *t, const uint8_t *accl, size_t size, int module, uint32_t id)
{
	size_t at = 1;
	int n = size ? accl[0] : 0, k;
	for (k = 0; k < n && at + 5 <= size; k++) {
		uint32_t count = kp_le_u16(accl + at + 3), j;
		const uint8_t *e = accl + at + 5;
		if (at + 5 + ENTRY_BYTES * (size_t)count > size) return;
		if (accl[at] == module && kp_le_u16(accl + at + 1) == id) {
			for (j = 0; j < count && t->count < KP_UI_ACCELS; j++, e += ENTRY_BYTES) {
				t->fvirt[t->count] = (uint16_t)kp_le_u16(e);
				t->key[t->count] = (uint16_t)kp_le_u16(e + 2);
				t->cmd[t->count] = (uint16_t)kp_le_u16(e + 4);
				t->count++;
			}
		}
		at += 5 + ENTRY_BYTES * (size_t)count;
	}
}

void kp_ui_load_accelerators(KpUi *ui, const uint8_t *accl, size_t size)
{
	KpUiKeys *k = &ui->keys;
	memset(k->tables, 0, sizeof k->tables);
	append(&k->tables[KP_UI_ACCEL_GAME], accl, size, 0, TABLE_GAME);
	append(&k->tables[KP_UI_ACCEL_GAME], accl, size, 1, TABLE_EXE);
	append(&k->tables[KP_UI_ACCEL_TEXT], accl, size, 0, TABLE_TEXT);
	append(&k->tables[KP_UI_ACCEL_DEFAULT], accl, size, 0, TABLE_DEFAULT);
	k->active = KP_UI_ACCEL_DEFAULT;
}

void kp_ui_set_accelerators(KpUi *ui, int table) { ui->keys.active = table; }

/* GetModifierKeyState 0x440060 without its lock keys' toggles (CapsLock 0x40, NumLock 0x20, ScrollLock 0x10, Insert
 * 0x80): nothing in the image tests them in an event's flags (+0x0a is masked with 3, 4, 8, 0xf and 0x100 only). */
static uint16_t modifiers(const KpUi *ui)
{
	const KpUiInput *p = &ui->input;
	return (uint16_t)((p->keys[0] ? MOD_SHIFT : 0) | (p->keys[1] ? MOD_CONTROL : 0) | (p->keys[2] ? MOD_ALT : 0));
}

/* TranslateAcceleratorA over the active table: a key down (virtual keys) or a character (the others); the command or
 * -1. */
static int32_t translate(const KpUi *ui, uint16_t code, int virtual_key)
{
	const KpUiAccelTable *t = &ui->keys.tables[ui->keys.active];
	const KpUiInput *p = &ui->input;
	int k;
	for (k = 0; k < t->count; k++) {
		uint16_t f = t->fvirt[k];
		if (!(f & FVIRTKEY) != !virtual_key || t->key[k] != code) continue;
		if (virtual_key) {
			uint16_t held = (uint16_t)((p->keys[0] ? FSHIFT : 0) | (p->keys[1] ? FCONTROL : 0) | (p->keys[2] ? FALT : 0));
			if ((f & MODIFIERS) != held) continue;
		} else if (!(f & FALT) != !p->keys[2]) {
			continue;
		}
		return t->cmd[k];
	}
	return -1;
}

static void send(KpUi *ui, uint16_t type, uint16_t key, uint16_t mods)
{
	KpUiEvent e;
	memset(&e, 0, sizeof e);
	e.type = type;
	e.key = key;
	e.mods = mods;
	e.x = (int16_t)ui->mouse_x;
	e.y = (int16_t)ui->mouse_y;
	kp_ui_input_event(ui, &e);
}

int kp_ui_window_key(KpUi *ui, int down, uint16_t vk, int repeat)
{
	KpUiInput *p = &ui->input;
	int32_t cmd;
	if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) p->keys[vk - VK_SHIFT] = (uint8_t)(down != 0);
	if (down && (cmd = translate(ui, vk, 1)) >= 0) { /* TranslateAcceleratorA took it: no TranslateMessage */
		send(ui, KP_UI_COMMAND, (uint16_t)cmd, 0);
		return 0;
	}
	if (p->mouse_state & KP_UI_MOUSE_BUTTONS) { /* the bytes only: no event while a button is held */
		p->vk[vk & 0xff] = (uint8_t)(down != 0);
		return 1;
	}
	ui->abort = 0; /* the pump's previous pass ended on it (0x440130) */
	send(ui, down ? (repeat ? KP_UI_KEY_REPEAT : KP_UI_KEY_DOWN) : KP_UI_KEY_UP, vk, modifiers(ui));
	return !ui->abort;
}

void kp_ui_window_char(KpUi *ui, uint16_t code, int repeat)
{
	int32_t cmd = translate(ui, code, 0);
	if (cmd >= 0) {
		send(ui, KP_UI_COMMAND, (uint16_t)cmd, 0);
		return;
	}
	if (ui->input.mouse_state & KP_UI_MOUSE_BUTTONS) return;
	send(ui, KP_UI_CHAR, code, (uint16_t)(modifiers(ui) | (repeat ? CHAR_REPEAT : 0)));
}
