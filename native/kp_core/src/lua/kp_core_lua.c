/*
 * The Lua binding of kp_core (ADR 0008): the library's foundations, with the APIs of their
 * Lua counterparts where one exists, so the suites check them on the same vectors:
 *   kp_core.int        game/simulation/int.lua
 *   kp_core.rng        the generators of kp_rng.h (game, crt, portrait)
 *   kp_core.kpf_parse  game/data/kpf.lua's parse, as a plain table
 *   kp_core.dat        game/data/dat.lua's parse
 *   kp_core.mission    game/data/mission.lua's parse (the packed sections as strings)
 *   kp_core.grpmeta    game/data/grpmeta.lua's parse (without the opacity runs' accessors)
 *   kp_core.iscript    game/data/scripts.lua's parse_iscript
 *   kp_core.images     the C images world (stage N.1; kp_images_lua.c)
 *   kp_core.game       the native games (kp_game_lua.c)
 */
#include "kp_core_lua.h"

#include <string.h>

#include "../core/kp_dat.h"
#include "../core/kp_grpmeta.h"
#include "../core/kp_int.h"
#include "../core/kp_iscript.h"
#include "../core/kp_kpf.h"
#include "../core/kp_mission.h"
#include "../core/kp_rng.h"

#define TWO32 4294967296.0

static int raise_error(lua_State *L, const char *msg)
{
	lua_pushstring(L, msg);
	return lua_error(L);
}

/* A Lua number (any integral value) to its low 32 bits, as `x % 2^32`. */
static uint32_t arg_u32(lua_State *L, int i)
{
	lua_Number v = luaL_checknumber(L, i);
	lua_Number m = v - TWO32 * (lua_Number)(int64_t)(v / TWO32);
	if (m < 0) m += TWO32;
	if (m >= TWO32) m -= TWO32;
	return (uint32_t)m;
}

static void push_u(lua_State *L, uint32_t v) { lua_pushnumber(L, (lua_Number)v); }
static void push_s(lua_State *L, int32_t v) { lua_pushnumber(L, (lua_Number)v); }

static void set_number(lua_State *L, const char *key, lua_Number v)
{
	lua_pushnumber(L, v);
	lua_setfield(L, -2, key);
}

static void set_lstring(lua_State *L, const char *key, const void *p, size_t n)
{
	lua_pushlstring(L, (const char *)p, n);
	lua_setfield(L, -2, key);
}

/* --- kp_core.int --- */

static int int_u8(lua_State *L) { push_u(L, arg_u32(L, 1) & 0xffu); return 1; }
static int int_u16(lua_State *L) { push_u(L, arg_u32(L, 1) & 0xffffu); return 1; }
static int int_u32(lua_State *L) { push_u(L, arg_u32(L, 1)); return 1; }
static int int_s8(lua_State *L) { push_s(L, kp_s8(arg_u32(L, 1))); return 1; }
static int int_s16(lua_State *L) { push_s(L, kp_s16(arg_u32(L, 1))); return 1; }
static int int_s32(lua_State *L) { push_s(L, kp_s32(arg_u32(L, 1))); return 1; }
static int int_add(lua_State *L) { push_u(L, arg_u32(L, 1) + arg_u32(L, 2)); return 1; }
static int int_sub(lua_State *L) { push_u(L, arg_u32(L, 1) - arg_u32(L, 2)); return 1; }
static int int_neg(lua_State *L) { push_u(L, 0u - arg_u32(L, 1)); return 1; }
static int int_mul(lua_State *L) { push_u(L, kp_mul(arg_u32(L, 1), arg_u32(L, 2))); return 1; }
static int int_mulhi(lua_State *L) { push_u(L, kp_mulhi(arg_u32(L, 1), arg_u32(L, 2))); return 1; }
static int int_shl(lua_State *L) { push_u(L, kp_shl(arg_u32(L, 1), arg_u32(L, 2))); return 1; }
static int int_shr(lua_State *L) { push_u(L, kp_shr(arg_u32(L, 1), arg_u32(L, 2))); return 1; }
static int int_sar(lua_State *L) { push_s(L, kp_sar(kp_s32(arg_u32(L, 1)), arg_u32(L, 2))); return 1; }
static int int_bnot(lua_State *L) { push_u(L, ~arg_u32(L, 1)); return 1; }

