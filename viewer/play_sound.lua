-- The viewer's sound mixer for the interface (plan phase 9D): kp_ui decides what plays on which of the original's
-- eight channels (kp_ui_sound.c); this plays its commands on the resource packs' sound components (each pack's
-- sounds<k> object holds sfx<id>, converter/emit/packs.py; sounds.kpd PACK names the pack). A channel's new sound
-- stops the one it held. The music (kp_ui_music.c) plays the music pack's track<k>, a trigger's WAV the mission
-- assets collection's wav<index> (the map's WAVS index of its string id); a streamed sfxdata file (the result
-- dialogs' sounds) plays its pack's sfx<id> as a stream.
local M = {}

local PLAY, STOP, MUSIC, MUSIC_VOLUME, MUSIC_STOP, STREAM, STREAMS_STOP, STREAM_SFX, CLICK = 1, 2, 3, 4, 5, 6, 7, 8, 9 -- kp_ui.h
local PER_OBJECT = 128 -- sfx and wav ids per sounds<k> / wavs<k> object
local PAN_FULL = 10000 -- DirectSound's pan, hundredths of a decibel either side

local Mixer = {}
Mixer.__index = Mixer

-- packs: { name = the created collection's ids (collectionfactory.create) }; sounds: game/data/sounds.lua parsed.
-- mission: { sounds = the map's WAVS (string id -> index), assets = its assets collection's name } or nil.
function M.new(packs, sounds, mission)
	return setmetatable({ packs = packs, sounds = sounds, mission = mission, channels = {}, streams = {}, started = 0 },
		Mixer)
end

-- DirectSound's volume (hundredths of a decibel, 0 full, -10000 silent) as a linear gain.
local function gain(volume) return 10 ^ (math.max(volume, -10000) / 2000) end

local function stop(url)
	if url then sound.stop(url) end
end

function Mixer:url(sfx)
	local pack = self.sounds.pack[sfx]
	local ids = pack and self.packs[pack]
	local object = ids and ids[hash("/sounds" .. math.floor(sfx / PER_OBJECT))]
	return object and msg.url(nil, object, "sfx" .. sfx)
end

-- The music pack's created ids (collectionfactory.create of packs#music), once it is loaded: the track kp_ui
-- started before plays from here.
function Mixer:set_music(ids)
	self.music_ids = ids
	if self.track and not self.music then
		self.music = self:track_url(self.track)
		if self.music then sound.play(self.music, { gain = gain(self.music_volume) }) end
	end
end

function Mixer:track_url(track)
	local object = self.music_ids and self.music_ids[hash("/tracks")]
	return object and msg.url(nil, object, "track" .. track)
end

function Mixer:stream_url(string_id)
	local m = self.mission
	local index = m and m.sounds[string_id]
	return index and msg.url(m.assets, "/wavs" .. math.floor(index / PER_OBJECT), "wav" .. index)
end

-- The commands of kp_ui's take_sound_commands().
function Mixer:play(commands)
	for _, c in ipairs(commands) do
		local kind, channel, sfx, volume, pan = c[1], c[2], c[3], c[4], c[5]
		if kind == PLAY or kind == STOP then
			stop(self.channels[channel])
			self.channels[channel] = nil
			local url = kind == PLAY and self:url(sfx)
			if url then
				sound.play(url, { gain = gain(volume), pan = math.max(-1, math.min(1, pan / PAN_FULL)) })
				self.channels[channel] = url
				self.started = self.started + 1
			end
		elseif kind == MUSIC then
			-- StartMusicTrack 0x46f1b0: a new track closes the stream before it
			stop(self.music)
			self.music, self.track, self.music_volume = self:track_url(sfx), sfx, volume
			if self.music then sound.play(self.music, { gain = gain(volume) }) end
		elseif kind == MUSIC_VOLUME then
			self.music_volume = volume
			if self.music then sound.set_gain(self.music, gain(volume)) end
		elseif kind == MUSIC_STOP then
			stop(self.music)
			self.music, self.track = nil, nil
		elseif kind == STREAM or kind == STREAM_SFX then -- a trigger's WAV, or an sfxdata file streamed (0x49e7c0)
			local url = kind == STREAM and self:stream_url(sfx) or self:url(sfx)
			if url then
				sound.play(url, { gain = gain(volume) })
				self.streams[#self.streams + 1] = url
				self.streamed = (self.streamed or 0) + 1
			end
		elseif kind == STREAMS_STOP then
			for _, url in ipairs(self.streams) do sound.stop(url) end
			self.streams = {}
		elseif kind == CLICK and not self.clicking then -- PlayUiClickSound 0x49e460: not again while it sounds
			local url = self:url(sfx)
			if url then
				self.clicking = true
				sound.play(url, { gain = gain(volume) }, function() self.clicking = false end)
			end
		end
	end
end

-- Everything this mixer started stops (the scene is destroyed, or the glue's music gives way to a game's): its track
-- too, which a later set_music would otherwise start.
function Mixer:stop_all()
	for _, url in pairs(self.channels) do sound.stop(url) end
	for _, url in ipairs(self.streams) do sound.stop(url) end
	stop(self.music)
	self.channels, self.streams, self.music, self.track = {}, {}, nil, nil
end

return M
