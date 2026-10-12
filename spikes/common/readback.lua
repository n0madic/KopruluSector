-- HTML5-only pixel readback for spikes: reads the red channel (palette index) or all four
-- channels of a framebuffer object created by the render script, through Emscripten's GL
-- tables.
-- `ordinal` selects the n-th live framebuffer object (1 = the first one created).
local M = {}

local JS = [[
(function(ordinal, x, y, w, h, channels) {
  const gl = GLctx;
  let fb = null, seen = 0;
  for (let i = 1; i < GL.framebuffers.length; i++) {
    if (GL.framebuffers[i]) { seen++; if (seen === ordinal) { fb = GL.framebuffers[i]; break; } }
  }
  if (!fb) return 'ERR no framebuffer ' + ordinal;
  const prev = gl.getParameter(gl.FRAMEBUFFER_BINDING);
  gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
  const px = new Uint8Array(w * h * 4);
  gl.readPixels(x, y, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
  gl.bindFramebuffer(gl.FRAMEBUFFER, prev);
  let out = '';
  for (let i = 0; i < w * h; i++)
    for (let c = 0; c < channels; c++) out += px[i * 4 + c].toString(16).padStart(2, '0');
  return out;
})(%d, %d, %d, %d, %d, %d)
]]

local function read(ordinal, x, y, w, h, channels)
	local hex = html5.run(JS:format(ordinal, x, y, w, h, channels))
	if hex:sub(1, 3) == "ERR" then return nil, hex end
	return (hex:gsub("..", function(b) return string.char(tonumber(b, 16)) end))
end

-- Returns a string of w*h bytes (rows bottom-up, as GL stores them) or nil, error.
function M.red(ordinal, x, y, w, h) return read(ordinal, x, y, w, h, 1) end

-- Returns a string of w*h*4 bytes (RGBA, rows bottom-up) or nil, error.
function M.rgba(ordinal, x, y, w, h) return read(ordinal, x, y, w, h, 4) end

return M