static int signed_division(lua_State *L, int remainder)
{
	int32_t a = kp_s32(arg_u32(L, 1)), b = kp_s32(arg_u32(L, 2));
	if (b == 0) return raise_error(L, "integer division by zero");
	if (kp_idiv_faults(a, b)) return raise_error(L, "integer overflow in division");
	push_s(L, remainder ? kp_irem(a, b) : kp_idiv(a, b));
	return 1;
}
static int int_idiv(lua_State *L) { return signed_division(L, 0); }
static int int_irem(lua_State *L) { return signed_division(L, 1); }

static int unsigned_division(lua_State *L, int remainder)
{
	uint32_t a = arg_u32(L, 1), b = arg_u32(L, 2);
	if (b == 0) return raise_error(L, "integer division by zero");
	push_u(L, remainder ? a % b : a / b);
	return 1;
}
static int int_udiv(lua_State *L) { return unsigned_division(L, 0); }
static int int_urem(lua_State *L) { return unsigned_division(L, 1); }

/* Bitwise operations over any number of operands. */
static int bitwise(lua_State *L, int op, int raw)
{
	int i, n = lua_gettop(L);
	uint32_t v = arg_u32(L, 1);
	for (i = 2; i <= n; i++) {
		uint32_t w = arg_u32(L, i);
		v = op == 0 ? v & w : op == 1 ? v | w : v ^ w;
	}
	if (raw) push_s(L, kp_s32(v)); /* the bit library's signed result */
	else push_u(L, v);
	return 1;
}
static int int_band(lua_State *L) { return bitwise(L, 0, 0); }
static int int_bor(lua_State *L) { return bitwise(L, 1, 0); }
static int int_bxor(lua_State *L) { return bitwise(L, 2, 0); }
static int int_bit_band(lua_State *L) { return bitwise(L, 0, 1); }

static const luaL_Reg INT_FUNCTIONS[] = {
	{ "u8", int_u8 }, { "u16", int_u16 }, { "u32", int_u32 }, { "s8", int_s8 }, { "s16", int_s16 },
	{ "s32", int_s32 }, { "add", int_add }, { "sub", int_sub }, { "neg", int_neg }, { "mul", int_mul },
	{ "mulhi", int_mulhi }, { "idiv", int_idiv }, { "irem", int_irem }, { "udiv", int_udiv },
	{ "urem", int_urem }, { "shl", int_shl }, { "shr", int_shr }, { "sar", int_sar }, { "band", int_band },
	{ "bor", int_bor }, { "bxor", int_bxor }, { "bnot", int_bnot }, { "bit_band", int_bit_band },
	{ NULL, NULL },
};

/* --- kp_core.rng: userdata over the generators of kp_rng.h --- */

#define RNG_GAME KP_LUA_RNG_GAME
#define RNG_CRT KP_LUA_RNG_CRT
#define RNG_PORTRAIT "kp_core.rng.portrait"

static void *new_udata(lua_State *L, size_t size, const char *meta)
{
	void *p = lua_newuserdata(L, size);
	luaL_getmetatable(L, meta);
	lua_setmetatable(L, -2);
	return p;
}

static void push_list(lua_State *L, const uint32_t *v, int n, int base)
{
	int i;
	lua_createtable(L, n, 0);
	for (i = 0; i < n; i++) {
		push_u(L, v[i]);
		lua_rawseti(L, -2, i + base);
	}
}

