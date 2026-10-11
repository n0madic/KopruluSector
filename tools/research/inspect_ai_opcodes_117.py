#!/usr/bin/env python3
"""Recover static opcode witnesses for the exact reviewed StarCraft 1.17 EXE.

Reads a PE as bytes; never loads or executes the retail program. The descriptions
are reviewed analytical annotations, not automatically inferred decompilation.
An opcode wrapper and its called gameplay helpers have separate evidence status.
"""

import argparse
import ast
import hashlib
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile


TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
SCHEMA_HASH = "b30ea0c1c2c4350d4b8de6c910eaf647d6c526a8a2efb64ba966e5c659a5125c"
START, LOOP, END, SWITCH = 0x493A60, 0x493A70, 0x494A98, 0x494A98
PLAYER_BASE, PLAYER_STRIDE = 0x67D068, 0x4E8

# Calls use the native register convention. Stack[] below lists logical arguments
# in their order at callee entry, excluding the return address. Widths are native
# byte/word reads; projected PyMS type names remain independently identifiable.
META = {}


def entry(name, operands, effect, calls=(), writes=(), unresolved=(), wait="unchanged"):
    META[name] = {
        "native_operands": operands,
        "pc_control": effect,
        "wait_effect": wait,
        "state_writes": list(writes),
        "reviewed_helper_arguments": list(calls),
        "unresolved_dependencies": list(unresolved),
        "wrapper_status": "reviewed_static",
        "gameplay_status": "partial_helpers" if unresolved else "static_local_effect",
    }


CONTINUE = "PC advances by opcode plus listed operand widths; continue dispatch in same invocation"

entry("goto", ["target:u16le"], "PC=target; continue", writes=["script+8=target"])
entry("wait", ["delay:u16le"], "PC advances 3 bytes; return", writes=["script+0xc=delay"], wait="u16 delay")
for name in ("start_town", "start_areatown"):
    creator = "0x487030 ECX=player EDX=center_x stack=[center_y, flags>>3]" if name == "start_town" else "0x486e80 ECX=player EDX=&area stack=[flags>>3]"
    entry(name, [], "Continue on nonzero new town; otherwise execute stop/free path", [creator, "0x482490 ECX=player", "0x494f80 ECX=player"], ["script+0x2c=creator result"], ["town selection/creation, player initialization, post-town processing"])
entry("build", ["count:u8", "unit:u16le", "priority:u8"], CONTINUE, ["0x487550 ECX=town EDX=count stack=[unit, priority, 0]"], unresolved=["town request insertion and production execution; see native-economy.md"])
entry("upgrade", ["level:u8", "upgrade:u16le", "priority:u8"], CONTINUE, ["0x4878b0 ECX=town EDX=level stack=[upgrade, priority]"], unresolved=["upgrade request insertion/execution; see native-economy.md"])
entry("tech", ["technology:u16le", "priority:u8"], CONTINUE, ["0x487890 ECX=town EDX=technology stack=[priority]"], unresolved=["technology request insertion/execution; see native-economy.md"])
for name, mode in (("wait_build", 1), ("wait_buildstart", 0)):
    entry(name, ["count:u8", "unit:u16le"], "If helper count >= adjusted count continue; otherwise PC-=4 and return", ["0x486070 ECX=player -> special unit", "0x487b70 ECX=town -> optional unit", "0x496690 ECX=optional unit EDX=player -> count cap", f"0x4872f0 ECX=town EDX=unit stack=[{mode}] -> observed count"], ["If encoded unit==special unit and optional unit exists, count=min(count, low byte of cap helper result)"], ["special-unit/cap/count helpers; their exact counting scopes"], "30 on failed comparison")
entry("attack_clear", [], CONTINUE, ["0x483770 ECX=player EDX=1; tail-calls 0x4ad230"], ["Zero 128-byte attack roster; grouping-region word=0; last-attack dword=0", "0x4ad230 changes qualifying region state 8 to 0, priority byte to10, clears flag0x20; may set priority40"], ["region-count helper0x481b00 and condition helper0x4831e0"])
entry("attack_add", ["count:u8", "unit:u16le"], CONTINUE, ["0x4839f0 ECX=player EDX=count stack=[unit]"], ["Require player<8, count<63, unit<228; append unit+1 words at first zero; stop at index63; zero count does nothing"])
for name in ("attack_prepare", "attack_do"):
    entry(name, [], "Continue unconditionally" if name == "attack_prepare" else "If helper !=0 continue; otherwise PC-=1 and return", ["0x4af300 ECX=player EDX=center_x stack=[center_y, 1, 0]"], unresolved=["attack preparation/grouping/helper result definition"], wait="unchanged" if name == "attack_prepare" else "30 on helper==0")
entry("capt_expand", [], CONTINUE, ["0x4ad310 ECX=player EDX=0"], unresolved=["regional expansion/capture algorithm"])
for category, build_va, use_va, build_off, use_off in (
    ("gg", 0x482670, 0x482E40, 0x2B0, 0x350),
    ("ag", 0x482610, 0x482DE0, 0x2D8, 0x378),
    ("ga", 0x4825B0, 0x482D80, 0x300, 0x3A0),
    ("aa", 0x482550, 0x482D20, 0x328, 0x3C8),
):
    for prefix, va, off in (("defensebuild", build_va, build_off), ("defenseuse", use_va, use_off)):
        entry(f"{prefix}_{category}", ["count:u8", "unit:u16le"], CONTINUE, [f"{va:#x} ECX=player EDX=count stack=[unit]"], [f"Require count<=10 and unit<228; append unit+1 at first zero in 20-word PlayerAi+{off:#x} array, truncating at capacity"], ["consumers deciding defense production, dispatch and region category semantics"])
entry("send_suicide", ["mode:u8"], CONTINUE, ["0x483770 ECX=player EDX=1", "0x4b11b0 ECX=player DL=mode", "0x483ad0 ECX=player EDX=5"], unresolved=["suicide assignment/attack-state helpers; regional side effects"])
for name, va in (("player_enemy", 0x48E170), ("player_ally", 0x48E160)):
    entry(name, [], CONTINUE, [f"{va:#x} ECX=player EDX=&area"], unresolved=["which players/units area query selects and diplomacy writes"])
