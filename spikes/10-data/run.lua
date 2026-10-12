-- Command-line entry: luajit spikes/10-data/run.lua (from the repository root).
package.path = "./?.lua;" .. package.path
local bench = require("spikes.10-data.bench")
local backend = jit and (jit.version .. (jit.status() and " JIT on" or " JIT off")) or _VERSION
local results = bench.run({ clock = os.clock })
for _, line in ipairs(bench.format(results)) do print("SPIKE [" .. backend .. "] " .. line) end