static uint32_t list_item(lua_State *L, int t, int i)
{
	uint32_t v;
	lua_rawgeti(L, t, i);
	v = arg_u32(L, -1);
	lua_pop(L, 1);
	return v;
}

static int rng_game_new(lua_State *L)
{
	uint32_t seed = lua_isnoneornil(L, 1) ? 0 : arg_u32(L, 1);
	kp_rng_game_init((KpRngGame *)new_udata(L, sizeof(KpRngGame), RNG_GAME), seed);
	return 1;
}
static KpRngGame *check_game(lua_State *L) { return (KpRngGame *)luaL_checkudata(L, 1, RNG_GAME); }
static int rng_game_set_enabled(lua_State *L)
{
	KpRngGame *r = check_game(L);
	push_u(L, kp_rng_game_set_enabled(r, arg_u32(L, 2)));
	return 1;
}
static int rng_game_seed(lua_State *L)
{
	kp_rng_game_seed(check_game(L), arg_u32(L, 2));
	return 0;
}
static int rng_game_raw(lua_State *L)
{
	KpRngGame *r = check_game(L);
	push_u(L, kp_rng_game_raw(r, (uint8_t)arg_u32(L, 2)));
	return 1;
}
static int rng_game_range(lua_State *L)
{
	KpRngGame *r = check_game(L);
	push_u(L, kp_rng_game_range(r, (uint8_t)arg_u32(L, 2), arg_u32(L, 3), arg_u32(L, 4)));
	return 1;
}
static int rng_game_snapshot(lua_State *L)
{
	KpRngGame *r = check_game(L);
	lua_createtable(L, 5, 0);
	push_u(L, r->initial_seed);
	lua_rawseti(L, -2, 1);
	push_u(L, r->state);
	lua_rawseti(L, -2, 2);
	push_u(L, r->enabled);
	lua_rawseti(L, -2, 3);
	push_u(L, r->total);
	lua_rawseti(L, -2, 4);
	push_list(L, r->counters, KP_RNG_SOURCES, 1);
	lua_rawseti(L, -2, 5);
	return 1;
}
static int rng_game_restore(lua_State *L)
{
	KpRngGame *r = check_game(L);
	int i;
	luaL_checktype(L, 2, LUA_TTABLE);
	r->initial_seed = list_item(L, 2, 1);
	r->state = list_item(L, 2, 2);
	r->enabled = list_item(L, 2, 3);
	r->total = list_item(L, 2, 4);
	lua_rawgeti(L, 2, 5);
	for (i = 0; i < KP_RNG_SOURCES; i++) r->counters[i] = list_item(L, lua_gettop(L), i + 1);
	lua_pop(L, 1);
	return 0;
}
static const luaL_Reg RNG_GAME_METHODS[] = {
	{ "set_enabled", rng_game_set_enabled }, { "seed", rng_game_seed }, { "raw", rng_game_raw },
	{ "range", rng_game_range }, { "snapshot", rng_game_snapshot }, { "restore", rng_game_restore },
	{ NULL, NULL },
};
static int rng_game_index(lua_State *L)
{
	KpRngGame *r = check_game(L);
	const char *key = luaL_checkstring(L, 2);
	if (strcmp(key, "state") == 0) push_u(L, r->state);
	else if (strcmp(key, "total") == 0) push_u(L, r->total);
	else if (strcmp(key, "enabled") == 0) push_u(L, r->enabled);
	else if (strcmp(key, "initial_seed") == 0) push_u(L, r->initial_seed);
	else if (strcmp(key, "counters") == 0) push_list(L, r->counters, KP_RNG_SOURCES, 0);
	else {
		lua_getfield(L, lua_upvalueindex(1), key);
	}
	return 1;
}

