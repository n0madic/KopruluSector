/* The options dialogs of the in-game menus (opt_dlgs.cpp P27 0x47bc70..0x47bdc0; snd.cpp P31 0x49ed20..0x49f3c0;
 * the video's P41 0x4be5c0..0x4be8d0): each opens with a snapshot of the options block 0x6d3130 that Cancel puts
 * back; the sound dialog's volumes and option bits (the volumes previewed five idle events after a change), the speed
 * dialog's game speed (the command 0x0f at OK) and scroll speeds (live). The video options are our own dialog
 * (koprulu\video.bin in place of rez\video.bin): the original's gamma (previewed) and portrait mode with the art, the
 * fog, the water, the text and the lighting (live); the host applies them (ui->video_options). Every dialog alike:
 * its OK keeps all it changed (ui->options_committed, the host's profile), any other end puts the snapshot back. */
#include "kp_ui.h"

#include <string.h>

#include "kp_game.h"

#define ID_OK (-2)
#define ID_CANCEL (-3)
#define FLAG_TILED 0x20000000u
#define FLAG_SLIDER 0x2000000u
#define TILED_OK 3                   /* 0x508ba0[3] = 0x67: OK's art */
#define TILED_MENU_INDEX 4           /* 0x508ba0[4] = 0x70: a menu button's */
#define TILED_CANCEL 5               /* 0x508ba0[5] = 0x79: Cancel's */
#define PREVIEW_IDLES 5              /* SnapshotOptions 0x47bd80's countdown */
#define VOLUME_STEPS 10              /* InitSoundSlider 0x49f000: page 10, tick 1, 0..100 */
#define VOLUME_MAX 100
#define SPEED_MAX 6
#define CMD_SPEED 0x0f               /* SendGameSpeedCommand 0x474380 */
#define MUSIC_OFF (-10000)
#define ID_MUSIC 1
#define ID_EFFECTS 2
#define ID_GAME_SPEED 1
#define ID_MOUSE_SCROLL 2
#define ID_KEY_SCROLL 3

static void back_to_options(KpUi *ui, int32_t dialog)
{
	/* the idiom of the in-game menus: one level up (OpenOptionsMenu 0x47b340), or the game */
	if (--ui->menu_depth == 0) {
		kp_ui_destroy_dialog(ui, dialog);
		ui->abort = 1;
	} else {
		ui->menu_depth--;
		kp_ui_game_menu(ui, KP_UI_GAME_MENU_OPTIONS);
	}
}

/* The options block 0x6d3130's fields the dialogs change. */
static void snapshot(KpUi *ui, struct KpUiOptionsBlock *b)
{
	b->speed = ui->game != NULL ? (int32_t)kp_scalar(ui->game, KP_SPEED) : 0;
	b->mouse_scroll = ui->mouse_scroll_speed;
	b->key_scroll = ui->key_scroll_speed;
	b->music = ui->sound.music;
	b->effects = ui->sound.effects;
	b->tip = ui->tip_index;
	b->bits = ui->options;
	b->gamma = ui->gamma;
	b->portrait = ui->portrait.mode;
	b->cpu_throttle = ui->cpu_throttle;
	b->video = ui->video_options;
}

static void restore(KpUi *ui, const struct KpUiOptionsBlock *b)
{
	KpUiVideoOptions *o = &ui->video_options;
	if (ui->game != NULL && kp_scalar(ui->game, KP_SPEED) != b->speed) kp_scalar_set(ui->game, KP_SPEED, b->speed);
	ui->mouse_scroll_speed = b->mouse_scroll;
	ui->key_scroll_speed = b->key_scroll;
	ui->sound.music = b->music;
	ui->sound.effects = b->effects;
	ui->tip_index = b->tip;
	ui->options = b->bits;
	ui->cpu_throttle = b->cpu_throttle;
	if (ui->gamma != b->gamma || ui->portrait.mode != b->portrait || o->graphics != b->video.graphics
		|| o->fog != b->video.fog || o->water != b->video.water || o->font != b->video.font
		|| o->lighting != b->video.lighting) {
		ui->gamma = b->gamma;
		if (ui->portrait.mode != b->portrait) kp_ui_portrait_set_mode(ui, b->portrait);
		o->graphics = b->video.graphics;
		o->fog = b->video.fog;
		o->water = b->video.water;
		o->font = b->video.font;
		o->lighting = b->video.lighting;
		o->serial++;
	}
}

