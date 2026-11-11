/*
 * The test scenarios (docs/phase3.md, level C; in C since ADR 0019, stage C1): a campaign map's terrain with our own
 * players, units and triggers, without the map's triggers, briefing or doodad units.
 *
 * A spec (tests/scenarios/<name>.json, committed) names the base catalog mission ("terran/terran01"), the slot
 * controllers and the units; the inputs and the frame count are read by the emulator's driver. The scenario rewrites
 * the base CHK chunk by chunk: OWNR from the spec, UNIT from the spec, THG2 keeps only its sprites and gets the spec's
 * doodad units (doors and traps, which campaign maps place in THG2), MBRF becomes empty and TRIG holds the spec's
 * triggers (none without "triggers"); the spec's "locations" replace MRGN records and its trigger texts are appended to
 * STR; an action's "script" (Run AI Script's four-character id) fills its number field; "researched" marks techs
 * available and researched in PTEC and PTEx. The emulator substitutes the same bytes for the campaign map at load
 * (tools/research/sc_emulator/scenario.py, through converter/scenarios.py: the twin of this writer until stage C7), and
 * kp_convert scenarios writes them as generated/scenarios/<name>.chk, which the game reads as a campaign mission's
 * scenario, so both games start from the same map. Only the spec is committed; the CHK is rebuilt from the install.
 */
#ifndef KP_SCENARIO_H
#define KP_SCENARIO_H

#include <stddef.h>
#include <stdint.h>

#include "kp_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The scenario of a spec over its base CHK: malloc'd bytes and *size, or NULL with the reason (a spec that does not
 * hold: an unknown field, controller, state, condition or action, a unit related to itself, too many texts). */
uint8_t *kp_scenario_chk(const uint8_t *base, size_t base_size, const KpJson *spec, size_t *size, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