static int rng_crt_new(lua_State *L)
{
	uint32_t seed = lua_isnoneornil(L, 1) ? 1u : arg_u32(L, 1);
	kp_rng_crt_init((KpRngCrt *)new_udata(L, sizeof(KpRngCrt), RNG_CRT), seed);
	return 1;
}
static KpRngCrt *check_crt(lua_State *L) { return (KpRngCrt *)luaL_checkudata(L, 1, RNG_CRT); }
static int rng_crt_srand(lua_State *L)
{
	kp_rng_crt_init(check_crt(L), arg_u32(L, 2));
	return 0;
}
static int rng_crt_rand(lua_State *L)
{
	push_u(L, kp_rng_crt_rand(check_crt(L)));
	return 1;
}
static int rng_crt_snapshot(lua_State *L)
{
	push_list(L, &check_crt(L)->holdrand, 1, 1);
	return 1;
}
static int rng_crt_restore(lua_State *L)
{
	KpRngCrt *r = check_crt(L);
	luaL_checktype(L, 2, LUA_TTABLE);
	r->holdrand = list_item(L, 2, 1);
	return 0;
}
static const luaL_Reg RNG_CRT_METHODS[] = {
	{ "srand", rng_crt_srand }, { "rand", rng_crt_rand }, { "snapshot", rng_crt_snapshot },
	{ "restore", rng_crt_restore }, { NULL, NULL },
};
static int rng_crt_index(lua_State *L)
{
	KpRngCrt *r = check_crt(L);
	const char *key = luaL_checkstring(L, 2);
	if (strcmp(key, "holdrand") == 0) push_u(L, r->holdrand);
	else lua_getfield(L, lua_upvalueindex(1), key);
	return 1;
}

static int rng_portrait_new(lua_State *L)
{
	KpRngPortrait p;
	kp_rng_portrait_init(&p);
	if (!lua_isnoneornil(L, 1)) p.x = arg_u32(L, 1);
	if (!lua_isnoneornil(L, 2)) p.y = arg_u32(L, 2);
	if (!lua_isnoneornil(L, 3)) p.z = arg_u32(L, 3);
	*(KpRngPortrait *)new_udata(L, sizeof(KpRngPortrait), RNG_PORTRAIT) = p;
	return 1;
}
static KpRngPortrait *check_portrait(lua_State *L) { return (KpRngPortrait *)luaL_checkudata(L, 1, RNG_PORTRAIT); }
static int rng_portrait_draw(lua_State *L)
{
	KpRngPortrait *r = check_portrait(L);
	int64_t low = (int64_t)luaL_checknumber(L, 2), high = (int64_t)luaL_checknumber(L, 3);
	lua_pushnumber(L, (lua_Number)kp_rng_portrait_draw(r, low, high, arg_u32(L, 4)));
	return 1;
}
static int rng_portrait_snapshot(lua_State *L)
{
	KpRngPortrait *r = check_portrait(L);
	uint32_t v[3];
	v[0] = r->x;
	v[1] = r->y;
	v[2] = r->z;
	push_list(L, v, 3, 1);
	return 1;
}
static int rng_portrait_restore(lua_State *L)
{
	KpRngPortrait *r = check_portrait(L);
	luaL_checktype(L, 2, LUA_TTABLE);
	r->x = list_item(L, 2, 1);
	r->y = list_item(L, 2, 2);
	r->z = list_item(L, 2, 3);
	return 0;
}
static const luaL_Reg RNG_PORTRAIT_METHODS[] = {
	{ "draw", rng_portrait_draw }, { "snapshot", rng_portrait_snapshot }, { "restore", rng_portrait_restore },
	{ NULL, NULL },
};
static int rng_portrait_index(lua_State *L)
{
	KpRngPortrait *r = check_portrait(L);
	const char *key = luaL_checkstring(L, 2);
	if (strcmp(key, "x") == 0) push_u(L, r->x);
	else if (strcmp(key, "y") == 0) push_u(L, r->y);
	else if (strcmp(key, "z") == 0) push_u(L, r->z);
	else lua_getfield(L, lua_upvalueindex(1), key);
	return 1;
}