void kp_ui_apply_volumes(KpUi *ui)
{
	/* ApplyVolumeOptions 0x49ea60: the music's volume (0x4efbfc[percent] less 0x6aa96c) and the current track again
	 * the first time it is on (ResumeMusic 0x46f160), or its silence and the stream closed (CloseMusic 0x46f290). Its
	 * controls' volume (0x4c3760) is a store nothing reads. */
	KpUiSound *s = &ui->sound;
	KpUiMusic *m = &ui->music;
	if (!s->device) return;
	if (s->music != 0) {
		kp_ui_music_volume(ui, kp_ui_attenuation_at(ui, (int32_t)s->music - 1) - s->master);
		if (!m->resumed) {
			m->resumed = 1;
			kp_ui_music_play(ui, m->track);
		}
	} else {
		kp_ui_music_volume(ui, MUSIC_OFF);
		kp_ui_music_close(ui);
		m->resumed = 0;
	}
}

/* An options dialog opens: what it changes is a preview until its answer. */
static void begin_options(KpUi *ui)
{
	snapshot(ui, &ui->options_snapshot);
	ui->options_preview = PREVIEW_IDLES;
	ui->options_pending = 1;
}

static void init_sub_dialog(KpUi *ui, int32_t dialog)
{
	/* InitOptionsSubDialog 0x49ee00 */
	int32_t c;
	kp_ui_tile_frame(ui, dialog);
	kp_ui_border_colour(ui, dialog);
	begin_options(ui);
	if ((c = kp_ui_find_control(ui, dialog, ID_OK)) >= 0) kp_ui_set_user_word(&ui->controls[c], TILED_OK, FLAG_TILED);
	if ((c = kp_ui_find_control(ui, dialog, ID_CANCEL)) >= 0) {
		kp_ui_set_user_word(&ui->controls[c], TILED_CANCEL, FLAG_TILED);
	}
	ui->menu_depth++;
}

static void apply_or_revert(KpUi *ui, uint32_t section)
{
	/* ApplyOrRevertOptions 0x47bdc0: OK leaves one level for back_to to close all, and the host keeps the section;
	 * any other answer puts the snapshot back */
	ui->options_pending = 0;
	if (ui->result == ID_OK) {
		ui->menu_depth = 1;
		ui->options_committed |= section;
	} else {
		restore(ui, &ui->options_snapshot);
	}
	if (ui->game != NULL) kp_ui_install_game_input(ui);
	kp_ui_apply_volumes(ui);
}

static int16_t pos_of(KpUi *ui, int32_t dialog, int16_t id)
{
	int32_t c = kp_ui_find_control(ui, dialog, id);
	return c >= 0 ? kp_ui_scrollbar_pos(&ui->controls[c]) : 0;
}

/* the sub-dialogs' destroy (FreeOptionsSnapshotAndClose 0x47bc70) */
static int sub_dialog_destroyed(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* closed with no answer (a result's dialog over it, the game's end): put back too, where the original keeps it */
	if (ui->options_pending) {
		ui->options_pending = 0;
		restore(ui, &ui->options_snapshot);
		kp_ui_apply_volumes(ui);
	}
	kp_ui_close_modeless(ui, 0);
	(void)kp_ui_modeless_default(ui, d, e);
	return 1;
}

static void arm_preview(KpUi *ui, uint32_t dialog, int32_t value)
{
	/* SoundSliderChanged 0x49ed30 */
	(void)dialog;
	(void)value;
	if (ui->options_preview == 0) ui->options_preview = PREVIEW_IDLES;
}

/* --- the sound options (rez\snd_dlg.bin) --- */

