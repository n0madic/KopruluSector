# Original StarCraft 1.17 RNG

Static inspection of the local StarCraft.exe found two functions implementing the same uint32 recurrence. This establishes arithmetic for SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, numerical PE version 1.17.0.1. Subsequent authorized probes executed these functions in private x86 memory; the complete game has not been launched. [Disassembly and derived vectors](evidence/prng-static.json), [isolated native results](evidence/native-leaf-probes.json).

## Addresses and mechanism

Preferred PE ImageBase is 0x400000. These are preferred VAs, not observed runtime addresses.

| Object | RVA | Preferred VA | File offset |
|---|---|---|---|
| Raw RNG function | 0x78200 | 0x478200 | 0x77600 |
| Raw RNG imul | 0x7820c | 0x47820c | 0x7760c |
| Ranged RNG function | 0x78270 | 0x478270 | 0x77670 |
| Ranged RNG imul | 0x782a0 | 0x4782a0 | 0x776a0 |
| RNG state | 0x27393c | 0x67393c | Zero-filled .data tail; no stored seed bytes in the executable |

The raw function checks a dword at 0x673930. Zero returns 0 without updating state. With a nonzero flag it multiplies the current seed by 0x015a4e35, adds 1, writes the low 32 bits back, and extracts a 15-bit result:

```text
state = (state * 22695477 + 1) mod 2^32
result = (state >> 16) & 0x7fff
```

The enabled branch increments a global counter at 0x673938 and an indexed counter array at 0x673940 using the incoming ecx index. This corresponds to OpenBW's source-counter model; meanings of all native source indices remain unresolved.

The ranged function receives the lower bound in ecx, upper bound in edx, and counter index on the stack. Its normal branch applies the same recurrence and then:

```text
span = (to - from + 1) mod 2^32
range_result = (from + (((result * span) mod 2^32) >> 15)) mod 2^32
```

For normal small ranges this is `from + ((result * (to-from+1)) >> 15)`. Using result modulo span would change the distribution and seed-to-result mapping. With the flag disabled, this function returns the lower bound. Native range probes include width overflow and wrapped bounds; they preserve the exact low-32-bit product. Flag lifecycle and initial/live seed transfer are recovered in [native frame/reset research](native-frame.md).

OpenBW independently contains the same recurrence and range scaling. This correspondence was checked against local machine code rather than assumed from the reconstruction. [OpenBW lcg_rand](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L13198-L13207).

## Exact arithmetic in Lua

Lua 5.1 numbers normally use double. The product 22695477 * state can exceed exact integer precision; taking modulo 2^32 afterward cannot recover lost bits. For state=4294967295:

| Next-seed calculation | Result |
|---|---|
| Exact x86 uint32 arithmetic | 4272271820 |
| Direct double multiplication, then modulo | 4272271824 |

A portable exact implementation for a nonnegative uint32 seed splits multiplication into 16-bit limbs. Intermediate values remain well below 2^53:

```lua
local floor = math.floor

local function next_seed(state)
    local lo = state % 65536
    local hi = floor(state / 65536)
    local product_lo = lo * 0x4e35 + 1
    local result_lo = product_lo % 65536
    local carry = floor(product_lo / 65536)
    local result_hi = (hi * 0x4e35 + lo * 0x015a + carry) % 65536
    return result_lo + result_hi * 65536
end
```

This specifies arithmetic transfer; a game RNG module has not been implemented. The ranged helper's register-level wrap/overflow rules are now statically recovered and probed. Legal caller domains still need the complete consumer inventory; a modular arithmetic result does not establish that an unusual range occurs in gameplay.

## Remaining evidence

Remaining: all callers, consumption order across AI/IScript/combat/audio, source-index meanings, and save/load restoration. The initial-to-live seed transfer and enable/disable around object/trigger steps are now recovered.

**Initial seed source (resolved by the function atlas, [P14 `0x443f40`](atlas/p14.md) and [P26 `0x4781e0`–`0x478240`](atlas/p26.md)).** Game start `0x443f40` chooses the seed once. Outside a replay (`0x67cc90` = 0) it calls CRT `_time64(NULL)` (`0x4d11e1`) and keeps the low 32 bits (`0x443fe9`–`0x443ff1`), stores them as the initial seed `0x673934` through `0x4781f0` and writes them into the replay header (`0x4808a0`). In a replay it takes the header seed (`0x4805b0`) instead. Later in the same function the seed is applied twice, at `0x444029`–`0x44403b` and again after computer-player naming at `0x44409b`–`0x4440ad`. Each application calls `0x478240(seed)` (live state `0x67393c` = seed, counter `0x673938` = 0, 1024-byte source counters cleared) and CRT `srand(seed)` (`0x4d14f5`). The game RNG and the CRT generator used by sound choice therefore start from the same wall-clock value. A deterministic remake must supply an explicit seed in place of this wall-clock value. Replay behavior is outside the inherited campaign-remake scope. The vectors in prng-static.json are **derived**; the separately labeled native-leaf-probes.json contains actual isolated original-instruction results.

Next, trace callers and writes to the seed/flag, then compare frames where random_jump, a miss, or IScript is the first consumer. Parity requires both the same seed and the same calls in the same order. A direct connection from native random_jump to the raw RNG is established in [native AI research](native-ai.md).
