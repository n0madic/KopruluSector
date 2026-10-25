-- Spike result lines. Printed with a "SPIKE " prefix; on HTML5 (where release builds
-- drop print output) also appended to window.SPIKE for reading from the page.
local M = {}

local is_web = rawget(_G, "sys") ~= nil and sys.get_sys_info().system_name == "HTML5" -- (headless tests: no sys)

function M.line(text)
	print("SPIKE " .. text)
	if is_web then
		local quoted = ("%q"):format(text)
		html5.run("(window.SPIKE=window.SPIKE||[]).push(" .. quoted .. ");console.log('SPIKE '+" .. quoted .. ")")
	end
end

-- Ends the spike: exits on platforms that allow it, marks completion on the web.
function M.done()
	M.line("done")
	if not is_web then sys.exit(0) end
end

M.is_web = is_web
return M