static int sound_slider(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SoundSliderInteract 0x49efc0 with InitSoundSlider 0x49f000 */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		int32_t v = 0;
		if (c->id == ID_MUSIC || c->id == ID_EFFECTS) {
			v = (int32_t)(c->id == ID_MUSIC ? ui->sound.music : ui->sound.effects);
			if (!ui->sound.device) kp_ui_set_enabled(ui, self, 0);
		}
		kp_ui_scrollbar_set_range(ui, self, 0, VOLUME_MAX);
		kp_ui_scrollbar_set_steps(ui, self, VOLUME_STEPS, 1);
		(void)kp_ui_send_user(ui, self, KP_UI_VALUE_CHANGED, (uint32_t)v);
		kp_ui_scrollbar_set_callback(ui, self, arm_preview);
		c->user = (uint32_t)c->parent;
	} else if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) {
		c->flags |= FLAG_SLIDER;
	}
	return kp_ui_scrollbar_proc(ui, c, e);
}

static int sound_check(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SoundCheckboxInteract 0x49ed40 with InitSoundCheckbox 0x49ed90 */
	static const uint32_t BITS[4] = { KP_UI_OPTION_SPEECH, KP_UI_OPTION_ANSWERS, KP_UI_OPTION_BUILDINGS,
		KP_UI_OPTION_SUBTITLES };
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		if (c->id >= 3 && c->id <= 6) {
			c->user = BITS[c->id - 3];
			if (c->id != 6 && !ui->sound.device) kp_ui_set_enabled(ui, self, 0);
		}
		(void)kp_ui_send_user(ui, self, KP_UI_VALUE_CHANGED, (c->user & ui->options) != 0);
	} else if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) {
		ui->options ^= c->user;
		arm_preview(ui, (uint32_t)c->parent, 0);
	}
	return kp_ui_type_interact(4)(ui, c, e);
}

static void sound_volumes(KpUi *ui, int32_t dialog)
{
	ui->sound.music = (uint32_t)(uint16_t)pos_of(ui, dialog, ID_MUSIC);
	ui->sound.effects = (uint32_t)(uint16_t)pos_of(ui, dialog, ID_EFFECTS);
}

static int sound_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* SoundOptionsDialogProc 0x49eeb0 */
	static const KpUiInteract CHILDREN[6] = { sound_slider, sound_slider, sound_check, sound_check, sound_check,
		sound_check };
	int32_t self = (int32_t)(d - ui->controls);
	if (e->type == KP_UI_IDLE) { /* SoundOptionsIdlePreview 0x49ef60 */
		if (ui->options_preview != 0 && --ui->options_preview == 0) {
			sound_volumes(ui, self);
			kp_ui_apply_volumes(ui);
		}
	} else if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: init_sub_dialog(ui, self); break;
		case KP_UI_DESTROY: return sub_dialog_destroyed(ui, d, e);
		case KP_UI_CLOSE: /* CloseSoundOptions 0x49ee50 (Cancel overwrites both from the snapshot) */
			sound_volumes(ui, self);
			apply_or_revert(ui, KP_UI_SECTION_SOUND);
			back_to_options(ui, self);
			return 1;
		case KP_UI_POST_LOAD: kp_ui_bind_interacts(ui, self, CHILDREN, 6); break;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

/* --- the speed options (rez\spd_dlg.bin) --- */

static void scroll_speeds_changed(KpUi *ui, uint32_t dialog, int32_t value)
{
	/* SpeedSliderChanged 0x49f360: the scroll speeds apply at once */
	(void)value;
	ui->mouse_scroll_speed = (uint32_t)(uint16_t)pos_of(ui, (int32_t)dialog, ID_MOUSE_SCROLL);
	ui->key_scroll_speed = (uint32_t)(uint16_t)pos_of(ui, (int32_t)dialog, ID_KEY_SCROLL);
}