for name, operands, va, call, write in (
    ("default_min", ["strength:u8"], 0x4837E0, "EDX=strength", "PlayerAi+0x222 byte=strength"),
    ("defaultbuild_off", [], 0x483990, "", "PlayerAi+0x218 word |=0x4"),
    ("max_force", ["strength:u16le"], 0x483290, "EDX=strength", "PlayerAi+0x21c word=strength"),
    ("nuke_rate", ["rate:u8"], 0x483370, "EDX=rate", "PlayerAi+0x212 byte=rate"),
    ("start_campaign", [], 0x482EE0, "", "PlayerAi+0x218 word |=0x20"),
    ("target_expansion", [], 0x483510, "", "PlayerAi+0x218 word |=0x800"),
    ("fake_nuke", [], 0x482F80, "", "PlayerAi+0x214 dword=global[0x5349d8]+1 mod2^32"),
):
    entry(name, operands, CONTINUE, [f"{va:#x} ECX=player {call}".strip()], [write], ["consumers of changed fields/flags"])
entry("stop", [], "Unlink current script from active list and prepend it to pool free list; return", ["0x493240 ECX=&pool/head globals0x6a2e6c stack=[script]"], ["Active head, next/prev links, pool+0x1450 free head; no reset of PC/wait/flags/town"])
entry("switch_rescue", [], CONTINUE, ["0x48dc50 ECX=player"], unresolved=["rescue player-state and unit-order effects"])
entry("move_dt", [], CONTINUE, ["0x48dcf0 ECX=player EDX=center_x stack=[center_y]"], unresolved=["Dark Templar selection and orders"])
entry("enter_bunker", [], CONTINUE, ["0x4af490 ECX=player EDX=&area"], unresolved=["bunker/unit capacity selection and enter orders"])
entry("value_area", [], CONTINUE, ["0x4ad9e0 ECX=player EDX=center_x stack=[center_y]"], unresolved=["region and strategic-target effects"])
entry("clear_combatdata", [], CONTINUE, ["0x4ad2a0 ECX=player EDX=&area"], unresolved=["regional data clearing and selected region set"])
entry("place_guard", ["unit:u16le", "position_selector:u8"], CONTINUE, ["0x4b27f0 ECX=player EDX=town stack=[unit, position_selector]", "0x487ac0 ECX=town EDX=selector stack=[&x,&y]", "On coordinates found:0x4b22a0 ECX=player EDX=unit stack=[x,y]"], ["selector0: signed16 town+0x20/+0x22; selector1: mineral pointer town+0x2c sprite position; other selector:0x485de0 category3, flag1, fallback category2, flag1, then returned unit sprite position"], ["category-unit selection0x485de0; guard allocation/replacement0x4b22a0"])
entry("call", ["target:u16le"], "Global return PC=PC after operand; PC=target; continue", writes=["global0x6a2e7c=following PC; script+8=target"])
entry("return", [], "PC=global0x6a2e7c; clear global; continue", writes=["script+8=shared return PC; global0x6a2e7c=0"])
entry("multirun", ["target:u16le"], CONTINUE, ["0x494d90 ECX=player EDX=target stack=[&area, flags&1]"], ["Success: insert at active head; child town=source town; failed allocation has no child"], ["full-slot-reuse effects; child eligibility with inherited flag bits"])
entry("killable", [], CONTINUE, writes=["script.flags |=2"])
entry("kill_thread", [], CONTINUE, writes=["Traverse entire active list, including other players and self; any flags&2 node receives flags|=4; no immediate unlink"])
entry("define_max", ["limit:u8", "unit:u16le"], CONTINUE, ["0x482c40 ECX=player EDX=limit stack=[unit]"], ["PlayerAi+0x3f0+unit byte=limit; helper has no unit-range check"], ["consumers of build limits; behavior with malformed out-of-range unit"])
entry("train", ["count:u8", "unit:u16le"], "If total-table count>=count continue; otherwise set training unit, PC-=4, return", ["0x4939e0 ECX=unit EDX=player stack=[0]", "0x482eb0 ECX=player EDX=unit"], ["Counting joins tank IDs5 and30; request helper writes PlayerAi+0x220 word=unit+1 modulo65536"], ["unit-counter maintenance and training request executor"], "30 on insufficient count")
entry("wait_train", ["count:u8", "unit:u16le"], "If completed-table count>=count continue; otherwise PC-=4, return", ["0x4939e0 ECX=unit EDX=player stack=[1]"], ["Counting joins tank IDs5 and30; no training request"], ["unit-counter maintenance and completed-table interpretation"], "30 on insufficient count")
for name, mode in (("sharedvision_on", 1), ("sharedvision_off", 0)):
    entry(name, ["other_player:u8"], CONTINUE, [f"0x4b0fe0 ECX=player DL=other_player stack=[{mode}]"], [f"global[0x5264cc+4*other_player] {'|=' if mode else '&=~'} (1<<player); native x86 shift masks index to5bits"], ["visibility recomputation and direction of UI-facing vision sharing"])
entry("harass_location", [], "Dispatch-table target is main loop itself: consume opcode byte; continue", writes=["No wrapper calls or writes beyond dispatcher PC+=1"])
entry("guard_all", [], CONTINUE, ["0x4256f0 ECX=player -> unit-list head", "0x4b00a0 ECX=player EDX=current unit for every node via unit+0x6c"], unresolved=["unit-list membership and guard conversion/eligibility"])
entry("enemyowns_jump", ["unit:u16le", "target:u16le"], "Traverse global list via unit+4; jump at first matching unit with hostile predicate!=0; otherwise continue", ["0x425600 -> global0x665ae0 unit-list head", "0x429110 ECX=player EDX=candidate -> alliance-table byte==0; owner11 uses sprite owner"], ["Match candidate+0x64 word against encoded unit; PC=target on first match; neither wrapper nor hostility helper tests fog/visibility"], ["global list membership/order, alliance-table maintenance"])
entry("enemyresources_jump", ["minerals:u16le", "gas:u16le", "target:u16le"], "Find FIRST global-list unit whose hostility predicate!=0; inspect only that owner's resources; jump if both signed comparisons >= thresholds; else continue", ["0x425600 -> global0x665ae0 unit-list head", "0x429110 ECX=player EDX=candidate -> alliance-table byte==0; owner11 uses sprite owner"], ["Resource owner is original byte at unit+0x4c even if hostility used sprite owner; resource dwords at0x5263d0+4*owner and0x526400+4*owner; PC=target iff both pass; no nearest-distance computation or fog test"], ["list ordering/membership, alliance/resource-global maintenance"])
entry("junkyard_dog", [], CONTINUE, ["0x4b0ec0 ECX=player EDX=0xbb stack=[packed16(left,top), packed16(right,bottom)]"], unresolved=["which units receive JunkYardDog order and filtering"])
for name, unit, energy_global, order in (("disruption_web", 60, 0x51F89A, 0xB5), ("recall_location", 71, 0x51F892, 0x89)):
    entry(name, [], "If first eligible unit exists, issue point order; continue regardless", [f"0x4932a0 ECX=player EDX={unit} stack=[u16[{energy_global:#x}]]", f"0x42a340 ECX=selected unit stack=[{order:#x}, center_x, center_y]"], ["Selector matches unit type, energy high byte>=threshold, order byte in{0x9c,0xa0,2}; scans player chain via+0x6c", "Target coordinate=left+trunc((right-left)/2), top+trunc((bottom-top)/2)"], ["energy-cost global initialization, player list ordering; order execution/research legality"])