/* A metatable named `meta` whose __index serves the fields through `index` and the methods. */
static void define_class(lua_State *L, const char *meta, const luaL_Reg *methods, lua_CFunction index)
{
	luaL_newmetatable(L, meta);
	lua_newtable(L);
	luaL_register(L, NULL, methods);
	lua_pushcclosure(L, index, 1);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

static void push_rng(lua_State *L)
{
	static const uint32_t FILE_STATE[3] = { 0x12345678u, 0xe9e9e9e9u, 0x10934134u };
	define_class(L, RNG_GAME, RNG_GAME_METHODS, rng_game_index);
	define_class(L, RNG_CRT, RNG_CRT_METHODS, rng_crt_index);
	define_class(L, RNG_PORTRAIT, RNG_PORTRAIT_METHODS, rng_portrait_index);
	lua_newtable(L);
	lua_pushcfunction(L, rng_game_new);
	lua_setfield(L, -2, "game");
	lua_pushcfunction(L, rng_crt_new);
	lua_setfield(L, -2, "crt");
	lua_pushcfunction(L, rng_portrait_new);
	lua_setfield(L, -2, "portrait");
	set_number(L, "SOURCE_COUNT", KP_RNG_SOURCES);
	push_list(L, FILE_STATE, 3, 1);
	lua_setfield(L, -2, "PORTRAIT_FILE_STATE");
}

/* --- loaders --- */

static const uint8_t *arg_bytes(lua_State *L, int i, size_t *size)
{
	return (const uint8_t *)luaL_checklstring(L, i, size);
}

static void push_tag(lua_State *L, const char *key, const char *tag) { set_lstring(L, key, tag, 4); }

static int kpf_parse(lua_State *L)
{
	size_t size;
	const uint8_t *data = arg_bytes(L, 1, &size);
	const char *name = luaL_optstring(L, 2, NULL);
	KpKpf c;
	KpError err;
	int i;
	if (kp_kpf_parse(&c, data, size, name, lua_toboolean(L, 3), &err) != 0) return raise_error(L, err.msg);
	lua_newtable(L);
	push_tag(L, "kind", c.kind);
	set_number(L, "schema_version", c.schema_version);
	set_number(L, "converter_version", c.converter_version);
	lua_pushstring(L, c.profile_id);
	lua_setfield(L, -2, "profile_id");
	set_lstring(L, "source_hash", c.source_hash, 32);
	set_lstring(L, "provenance_hash", c.provenance_hash, 32);
	lua_createtable(L, c.section_count, 0);
	for (i = 0; i < c.section_count; i++) {
		const KpKpfSection *s = &c.sections[i];
		lua_newtable(L);
		push_tag(L, "fourcc", s->fourcc);
		set_number(L, "class", s->class_);
		set_number(L, "flags", s->flags);
		set_number(L, "offset", s->offset);
		set_number(L, "size", s->size);
		set_number(L, "schema_version", s->schema_version);
		set_number(L, "crc32", s->crc32);
		push_tag(L, "algorithm_id", s->algorithm_id);
		set_number(L, "algorithm_version", s->algorithm_version);
		set_lstring(L, "inputs_hash", s->inputs_hash, 16);
		lua_rawseti(L, -2, i + 1);
	}
	lua_setfield(L, -2, "sections");
	kp_kpf_free(&c);
	return 1;
}

static int dat_parse(lua_State *L)
{
	size_t size;
	const uint8_t *data = arg_bytes(L, 1, &size);
	KpDat dat;
	KpError err;
	int t, i;
	if (kp_dat_load(&dat, data, size, luaL_optstring(L, 2, NULL), NULL, &err) != 0) return raise_error(L, err.msg);
	lua_newtable(L);
	lua_newtable(L); /* layout */
	for (t = 0; t < dat.table_count; t++) {
		lua_newtable(L);
		set_number(L, "records", dat.tables[t].records);
		lua_newtable(L);
		lua_setfield(L, -2, "columns");
		lua_setfield(L, -2, dat.tables[t].name);
	}
	lua_setfield(L, -2, "layout");
	for (i = 0; i < dat.column_count; i++) {
		const KpDatColumn *c = &dat.columns[i];
		uint32_t base = (uint32_t)c->first_id * c->per_id, k;
		lua_getfield(L, -1, c->table);
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			lua_newtable(L);
			lua_pushvalue(L, -1);
			lua_setfield(L, -3, c->table);
		}
		lua_createtable(L, 0, c->count);
		for (k = 0; k < c->count; k++) {
			push_u(L, kp_dat_value(c, base + k));
			lua_rawseti(L, -2, (int)(base + k));
		}
		lua_setfield(L, -2, c->name);
		lua_pop(L, 1);
		lua_getfield(L, -1, "layout");
		lua_getfield(L, -1, c->table);
		lua_getfield(L, -1, "columns");
		lua_newtable(L);
		set_number(L, "size", c->size);
		set_number(L, "per_id", c->per_id);
		set_number(L, "count", c->count);
		set_number(L, "first_id", c->first_id);
		lua_setfield(L, -2, c->name);
		lua_pop(L, 3);
	}
	lua_newtable(L);
	for (i = 0; i < KP_DAT_REQUIREMENTS; i++) {
		const KpDatRequirements *r = &dat.requirements[i];
		uint32_t k;
		if (!r->words) continue;
		lua_createtable(L, (int)r->count, 0);
		for (k = 0; k < r->count; k++) {
			push_u(L, kp_le_u16(r->words + 2 * k));
			lua_rawseti(L, -2, (int)k);
		}
		lua_setfield(L, -2, r->name);
	}
	lua_setfield(L, -2, "requirements");
	kp_dat_free(&dat);
	return 1;
}