static int speed_slider(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SpeedSliderInteract 0x49f1e0 with InitSpeedSlider 0x49f240 */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		int32_t v = -1;
		if (c->id == ID_GAME_SPEED) v = ui->game != NULL ? (int32_t)kp_scalar(ui->game, KP_SPEED) : 0;
		else if (c->id == ID_MOUSE_SCROLL) v = (int32_t)ui->mouse_scroll_speed;
		else if (c->id == ID_KEY_SCROLL) v = (int32_t)ui->key_scroll_speed;
		if (v >= 0) {
			kp_ui_scrollbar_set_range(ui, self, 0, SPEED_MAX);
			kp_ui_scrollbar_set_steps(ui, self, 1, 1);
			(void)kp_ui_send_user(ui, self, KP_UI_VALUE_CHANGED, (uint32_t)v);
			if (c->id == ID_GAME_SPEED && ui->menus.multiplayer) kp_ui_set_enabled(ui, self, 0);
		}
		kp_ui_scrollbar_set_callback(ui, self, scroll_speeds_changed);
		c->user = (uint32_t)c->parent;
	} else if (e->type == KP_UI_USER && e->code == KP_UI_DESTROY && c->id == ID_GAME_SPEED) {
		kp_ui_delete_timer(&ui->timers, self, 1);
	} else if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) {
		c->flags |= FLAG_SLIDER;
	}
	return kp_ui_scrollbar_proc(ui, c, e);
}

static int throttle_check(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* CpuThrottleCheckboxInteract 0x49f3c0 (Koprulu sleeps no idle time: the flag is only shown and kept) */
	if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) {
		ui->cpu_throttle = !ui->cpu_throttle;
	} else if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) {
		(void)kp_ui_send_user(ui, (int32_t)(c - ui->controls), KP_UI_VALUE_CHANGED, ui->cpu_throttle != 0);
	}
	return kp_ui_type_interact(4)(ui, c, e);
}

static int speed_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* SpeedOptionsDialogProc 0x49f0f0 */
	static const KpUiInteract CHILDREN[4] = { speed_slider, speed_slider, speed_slider, throttle_check };
	int32_t self = (int32_t)(d - ui->controls), s;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: init_sub_dialog(ui, self); break;
		case KP_UI_DESTROY: return sub_dialog_destroyed(ui, d, e);
		case KP_UI_CLOSE: /* CloseSpeedOptions 0x49f080 */
			if (ui->result == ID_OK) {
				s = kp_ui_find_control(ui, self, ID_GAME_SPEED);
				if (s >= 0 && !(ui->controls[s].flags & KP_UI_DISABLED)) {
					uint8_t cmd[2] = { CMD_SPEED, (uint8_t)kp_ui_scrollbar_pos(&ui->controls[s]) };
					kp_ui_queue_command(ui, cmd, sizeof cmd);
				}
			}
			apply_or_revert(ui, KP_UI_SECTION_SPEED);
			back_to_options(ui, self);
			return 1;
		case KP_UI_POST_LOAD: kp_ui_bind_interacts(ui, self, CHILDREN, 4); break;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

/* --- the video options (our koprulu\video.bin; the original's rez\video.bin had the gamma, the colour cycling, now
 * always on, and the portraits) --- */

static int gamma_slider(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* GammaSliderProc 0x4be8d0 */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		kp_ui_scrollbar_set_range(ui, self, KP_UI_GAMMA_MIN, KP_UI_GAMMA_MAX);
		kp_ui_scrollbar_set_steps(ui, self, VOLUME_STEPS, 1);
		(void)kp_ui_send_user(ui, self, KP_UI_VALUE_CHANGED, (uint32_t)ui->gamma);
		kp_ui_scrollbar_set_callback(ui, self, arm_preview);
		c->user = (uint32_t)c->parent;
	} else if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) {
		c->flags |= FLAG_SLIDER;
	}
	return kp_ui_scrollbar_proc(ui, c, e);
}

/* The host's choice an option button's group sets (NULL: the portraits'); its value is the ID's place in the group
 * less 1 (the first, Auto: KP_VIDEO_AUTO). */