entry("nuke_location", [], "If eligible Ghost and helper-selected linked missile exist, issue target-unit or point NukePaint order; continue", ["0x4932a0 ECX=player EDX=1 stack=[0]", "0x436f60 ECX=ghost stack=[1, ghost] -> missile", "0x432f60 ECX=ghost stack=[packed rectangle corners] -> target", "Target exists:0x42a380 ECX=ghost stack=[0x7e,target]; else0x42a340 ECX=ghost stack=[0x7e,rect midpoint x,rect midpoint y]"], ["missile+0xd0 points to object whose+0x80=ghost"], ["linked missile lookup, target selection, nuke/order state machines"])
entry("nuke_pos", ["x:u16le", "y:u16le"], "Same Ghost/missile eligibility as nuke_location; issue NukePaint point order to encoded x,y; continue", ["0x4932a0 ECX=player EDX=1 stack=[0]", "0x436f60 ECX=ghost stack=[1, ghost]", "0x42a340 ECX=ghost stack=[0x7e,x,y]"], ["missile+0xd0 object+0x80=ghost"], ["linked missile lookup, nuke/order state machines"])
entry("set_randomseed", ["seed:u32le"], CONTINUE, ["0x4781f0 ECX=seed"], ["global0x673934=seed; this is NOT raw RNG current state0x67393c"], ["seed-global transfer to current state; RNG initialization/save/replay lifecycle"])
entry("create_nuke", [], "Traverse player's unit chain; first unit id108 with dword+0xd4==0 invokes helper; continue", ["0x4256f0 ECX=player", "0x493450 ECX=eligible Nuclear Silo"], ["Helper creates unit14 at silo sprite position/owner, invokes initialization helpers, sets silo+0xd0=missile,+0xd4=1, missile+0x80=silo; tail-calls0x48d320"], ["allocation/initialization/tail helper; complete unit creation invariants"])
entry("create_unit", ["unit:u16le", "x:u16le", "y:u16le"], "Try creation; on success query idle order; if !=0x2c and helper result==1 invoke0x41d410; continue", ["0x4261f0 ECX=unit EDX=x stack=[y,player]", "0x434900 ECX=created unit -> order byte", "0x407f90 ECX=order EDX=owner stack=[unit]", "0x41d410 ECX=unit on predicate==1"], unresolved=["creation lifecycle, order availability predicate and postcreation order handler"])
entry("help_iftrouble", [], CONTINUE, ["0x4acca0 ECX=player"], unresolved=["regional help trigger/target/force assignment"])
entry("allies_watch", ["resource_area:u8", "target:u16le"], "If resource-area helper==0 continue; otherwise call script-spawn helper; continue", ["0x497360 ECX=resource_area", "0x494e80 ECX=script EDX=resource_area stack=[target]"], ["Spawn helper makes2x2 rectangle about area coordinates, inheritsBIN bit, sets flags highbits=area<<3, invokes regional helpers; child town initially0"], ["resource-area predicate, coordinate helpers and regional post-spawn calls"])
entry("try_townpoint", ["count:u8", "target:u16le"], "PC=target iff helper0x4872d0(player)==count exactly; otherwise continue", ["0x4872d0 ECX=player"], unresolved=["native town count/list interpretation"])

# Remaining dispatch ordinals are useful for melee and for validating the entire
# VM format even when absent from the conservative campaign-reachable corpus.
for name, jump_nonzero in (("notowns_jump", False), ("if_owned", True)):
    entry(name, ["unit:u16le", "target:u16le"], f"PC=target iff raw unit-table count {'!=0' if jump_nonzero else '==0'}; otherwise continue", ["0x4143c0 CX=unit DL=player"], ["Read dword0x529604+4*(unit*12+player); does not merge tank modes"])
entry("expand", ["limit:u8", "target:u16le"], "If native town-count helper >limit continue; otherwise invoke expansion spawning helper and continue", ["0x4872d0 ECX=player", "0x494f20 ECX=script EDX=target stack=[limit]"], unresolved=["town counting, expansion-site selection and child creation"])
for name, va in (("wait_secure", 0x482470), ("wait_upgrades", 0x483200), ("wait_bunkers", 0x4B05C0), ("wait_turrets", 0x4B0600)):
    entry(name, [], "If helper!=0 continue; otherwise PC-=1 and return", [f"{va:#x} ECX=player"], unresolved=["wait helper's exact completion predicate"], wait="30 on helper==0")
for name, va in (("build_bunkers", 0x4ADE10), ("build_turrets", 0x4ADED0)):
    entry(name, [], CONTINUE, [f"{va:#x} ECX=player"], unresolved=["defensive placement and construction"])
for category, va, build_off, use_off in (("gg", 0x482890, 0x2B0, 0x350), ("ag", 0x482850, 0x2D8, 0x378), ("ga", 0x482810, 0x300, 0x3A0), ("aa", 0x4827D0, 0x328, 0x3C8)):
    entry(f"defenseclear_{category}", [], CONTINUE, [f"{va:#x} ECX=player"], [f"Zero40bytes each at PlayerAi+{build_off:#x} and+{use_off:#x}; clears both build/use lists"])
for name, flag, mode, va in (("transports_off", 8, 0, 0x4835C0), ("check_transports", 8, 1, 0x4835C0), ("farms_notiming", 16, 0, 0x482FA0), ("farms_timing", 16, 1, 0x482FA0)):
    entry(name, [], CONTINUE, [f"{va:#x} ECX=player EDX={mode}"], [f"PlayerAi+0x218 word {'|=' if mode==0 else '&=~'} {flag:#x}"], ["consumers of these transport/supply flags"])
