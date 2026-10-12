-- Depth range of the world image ranks (ADR 0006), the single source for the backend, the
-- draw list and the render script (which passes it to render/materials/world.vp).
-- Clip z = (rank + 1) / RANK_RANGE: WebGL's 16-bit depth buffer maps it to 0.5..1 and must
-- keep a whole depth level between neighbouring ranks (65,536 ranks merged them); 8,192
-- leaves 4 levels per rank and exceeds the 5,220 image records.
local M = {}

M.RANK_RANGE = 8192

return M