static int8_t *video_choice(KpUi *ui, int16_t id)
{
	KpUiVideoOptions *o = &ui->video_options;
	switch (id / KP_VIDEO_GROUP_IDS * KP_VIDEO_GROUP_IDS) {
	case KP_VIDEO_ID_GRAPHICS: return &o->graphics;
	case KP_VIDEO_ID_FOG: return &o->fog;
	case KP_VIDEO_ID_WATER: return &o->water;
	case KP_VIDEO_ID_FONT: return &o->font;
	case KP_VIDEO_ID_LIGHTING: return &o->lighting;
	default: return NULL;
	}
}

static int8_t choice_value(int16_t id) { return (int8_t)(id % KP_VIDEO_GROUP_IDS - 1); }

/* Whether the host can honour an option button's choice (video_options.available; the others are disabled): the
 * animated water needs the water masks of an imported HD variant, the lighting the HD art's lighting layers. */
static int choice_offered(const KpUi *ui, int16_t id)
{
	uint8_t has = ui->video_options.available;
	if (id == KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_HD2)) return (has & KP_VIDEO_HAS_HD2) != 0;
	if (id == KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_HD)) return (has & KP_VIDEO_HAS_HD) != 0;
	if (id == KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_WATER, KP_WATER_ANIMATED)) {
		return (has & (KP_VIDEO_HAS_HD2 | KP_VIDEO_HAS_HD)) != 0;
	}
	if (id == KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_FONT, KP_VTEXT_VECTOR)) return (has & KP_VIDEO_HAS_VECTOR) != 0;
	if (id == KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_LIGHTING, KP_LIGHTING_ON)) return (has & KP_VIDEO_HAS_LIGHTING) != 0;
	return 1;
}

static int video_option(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* PortraitModeRadioProc 0x4be820 for the portraits (KP_VIDEO_ID_PORTRAITS + the mode), the host's choices alike */
	int8_t *choice = video_choice(ui, c->id);
	if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) {
		if (choice == NULL) kp_ui_portrait_set_mode(ui, (uint32_t)(c->id - KP_VIDEO_ID_PORTRAITS));
		else *choice = choice_value(c->id);
		ui->video_options.serial++;
	} else if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) {
		int32_t self = (int32_t)(c - ui->controls);
		int on = choice == NULL ? c->id == (int16_t)(KP_VIDEO_ID_PORTRAITS + ui->portrait.mode)
			: *choice == choice_value(c->id);
		(void)kp_ui_send_user(ui, self, KP_UI_VALUE_CHANGED, (uint32_t)on);
		if (!choice_offered(ui, c->id)) kp_ui_set_enabled(ui, self, 0);
	}
	return kp_ui_type_interact(3)(ui, c, e);
}

/* The slider's gamma (a change previewed: the host's palette follows the serial). */
static void set_gamma(KpUi *ui, int32_t dialog)
{
	uint32_t g = (uint32_t)(uint16_t)pos_of(ui, dialog, KP_VIDEO_ID_GAMMA);
	if (g != ui->gamma) { /* SetGamma 0x4cf930; the palette is the host's (0x4ccf20) */
		ui->gamma = g;
		ui->video_options.serial++;
	}
}

static int video_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* VideoOptionsDialogProc 0x4be6a0 */
	int32_t self = (int32_t)(d - ui->controls), c;
	if (e->type == KP_UI_IDLE) { /* the gamma previewed five idle events after a change */
		if (ui->options_preview != 0 && --ui->options_preview == 0) set_gamma(ui, self);
	} else if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: init_sub_dialog(ui, self); break;
		case KP_UI_DESTROY: return sub_dialog_destroyed(ui, d, e);
		case KP_UI_CLOSE: /* CloseVideoOptions 0x4be640 */
			if (ui->result == ID_OK) set_gamma(ui, self); /* a change still waiting for its preview */
			apply_or_revert(ui, KP_UI_SECTION_VIDEO);
			back_to_options(ui, self);
			return 1;
		case KP_UI_POST_LOAD:
			for (c = d->child; c >= 0; c = ui->controls[c].next) {
				int16_t id = ui->controls[c].id;
				if (id == KP_VIDEO_ID_GAMMA) {
					ui->controls[c].interact = gamma_slider;
				} else if (id >= KP_VIDEO_ID_GRAPHICS && id < KP_VIDEO_ID_LIGHTING + KP_VIDEO_GROUP_IDS) {
					ui->controls[c].interact = video_option;
				}
			}
			break;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