entry("default_build", [], CONTINUE, ["0x4837b0 ECX=player"], ["PlayerAi+0x218 word &=~0x4"], ["idle production consuming this flag"])
entry("random_jump", ["threshold:u8", "target:u16le"], "Call raw RNG(source0x34); PC=target iff (result&255)<=threshold; continue", ["0x478200 ECX=0x34"], ["Raw RNG call may update current state/counters; inclusive comparison"], ["RNG enable flag/lifecycle and consumption order"])
entry("time_jump", ["minutes:u8", "target:u16le"], "PC=target iff unsigned dword[0x5349d8]>=minutes*60; continue", writes=["No wait change; elapsed counter units documented separately"])
entry("harass_factor", ["divisor:u16le"], CONTINUE, ["0x483830 ECX=player EDX=divisor"], ["If target region exists, resolve enemy and sum max(unit+0x138,unit+0x13a) for nonexcluded enemy player units; duplicate existing attack roster by max(1,min(3,ceil(strength/divisor)-1)); truncate to63"], ["target-region/enemy resolver0x48e080, unit-exclusion predicate0x41fe40, strength-field maintenance, divisor0 behavior"])
entry("race_jump", ["terran_target:u16le", "zerg_target:u16le", "protoss_target:u16le"], "Town helper returns0:PC=zerg_target;1:PC=terran_target;2:PC=protoss_target;other:continue", ["0x486c50 ECX=town -> race"], unresolved=["town enemy/race selection"])
entry("region_size", ["size:u8", "target:u16le"], "threshold=size*32; PC=target iff unsigned helper result<threshold; otherwise continue", ["0x4877f0 ECX=town EDX=threshold"], unresolved=["connected/pathable region traversal"])
entry("get_oldpeons", ["count:u8"], CONTINUE, ["0x486480 ECX=town EDX=count"], unresolved=["worker reassignment source selection and transfer"])
entry("groundmap_jump", ["target:u16le"], "PC=target iff helper!=0; otherwise continue", ["0x486410 ECX=player EDX=center_x stack=[center_y]"], unresolved=["enemy selection and pathability predicate"])
for name in ("wait_force", "do_morph"):
    entry(name, ["count:u8", "unit:u16le"], "If completed-table count>=count continue; otherwise set training unit; do_morph continues, wait_force rewindsPC4 and returns", ["0x4939e0 ECX=unit EDX=player stack=[1]", "0x482eb0 ECX=player EDX=unit"], ["Tank modes5/30 are counted together; training word=unit+1"], ["counter maintenance/training executor"], wait="unchanged for do_morph;30 on insufficient wait_force")
entry("guard_resources", ["unit:u16le"], CONTINUE, ["0x493790 ECX=player EDX=unit"], unresolved=["resource-area iteration and guard creation"])
entry("eval_harass", ["target:u16le"], "PC=target iff helper==0; otherwise continue", ["0x4af5b0 ECX=player EDX=center_x stack=[center_y]"], unresolved=["attack/harass predicate"])
entry("creep", ["mode:u8"], CONTINUE, ["0x4828d0 ECX=player DL=mode"], unresolved=["creep/spread field changes and placement consumers"])
entry("panic", ["target:u16le"], CONTINUE, ["0x482c60 ECX=player EDX=target"], ["PlayerAi+0x21a word=target"], ["panic expansion trigger/thresholds"])
entry("player_need", ["count:u8", "unit:u16le"], CONTINUE, ["0x487550 ECX=town EDX=count stack=[unit,80,1]"], unresolved=["global/player-scoped request interpretation; see native-economy.md"])
entry("rush", ["kind:u8", "target:u16le"], "PC=target iff helper==0; otherwise continue", ["0x4ad550 ECX=player EDX=kind stack=[center_x,center_y]"], unresolved=["rush classification and opponent selection"])
entry("scout_with", ["unit:u16le"], "Read/discard word; continue", writes=["No operation beyond PC+=3"])
entry("set_attacks", ["count:u8"], CONTINUE, ["0x4834c0 ECX=player EDX=count"], ["PlayerAi+0x213 byte=count"], ["expansion-attack consumption"])
entry("set_gencmd", [], CONTINUE, ["0x483ab0 ECX=player EDX=&area"], unresolved=["generic-command target state"])
entry("make_patrol", [], CONTINUE, ["0x4b0f10 ECX=player EDX=&area"], unresolved=["unit filtering, generic target and patrol orders"])
entry("give_money", [], CONTINUE, ["0x482ff0 ECX=player"], ["For each signed resource dword(mineral0x5263d0/gas0x526400), if<500 add2000; otherwise leave unchanged"])
entry("prep_down", ["subtract:u8", "minimum:u8", "unit:u16le"], CONTINUE, ["0x4143c0 CX=unit DL=player, plus opposite tank mode if unit5/30", "0x4839f0 ECX=player EDX=max_signed(total_count-subtract, minimum) stack=[unit]"], ["Roster helper rejects count>=63; arithmetic wraps32bits before signed maximum"], ["unit-counter maintenance"])
entry("resources_jump", ["minerals:u16le", "gas:u16le", "target:u16le"], "PC=target iff both signed resource dwords>=encoded unsigned16 thresholds; otherwise continue", writes=["Read0x5263d0+4*player and0x526400+4*player"])
for name, va in (("enter_transport", 0x4AF520), ("exit_transport", 0x4AF6D0)):
    entry(name, [], CONTINUE, [f"{va:#x} ECX=player EDX=&area"], unresolved=["transport capacity/selection/unload position and orders"])
entry("implode", [], CONTINUE, ["0x4ad430 ECX=player"], unresolved=["region topology and state changes"])
entry("if_dif", ["comparison:u8", "threshold:u8", "target:u16le"], "comparison0 jumps if difficulty<threshold; any nonzero comparison jumps if difficulty>threshold; strict unsigned predicates", ["0x482fc0 ECX=player -> byte PlayerAi+0x22f"])
entry("easy_attack", ["count:u8", "unit:u16le"], "Append attack entries iff difficulty==0; otherwise continue unchanged", ["0x482fc0 ECX=player", "0x4839f0 ECX=player EDX=count stack=[unit]"], ["Uses ordinary attack_add bounds/representation"])
entry("wait_finishattack", [], "Continue if helper==0; else PC-=1 and return", ["0x4ab5d0 ECX=player"], unresolved=["which active attack-region states count"], wait="30 on helper!=0")
entry("quick_attack", [], CONTINUE, ["0x4af300 ECX=player EDX=center_x stack=[center_y,1,0]", "0x483ad0 ECX=player EDX=5"], unresolved=["attack preparation and timer-setting helper"])
entry("debug", ["target:u16le"], "Read target; pass text pointer at currentPC+AIScript base to helper; PC=target; continue", ["0x46a860 ECX=global0x6a2e74+PC EDX=player stack=[0]"], ["Text pointer always uses AIScript base even for a BWScript thread; helper consumes text, no explicit string PC increment"], ["debug printing/string reads and malformed string behavior"])
entry("fatal_error", [], CONTINUE, ["0x4cc190 stack=[literal0x4efa30]; caller adjustsESP4 if helper returns"], unresolved=["error helper termination behavior"])


