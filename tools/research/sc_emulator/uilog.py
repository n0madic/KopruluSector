"""The interface's input log (input.jsonl) for the behaviour oracle of plan phase 9 (docs/phase9.md).

Everything the original's interface takes from outside the game, in the order it takes it, so that Koprulu's
interface (kp_ui) can be driven by the same input and checked for the same commands, generator draws, sounds and
portraits:
- every event the input handlers give the dialog dispatcher DispatchUiEvent 0x4c2bb0 (the window procedure 0x43f600
  and its mouse routines 0x43f480/0x43f520/0x43f5b0, the pump's mouse move 0x4400d0 and idle event 0x4401c0; atlas
  P13): the event's type and the fields its sender writes, the key-down bytes of Shift, Ctrl and Alt (0x665e68..;
  mouse senders leave the event's modifier field stale, and the selection code reads the key bytes), the mouse state
  word 0x6d315c and the GetTickCount value the dispatcher reads for its press slots (0x4c2c00);
- every WM_COMMAND event the window procedure gives the registered handler 0x665e30 directly (0x43f768: the
  accelerators of Local.dll, P16);
- every UI timer set (SetUiTimer 0x4c5960: owner, ID, period, whether it has a callback, and the tick it starts
  from, 0x4c59f7), deleted (0x4c58f0, 0x4c5840) and fired (RunUiTimers 0x4c5a20 at 0x4c5a65: owner, ID and the
  pass's tick), P43.
A timer's owner is a control: it is named by its dialog BIN, the control's record index there (LoadDialog
0x4c33a0 keeps the BIN in one buffer of 86-byte records, so a control of a loaded BIN is at root + 86 * index) and
the load of that BIN it belongs to (0 for the first), else by its address. An owner gets a number the first time it is logged ({"owner": n, "ref": [BIN, index] or address},
written at once, before the record that uses it), and the timer records name it by that number.

Records (one JSON line each; "f" the frame of install_logs's current_frame, "glue" the glue state 0x66a274 while the
game mode 0x6662b8 is not 3):
  {"f", "ev": [type, ...fields], "k": [shift, ctrl, alt], "ms": mouse state, "t": tick, "scr": [x, y]}   ("scr":
      the game view's top-left in map pixels, 0x543218/0x54321c)
  {"f", "cmd": command id}
  {"f", "owner": n, "ref": [BIN, index, load] or "0x..."}
  {"f", "tset": [owner n, id, period, has callback], "t": tick}
  {"f", "tpass": tick, "fired": [[owner n, id], ...], "during": [[k, record], ...]}   (a RunUiTimers pass that fired
      timers, in order; "during" holds the records made while the k-th fired timer ran, e.g. the timers its callback
      set, in order)
  {"f", "tdel": [owner n, id]}, {"f", "tdelall": owner n}   (DeleteUiTimer 0x4c58f0, DeleteTimersOfOwner 0x4c5840)
  {"f", "portrait": [unit, portrait, mode]}   (ShowPortrait 0x4a6f10: the unit's index or 0, mode 2 talking)
  {"f", "talk": [unit, duration, sound], "ping": [flag, colour], "t": virtual ms}   (RequestUnitTalking 0x4a7200)
  {"f", "svid": path}   (SVidPlayBegin, Storm 454: a movie opened)
  {"f", "prng": [x, y, z, which]}   (the portrait RNG 0x50d7cc at OpenIdlePortrait 0x4a6b40 (which 0) and
      OpenTalkingPortrait 0x4a6c50 (1))
  {"f", "static": fade}   (DrawPortraitStatic 0x4a6610: a draw of the fade-in noise with that threshold)
  {"f", "present": tick}   (PresentFrame 0x4c5360 in a game, mode 3: the dialogs' repaint and the screen's flip)
  {"f", "hud": 1}   (UpdateStatusUi 0x4a7ee0: the console's per-frame update)
  {"f", "sel": [unit, ...]}   (SelectUnitsLocally 0x49cf80: the local selection as the interface sets it)
  {"f", "cursor": [id, counter]}   (UpdateCursorLayerRect 0x43e610 with the cursor id 0x665d74 or its animation
      counter 0x665d58 changed since the last record)
  {"f", "curupd": id, "pos": [x, y]}   (UpdateCursor 0x43ea40, the game loop's choice of the cursor, when it changed,
      and the system cursor's position its GetEdgeScrollCursorId saw, 0x503220/0x503224)
  {"f", "scroll": [x, y, step, dx, dy, suppressed, cursor x, cursor y], "speeds": [mouse, key]}   (UpdateMapScroll
      0x49bf10 left its state changed: the view 0x543218/0x54321c, the step 0x6a5eec, the last direction
      0x6a5ee4/0x6a5ee8, the edge scroll's suppression 0x6a5ecc and its cursor position 0x503220/0x503224; the
      speed settings 0x6d3134/0x6d3138; the game loop runs it every 10 ms or more, not every frame)
  {"f", "snd": [id, attenuation, pan, unit type or -1, path, result], "dev": [device, effects volume], "t": tick,
      "ch": [[id, unit type, flags, priority, buffer], ...] (the eight channels 0x6aa990 when they changed)}   (PlaySoundInternal 0x49e4e0 at its return:
      path 0 rejected early, 1 queued for the loader (QueueSoundLoad 0x49e000), 2 started (StartCachedSound
      0x49e620); buffer 1 when the channel holds one)
  {"f", "sndload": 1, "ch": [...]}   (SoundLoaderThread 0x49e090 finished a pass and the channels changed)
  {"f", "tip": [left, top, right, bottom]} and {"f", "tip": []}   (PlaceTooltip 0x4193e0: the tooltip shown there;
      HideTooltip 0x4191d0 of a shown one)
  {"f", "ui": {...}}   (scenario.py: a game call of the interface that ui.jsonl logs, the paused frames, the trigger
      wait's release, the Escape skip and the screen's draws, at its place among the interface's work)
  with SC_UI_REGIONS set (diagnostics): {"f", "dirty": [l, t, r, b]} (AddDirtyRect 0x4c5490, inclusive) and
  {"f", "region": [[l, t, r, b], ...]} (PaintDialogLayer 0x4c4d90's rectangles of the region, exclusive)
  {"f", "svidc": [result, updated, frame]}   (the console portrait's SVidPlayContinueSingle, Storm 457, in
      ContinuePortraitVideo 0x4a6700: its result, the frame-decoded flag and the Smack record's next frame, -1 once
      the movie ended)
  {"f", "brief": [record, action index, type, result], "t": tick}   (the briefing VM 0x44fee0: an action's handler
      of 0x503908 returned; record counts the records in their first run's order from 0, result 0 suspends)
  {"f", "flc": [clip, result, shown, frame], "t": tick}   (outside the game: a flic button's clip continued by
      AdvanceFlicButton 0x4c89b0: the clips counted in their first continuation's order from 0 (a node's number ends with
      its clip, CloseFlcButtonVideos 0x4c8470 or the end), SVidPlayContinueSingle's
      result, the frame-shown flag and the Smack record's next frame, -1 once the movie ended; the tick after the call
      and, when SmackWait read the clock, "wait": the timeGetTime value it compared)
  {"f", "bpc": [control id, result, shown, frame], "t": tick}   (a ready room's portrait slot continued by
      AdvanceBriefingPortraitVideo 0x4502e0, likewise)
  {"f", "credits": name, "t": tick}, {"f", "page": 1, "t": tick}   (RunCreditsScreen 0x415010 of rez\\<name>.txt, e.g. a
      mission's establishing text, and ShowNextCreditsPage 0x414c60 at its entry with the page before's deadline)
  {"f", "rand": holdrand}   (outside the game: CRT rand 0x4d14d4, the per-thread generator's state before the draw;
      the menus' draws, e.g. the briefing portraits' variants, before the game's seed)
  {"f", "turn": 1, "t": tick}   (outside the game: the network turn step 0x476c00 of a modal dialog's idle callback,
      e.g. the ready room's PollBriefingReady 0x461120; offline it takes Start's command 0x54)
  {"f", "music": [function, track], "t": tick}   (PlayMusicTrack 0x46ef10 as 0, StartMusicTrack 0x46f1b0 as 1)
  {"f", "score": {"roster": [{name, force, index, storm, type, race} x 8], "count", "arrays": {name: [12 x s32]},
      "colours": [12], "player_colours": [8]}, "t": tick}   (RunScoreScreen 0x461940: what the score screen reads)
  {"f", "elapsed": seconds}   (SetScoreElapsedTime 0x4625f0: GetElapsedSeconds 0x47a780's value)
Event fields by type: keys 0/1/2 and characters 15 [key, modifiers]; mouse 3..13 [x, y]; wheel 17/18 [lParam].
"""
from __future__ import annotations

