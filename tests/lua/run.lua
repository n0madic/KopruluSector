-- Command-line test entry. Run from the repository root:
--   luajit tests/lua/run.lua          (LuaJIT, JIT on)
--   luajit -joff tests/lua/run.lua    (LuaJIT interpreter, as on iOS)
--   tools/toolchain/lua5.1 tests/lua/run.lua   (PUC Lua 5.1, as on HTML5)
--   luajit tests/lua/run.lua -j 8     (the suites in 8 parallel processes, the long tests split into shards)
--   luajit tests/lua/run.lua MODULE[:TEST[@I/N]]   (one suite, one test, or shard I of N of a test)
package.path = "./?.lua;" .. package.path

local backend
if jit then
	backend = jit.version .. (jit.status() and " (JIT on)" or " (JIT off)")
else
	backend = _VERSION
end

if not bit then
	local ok, lib = pcall(require, "bit")
	if ok then
		bit = lib
	else
		bit = dofile("tests/lua/bitshim.lua")
		package.loaded.bit = bit
		backend = backend .. " + bit shim"
	end
end

local env = {
	backend = backend,
	log = print,
	read = function(path)
		local f = assert(io.open(path, "rb"))
		local data = f:read("*a")
		f:close()
		return data
	end,
}

local runner = require("tests.lua.runner")

-- A word quoted for sh.
local function quote(word)
	return "'" .. word:gsub("'", "'\\''") .. "'"
end

-- The command that runs this interpreter with its options (arg[-n..-1]) on run.lua, for the parallel jobs.
local function interpreter()
	local first, words = 0, {}
	while arg[first - 1] do first = first - 1 end
	for i = first, -1 do words[#words + 1] = quote(arg[i]) end
	return table.concat(words, " ")
end

-- Parallel run (-j N): each suite in a process of its own, a suite's tests listed in its SHARDS one per process and
-- split into their shards (T.mine). The jobs' logs go to build/lua-tests/; a sharded test passes when all its shards
-- pass, and a job that ends without its summary line fails.
local function run_parallel(jobs_wanted)
	local DIR = "build/lua-tests"
	local jobs, tests = {}, {}
	for _, module in ipairs(runner.SUITES) do
		local sharded = require(module).SHARDS
		if sharded then
			for _, name in ipairs(runner.tests(module)) do
				local n = runner.shards(module, name)
				for i = 1, n do
					local job = n > 1 and ("%s:%s@%d/%d"):format(module, name, i, n) or module .. ":" .. name
					jobs[#jobs + 1] = { entry = job, test = module .. "." .. name }
				end
			end
		else
			jobs[#jobs + 1] = { entry = module }
		end
	end
	-- the shards first: they are the long ones
	table.sort(jobs, function(a, b)
		local sa, sb = a.entry:find("@") ~= nil, b.entry:find("@") ~= nil
		if sa ~= sb then return sa end
		return a.entry < b.entry
	end)
	os.execute("mkdir -p " .. DIR)
	local function log_of(entry) return DIR .. "/" .. entry:gsub("[:@/]", "_") .. ".log" end
	-- a job that never runs must not leave an earlier run's log behind: it would be read as this run's
	for _, j in ipairs(jobs) do os.remove(log_of(j.entry)) end
	-- the sanitizer runtime the jobs preload: dyld takes DYLD_INSERT_LIBRARIES out of a process's environment (and
	-- macOS out of /bin/sh's), so the caller names it again in KP_TEST_PRELOAD (tools/ci.sh)
	local words = {}
	local preload = os.getenv("KP_TEST_PRELOAD")
	if preload then words[1] = "DYLD_INSERT_LIBRARIES=" .. quote(preload) end
	words[#words + 1] = interpreter()
	local list = assert(io.open(DIR .. "/jobs", "w"))
	for _, j in ipairs(jobs) do list:write(j.entry, "\n") end
	list:close()
	-- each job: its words, then run.lua with the job's entry ($0), its output in its log
	local job = ("%s tests/lua/run.lua \"$0\" > \"%s/$(printf %%s \"$0\" | tr \":@/\" ___).log\" 2>&1")
		:format(table.concat(words, " "), DIR)
	os.execute(("xargs -P %d -n 1 sh -c %s < %s/jobs"):format(jobs_wanted, quote(job), DIR))
	local passed, failed, shard_ok = 0, 0, {}
	for _, j in ipairs(jobs) do
		local f = io.open(log_of(j.entry), "rb")
		local text = f and f:read("*a") or ""
		if f then f:close() end
		for line in text:gmatch("[^\n]+") do
			if line:find("^FAIL ") then env.log(line .. (j.entry:find("@") and " [" .. j.entry .. "]" or "")) end
		end
		local p, q = text:match(": (%d+) passed, (%d+) failed%s*$")
		if not p then
			env.log("FAIL " .. j.entry .. ": no summary (see " .. log_of(j.entry) .. ")")
			p, q = 0, 1
		end
		p, q = tonumber(p), tonumber(q)
		if j.test then
			shard_ok[j.test] = (shard_ok[j.test] ~= false) and q == 0
		else
			passed, failed = passed + p, failed + q
		end
	end
	for _, ok in pairs(shard_ok) do
		if ok then passed = passed + 1 else failed = failed + 1 end
	end
	env.log(("%s: %d passed, %d failed (%d jobs in parallel)"):format(env.backend, passed, failed, jobs_wanted))
	return failed
end

local failed
if arg[1] == "-j" then
	failed = run_parallel(assert(tonumber(arg[2]), "-j N"))
else
	failed = runner.run(env, arg[1] and { arg[1] } or nil)
end
os.exit(failed == 0 and 0 or 1)
