/* The original's identifiers that the AI code shares, named once (docs/research/atlas/p28.md, p37.md): units.dat
 * types and AI flags, orders.dat orders, upgrades, the requirement evaluator's errors, the AI region states and record
 * flags, the PlayerAi flags +0x218 and the player controllers. The older modules still keep their own local copies of
 * some of them. */
#ifndef KP_IDS_H
#define KP_IDS_H

/* Unit types (units.dat). */
enum {
	KP_TYPE_MARINE = 0, KP_TYPE_GHOST = 1, KP_TYPE_VULTURE = 2, KP_TYPE_GOLIATH = 3, KP_TYPE_SIEGE_TANK = 5,
	KP_TYPE_SCV = 7, KP_TYPE_WRAITH = 8, KP_TYPE_SCIENCE_VESSEL = 9, KP_TYPE_DROPSHIP = 11, KP_TYPE_BATTLECRUISER = 12,
	KP_TYPE_NUCLEAR_MISSILE = 14, KP_TYPE_SIEGE_TANK_SIEGED = 30, KP_TYPE_FIREBAT = 32, KP_TYPE_MEDIC = 34,
	KP_TYPE_LARVA = 35, KP_TYPE_EGG = 36, KP_TYPE_ZERGLING = 37, KP_TYPE_HYDRALISK = 38, KP_TYPE_BROODLING = 40,
	KP_TYPE_DRONE = 41, KP_TYPE_OVERLORD = 42, KP_TYPE_MUTALISK = 43, KP_TYPE_GUARDIAN = 44, KP_TYPE_QUEEN = 45,
	KP_TYPE_DEFILER = 46, KP_TYPE_SCOURGE = 47, KP_TYPE_COCOON = 59, KP_TYPE_CORSAIR = 60, KP_TYPE_DARK_TEMPLAR = 61,
	KP_TYPE_DEVOURER = 62, KP_TYPE_DARK_ARCHON = 63, KP_TYPE_PROBE = 64, KP_TYPE_ZEALOT = 65, KP_TYPE_DRAGOON = 66,
	KP_TYPE_HIGH_TEMPLAR = 67, KP_TYPE_ARCHON = 68, KP_TYPE_SHUTTLE = 69, KP_TYPE_SCOUT = 70, KP_TYPE_ARBITER = 71,
	KP_TYPE_CARRIER = 72, KP_TYPE_HERO_DARK_TEMPLAR = 74, KP_TYPE_REAVER = 83, KP_TYPE_OBSERVER = 84,
	KP_TYPE_LURKER_EGG = 97, KP_TYPE_LURKER = 103,
	KP_TYPE_COMMAND_CENTER = 106, KP_TYPE_COMSAT = 107, KP_TYPE_NUCLEAR_SILO = 108, KP_TYPE_SUPPLY_DEPOT = 109,
	KP_TYPE_REFINERY = 110, KP_TYPE_BARRACKS = 111, KP_TYPE_FACTORY = 113, KP_TYPE_STARPORT = 114,
	KP_TYPE_CONTROL_TOWER = 115, KP_TYPE_SCIENCE_FACILITY = 116, KP_TYPE_COVERT_OPS = 117, KP_TYPE_PHYSICS_LAB = 118,
	KP_TYPE_MACHINE_SHOP = 120, KP_TYPE_MISSILE_TURRET = 124, KP_TYPE_BUNKER = 125,
	KP_TYPE_HATCHERY = 131, KP_TYPE_LAIR = 132, KP_TYPE_HIVE = 133, KP_TYPE_GREATER_SPIRE = 137, KP_TYPE_SPIRE = 141,
	KP_TYPE_CREEP_COLONY = 143, KP_TYPE_SPORE_COLONY = 144, KP_TYPE_SUNKEN_COLONY = 146, KP_TYPE_EXTRACTOR = 149,
	KP_TYPE_NEXUS = 154, KP_TYPE_ROBOTICS_FACILITY = 155, KP_TYPE_PYLON = 156, KP_TYPE_ASSIMILATOR = 157,
	KP_TYPE_GATEWAY = 160, KP_TYPE_PHOTON_CANNON = 162, KP_TYPE_STARGATE = 167, KP_TYPE_GEYSER = 188,
	KP_TYPE_FIRST_BEACON = 194, KP_TYPE_LAST_BEACON = 199, /* the special buildings 194..199 */
	KP_TYPE_NONE = 228 /* an empty queue slot, no type */
};

/* Orders (orders.dat). */
enum {
	KP_ORD_GUARD = 2, KP_ORD_MOVE = 6, KP_ORD_ATTACK_MOVE = 14, KP_ORD_NOTHING = 23 /* 0x17, the idle secondary order */,
	KP_ORD_DRONE_START_BUILD = 25, KP_ORD_DRONE_BUILD = 26, KP_ORD_PLACE_BUILDING = 30, KP_ORD_PLACE_PROTOSS_BUILDING = 31,
	KP_ORD_CONSTRUCTING = 33, KP_ORD_REPAIR = 34, KP_ORD_PLACE_ADDON = 36, KP_ORD_TRAIN = 38,
	KP_ORD_ZERG_UNIT_MORPH = 42, KP_ORD_ZERG_BUILDING_MORPH = 43, KP_ORD_DRONE_LAND = 70, KP_ORD_RESEARCH = 75,
	KP_ORD_UPGRADE = 76, KP_ORD_HARVEST1 = 79, KP_ORD_HARVEST_GAS = 83, KP_ORD_RETURN_GAS = 84,
	KP_ORD_MINING_MINERALS = 87, KP_ORD_HARVEST3 = 88, KP_ORD_RETURN_MINERALS = 90, KP_ORD_ENTER_TRANSPORT = 92,
	KP_ORD_TRANSPORT_REQUEST = 96, KP_ORD_UNSIEGING = 98, KP_ORD_SIEGING = 99, KP_ORD_SPREAD_CREEP = 102,
	KP_ORD_ARCHON_WARP = 105, KP_ORD_ARCHON_SUMMON = 106, KP_ORD_MOVE_UNLOAD = 112, KP_ORD_NUKE_TRAIN = 124, KP_ORD_NUKE_PAINT = 126,
	KP_ORD_CAST_RECALL = 137, KP_ORD_RESET_COLLISION = 150, KP_ORD_COMPUTER_AI = 156, KP_ORD_AI_ATTACK_MOVE = 157,
	KP_ORD_HARASS_MOVE = 158, KP_ORD_AI_GUARD = 160, KP_ORD_RESCUE_PASSIVE = 161, KP_ORD_HEAL_MOVE = 177,
	KP_ORD_CAST_DISRUPTION_WEB = 181, KP_ORD_DARK_ARCHON_MELD = 183, KP_ORD_JUNKYARD_DOG = 187
};

