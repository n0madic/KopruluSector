-- HTML5 byte check of spike 6: reads the 2560x2560 index target through Emscripten's
-- GL tables, extracts each tile in original top-down order between 16-byte 0xE7
-- guards, hashes it with SHA-256 and compares with the original result. Results
-- accumulate in window.KP6; finish() prints the summary and "SPIKE done".
local M = {}

local JS = [[
window.kp6Setup = function(tileW, tileH, perRow, corner) {
  window.KP6 = {ok: 0, fail: 0, failures: [], pending: 0, fb: null, done: false,
                tileW: tileW, tileH: tileH, perRow: perRow, corner: corner};
};
window.kp6Summary = function() {
  const st = window.KP6;
  console.log('SPIKE byte check: ' + st.ok + ' match, ' + st.fail + ' differ' +
              (st.fail ? ' (first cases ' + st.failures.join(',') + ')' : ''));
  console.log('SPIKE done');
};
window.kp6Read = function(count, expected) {
  const gl = GLctx, S = 2560, st = window.KP6, TW = st.tileW, TH = st.tileH;
  const prev = gl.getParameter(gl.FRAMEBUFFER_BINDING);
  const px = new Uint8Array(S * S * 4), probe = new Uint8Array(4);
  if (st.fb === null) {
    // The index target is the framebuffer whose tile-0 pixel (original row TH-1,
    // column 310) holds the baseline value.
    for (let i = 1; i < GL.framebuffers.length; i++) {
      if (!GL.framebuffers[i]) continue;
      gl.bindFramebuffer(gl.FRAMEBUFFER, GL.framebuffers[i]);
      gl.readPixels(310, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, probe);
      if (probe[0] === st.corner) { st.fb = i; break; }
    }
  }
  gl.bindFramebuffer(gl.FRAMEBUFFER, GL.framebuffers[st.fb]);
  gl.readPixels(0, 0, S, S, gl.RGBA, gl.UNSIGNED_BYTE, px);
  gl.bindFramebuffer(gl.FRAMEBUFFER, prev);
  for (let t = 0; t < count; t++) {
    const ox = (t % st.perRow) * TW, oy = Math.floor(t / st.perRow) * TH;
    const buf = new Uint8Array(16 + TW * TH + 16).fill(0xE7);
    for (let r = 0; r < TH; r++) {
      const gy = oy + TH - 1 - r, base = 16 + r * TW;
      for (let c = 0; c < TW; c++) buf[base + c] = px[(gy * S + ox + c) * 4];
    }
    st.pending++;
    const want = expected[t].hash, id = expected[t].id;
    crypto.subtle.digest('SHA-256', buf).then(d => {
      const h = Array.from(new Uint8Array(d)).map(b => b.toString(16).padStart(2, '0')).join('');
      if (h === want) st.ok++; else { st.fail++; if (st.failures.length < 20) st.failures.push(id); }
      st.pending--;
      if (st.done && st.pending === 0) window.kp6Summary();
    });
  }
};
]]

function M.install(tile_w, tile_h, per_row, corner_value)
	html5.run(JS)
	html5.run(("kp6Setup(%d,%d,%d,%d)"):format(tile_w, tile_h, per_row, corner_value))
end

-- expected_js: JavaScript array literal of {id, hash} for the tiles of this batch.
function M.read(count, expected_js)
	html5.run(("kp6Read(%d,%s)"):format(count, expected_js))
end

function M.finish()
	html5.run("window.KP6.done = true; if (window.KP6.pending === 0) kp6Summary();")
end

return M
