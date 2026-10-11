/* Native Unicorn block hook: execution coverage and deterministic time slicing.
 *
 * Unicorn calls cov_block_hook at the start of every executed translation block inside
 * the hooked range. The hook
 *   - marks the block's start address in a byte map owned by Python (one byte per guest
 *     address of each covered image), and
 *   - counts executed blocks; when the count reaches the slice budget it calls
 *     uc_emu_stop, so the host regains control at a deterministic point.
 * Doing this in C keeps it nearly free; a Python callback per block would make full-game
 * runs impractically slow, and Unicorn's wall-clock timeout would stop at
 * non-deterministic points.
 */
#include <stdint.h>

typedef struct {
    uint64_t base;
    uint64_t size;
    uint8_t *map;
} cov_region;

typedef int (*stop_fn_t)(void *uc);

#define MAX_REGIONS 8
static cov_region regions[MAX_REGIONS];
static int region_count;
static uint64_t blocks_executed;
static uint64_t budget = UINT64_MAX;
static stop_fn_t stop_fn;
static int coverage_enabled = 1;

void cov_reset(void *stop) {
    region_count = 0;
    blocks_executed = 0;
    budget = UINT64_MAX;
    stop_fn = (stop_fn_t)stop;
}

int cov_add_region(uint64_t base, uint64_t size, uint8_t *map) {
    if (region_count >= MAX_REGIONS) return -1;
    regions[region_count].base = base;
    regions[region_count].size = size;
    regions[region_count].map = map;
    return region_count++;
}

void cov_set_enabled(int on) { coverage_enabled = on; }
uint64_t cov_blocks_executed(void) { return blocks_executed; }
void cov_set_budget(uint64_t absolute) { budget = absolute; }

void cov_block_hook(void *uc, uint64_t address, uint32_t size, void *user_data) {
    (void)size; (void)user_data;
    if (++blocks_executed >= budget) {
        budget = UINT64_MAX;
        if (stop_fn) stop_fn(uc);
    }
    if (!coverage_enabled) return;
    for (int i = 0; i < region_count; i++) {
        uint64_t off = address - regions[i].base;
        if (off < regions[i].size) {
            regions[i].map[off] = 1;
            return;
        }
    }
}