import struct

from .hle.kernel32 import TICK_BASE_MS

DISPATCH_UI_EVENT, DISPATCH_TICK = 0x4C2BB0, 0x4C2C00  # entry; past `call GetTickCount` (EAX = the tick)
INPUT_SENDERS = (0x43F480, 0x440306)  # 0x43f480..PumpWindowMessages's end: the input handlers of P13
COMMAND_CALL = 0x43F768  # MainWindowProc's `call edx` of the WM_COMMAND handler (ECX = the event)
SET_UI_TIMER, SET_UI_TIMER_TICK = 0x4C5960, 0x4C59F7  # entry (ECX owner, DX id, [esp+4] period, [esp+8] callback)
DELETE_UI_TIMER, DELETE_OWNER_TIMERS = 0x4C58F0, 0x4C5840  # ECX owner, DX id; ECX owner (P43)
RUN_UI_TIMERS, TIMER_FIRE, TIMERS_DONE = 0x4C5A20, 0x4C5A65, 0x4C5AF7  # entry; a fire (ESI = the node, EBX =
# the pass's tick); the walk's end, before the fired queue is sorted back
LOAD_DIALOG, LOAD_DIALOG_LOADED = 0x4C33A0, 0x4C33B2  # entry (ECX = BIN name); past the loader call (EAX = root)
SHOW_PORTRAIT, REQUEST_TALK = 0x4A6F10, 0x4A7200  # ECX unit, DX portrait, [esp+4] mode; ECX unit, EDX duration,
# [esp+0xc] sound
PRESENT_FRAME, UPDATE_STATUS_UI = 0x4C5360, 0x4A7EE0  # entries
SELECT_LOCALLY = 0x49CF80  # ECX = count, EDX = the unit pointers
UPDATE_CURSOR, CURSOR_ID, CURSOR_COUNTER = 0x43E610, 0x665D74, 0x665D58
CURSOR_CHOSEN = 0x43EA7C  # UpdateCursor 0x43ea40 after its choice, before AnimateCursor
SCREEN_X, SCREEN_Y = 0x543218, 0x54321C  # the game view's top-left in map pixels
SCROLL_DONE = 0x49C0E5  # UpdateMapScroll 0x49bf10's common exit
PLACE_TOOLTIP, HIDE_TOOLTIP, TOOLTIP_VISIBLE = 0x4193E0, 0x4191D0, 0x5C999C  # ECX = {s16 l, t, r, b}; no arguments
PLAY_SOUND_INTERNAL = 0x49E4E0  # ECX id, EDX attenuation, [esp+4] pan, [esp+8] unit
PLAY_SOUND_RETURNS = {0x49E5A6: 0, 0x49E617: 0, 0x49E5D4: 1, 0x49E60D: 2}  # its `ret 8`s: rejected, queued, started
LOADER_WAIT = 0x49E246  # SoundLoaderThread's WaitForSingleObject after a pass
SOUND_CHANNELS, CHANNEL_SIZE = 0x6AA990, 24
SOUND_DEVICE, EFFECTS_VOLUME = 0x6AA954, 0x6D3140
SCROLL_STEP, SCROLL_DX, SCROLL_DY, SCROLL_SUPPRESSED = 0x6A5EEC, 0x6A5EE4, 0x6A5EE8, 0x6A5ECC
EDGE_CURSOR_X, EDGE_CURSOR_Y, MOUSE_SCROLL_SPEED, KEY_SCROLL_SPEED = 0x503220, 0x503224, 0x6D3134, 0x6D3138
ADD_DIRTY_RECT, REGION_RECTS, REGION_COUNT, REGION_LIST = 0x4C5490, 0x4C4E75, 0x6CE78C, 0x6CE790
OPEN_IDLE, OPEN_TALKING, PORTRAIT_STATIC, PORTRAIT_RNG = 0x4A6B40, 0x4A6C50, 0x4A6610, 0x50D7CC
SVID_BEGIN = 0x15030E20  # storm.dll ordinal 454 ([esp+4] = the file name)
PORTRAIT_CONTINUED, PORTRAIT_VIDEO = 0x4A6733, 0x6AB390  # past `call SVidPlayContinueSingle` (EAX; ESI = the flag)
UNIT_POOL, UNIT_SIZE = 0x5DA3A0, 0x150  # game.py: CUnit index 1
KEY_SHIFT, MOUSE_STATE = 0x665E68, 0x6D315C
CREDITS_RUN, CREDITS_PAGE = 0x415010, 0x414C60  # ECX = the script's base name; ECX = the dialog
SCORE_RUN = 0x461940             # RunScoreScreen: the roster (captured at game start) and the score arrays it reads
SCORE_ELAPSED = 0x46260E         # SetScoreElapsedTime 0x4625f0 past `call GetElapsedSeconds` (EAX = seconds)
SCORE_ROSTER, SCORE_ROSTER_COUNT = 0x66C710, 0x66CA6C  # 8 x 0x48 bytes; the listed count
SCORE_ARRAYS = {  # 12 x s32 each (P21 "Score arrays")
    "minerals": 0x5263D0, "gas": 0x526400, "cumulative_gas": 0x526430, "cumulative_minerals": 0x526460,
    "men_produced": 0x529124, "men_lost": 0x529154, "men_killed": 0x529184, "unit_score": 0x5291B4,
    "kill_score": 0x5291E4, "buildings_constructed": 0x529274, "buildings_lost": 0x5292A4,
    "buildings_razed": 0x5292D4, "structure_score": 0x529304, "razing_score": 0x529334}