WITNESSES = {
    "dispatch_and_all_handlers": (START, END),
    "byte_reader": (0x4932F0, 0x49330D),
    "word_reader": (0x493310, 0x493330),
    "script_free": (0x493240, 0x493291),
    "eligible_spell_unit": (0x4932A0, 0x4932ED),
    "guard_position": (0x487AC0, 0x487B64),
    "place_guard_helper": (0x4B27F0, 0x4B2828),
    "script_pool_initialize": (0x493330, 0x49344E),
    "create_nuke_helper": (0x493450, 0x4934BD),
    "script_pointer_restore": (0x4934C0, 0x493529),
    "script_pointer_encode": (0x493530, 0x49360E),
    "script_load_record": (0x493610, 0x493695),
    "script_save_record": (0x4936A0, 0x49373D),
    "bin_unload": (0x493740, 0x493787),
    "bin_load_and_pool_reset": (0x493800, 0x493859),
    "stop_town_scripts": (0x493860, 0x4938D4),
    "script_scheduler": (0x4938E0, 0x49396B),
    "script_launch_gate": (0x493970, 0x4939D7),
    "train_count": (0x4939E0, 0x493A5E),
    "bin_resolve_id": (0x494D20, 0x494D89),
    "script_create": (0x494D90, 0x494E76),
    "allies_watch_spawn": (0x494E80, 0x494F1F),
    "defensebuild_aa": (0x482550, 0x4825A8),
    "defensebuild_ga": (0x4825B0, 0x482608),
    "defensebuild_ag": (0x482610, 0x482668),
    "defensebuild_gg": (0x482670, 0x4826C8),
    "defenseclear_aa": (0x4827D0, 0x48280A),
    "defenseclear_ga": (0x482810, 0x48284A),
    "defenseclear_ag": (0x482850, 0x48288A),
    "defenseclear_gg": (0x482890, 0x4828CA),
    "define_max_and_panic": (0x482C40, 0x482C6E),
    "defenseuse_aa": (0x482D20, 0x482D78),
    "defenseuse_ga": (0x482D80, 0x482DD8),
    "defenseuse_ag": (0x482DE0, 0x482E38),
    "defenseuse_gg": (0x482E40, 0x482E98),
    "train_request_and_campaign_flag": (0x482EB0, 0x482EEF),
    "fake_nuke_and_difficulty": (0x482F80, 0x482FCE),
    "max_force": (0x483290, 0x48329E),
    "nuke_rate": (0x483370, 0x48337D),
    "set_attacks_and_target_expansion": (0x4834C0, 0x483523),
    "attack_clear_and_default_build": (0x483770, 0x4837C3),
    "default_min": (0x4837E0, 0x4837ED),
    "harass_factor": (0x483830, 0x48393E),
    "attack_grouping_timeout": (0x483940, 0x483983),
    "defaultbuild_off": (0x483990, 0x48399F),
    "attack_add": (0x4839F0, 0x483A5F),
    "attack_clear_region_effects": (0x4AD230, 0x4AD296),
    "sharedvision_bit_write": (0x4B0FE0, 0x4B100B),
    "unit_count_tables": (0x4143C0, 0x4143F4),
    "seed_getter_setter": (0x4781E0, 0x4781F7),
    "global_unit_head": (0x425600, 0x425606),
    "player_unit_head": (0x4256F0, 0x4256F8),
    "unit_hostility": (0x429110, 0x429130),
    "give_money": (0x482FF0, 0x483025),
}


def read_schema(data):
    tree = ast.parse(data)
    cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "AIBIN")
    labels = next(ast.literal_eval(n.value) for n in cls.body if isinstance(n, ast.Assign)
                  and any(isinstance(t, ast.Name) and t.id == "short_labels" for t in n.targets))
    init = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == "__init__")
    param_node = next(n.value for n in ast.walk(init) if isinstance(n, ast.Assign)
                      and any(isinstance(t, ast.Attribute) and t.attr == "parameters" for t in n.targets))
    params = []
    for element in param_node.elts:
        params.append([] if isinstance(element, ast.Constant) else [a.attr for a in element.elts])
    return labels, params