/* --- the tips (tips.cpp, atlas P39 0x4b4f50..0x4b52a0) --- */

#define ID_TIP_TEXT (-10)
#define ID_TIP_PREVIOUS 3
#define TIPS_ORIGINAL 0x50           /* ShowTip 0x4b5000: the tips 1..80, Brood War's 1..103 */
#define TIPS_EXPANSION 0x67
#define FONT_SLOT_TIP 2              /* 0x6d302c */
#define FLAGS_FONT_DEFAULT 0x4c00u   /* 0x4c2680 clears them, 0x4c2590 sets 0x800 (the larger font) */
#define FLAG_FONT_LARGE 0x800u
#define FORMAT_TIP 0x11
#define COLOUR_TIP 2

/* g_missionTipIndex 0x50ded8: the automatic tip of a campaign mission key (0xffff none) */
static const uint16_t MISSION_TIPS[66] = { 0xffff, 1, 2, 4, 8, 0xb, 0xe, 0x11, 0x14, 0x17, 0x1a, 0x1d, 0x20, 0x23, 0x26,
	0x29, 0x2c, 0x2f, 0x32, 0x35, 0x38, 0x3a, 0x3c, 0x3e, 0x40, 0x42, 0x44, 0x46, 0x48, 0x4a, 0x4c, 0x4e, 0x51, 0x52, 0x53,
	0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0xffff, 0x60, 0x61, 0xffff, 0x62, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x64, 0x65, 0x66, 0x67, 0xffff, 0xffff, 0xffff, 0 };

static void show_tip(KpUi *ui, int32_t dialog, int step)
{
	/* ShowTip 0x4b5000: the next or previous tip, wrapping; the larger font when it fits */
	uint32_t last = ui->menus.expansion ? TIPS_EXPANSION : TIPS_ORIGINAL;
	int32_t t = kp_ui_find_control(ui, dialog, ID_TIP_TEXT), width, area;
	KpUiControl *c;
	ui->tip_index = (uint32_t)((int32_t)ui->tip_index + step);
	if (ui->tip_index > last) ui->tip_index = 1;
	else if (ui->tip_index == 0) ui->tip_index = last;
	if (t < 0) return;
	c = &ui->controls[t];
	c->text = kp_ui_tbl(ui, KP_UI_TIPS, ui->tip_index);
	(void)kp_ui_text_select_font(&ui->text, ui->fonts[FONT_SLOT_TIP], ui->font_sizes[FONT_SLOT_TIP]);
	width = c->text != NULL ? (uint16_t)kp_ui_text_width(&ui->text, c->text) : 0;
	area = ui->text.max_height != 0 ? ((c->bottom - c->top) / ui->text.max_height - 1) * (c->right - c->left) : 0;
	if (width < area) c->flags = (c->flags & ~0x4400u) | FLAG_FONT_LARGE;
	else c->flags &= ~FLAGS_FONT_DEFAULT;
	ui->text.font = NULL;
	kp_ui_mark_dirty(ui, t);
}

static void draw_tip(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawTipText 0x4b4f70 */
	(void)x;
	(void)y;
	(void)clip;
	if (c->text == NULL) return;
	kp_ui_select_control_font(ui, c);
	ui->text.flags = FORMAT_TIP;
	kp_ui_select_color(&ui->text, COLOUR_TIP);
	ui->text.gap = 0; /* 0x6d3034 */
	ui->text.clip_left = c->left;
	ui->text.clip_top = c->top;
	ui->text.clip_right = c->right;
	ui->text.clip_bottom = c->bottom;
	(void)kp_ui_draw_text(&ui->text, c->left, c->top, c->text);
	ui->text.font = NULL;
}