SCORE_COLOURS, PLAYER_COLOURS = 0x536722, 0x5264FC  # the score screen's colour indexes (12 bytes), the colour per slot
# (8 u32, MapPlayerColor 0x41d560)
CREDITS_DEADLINE = 0x5AEF1C  # the page's deadline (GetTickCount after its fades plus the display time)
FLC_CONTINUED = 0x4C89D8  # AdvanceFlicButton past `call SVidPlayContinueSingle` (EAX; [esi] = the clip, [ebp-4] shown)
FLC_ENDED = 0x4C89DC  # AdvanceFlicButton unlinking an ended clip ([esi] = the clip)
FLC_CLOSE = 0x4C8470  # CloseFlcButtonVideos (ECX = the control, +0x42 its clips linked through +0)
BRIEF_PORTRAIT_CONTINUED = 0x450305  # 0x4502e0 past `call SVidPlayContinueSingle` (EAX; EDI = the control, ESI its state)
SMACK_WAIT_NOW = 0x100077D2  # Smackw32 SmackWait 0x10007720 past `call timeGetTime` (EAX = the clock it compares)
TURN_STEP = 0x476C00  # the network turn step (a modal dialog's idle callback, PollBriefingReady 0x461120)
MUSIC_PLAY, MUSIC_START = 0x46EF10, 0x46F1B0  # ECX = the track
CRT_RAND_STATE = 0x4D14D9  # CRT rand past _getptd (EAX = the thread data, +0x14 holdrand)
BRIEF_ACTION_CALL, BRIEF_ACTION_RETURN = 0x44FF26, 0x44FF28  # 0x44fee0's `call eax` (ESI = the record, ECX = the
# action, [esi+0x95f] = its index, [ecx+0x1a] = its type) and its return (EAX)
GAME_MODE, GLUE_STATE = 0x6662B8, 0x66A274
CONTROL_SIZE, MAX_CONTROLS = 0x56, 256
KEY_TYPES, MOUSE_TYPES, WHEEL_TYPES = (0, 1, 2, 15), range(3, 14), (17, 18)