def isolated_probes(data, pe, output):
    """Execute only reviewed original blocks, with data fixtures and no stubs."""
    if platform.machine().lower() not in ("x86_64", "amd64"):
        raise RuntimeError("Use the reviewed x86_64 runtime; arm64 Unicorn mem_map crashed during environment validation")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

    scratch, stack, stop = 0x800000, 0x900000, 0xA00000
    script, program, bw_program, unit = scratch, scratch + 0x1000, scratch + 0x2000, scratch + 0x3000
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allowed = [(START, END), (0x4932F0, 0x493330), (0x493240, 0x493291),
               (0x493330, 0x49344E), (0x494D20, 0x494D89), (0x494D90, 0x494E76), (0x4938E0, 0x49396B),
               (0x4932A0, 0x4932ED), (0x4256F0, 0x4256F8),
               (0x4939E0, 0x493A5E), (0x4143C0, 0x4143F4),
               (0x4839F0, 0x483A5F), (0x482550, 0x4826C8),
               (0x482D20, 0x482E98), (0x4827D0, 0x4828CA),
               (0x482EB0, 0x482EBF), (0x4781F0, 0x4781F7),
               (0x478200, 0x478232), (0x425600, 0x425606),
               (0x429110, 0x429130), (0x482FF0, 0x483025)]
    cases = []

    def w32(uc, address, value):
        uc.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def r32(uc, address):
        return struct.unpack("<I", uc.mem_read(address, 4))[0]

    def w16(uc, address, value):
        uc.mem_write(address, struct.pack("<H", value))

    def new(code=b"", bw=b""):
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, size)
        uc.mem_write(base, image)
        uc.mem_map(scratch, 0x4000)
        uc.mem_map(stack, 0x1000)
        uc.mem_map(stop, 0x1000)
        w32(uc, 0x6A2E74, program)
        w32(uc, 0x6A2E78, bw_program)
        if code:
            uc.mem_write(program, code)
        if bw:
            uc.mem_write(bw_program, bw)
        return uc

    def call(uc, start, ecx=0, edx=0, args=()):
        reached, count, dispatch = False, 0, []

        def instruction(machine, address, ins_size, _):
            nonlocal reached, count
            if address == stop:
                reached = True
                machine.emu_stop()
                return
            if not any(lo <= address and address + ins_size <= hi for lo, hi in allowed):
                raise RuntimeError(f"Original instruction outside reviewed allowlist: {address:#x}")
            count += 1
            if address == LOOP:
                # ESI is the dispatcher's native script pointer.
                from unicorn.x86_const import UC_X86_REG_ESI
                active = machine.reg_read(UC_X86_REG_ESI)
                dispatch.append([active, r32(machine, active + 8)])

        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        sp = stack + 0x800
        uc.mem_write(sp, struct.pack("<" + "I" * (len(args) + 1), stop, *args))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        uc.emu_start(start, stop + 1, count=20000)
        uc.hook_del(hook)
        if not reached:
            raise RuntimeError("Probe did not return within the instruction bound")
        return uc.reg_read(UC_X86_REG_EAX), count, dispatch

    def observed(kind, inputs, values, expected, count, dispatch=()):
        if values != expected:
            raise AssertionError((kind, inputs, values, expected))
        cases.append({"kind": kind, "fixture_inputs": inputs, "observed": values,
                      "expected_from_static_contract": expected, "original_instructions": count,
                      "dispatch_script_and_pc": list(dispatch), "helper_stubs": []})

    for player, count_arg, unit_arg, occupied in [(0, 0, 0, 0), (0, 1, 0, 0), (0, 62, 227, 0),
                                              (0, 63, 0, 0), (0, 255, 0, 0), (8, 1, 0, 0),
                                              (7, 1, 0, 0), (0, 1, 228, 0), (0, 5, 0, 61),
                                              (0, 1, 0, 63)]:
        uc = new()
        roster = PLAYER_BASE + player * PLAYER_STRIDE + 0x230
        initial = [7] * occupied + [0] * (64 - occupied)
        uc.mem_write(roster, struct.pack("<64H", *initial))
        _, count, _ = call(uc, 0x4839F0, player, count_arg, [unit_arg])
        result = list(struct.unpack("<64H", uc.mem_read(roster, 128)))
        expected = initial[:]
        if player < 8 and count_arg < 63 and unit_arg < 228:
            for j in range(min(count_arg, 63 - occupied)):
                expected[occupied + j] = unit_arg + 1
        observed("attack_roster", [player, count_arg, unit_arg, occupied], result, expected, count)

    for va, off in [(0x482670, 0x2B0), (0x482610, 0x2D8), (0x4825B0, 0x300), (0x482550, 0x328),
                    (0x482E40, 0x350), (0x482DE0, 0x378), (0x482D80, 0x3A0), (0x482D20, 0x3C8)]:
        for count_arg, unit_arg, occupied in [(0, 0, 0), (10, 227, 0), (11, 0, 0), (1, 228, 0), (10, 0, 19), (1, 0, 20)]:
            uc = new()
            array = PLAYER_BASE + off
            initial = [7] * occupied + [0] * (20 - occupied)
            uc.mem_write(array, struct.pack("<20H", *initial))
            _, count, _ = call(uc, va, 0, count_arg, [unit_arg])
            result = list(struct.unpack("<20H", uc.mem_read(array, 40)))
            expected = initial[:]
            if count_arg <= 10 and unit_arg < 228:
                for j in range(min(count_arg, 20 - occupied)):
                    expected[occupied + j] = unit_arg + 1
            observed("defense_roster", [hex(va), count_arg, unit_arg, occupied], result, expected, count)

    for all5, all30, complete5, complete30 in [(1, 2, 0, 0), (0, 0, 1, 2), (2, 2, 2, 1)]:
        for opcode in (0x4C, 0x3E, 0x46, 0x4E):
            uc = new(bytes([opcode, 3, 5, 0, 2, 7, 0]))
            for table, a, b in [(0x529604, all5, all30), (0x52C0C4, complete5, complete30)]:
                w32(uc, table + 5 * 12 * 4, a)
                w32(uc, table + 30 * 12 * 4, b)
            _, count, dispatch = call(uc, START, script)
            sufficient = (all5 + all30 if opcode == 0x4C else complete5 + complete30) >= 3
            request = not sufficient and opcode != 0x4E
            retry = not sufficient and opcode != 0x46
            values = [r32(uc, script + 8), r32(uc, script + 12), struct.unpack("<H", uc.mem_read(PLAYER_BASE + 0x220, 2))[0]]
            expected = [0 if retry else 7, 30 if retry else 7, 6 if request else 0]
            observed("training_opcode", [opcode, all5, all30, complete5, complete30], values, expected, count, dispatch)

    uc = new(b"\x69\x78\x56\x34\x12\x02\x07\0")
    w32(uc, 0x67393C, 99)
    _, count, dispatch = call(uc, START, script)
    observed("seed_opcode", [], [r32(uc, 0x673934), r32(uc, 0x67393C), r32(uc, script + 8)], [0x12345678, 99, 8], count, dispatch)

    code = bytearray(32)
    code[0:7] = b"\x40\x0a\0\x5a\x02\x07\0"
    code[10:14] = b"\x02\0\0\x41"
    other = bytearray(32)
    other[0:7] = b"\x5a\x40\x14\0\x02\x0b\0"
    other[20:24] = b"\x02\0\0\x41"
    uc = new(bytes(code), bytes(other))
    bscript = script + 0x100
    w32(uc, bscript + 0x30, 1)
    _, n1, _ = call(uc, START, script)
    a_saved = r32(uc, 0x6A2E7C)
    _, n2, _ = call(uc, START, bscript)
    b_saved = r32(uc, 0x6A2E7C)
    _, n3, dispatch = call(uc, START, script)
    observed("cross_bin_shared_return", [], [a_saved, b_saved, r32(uc, 0x6A2E7C), r32(uc, script + 8), dispatch[-1][1]], [3, 4, 0, 7, 4], n1 + n2 + n3, dispatch)

    for order in (0, 1):
        code = bytearray(20)
        code[0:10] = b"\x5e\x64\0\x32\0\x0c\0\x02\x01\0"
        code[12:15] = b"\x02\x02\0"
        uc = new(bytes(code))
        for index, owner in enumerate((1, 2) if order == 0 else (2, 1)):
            ptr = unit + index * 0x200
            uc.mem_write(ptr + 0x4C, bytes([owner]))
            w32(uc, ptr + 4, ptr + 0x200 if index == 0 else 0)
        w32(uc, 0x665AE0, unit)
        w32(uc, 0x5263D0 + 4, 0)
        w32(uc, 0x526400 + 4, 0)
        w32(uc, 0x5263D0 + 8, 500)
        w32(uc, 0x526400 + 8, 500)
        _, count, dispatch = call(uc, START, script)
        observed("enemy_resource_first_owner", [order], [r32(uc, script + 8), r32(uc, script + 12)], [10, 1] if order == 0 else [15, 2], count, dispatch)

    for candidate_type, energy, order in [(60, 49 * 256 + 255, 2), (60, 50 * 256, 2),
                                         (60, 51 * 256, 0x9C), (60, 50 * 256, 0xA0),
                                         (60, 50 * 256, 3), (60, 50 * 256, 0x9D),
                                         (71, 50 * 256, 2)]:
        uc = new()
        w32(uc, 0x665B08, unit)
        w16(uc, unit + 0x64, candidate_type)
        w16(uc, unit + 0xA2, energy)
        uc.mem_write(unit + 0x4D, bytes([order]))
        result, count, _ = call(uc, 0x4932A0, 0, 60, [50])
        expected = unit if candidate_type == 60 and energy >> 8 >= 50 and order in (2, 0x9C, 0xA0) else 0
        observed("spell_unit_eligibility", [candidate_type, energy, order, 50], result, expected, count)
    uc = new()
    w32(uc, 0x665B08, unit)
    for index, order in enumerate((3, 0xA0, 2)):
        ptr = unit + index * 0x200
        w16(uc, ptr + 0x64, 60)
        w16(uc, ptr + 0xA2, (80 - 10 * index) * 256)
        uc.mem_write(ptr + 0x4D, bytes([order]))
        w32(uc, ptr + 0x6C, ptr + 0x200 if index < 2 else 0)
    result, count, _ = call(uc, 0x4932A0, 0, 60, [50])
    observed("spell_unit_first_eligible", [], result, unit + 0x200, count)

    for ai_offset, ai_id, bw_id, bw_offset, requested, expected in [
        (200, b"One1", b"One1", 300, b"One1", [200, 0]),
        (0, b"One1", b"One1", 300, b"One1", [300, 1]),
        (200, b"One1", b"Two2", 220, b"Two2", [220, 1]),
        (200, b"One1", b"Two2", 220, b"Bad3", [0, 0]),
    ]:
        uc = new()
        w32(uc, program, 64)
        w32(uc, bw_program, 64)
        uc.mem_write(program + 64, struct.pack("<4sIII", ai_id, ai_offset, 7, 0xFFFFFFFF))
        uc.mem_write(bw_program + 64, struct.pack("<4sI", bw_id, bw_offset))
        result, count, _ = call(uc, 0x494D20, int.from_bytes(requested, "little"), scratch + 0x600)
        observed("bin_id_resolution", [ai_offset, ai_id.decode(), bw_id.decode(), bw_offset, requested.decode()], [result, r32(uc, scratch + 0x600)], expected, count)

    # Execute native pool initialization/creation/scheduler, without allocation stubs.
    code = bytearray(20)
    code[0:7] = b"\x48\x0a\0\x02\0\0\x24"
    code[10:13] = b"\x02\x07\0"
    uc = new(bytes(code))
    _, n1, _ = call(uc, 0x493330, 0x6A1A18)
    w32(uc, 0x6A2E6C, 0x6A1A18)
    w32(uc, 0x6A2E70, 0)
    parent, n2, _ = call(uc, 0x494D90, 0, 0, [0, 0])
    _, n3, dispatch = call(uc, 0x4938E0)
    child = r32(uc, 0x6A2E70)
    first = [r32(uc, child + 8), r32(uc, child + 12), r32(uc, child)]
    _, n4, dispatch2 = call(uc, 0x4938E0)
    values = first + [r32(uc, child + 8), r32(uc, child + 12), r32(uc, 0x6A2E70)]
    observed("multirun_scheduler", [], values, [10, 0, parent, 13, 7, child], n1 + n2 + n3 + n4, dispatch + dispatch2)

    uc = new()
    _, n1, _ = call(uc, 0x493330, 0x6A1A18)
    w32(uc, 0x6A2E6C, 0x6A1A18)
    first, n2, _ = call(uc, 0x494D90, 0, 0, [0, 0])
    w32(uc, first + 0x30, 6)
    _, n3, _ = call(uc, 0x493240, 0x6A2E6C, 0, [first])
    reused, n4, _ = call(uc, 0x494D90, 1, 10, [0, 1])
    flags = r32(uc, reused + 0x30)
    _, n5, dispatch = call(uc, 0x4938E0)
    observed("flagged_slot_reuse", [], [first == reused, flags, r32(uc, 0x6A2E70), len(dispatch)], [True, 7, 0, 0], n1 + n2 + n3 + n4 + n5, dispatch)

    uc = new(b"\x61\x02\x64\0")
    victim, unmarked = script + 0x100, script + 0x200
    w32(uc, 0x6A2E6C, 0x6A1A18)
    w32(uc, 0x6A2E70, script)
    w32(uc, script, victim)
    w32(uc, victim + 4, script)
    w32(uc, victim, unmarked)
    w32(uc, unmarked + 4, victim)
    w32(uc, victim + 0x10, 5)
    w32(uc, victim + 0x30, 2)
    w32(uc, victim + 12, 2)
    _, n1, _ = call(uc, START, script)
    values = [r32(uc, victim + 0x30)]
    # Give unmarked fixture a long wait so it never dispatches fixture bytecode.
    w32(uc, unmarked + 12, 100)
    for _ in range(3):
        _, n, _ = call(uc, 0x4938E0)
        n1 += n
        values.append(r32(uc, script) == victim)
    observed("delayed_cross_player_kill", [], values, [6, True, True, False], n1)

    result = {"target_sha256": TARGET_HASH, "target_version": "1.17.0.1",
              "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "original_code_executed": True, "complete_game_executed": False,
              "engine": "Unicorn x86-32; private in-memory PE", "unicorn_version": unicorn.__version__,
              "python_machine": platform.machine(), "host_system": platform.system(),
              "runtime_versions": {"python": platform.python_version(), "unicorn": unicorn.__version__,
                                   "capstone": capstone.__version__, "pefile": pefile.__version__},
              "allowlist": [[hex(a), hex(b)] for a, b in allowed],
              "limits": "No WinMain/Windows API/host file/network execution; only allowlisted instructions; data/list/unit fixtures; no helper stubs; no complete mission or frame trace",
              "checks": len(cases), "all_expected_results_matched": True, "cases": cases}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n")
    return {"isolated_original_checks": len(cases), "output": str(output)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("--inventory", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--execute-isolated-original-code", action="store_true",
                        help="Explicitly execute only reviewed original blocks in private Unicorn memory")
    parser.add_argument("--probe-out", type=Path)
    args = parser.parse_args()
    if args.execute_isolated_original_code and args.probe_out is None:
        parser.error("--probe-out required for explicit isolated execution")
    data, schema, inventory_bytes = args.exe.read_bytes(), args.schema.read_bytes(), args.inventory.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown EXE hash: the reviewed addresses do not apply")
    if hashlib.sha256(schema).hexdigest() != SCHEMA_HASH:
        parser.error("Unknown PyMS schema hash")
    inventory = json.loads(inventory_bytes)
    if inventory["schema_sha256"] != SCHEMA_HASH:
        parser.error("Campaign inventory uses a different schema")
    if inventory["files"]["aiscript.bin"]["sha256"] != "635433e546998e05529a9d6acc806c06b528c7ce5075a206f6f2c8de44ffed6e":
        parser.error("Unknown AIScript corpus")
    if inventory["files"]["bwscript.bin"]["sha256"] != "15a1a5b3fb4f2cab2e7b7b507ae04d4e2ef097c77081cfeb950d9a2e4ebee382":
        parser.error("Unknown BWScript corpus")
    campaign = set()
    for root in inventory["campaign_scopes"]["catalog"]["roots"].values():
        campaign.update(root["opcode_types"])
    if campaign - set(META):
        parser.error(f"Campaign metadata missing: {campaign-set(META)}")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    if base != 0x400000:
        parser.error("Unexpected ImageBase")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True

    def insns(lo, hi):
        offset = pe.get_offset_from_rva(lo - base)
        return list(md.disasm(data[offset:offset + hi - lo], lo))

    def record(i):
        return {"va": hex(i.address), "rva": hex(i.address - base),
                "file_offset": hex(pe.get_offset_from_rva(i.address - base)),
                "bytes": i.bytes.hex(), "mnemonic": i.mnemonic, "operands": i.op_str}

    body = {i.address: i for i in insns(START, END)}

    def wrapper_cfg(va):
        # Do not follow calls. Conditional edges form a static upper bound: shared
        # handlers can contain alternatives selected by the known opcode ordinal.
        pending, seen, outside = [va], set(), set()
        while pending:
            addr = pending.pop()
            if addr == LOOP or addr in seen:
                continue
            if addr not in body:
                outside.add(addr)
                continue
            seen.add(addr)
            i = body[addr]
            if i.group(capstone.CS_GRP_RET):
                continue
            next_addr = i.address + i.size
            if i.group(capstone.CS_GRP_JUMP):
                if i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
                    pending.append(i.operands[0].imm)
                if i.mnemonic != "jmp":
                    pending.append(next_addr)
            else:
                pending.append(next_addr)
        return [record(body[a]) for a in sorted(seen)], sorted(outside)

    labels, params = read_schema(schema)
    if set(labels[:113]) != set(META):
        parser.error("Reviewed metadata does not cover exactly the 113 native ordinals")
    offset = pe.get_offset_from_rva(SWITCH - base)
    targets = struct.unpack_from("<113I", data, offset)
    entries = []
    for opcode, va in enumerate(targets):
        instructions, outside = wrapper_cfg(va)
        item = dict(META[labels[opcode]])
        item.update({"opcode": opcode, "opcode_hex": hex(opcode), "name_from_pinned_schema": labels[opcode],
                     "campaign_reachable_upper_bound": labels[opcode] in campaign, "entry_va": hex(va),
                     "pyms_parameter_types": params[opcode], "instructions": instructions,
                     "outside_cfg_edges": [hex(x) for x in outside],
                     "cfg_scope": "Within dispatcher, stops at next dispatch/return; calls not followed; shared-handler conditional branches conservatively retained"})
        item["native_operand_bytes"] = sum(int(s.rsplit(":u", 1)[1].replace("le", "")) // 8 for s in item["native_operands"])
        widths = {"ai_byte": 1, "ai_compare": 1, "ai_dword": 4, "ai_string": 0}
        schema_bytes = sum(widths.get(p, 2) for p in params[opcode])
        if item["native_operand_bytes"] != schema_bytes:
            raise RuntimeError(f"Operand width disagreement for {labels[opcode]}: native={item['native_operand_bytes']}, schema={schema_bytes}")
        item["scalar_operand_width_schema_check"] = True
        if "ai_string" in params[opcode]:
            item["inline_text_limit"] = "debug passes a text pointer then assigns target PC; zero is scalar width only, not an assertion that no text bytes exist"
        item["direct_calls_in_cfg"] = sorted({i["operands"] for i in instructions if i["mnemonic"] == "call"})
        entries.append(item)
    witnesses = {name: [record(i) for i in insns(lo, hi)] for name, (lo, hi) in WITNESSES.items()}
    xrefs = []
    md.skipdata = True
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        for i in md.disasm(section.get_data(), base + section.VirtualAddress):
            if not i.id:
                continue
            for operand in i.operands:
                if operand.type == capstone.x86.X86_OP_MEM and operand.mem.disp in (0x6A2E74, 0x6A2E78, 0x6A2E7C, 0x673934):
                    xrefs.append(dict(record(i), referenced_va=hex(operand.mem.disp)))
    strings = {}
    for va in (0x4EFA5C, 0x4EFA74):
        off = pe.get_offset_from_rva(va - base)
        strings[hex(va)] = data[off:off + 64].split(b"\0", 1)[0].decode("ascii")
    result = {
        "target_version": "1.17.0.1", "target_sha256": TARGET_HASH, "image_base": hex(base),
        "inspector_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "evidence_level": "original-static; no original execution or runtime trace",
        "annotation_policy": "Manual reviewed native effects; PyMS names projected; unresolved callees explicit; no full-gameplay claim for wrappers",
        "schema_sha256": SCHEMA_HASH, "schema_commit": "bfc5d3aad0b5614a5aff72c223f8efa00afddfa4",
        "inventory_sha256": hashlib.sha256(inventory_bytes).hexdigest(),
        "campaign_cfg_scope": inventory["scope"], "opcode_limit": 0x70,
        "dispatch_entries": [{"opcode": i, "name": labels[i], "va": hex(v)} for i, v in enumerate(targets)],
        "campaign_wrapper_count": sum(e["campaign_reachable_upper_bound"] for e in entries),
        "reviewed_wrapper_count": len(entries), "opcodes": entries,
        "witnesses": witnesses, "loader_strings": strings,
        "direct_absolute_xrefs": xrefs,
        "xref_limit": "Linear disassembly of executable sections with skipdata; indirect/address-built references and instructions hidden by data-induced alignment are not excluded",
        "player_ai_layout": {"base": hex(PLAYER_BASE), "stride": hex(PLAYER_STRIDE)},
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"reviewed_wrappers": len(entries), "campaign_wrappers": sum(e["campaign_reachable_upper_bound"] for e in entries), "dispatch_entries": len(targets),
                      "xrefs": len(xrefs), "output": str(args.out)}))
    if args.execute_isolated_original_code:
        print(json.dumps(isolated_probes(data, pe, args.probe_out)))


if __name__ == "__main__":
    main()