/* The AI region states (KpAiRegions.state; SetRegionState 0x4ab900, the handlers of StepRegion 0x4b0ce0). */
enum {
	KP_RSTATE_IDLE = 0,      /* 0x4ac9b0 */
	KP_RSTATE_MOVE = 1,      /* moving to an attack (0x4ac7c0) */
	KP_RSTATE_GROUP = 2,     /* a moving group gathering (0x4ac7c0) */
	KP_RSTATE_ABANDONED = 3, /* given up, asking for help (0x4aca00) */
	KP_RSTATE_WATCH = 4,     /* defended, watched (0x4ac740) */
	KP_RSTATE_BASE = 5,      /* a base region, valued (0x4ac740) */
	KP_RSTATE_DEFEND = 6,    /* fighting a threat (0x4ac690) */
	KP_RSTATE_HELP = 7,      /* helping another region (0x4acbc0) */
	KP_RSTATE_GATHER = 8,    /* an attack's grouping region (0x4ac870) */
	KP_RSTATE_ATTACK = 9     /* attacking, waiting to (0x4ac870) */
};

/* An AI region record's flags. */
enum {
	KP_RFLAG_GUARDED = 0x01, KP_RFLAG_WANTS_BUNKER = 0x02, KP_RFLAG_WANTS_TURRET = 0x10,
	KP_RFLAG_UNDER_ATTACK = 0x20, /* also: no staging there, a weak gathering or helping region there disperses */
	KP_RFLAG_NO_DECAY = 0x40
};

/* The PlayerAi flags WORD +0x218. */
enum {
	KP_AIF_BUDGET_REFRESHED = 0x1, KP_AIF_SCRIPT_STARTED = 0x2, KP_AIF_DEFAULT_BUILD_OFF = 0x4,
	KP_AIF_TRANSPORTS_OFF = 0x8, KP_AIF_FARMS_NO_TIMING = 0x10, KP_AIF_CAMPAIGN = 0x20, KP_AIF_REQUESTED = 0x40,
	KP_AIF_CREEP = 0x80,    /* creep (0x4828d0); also: a Creep Colony takes no defense anchor (GetAiFlag80 0x482c90) */
	KP_AIF_ENGAGED = 0x100, /* 0x4832d0: an attack prepared, a detection requested */
	KP_AIF_SUPPRESS_UNITS = 0x200, /* stronger than its rival (0x4acdf0): no ordinary unit requests (0x482990), and
	                                * the rich expansion of MaintainNextPlayerTowns (GetAiFlag200 0x482c20) */
	KP_AIF_REQUESTS_FIRST = 0x400, KP_AIF_TARGET_EXPANSION = 0x800
};

/* A unit type's AI flags (units.dat ai_internal). */
enum {
	KP_AIINT_IGNORE = 0x1, /* not counted for an attack roster (GetMissingRosterUnits 0x4aa500) */
	KP_AIINT_TOWN = 0x2    /* a town's unit: no guard record (AssignCreatedUnitAi 0x4b1fe0) */
};

/* Upgrades (upgrades.dat). */
enum { KP_UPG_VENTRAL_SACS = 24 };

/* The requirement evaluator's errors (kp_requirements.c, the scalar KP_REQUIREMENT_ERROR 0x66ff60). */
enum {
	KP_ERR_NONE = 0, KP_ERR_OWNER = 1, KP_ERR_UNAVAILABLE = 2, KP_ERR_QUEUE = 3, KP_ERR_ADDON = 4, KP_ERR_STATE = 5,
	KP_ERR_GROUND_ONLY = 6, KP_ERR_UNREQUIRED = 7, KP_ERR_MISSING = 8, KP_ERR_TECH = 9, KP_ERR_DISABLED = 10,
	KP_ERR_IN_PROGRESS = 12, KP_ERR_RESEARCHED = 13, KP_ERR_MAX_LEVEL = 14, KP_ERR_RANGE_UNIT = 16,
	KP_ERR_RANGE_TECH = 17, KP_ERR_RANGE_UPGRADE = 18, KP_ERR_PROPERTY = 19, KP_ERR_INCOMPLETE = 20,
	KP_ERR_DISALLOWED = 21, KP_ERR_HALLUCINATION = 22, KP_ERR_NO_RECIPE = 23, KP_ERR_PRODUCER = 25,
	KP_ERR_EXPANSION = 26
};

/* The player controllers (the runtime player types 0x6726d0). */
enum { KP_CTRL_COMPUTER = 1, KP_CTRL_HUMAN = 2, KP_CTRL_RESCUE = 3, KP_CTRL_NEUTRAL = 7 };

#endif