def event_fields(ev: bytes) -> list[int]:
    """The event's type and the fields its sender writes (atlas P13)."""
    _user, param, key, mods, etype, x, y = struct.unpack("<IIHHHhh", ev[:18])
    if etype in KEY_TYPES or etype == 16:
        return [etype, key, mods]
    if etype in MOUSE_TYPES:
        return [etype, x, y]
    if etype in WHEEL_TYPES:
        return [etype, param]
    return [etype]


def install(session, write, current_frame) -> dict:
    """Hooks the log; `write(record)` appends a record, `current_frame()` gives its frame."""
    from unicorn import x86_const as X
    from .hle.kernel32 import TICK_BASE_MS  # the guest's clock (needs unicorn; reading a log does not)
    m = session.m
    st = {"dialogs": {}, "loads": {}, "loads_seq": [], "loading": [], "dispatch": [], "set": None, "owners": {}, "passes": []}

    def reg(r):
        return m.reg(r)

    def record(rec: dict) -> None:
        rec = {"f": current_frame(), **rec}
        if m.r16(GAME_MODE) != 3:
            rec["glue"] = m.r32(GLUE_STATE)
        p = st["passes"][-1] if st["passes"] else None
        if p is not None and p["fired"]:  # made by a timer of the pass that runs
            p["during"].append([len(p["fired"]) - 1, rec])
        else:
            write(rec)

    def record_count(root: int) -> int | None:
        """The root and its children (the BIN's records, contiguous: the first child +0x42, then each +0x00) once the
        dialog is initialized; MAX_CONTROLS before (cached only when known)."""
        try:
            count, c = 1, m.r32(root + 0x42)
            while c and count < MAX_CONTROLS:
                if not root < c < root + CONTROL_SIZE * MAX_CONTROLS:
                    return None
                count += 1
                c = m.r32(c)
            return count
        except Exception:  # noqa: BLE001 - not initialized yet: its pointers are not mapped
            return None

    def dialog_ref(p: int):
        """[BIN, record index, load] of a control of a loaded BIN (load: how many loads of that BIN came before, so
        that two dialogs of one BIN, e.g. the console's four statfluf.bin, are told apart), else its address."""
        best = None  # the latest load whose records cover p (a freed BIN's memory is reused by later loads)
        for root, entry in st["dialogs"].items():
            name, k, seq, count = entry
            if m.r16(root + 0x22) != 0:  # the root's memory holds no dialog any more (freed and reused)
                continue
            if count is None:
                count = entry[3] = record_count(root)
            d = p - root
            if 0 <= d < CONTROL_SIZE * (count or MAX_CONTROLS) and d % CONTROL_SIZE == 0 and \
                    (best is None or seq > best[0]):
                best = (seq, [name, d // CONTROL_SIZE, k])
        return best[1] if best else f"{p:#x}"

    st["dialog_ref"] = dialog_ref

    def owner_ref(p: int) -> int:
        ref = dialog_ref(p)
        key = repr(ref)
        if key not in st["owners"]:
            st["owners"][key] = len(st["owners"])
            write({"f": current_frame(), "owner": st["owners"][key], "ref": ref})  # a definition: never buffered
        return st["owners"][key]

    def on_load_dialog():
        st["loading"].append(m.cstr(reg(X.UC_X86_REG_ECX)).decode("latin-1"))

    def on_dialog_loaded():
        name = st["loading"].pop() if st["loading"] else "?"
        root = reg(X.UC_X86_REG_EAX)
        if root:
            k = st["loads"].get(name, 0)
            st["loads"][name] = k + 1
            st["dialogs"][root] = [name, k, len(st["loads_seq"]), None]  # the record count: when first used
            st["loads_seq"].append(root)

    def on_dispatch():
        ret = m.r32(m.esp)
        if INPUT_SENDERS[0] <= ret < INPUT_SENDERS[1]:
            st["dispatch"].append(m.read(reg(X.UC_X86_REG_ECX), 20))
        else:
            st["dispatch"].append(None)

    def on_dispatch_tick():
        ev = st["dispatch"].pop() if st["dispatch"] else None
        if ev is not None:
            record({"ev": event_fields(ev), "k": list(m.read(KEY_SHIFT, 3)), "ms": m.r16(MOUSE_STATE),
                    "t": reg(X.UC_X86_REG_EAX), "scr": [m.r32(SCREEN_X) & 0xFFFF, m.r32(SCREEN_Y) & 0xFFFF]})

    def on_command():
        record({"cmd": struct.unpack("<H", m.read(reg(X.UC_X86_REG_ECX) + 8, 2))[0]})

    def on_set_timer():
        st["set"] = [owner_ref(reg(X.UC_X86_REG_ECX)), reg(X.UC_X86_REG_EDX) & 0xFFFF, m.r32(m.esp + 4),
                     int(m.r32(m.esp + 8) != 0)]

    def on_set_timer_tick():
        if st["set"] is not None:
            record({"tset": st["set"], "t": reg(X.UC_X86_REG_EAX)})
            st["set"] = None

    def on_delete_timer():
        record({"tdel": [owner_ref(reg(X.UC_X86_REG_ECX)), reg(X.UC_X86_REG_EDX) & 0xFFFF]})

    def on_delete_owner_timers():
        record({"tdelall": owner_ref(reg(X.UC_X86_REG_ECX))})

    def on_run_timers():  # a stack: a timer's modal loop may pump messages and run the timers again
        st["passes"].append({"fired": [], "during": []})

    def on_timer_fire():
        if not st["passes"]:
            return
        p, node = st["passes"][-1], reg(X.UC_X86_REG_ESI)
        if not p["fired"]:
            p["tick"] = reg(X.UC_X86_REG_EBX)
        p["fired"].append([owner_ref(m.r32(node + 4)), m.r16(node + 0x14)])

    def on_timers_done():
        p = st["passes"].pop() if st["passes"] else None
        if p and p["fired"]:
            rec = {"tpass": p["tick"], "fired": p["fired"]}
            if p["during"]:
                rec["during"] = p["during"]
            record(rec)

    def unit_index(p: int) -> int:
        return (p - UNIT_POOL) // UNIT_SIZE + 1 if p else 0

    def on_show_portrait():
        record({"portrait": [unit_index(reg(X.UC_X86_REG_ECX)), reg(X.UC_X86_REG_EDX) & 0xFFFF,
                             m.r32(m.esp + 4) & 0xFFFF]})

    def on_request_talk():
        record({"talk": [unit_index(reg(X.UC_X86_REG_ECX)), reg(X.UC_X86_REG_EDX), m.r32(m.esp + 0xC)],
                "ping": [m.r32(m.esp + 4), m.r32(m.esp + 8) & 0xFF], "t": m.clock_us // 1000 + TICK_BASE_MS})

    def on_svid_begin():
        record({"svid": m.cstr(m.r32(m.esp + 4)).decode("latin-1")})

    def on_portrait_continued():
        flag = reg(X.UC_X86_REG_ESI)
        video = m.r32(PORTRAIT_VIDEO)
        frame = -1
        try:  # an ended movie's record is freed
            smk = m.r32(m.r32(video + 0x58) + 4) if video else 0
            frame = m.r32(smk + 0x374) if smk else -1
        except Exception:  # noqa: BLE001 - unmapped guest memory
            frame = -1
        smack_now[0] = None  # the console portrait's wait is not a glue movie's
        record({"svidc": [reg(X.UC_X86_REG_EAX), m.r32(flag) if flag else 0, frame]})

    def on_open(which):
        return lambda: record({"prng": [m.r32(PORTRAIT_RNG), m.r32(PORTRAIT_RNG + 4), m.r32(PORTRAIT_RNG + 8), which]})

    def on_static():
        record({"static": reg(X.UC_X86_REG_ECX) & 0xFF})

    def on_present():
        if m.r16(GAME_MODE) == 3:
            record({"present": m.clock_us // 1000 + TICK_BASE_MS})

    def on_update_status():
        record({"hud": 1})

    def on_select_locally():
        n, units = reg(X.UC_X86_REG_ECX), reg(X.UC_X86_REG_EDX)
        record({"sel": [unit_index(m.r32(units + 4 * k)) for k in range(min(n, 12))]})

    session.hook(SELECT_LOCALLY, on_select_locally)
    last_cursor = [None]

    def on_update_cursor():
        c = [m.r32(CURSOR_ID) & 0xFFFF, m.r32(CURSOR_COUNTER)]
        if c != last_cursor[0] and m.r16(GAME_MODE) == 3:
            last_cursor[0] = c
            record({"cursor": c})

    session.hook(UPDATE_CURSOR, on_update_cursor)
    last_choice = [None]

    def on_cursor_chosen():
        c = m.r32(CURSOR_ID) & 0xFFFF
        if c != last_choice[0] and m.r16(GAME_MODE) == 3:
            last_choice[0] = c
            record({"curupd": c, "pos": [m.rs32(EDGE_CURSOR_X), m.rs32(EDGE_CURSOR_Y)]})

    session.hook(CURSOR_CHOSEN, on_cursor_chosen)
    last_scroll = [None]

    def on_scroll_done():
        if m.r16(GAME_MODE) != 3:
            return
        st = [m.rs32(SCREEN_X), m.rs32(SCREEN_Y), m.r32(SCROLL_STEP), m.rs32(SCROLL_DX), m.rs32(SCROLL_DY),
              m.r32(SCROLL_SUPPRESSED), m.rs32(EDGE_CURSOR_X), m.rs32(EDGE_CURSOR_Y)]
        speeds = [m.r32(MOUSE_SCROLL_SPEED), m.r32(KEY_SCROLL_SPEED)]
        if [st, speeds] != last_scroll[0]:
            last_scroll[0] = [st, speeds]
            record({"scroll": st, "speeds": speeds})

    session.hook(SCROLL_DONE, on_scroll_done)

    def on_place_tooltip():
        record({"tip": list(struct.unpack("<4h", m.read(reg(X.UC_X86_REG_ECX), 8)))})

    def on_hide_tooltip():
        if m.r32(TOOLTIP_VISIBLE):
            record({"tip": []})

    session.hook(PLACE_TOOLTIP, on_place_tooltip)
    session.hook(HIDE_TOOLTIP, on_hide_tooltip)
    last_channels = [None]
    pending_sound = []

    def channels():
        out = []
        for c in range(8):
            b = m.read(SOUND_CHANNELS + CHANNEL_SIZE * c, CHANNEL_SIZE)
            buf, utype, flags, prio = struct.unpack_from("<IHBB", b, 0)
            out.append([struct.unpack_from("<I", b, 0xC)[0], utype, flags, prio, int(buf != 0)])
        return out

    def with_channels(rec):
        ch = channels()
        if ch != last_channels[0]:
            last_channels[0] = ch
            rec["ch"] = ch
        return rec

    def on_play_sound():
        unit = m.r32(m.esp + 8)
        pending_sound.append([reg(X.UC_X86_REG_ECX), struct.unpack("<i", struct.pack("<I", reg(X.UC_X86_REG_EDX)))[0],
                              struct.unpack("<i", m.read(m.esp + 4, 4))[0],
                              m.r16(unit + 0x64) if unit else -1])

    def on_play_sound_return(path):
        def hook():
            if pending_sound:
                record(with_channels({"snd": pending_sound.pop() + [path, reg(X.UC_X86_REG_EAX)],
                                      "dev": [int(m.r32(SOUND_DEVICE) != 0), m.r32(EFFECTS_VOLUME)],
                                      "t": TICK_BASE_MS + m.clock_us // 1000}))
        return hook

    def on_loader_wait():
        rec = with_channels({"sndload": 1})
        if "ch" in rec:
            record(rec)

    brief = {"records": {}, "call": None}

    def on_brief_call():
        rec_ptr, action = reg(X.UC_X86_REG_ESI), reg(X.UC_X86_REG_ECX)
        k = brief["records"].setdefault(rec_ptr, len(brief["records"]))
        brief["call"] = [k, m.read(rec_ptr + 0x95F, 1)[0], m.read(action + 0x1A, 1)[0]]

    def on_brief_return():
        if brief["call"] is not None:
            record({"brief": brief["call"] + [reg(X.UC_X86_REG_EAX)], "t": m.clock_us // 1000 + TICK_BASE_MS})
            brief["call"] = None

    def on_crt_rand():
        if m.r16(GAME_MODE) != 3:
            record({"rand": m.r32(reg(X.UC_X86_REG_EAX) + 0x14)})

    session.hook(CRT_RAND_STATE, on_crt_rand)

    def on_turn_step():
        if m.r16(GAME_MODE) != 3:
            record({"turn": 1, "t": m.clock_us // 1000 + TICK_BASE_MS})

    def on_music(kind):
        def fn():
            record({"music": [kind, reg(X.UC_X86_REG_ECX) & 0xFFFF], "t": m.clock_us // 1000 + TICK_BASE_MS})
        return fn

    flc_clips, flc_next = {}, [0]  # the clips' numbers by node, until the node's clip ends (its memory is reused)
    smack_now = [None]  # the clock SmackWait read in the continuation under way

    def on_smack_wait_now():
        smack_now[0] = reg(X.UC_X86_REG_EAX)

    def video_tick(r: dict) -> dict:
        r["t"] = m.clock_us // 1000 + TICK_BASE_MS
        if smack_now[0] is not None:
            r["wait"] = smack_now[0]
            smack_now[0] = None
        return r

    def on_flc_continued():
        if m.r16(GAME_MODE) == 3:
            return
        node = m.r32(reg(X.UC_X86_REG_ESI))
        if node not in flc_clips:
            flc_clips[node] = flc_next[0]
            flc_next[0] += 1
        k = flc_clips[node]
        frame = -1
        try:
            video = m.r32(node + 0xE)
            smk = m.r32(m.r32(video + 0x58) + 4) if video else 0
            frame = m.r32(smk + 0x374) if smk else -1
        except Exception:  # noqa: BLE001 - an ended movie's record is freed
            frame = -1
        record(video_tick({"flc": [k, reg(X.UC_X86_REG_EAX), m.r32(reg(X.UC_X86_REG_EBP) - 4), frame]}))

    def smack_frame(video):
        try:
            smk = m.r32(m.r32(video + 0x58) + 4) if video else 0
            return m.r32(smk + 0x374) if smk else -1
        except Exception:  # noqa: BLE001 - an ended movie's record is freed
            return -1

    def on_brief_portrait_continued():
        control, state = reg(X.UC_X86_REG_EDI), reg(X.UC_X86_REG_ESI)
        cid = struct.unpack("<h", m.read(control + 0x20, 2))[0]
        record(video_tick({"bpc": [cid, reg(X.UC_X86_REG_EAX), m.r32(reg(X.UC_X86_REG_EBP) - 4),
                                   smack_frame(m.r32(state + 8))]}))

    session.hook(SMACK_WAIT_NOW, on_smack_wait_now)
    def on_flc_ended():
        flc_clips.pop(m.r32(reg(X.UC_X86_REG_ESI)), None)

    def on_flc_close():
        node = m.r32(reg(X.UC_X86_REG_ECX) + 0x42)
        while node:
            flc_clips.pop(node, None)
            node = m.r32(node)

    session.hook(FLC_CONTINUED, on_flc_continued)
    session.hook(FLC_ENDED, on_flc_ended)
    session.hook(FLC_CLOSE, on_flc_close)
    session.hook(BRIEF_PORTRAIT_CONTINUED, on_brief_portrait_continued)
    def on_score_run():
        roster = []
        for i in range(8):
            r = m.read(SCORE_ROSTER + 0x48 * i, 0x48)
            roster.append({"name": r[:25].split(b"\0")[0].decode("latin-1"),
                           "force": r[0x19:0x37].split(b"\0")[0].decode("latin-1"),
                           "index": struct.unpack("<I", r[0x38:0x3c])[0], "storm": struct.unpack("<I", r[0x3c:0x40])[0],
                           "type": struct.unpack("<I", r[0x40:0x44])[0], "race": r[0x44]})
        arrays = {k: list(struct.unpack("<12i", m.read(a, 48))) for k, a in SCORE_ARRAYS.items()}
        record({"score": {"roster": roster, "count": m.r32(SCORE_ROSTER_COUNT), "arrays": arrays,
                          "colours": list(m.read(SCORE_COLOURS, 12)),
                          "player_colours": list(struct.unpack("<8I", m.read(PLAYER_COLOURS, 32)))},
                "t": m.clock_us // 1000 + TICK_BASE_MS})

    session.hook(SCORE_RUN, on_score_run)
    session.hook(SCORE_ELAPSED, lambda: record({"elapsed": reg(X.UC_X86_REG_EAX)}))
    session.hook(CREDITS_RUN, lambda: record({"credits": m.cstr(reg(X.UC_X86_REG_ECX)).decode("latin-1"),
                                              "t": m.clock_us // 1000 + TICK_BASE_MS}))
    session.hook(CREDITS_PAGE, lambda: record({"page": 1, "t": m.clock_us // 1000 + TICK_BASE_MS,
                                                "deadline": m.r32(CREDITS_DEADLINE)}))
    session.hook(TURN_STEP, on_turn_step)
    session.hook(MUSIC_PLAY, on_music(0))
    session.hook(MUSIC_START, on_music(1))
    session.hook(BRIEF_ACTION_CALL, on_brief_call)
    session.hook(BRIEF_ACTION_RETURN, on_brief_return)
    session.hook(PLAY_SOUND_INTERNAL, on_play_sound)
    for addr, path in PLAY_SOUND_RETURNS.items():
        session.hook(addr, on_play_sound_return(path))
    session.hook(LOADER_WAIT, on_loader_wait)
    import os
    if os.environ.get("SC_UI_REGIONS"):
        def on_dirty():
            if m.r16(GAME_MODE) == 3:
                record({"dirty": list(struct.unpack("<4i", m.read(reg(X.UC_X86_REG_ECX), 16)))})

        def on_region():
            n = m.r32(REGION_COUNT)
            if n and m.r16(GAME_MODE) == 3:
                record({"region": [list(struct.unpack("<4i", m.read(REGION_LIST + 16 * k, 16))) for k in range(n)]})

        session.hook(ADD_DIRTY_RECT, on_dirty)
        session.hook(REGION_RECTS, on_region)
    session.hook(PRESENT_FRAME, on_present)
    session.hook(UPDATE_STATUS_UI, on_update_status)
    session.hook(OPEN_IDLE, on_open(0))
    session.hook(OPEN_TALKING, on_open(1))
    session.hook(PORTRAIT_STATIC, on_static)
    session.hook(SHOW_PORTRAIT, on_show_portrait)
    session.hook(REQUEST_TALK, on_request_talk)
    session.hook(SVID_BEGIN, on_svid_begin)
    session.hook(PORTRAIT_CONTINUED, on_portrait_continued)
    session.hook(LOAD_DIALOG, on_load_dialog)
    session.hook(LOAD_DIALOG_LOADED, on_dialog_loaded)
    session.hook(DISPATCH_UI_EVENT, on_dispatch)
    session.hook(DISPATCH_TICK, on_dispatch_tick)
    session.hook(COMMAND_CALL, on_command)
    session.hook(SET_UI_TIMER, on_set_timer)
    session.hook(SET_UI_TIMER_TICK, on_set_timer_tick)
    session.hook(DELETE_UI_TIMER, on_delete_timer)
    session.hook(DELETE_OWNER_TIMERS, on_delete_owner_timers)
    session.hook(RUN_UI_TIMERS, on_run_timers)
    session.hook(TIMER_FIRE, on_timer_fire)
    session.hook(TIMERS_DONE, on_timers_done)
    st["record"] = record
    return st