static int tip_button(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* the tips' Previous and Next 0x4b52a0 */
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		kp_ui_set_user_word(c, c->id == ID_TIP_PREVIOUS ? TILED_MENU_INDEX : TILED_CANCEL, FLAG_TILED);
	} else if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) {
		show_tip(ui, c->parent, c->id == ID_TIP_PREVIOUS ? -1 : 1);
	}
	return kp_ui_type_interact(c->type)(ui, c, e);
}

static int tip_startup(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* the tips' Show Tips at Startup 0x4b50e0 */
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		c->user = KP_UI_OPTION_TIPS;
		(void)kp_ui_send_user(ui, (int32_t)(c - ui->controls), KP_UI_VALUE_CHANGED, (ui->options >> 8) & 1);
	} else if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) {
		ui->options ^= c->user;
	}
	return kp_ui_type_interact(4)(ui, c, e);
}

static int tips_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* the tips dialog 0x4b5130 with InitTipsDialog 0x4b51c0 */
	static const KpUiInteract CHILDREN[3] = { tip_button, tip_startup, tip_button };
	int32_t self = (int32_t)(d - ui->controls), t;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: {
			uint32_t last = ui->menus.expansion ? TIPS_EXPANSION : TIPS_ORIGINAL;
			int64_t key = ui->game != NULL ? kp_scalar(ui->game, KP_MISSION_INDEX) : 0;
			int step = 1; /* the next tip; the mission's own when opened at its start */
			if (!ui->menus.multiplayer && key > 0 && key < 66 && ui->tips_automatic) {
				ui->tip_index = MISSION_TIPS[key];
				step = 0;
			}
			if (ui->tip_index == 0 || ui->tip_index > last) ui->tip_index = 1;
			kp_ui_tile_frame(ui, self);
			kp_ui_border_colour(ui, self);
			kp_ui_bind_interacts(ui, self, CHILDREN, 3);
			begin_options(ui);
			show_tip(ui, self, step);
			if ((t = kp_ui_find_control(ui, self, ID_TIP_TEXT)) >= 0) ui->controls[t].draw = draw_tip;
			if ((t = kp_ui_find_control(ui, self, ID_OK)) >= 0) {
				kp_ui_set_user_word(&ui->controls[t], TILED_OK, FLAG_TILED);
			}
			ui->menu_depth++;
			break;
		}
		case KP_UI_DESTROY: return sub_dialog_destroyed(ui, d, e);
		case KP_UI_CLOSE:
			apply_or_revert(ui, KP_UI_SECTION_TIPS);
			if (--ui->menu_depth == 0) {
				kp_ui_destroy_dialog(ui, self);
				ui->abort = 1;
			} else {
				ui->menu_depth--;
				kp_ui_game_menu(ui, KP_UI_GAME_MENU_HELP);
			}
			return 1;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

/* --- the openers (0x49ed20, 0x49f060, 0x4be5c0, 0x4b4f50) --- */

void kp_ui_options_dialog(KpUi *ui, int which)
{
	switch (which) {
	case KP_UI_OPTIONS_SOUND: (void)kp_ui_open_modeless(ui, "rez\\snd_dlg.bin", sound_proc); break;
	case KP_UI_OPTIONS_SPEED:
		(void)kp_ui_open_modeless(ui, "rez\\spd_dlg.bin", speed_proc);
		break;
	case KP_UI_OPTIONS_VIDEO:
		(void)kp_ui_open_modeless(ui, "koprulu\\video.bin", video_proc);
		break;
	case KP_UI_OPTIONS_TIPS:
	case KP_UI_OPTIONS_TIPS_AUTOMATIC: /* OpenTipsDialog 0x4b4f50 */
		ui->tips_automatic = which == KP_UI_OPTIONS_TIPS_AUTOMATIC;
		(void)kp_ui_open_modeless(ui, "rez\\tips_dlg.bin", tips_proc);
		break;
	default: break;
	}
}