static void push_bytes_list(lua_State *L, const char *key, const uint8_t *p, int n)
{
	int i;
	lua_createtable(L, n, 0);
	for (i = 0; i < n; i++) {
		lua_pushnumber(L, p[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_setfield(L, -2, key);
}

static void push_section(lua_State *L, const char *key, KpBytes b) { set_lstring(L, key, b.data, b.size); }

static int mission_parse(lua_State *L)
{
	size_t size;
	const uint8_t *data = arg_bytes(L, 1, &size);
	KpMission m;
	KpError err;
	int i;
	if (kp_mission_load(&m, data, size, luaL_optstring(L, 2, NULL), NULL, &err) != 0) return raise_error(L, err.msg);
	lua_newtable(L);
	set_number(L, "chk_version", m.chk_version);
	set_number(L, "era", m.era);
	set_number(L, "width", m.width);
	set_number(L, "height", m.height);
	set_number(L, "tileset", m.tileset);
	push_bytes_list(L, "owners", m.owners, 12);
	push_bytes_list(L, "races", m.races, 12);
	lua_newtable(L);
	push_bytes_list(L, "player", m.force_player, 8);
	push_bytes_list(L, "flags", m.force_flags, 4);
	lua_createtable(L, 4, 0);
	for (i = 0; i < 4; i++) {
		lua_pushnumber(L, m.force_name[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_setfield(L, -2, "name");
	lua_setfield(L, -2, "forces");
	set_number(L, "name_string", m.name_string);
	set_number(L, "description_string", m.description_string);
	if (m.has_colors) push_bytes_list(L, "colors", m.colors, 8);
	push_section(L, "tiles", m.tiles);
	push_section(L, "mask", m.mask);
	push_section(L, "sprites", m.sprites);
	push_section(L, "units", m.units);
	push_section(L, "locations", m.locations);
	push_section(L, "strings", m.strings);
	push_section(L, "triggers", m.triggers);
	push_section(L, "briefing", m.briefing);
	push_section(L, "properties", m.properties);
	push_section(L, "walk", m.walk);
	lua_createtable(L, m.rule_count, 0);
	for (i = 0; i < m.rule_count; i++) {
		lua_newtable(L);
		lua_pushstring(L, m.rules[i].tag);
		lua_setfield(L, -2, "tag");
		push_section(L, "data", m.rules[i].data);
		lua_rawseti(L, -2, i + 1);
	}
	lua_setfield(L, -2, "rules");
	lua_newtable(L); /* sounds */
	lua_newtable(L); /* sound_info */
	for (i = 0; i < m.sound_count; i++) {
		const KpMissionSound *s = &m.sounds[i];
		if (s->index == KP_MISSION_NO_SOUND) lua_pushboolean(L, 0);
		else lua_pushnumber(L, s->index);
		lua_rawseti(L, -3, s->string_id);
		lua_newtable(L);
		set_number(L, "length", s->length);
		set_number(L, "avg_bytes_per_sec", s->avg_bytes_per_sec);
		lua_rawseti(L, -2, s->string_id);
	}
	lua_setfield(L, -3, "sound_info");
	lua_setfield(L, -2, "sounds");
	lua_newtable(L);
	push_section(L, "data", m.regions.data);
	set_number(L, "width", m.regions.width);
	set_number(L, "height", m.regions.height);
	set_number(L, "count", m.regions.count);
	set_number(L, "split_count", m.regions.split_count);
	set_number(L, "pool_words", m.regions.pool_words);
	set_number(L, "regions_at", m.regions.regions_at);
	set_number(L, "pool_at", m.regions.pool_at);
	set_number(L, "raster_at", m.regions.raster_at);
	set_number(L, "splits_at", m.regions.splits_at);
	lua_setfield(L, -2, "regions");
	lua_newtable(L);
	push_section(L, "data", m.contours.data);
	push_list(L, m.contours.counts, 4, 1);
	lua_setfield(L, -2, "counts");
	push_list(L, m.contours.capacities, 4, 1);
	lua_setfield(L, -2, "capacities");
	push_list(L, m.contours.at, 4, 1);
	lua_setfield(L, -2, "at");
	lua_setfield(L, -2, "contours");
	lua_createtable(L, m.pack_count, 0);
	for (i = 0; i < m.pack_count; i++) {
		lua_pushlstring(L, (const char *)m.packs[i].data, m.packs[i].size);
		lua_rawseti(L, -2, i + 1);
	}
	lua_setfield(L, -2, "packs");
	kp_mission_free(&m);
	return 1;
}

static void set_label(lua_State *L, const char *key, KpLabel l) { set_lstring(L, key, l.text, l.size); }

static int grpmeta_parse(lua_State *L)
{
	size_t size;
	const uint8_t *data = arg_bytes(L, 1, &size);
	KpGrpMeta g;
	KpError err;
	uint32_t i;
	if (kp_grpmeta_load(&g, data, size, luaL_optstring(L, 2, NULL), NULL, &err) != 0) return raise_error(L, err.msg);
	lua_newtable(L);
	set_number(L, "tbl_count", g.tbl_count);
	lua_newtable(L);
	lua_newtable(L);
	for (i = 1; i <= g.tbl_count; i++) {
		if (g.grp_of_tbl[i]) {
			push_u(L, g.grp_of_tbl[i]);
			lua_rawseti(L, -3, (int)i);
		}
		if (g.lo_of_tbl[i]) {
			push_u(L, g.lo_of_tbl[i]);
			lua_rawseti(L, -2, (int)i);
		}
	}
	lua_setfield(L, -3, "lo_of_tbl");
	lua_setfield(L, -2, "grp_of_tbl");
	lua_createtable(L, (int)g.atlas_count, 0);
	for (i = 1; i <= g.atlas_count; i++) {
		lua_newtable(L);
		set_label(L, "name", g.atlases[i].name);
		set_label(L, "pack", g.atlases[i].pack);
		lua_pushboolean(L, g.atlases[i].mask);
		lua_setfield(L, -2, "mask");
		lua_rawseti(L, -2, (int)i);
	}
	lua_setfield(L, -2, "atlases");
	lua_createtable(L, (int)g.grp_count, 0);
	for (i = 1; i <= g.grp_count; i++) {
		const KpGrpEntry *e = &g.grps[i];
		lua_newtable(L);
		set_label(L, "path", e->path);
		set_number(L, "frames", e->frames);
		set_number(L, "width", e->width);
		set_number(L, "height", e->height);
		set_number(L, "atlas", e->atlas);
		set_label(L, "animation", e->animation);
		set_number(L, "first_frame", e->first_frame);
		lua_rawseti(L, -2, (int)i);
	}
	lua_setfield(L, -2, "grps");
	lua_createtable(L, (int)g.lo_count, 0);
	for (i = 1; i <= g.lo_count; i++) {
		const KpLoEntry *e = &g.los[i];
		lua_newtable(L);
		set_label(L, "path", e->path);
		set_lstring(L, "data", e->data, e->size);
		set_number(L, "frames", e->frames);
		set_number(L, "points", e->points);
		lua_rawseti(L, -2, (int)i);
	}
	lua_setfield(L, -2, "los");
	set_lstring(L, "frms", g.frms, g.frms_size);
	set_lstring(L, "opac", g.opac, g.opac_size);
	kp_grpmeta_free(&g);
	return 1;
}

static int iscript_parse(lua_State *L)
{
	size_t size;
	const uint8_t *data = arg_bytes(L, 1, &size);
	KpIScript s;
	KpError err;
	uint32_t i;
	if (kp_iscript_load(&s, data, size, luaL_optstring(L, 2, NULL), NULL, &err) != 0) return raise_error(L, err.msg);
	lua_newtable(L);
	set_lstring(L, "code", s.code, s.code_size);
	lua_newtable(L);
	for (i = 0; i < s.id_count; i++) {
		if (s.headers[i] == KP_ISCRIPT_NO_HEADER) continue;
		push_s(L, s.headers[i]);
		lua_rawseti(L, -2, (int)i);
	}
	lua_setfield(L, -2, "headers");
	lua_newtable(L);
	for (i = 0; i < 256; i++) {
		if (s.slots[i] < 0) continue;
		push_s(L, s.slots[i]);
		lua_rawseti(L, -2, (int)i);
	}
	lua_setfield(L, -2, "slots");
	kp_iscript_free(&s);
	return 1;
}

static const luaL_Reg FUNCTIONS[] = {
	{ "kpf_parse", kpf_parse },
	{ "dat", dat_parse },
	{ "mission", mission_parse },
	{ "grpmeta", grpmeta_parse },
	{ "iscript", iscript_parse },
	{ NULL, NULL },
};

int kp_core_register(lua_State *L)
{
	luaL_register(L, "kp_core", FUNCTIONS);
	lua_newtable(L);
	luaL_register(L, NULL, INT_FUNCTIONS);
	set_number(L, "U32_MAX", TWO32 - 1);
	set_number(L, "S32_MIN", -2147483648.0);
	set_number(L, "S32_MAX", 2147483647.0);
	lua_setfield(L, -2, "int");
	push_rng(L);
	lua_setfield(L, -2, "rng");
	kp_images_lua_push(L);
	lua_setfield(L, -2, "images");
	kp_game_lua_push(L);
	lua_setfield(L, -2, "game");
	return 1;
}

#ifndef KP_DEFOLD
/* The entry point as a plain Lua C module (build/native/kp_core.so, tools/native/build.sh). */
int luaopen_kp_core(lua_State *L) { return kp_core_register(L); }
#endif
