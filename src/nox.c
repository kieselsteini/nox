/*
================================================================================

	Nox - a minimal 2D Lua game engine
	written by Sebastian Steinhauer <s.steinhauer@yahoo.de>

	This is free and unencumbered software released into the public domain.

	Anyone is free to copy, modify, publish, use, compile, sell, or
	distribute this software, either in source code form or as a compiled
	binary, for any purpose, commercial or non-commercial, and by any
	means.

	In jurisdictions that recognize copyright laws, the author or authors
	of this software dedicate any and all copyright interest in the
	software to the public domain. We make this dedication for the benefit
	of the public at large and to the detriment of our heirs and
	successors. We intend this dedication to be an overt act of
	relinquishment in perpetuity of all present and future rights to this
	software under copyright law.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
	OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
	OTHER DEALINGS IN THE SOFTWARE.

	For more information, please refer to <http://unlicense.org/>

================================================================================
*/
/*
================================================================================

		INCLUDES

================================================================================
*/
/*----------------------------------------------------------------------------*/
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


/*----------------------------------------------------------------------------*/
#include "xxhash.h"
#include "lz4frame.h"


/*----------------------------------------------------------------------------*/
#include "SDL.h"


/*----------------------------------------------------------------------------*/
#include "../stb/SDL_stbimage.h"
#define STB_VORBIS_HEADER_ONLY
#include "../stb/stb_vorbis.c"


/*
================================================================================

		DEFINES & TYPES

================================================================================
*/
/*----------------------------------------------------------------------------*/
#define NOX_AUTHOR			"Sebastian Steinhauer <s.steinhauer@yahoo.de>"
#define NOX_VERSION			"0.1.0"


/*----------------------------------------------------------------------------*/
#define WINDOW_TITLE		"Nox Window"
#define WINDOW_WIDTH		1280
#define WINDOW_HEIGHT		720
#define WINDOW_PADDING		64


/*----------------------------------------------------------------------------*/
#define AUDIO_VOICES		32
#define AUDIO_FREQUENCY		44100


/*----------------------------------------------------------------------------*/
typedef struct image_t {
	struct image_t			*root;
	SDL_Texture 			*texture;
	SDL_Rect				rect;
} image_t;


/*----------------------------------------------------------------------------*/
typedef struct sample_t {
	Sint16					*data;
	float					freq;
	int						length;
	int						channels;
} sample_t;


/*----------------------------------------------------------------------------*/
typedef struct voice_t {
	sample_t				*sample;
	int						sample_ref;
	float					position;
	float					gain;
	float					pitch;
	float					pan;
	int						looping;
} voice_t;


/*
================================================================================

		GLOBAL VARIABLES

================================================================================
*/
/*----------------------------------------------------------------------------*/
static int event_loop_running = -1;


/*----------------------------------------------------------------------------*/
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Color draw_color = { 255, 255, 255, 255 };
static SDL_BlendMode blend_mode = SDL_BLENDMODE_BLEND;
static SDL_Texture *render_target = NULL;
static int render_target_ref = LUA_NOREF;


/*----------------------------------------------------------------------------*/
static SDL_AudioDeviceID audio_device = 0;
static float audio_gain = 1.0f;
static float audio_freq;
static voice_t audio_voices[AUDIO_VOICES];


/*
================================================================================

		HELPER FUNCTIONS

================================================================================
*/
/*----------------------------------------------------------------------------*/
#define minimum(a, b)			((a) < (b) ? (a) : (b))
#define maximum(a, b)			((a) > (b) ? (a) : (b))
#define clamp(x, min, max)		maximum(minimum(x, max), min)


/*----------------------------------------------------------------------------*/
static int push_error(lua_State *L, const char *fmt, ...) {
	va_list va;

	va_start(va, fmt);
	lua_pushnil(L);
	lua_pushvfstring(L, fmt, va);
	va_end(va);

	return 2;
}


/*----------------------------------------------------------------------------*/
static int push_callback(lua_State *L, const char *name) {
	if (lua_getfield(L, LUA_REGISTRYINDEX, "nox_callbacks") != LUA_TTABLE) {
		lua_pop(L, 1);
		return 0;
	}
	if (lua_getfield(L, -1, name) != LUA_TFUNCTION) {
		lua_pop(L, 2);
		return 0;
	}
	lua_remove(L, -2);
	return -1;
}


/*----------------------------------------------------------------------------*/
static void *push_object(lua_State *L, const char *name, size_t length) {
	void *data = lua_newuserdata(L, length);
	luaL_setmetatable(L, name);
	SDL_memset(data, 0, length);
	return data;
}


/*----------------------------------------------------------------------------*/
static void register_metatable(lua_State *L, const char *name, const luaL_Reg funcs[]) {
	luaL_newmetatable(L, name);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, funcs, 0);
	lua_pop(L, 1);
}


/*----------------------------------------------------------------------------*/
static voice_t *check_voice(lua_State *L, int idx) {
	int n = (int)luaL_checkinteger(L, 1);
	luaL_argcheck(L, n >= 1 && n <= AUDIO_VOICES, idx, "invalid voice index");
	return &audio_voices[n - 1];
}


/*----------------------------------------------------------------------------*/
static sample_t *check_sample(lua_State *L, int idx) {
	sample_t *self = luaL_checkudata(L, idx, "nox_sample");
	luaL_argcheck(L, self->data != NULL, idx, "attempt to operate on destroyed sample");
	return self;
}


/*----------------------------------------------------------------------------*/
static image_t *check_image(lua_State *L, int idx) {
	image_t *self = luaL_checkudata(L, idx, "nox_image");
	luaL_argcheck(L, self->root->texture != NULL, idx, "attempt to operate on destroyed image");
	return self;
}


/*----------------------------------------------------------------------------*/
static void reset_render_target(lua_State *L) {
	luaL_unref(L, LUA_REGISTRYINDEX, render_target_ref);
	if (SDL_SetRenderTarget(renderer, NULL))
		luaL_error(L, "SDL_SetRenderTarget(NULL) failed: %s", SDL_GetError());

	render_target = NULL;
	render_target_ref = LUA_NOREF;
}


/*----------------------------------------------------------------------------*/
static void set_draw_parameters(lua_State *L) {
	if (SDL_SetRenderDrawColor(renderer, draw_color.r, draw_color.g, draw_color.b, draw_color.a))
		luaL_error(L, "SDL_SetRenderDrawColor() failed: %s", SDL_GetError());
	if (SDL_SetRenderDrawBlendMode(renderer, blend_mode))
		luaL_error(L, "SDL_SetRenderDrawBlendMode() failed: %s", SDL_GetError());
}


/*----------------------------------------------------------------------------*/
static void stop_voice(lua_State *L, voice_t *voice) {
	SDL_LockAudioDevice(audio_device);
	voice->sample = NULL;
	SDL_UnlockAudioDevice(audio_device);

	luaL_unref(L, LUA_REGISTRYINDEX, voice->sample_ref);
	voice->sample_ref = LUA_NOREF;
}


/*----------------------------------------------------------------------------*/
static void stop_sample(lua_State *L, sample_t *sample) {
	int i;

	SDL_LockAudioDevice(audio_device);
	for (i = 0; i < AUDIO_VOICES; ++i) {
		if (audio_voices[i].sample == sample)
			stop_voice(L, &audio_voices[i]);
	}
	SDL_UnlockAudioDevice(audio_device);
}


/*----------------------------------------------------------------------------*/
static void purge_voices(lua_State *L) {
	int i;

	SDL_LockAudioDevice(audio_device);
	for (i = 0; i < AUDIO_VOICES; ++i) {
		if (audio_voices[i].sample == NULL && audio_voices[i].sample_ref != LUA_NOREF)
			stop_voice(L, &audio_voices[i]);
	}
	SDL_UnlockAudioDevice(audio_device);
}


/*----------------------------------------------------------------------------*/
static void mix_audio_voices(void *userdata, Uint8 *stream8, int len8) {
	voice_t *voice;
	int i, j, pos, len = len8 / (sizeof(float) * 2);
	float l, r, sample, *stream = (float*)stream8;

	for (i = 0; i < len; ++i) {
		l = r = 0.0f;
		for (j = 0; j < AUDIO_VOICES; ++j) {
			voice = &audio_voices[j];
			if (voice->sample != NULL) {
				pos = (Uint32)voice->position;
				if (pos < voice->sample->length) {
					if (voice->sample->channels == 1) {
						sample = ((float)voice->sample->data[pos] / 32768.0f) * voice->gain;
						// FIXME: implement panning
						l += sample;
						r += sample;
						voice->position += ((float)voice->sample->freq / audio_freq) * voice->pitch;
					} else {
						l += ((float)voice->sample->data[pos + 0] / 32768.0f) * voice->gain;
						r += ((float)voice->sample->data[pos + 1] / 32768.0f) * voice->gain;
						voice->position += ((float)voice->sample->freq / audio_freq) * (voice->pitch * 2.0f);
					}
				} else {
					if (voice->looping) {
						voice->position = 0.0f;
					} else {
						voice->sample = NULL;
					}
				}
			}
		}

		l *= audio_gain; *stream++ = clamp(l, -1.0f, 1.0f);
		r *= audio_gain; *stream++ = clamp(r, -1.0f, 1.0f);
	}

	(void)userdata;
}


/*
================================================================================

		LUA API

================================================================================
*/
/*
--------------------------------------------------------------------------------

		Module: nox.audio

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int f_nox_audio_get_global_gain(lua_State *L) {
	lua_pushnumber(L, audio_gain);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_set_global_gain(lua_State *L) {
	float gain = (float)luaL_checknumber(L, 1);

	SDL_LockAudioDevice(audio_device);
	audio_gain = clamp(gain, 0.0f, 1.0f);
	SDL_UnlockAudioDevice(audio_device);

	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_is_voice_playing(lua_State *L) {
	voice_t *self = check_voice(L, 1);
	lua_pushboolean(L, self->sample != NULL);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_stop_voice(lua_State *L) {
	voice_t *self = check_voice(L, 1);
	stop_voice(L, self);
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_stop_all_voices(lua_State *L) {
	int i;

	SDL_LockAudioDevice(audio_device);
	for (i = 0; i < AUDIO_VOICES; ++i)
		stop_voice(L, &audio_voices[i]);
	SDL_UnlockAudioDevice(audio_device);

	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_destroy_sample(lua_State *L) {
	sample_t *self = luaL_checkudata(L, 1, "nox_sample");
	if (self->data != NULL) {
		stop_sample(L, self);
		SDL_FreeWAV((Uint8*)self->data);
		self->data = NULL;
	}
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_load_sample(lua_State *L) {
	sample_t *new;
	size_t length;
	int channels, sample_rate, samples;
	short *data;
	const char *binary = luaL_checklstring(L, 1, &length);

	new = push_object(L, "nox_sample", sizeof(sample_t));
	new->data = NULL;

	samples = stb_vorbis_decode_memory((const unsigned char*)binary, (int)length, &channels, &sample_rate, &data);
	if (samples <= 0)
		return push_error(L, "stb_vorbis_decode_memory() failed");

	new->data = data;
	new->freq = sample_rate;
	new->length = samples;
	new->channels = channels;

	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_is_sample_valid(lua_State *L) {
	sample_t *self = luaL_checkudata(L, 1, "nox_sample");
	lua_pushboolean(L, self->data != NULL);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_is_sample_playing(lua_State *L) {
	int i;
	sample_t *self = check_sample(L, 1);

	for (i = 0; i < AUDIO_VOICES; ++i) {
		if (audio_voices[i].sample == self) {
			lua_pushboolean(L, 1);
			return 1;
		}
	}

	lua_pushboolean(L, 0);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_get_sample_length(lua_State *L) {
	sample_t *self = check_sample(L, 1);
	if (self->channels == 1) {
		lua_pushnumber(L, (lua_Number)self->length / (lua_Number)self->freq);
		return 1;
	} else if (self->channels == 2) {
		lua_pushnumber(L, ((lua_Number)self->length / 2.0) / (lua_Number)self->freq);
		return 1;
	} else {
		return push_error(L, "invalid number of channels");
	}
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_stop_sample(lua_State *L) {
	sample_t *self = check_sample(L, 1);
	stop_sample(L, self);
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_audio_play_sample(lua_State *L) {
	int i;
	voice_t *voice;
	sample_t *self = check_sample(L, 1);
	float gain = (float)luaL_optnumber(L, 2, 1.0);
	float pitch = (float)luaL_optnumber(L, 3, 1.0);
	float pan = (float)luaL_optnumber(L, 4, 0.0);
	int looping = lua_toboolean(L, 5);

	for (i = 0; i < AUDIO_VOICES; ++i) {
		voice = &audio_voices[i];
		if (voice->sample == NULL && voice->sample_ref == LUA_NOREF) {

			voice->position = 0.0f;
			voice->gain = clamp(gain, 0.0f, 1.0f);
			voice->pitch = clamp(pitch, 0.5f, 2.0f);
			voice->pan = clamp(pan, -1.0f, 1.0f);
			voice->looping = looping;

			lua_pushvalue(L, 1);
			voice->sample_ref = luaL_ref(L, LUA_REGISTRYINDEX);

			SDL_LockAudioDevice(audio_device);
			voice->sample = self;
			SDL_UnlockAudioDevice(audio_device);

			lua_pushinteger(L, i + 1);
			return 1;
		}
	}

	return push_error(L, "no free audio voice");
}


/*----------------------------------------------------------------------------*/
static const luaL_Reg nox_audio__funcs[] = {
	{ "get_global_gain", f_nox_audio_get_global_gain },
	{ "set_global_gain", f_nox_audio_set_global_gain },
	{ "is_voice_playing", f_nox_audio_is_voice_playing },
	{ "stop_voice", f_nox_audio_stop_voice },
	{ "stop_all_voices", f_nox_audio_stop_all_voices },
	{ "destroy_sample", f_nox_audio_destroy_sample },
	{ "load_sample", f_nox_audio_load_sample },
	{ "is_sample_valid", f_nox_audio_is_sample_valid },
	{ "is_sample_playing", f_nox_audio_is_sample_playing },
	{ "get_sample_length", f_nox_audio_get_sample_length },
	{ "stop_sample", f_nox_audio_stop_sample },
	{ "play_sample", f_nox_audio_play_sample },
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static const luaL_Reg nox_sample__funcs[] = {
	{ "__gc", f_nox_audio_destroy_sample },
	{ "destroy", f_nox_audio_destroy_sample },
	{ "is_valid", f_nox_audio_is_sample_valid },
	{ "is_playing", f_nox_audio_is_sample_playing },
	{ "get_length", f_nox_audio_get_sample_length },
	{ "stop", f_nox_audio_stop_sample },
	{ "play", f_nox_audio_play_sample },
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static int open_module_nox_audio(lua_State *L) {
	register_metatable(L, "nox_sample", nox_sample__funcs);
	luaL_newlib(L, nox_audio__funcs);
	return 1;
}


/*
--------------------------------------------------------------------------------

		Module: nox.events

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int f_nox_events_emit(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	if (push_callback(L, name)) {
		lua_replace(L, 1);
		lua_call(L, lua_gettop(L) - 1, 0);
		lua_pushboolean(L, 1);
		return 1;
	}
	return push_error(L, "undefined event callback: '%s'", name);
}


/*----------------------------------------------------------------------------*/
static const luaL_Reg nox_events__funcs[] = {
	{ "emit", f_nox_events_emit },
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static int open_module_nox_events(lua_State *L) {
	luaL_newlib(L, nox_events__funcs);
	lua_pushvalue(L, -1);
	lua_setfield(L, LUA_REGISTRYINDEX, "nox_callbacks");
	return 1;
}


/*
--------------------------------------------------------------------------------

		Module: nox.math

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int f_nox_math_xxhash(lua_State *L) {
	size_t length;
	const char *data = luaL_checklstring(L, 1, &length);
	unsigned int seed = (unsigned int)luaL_optinteger(L, 2, 0);

	lua_pushinteger(L, XXH32(data, length, seed));

	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_math_compress(lua_State *L) {
	LZ4F_preferences_t prefs;
	size_t src_size, dst_size;
	luaL_Buffer buffer;
	char *dst_data;
	const char *src_data = luaL_checklstring(L, 1, &src_size);
	int compression_level = (int)luaL_optinteger(L, 2, 9);

	SDL_zero(prefs);
	prefs.compressionLevel = compression_level;

	dst_size = LZ4F_compressFrameBound(src_size, &prefs);
	if (LZ4F_isError(dst_size))
		return push_error(L, "LZ4F_compressFrameBound() failed: %s", LZ4F_getErrorName(dst_size));

	dst_data = luaL_buffinitsize(L, &buffer, dst_size);
	dst_size = LZ4F_compressFrame(dst_data, dst_size, src_data, src_size, &prefs);

	if (LZ4F_isError(dst_size))
		return push_error(L, "LZ4F_compressFrame() failed: %s", LZ4F_getErrorName(dst_size));

	luaL_pushresultsize(&buffer, dst_size);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_math_decompress(lua_State *L) {
	size_t src_size, dst_size, lz_avail, lz_hint;
	char data[1024 * 64];
	luaL_Buffer buffer;
	LZ4F_dctx *dctx;
	LZ4F_errorCode_t lz_err;
	LZ4F_decompressOptions_t options;
	const char *src_data = luaL_checklstring(L, 1, &src_size);

	lz_err = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
	if (LZ4F_isError(lz_err))
		return push_error(L, "LZ4F_createDecompressionContext() failed: %s", LZ4F_getErrorName(lz_err));

	SDL_zero(options);
	luaL_buffinit(L, &buffer);

	for (;;) {
		lz_avail = src_size;
		dst_size = sizeof(data);
		lz_hint = LZ4F_decompress(dctx, data, &dst_size, src_data, &lz_avail, &options);

		if (LZ4F_isError(lz_hint)) {
			LZ4F_freeDecompressionContext(dctx);
			return push_error(L, "LZ4F_decompress() failed: %s", LZ4F_getErrorName(lz_hint));
		}
		if (dst_size == 0) {
			LZ4F_freeDecompressionContext(dctx);
			return push_error(L, "LZ4F_decompress() returned no output");
		}

		luaL_addlstring(&buffer, data, dst_size);
		src_data += lz_avail;
		src_size -= lz_avail;

		if (lz_hint == 0) break;
	}

	LZ4F_freeDecompressionContext(dctx);
	luaL_pushresult(&buffer);
	return 1;
}


/*----------------------------------------------------------------------------*/
static const luaL_Reg nox_math__funcs[] = {
	{ "xxhash", f_nox_math_xxhash },
	{ "compress", f_nox_math_compress },
	{ "decompress", f_nox_math_decompress },
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static int open_module_nox_math(lua_State *L) {
	luaL_newlib(L, nox_math__funcs);
	return 1;
}


/*
--------------------------------------------------------------------------------

		Module: nox.system

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int f_nox_system_get_base_path(lua_State *L) {
	char *path = SDL_GetBasePath();
	if (path == NULL)
		return push_error(L, "SDL_GetBasePath() failed: %s", SDL_GetError());

	lua_pushstring(L, path);
	SDL_free(path);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_system_get_pref_path(lua_State *L) {
	const char *org = luaL_checkstring(L, 1);
	const char *app = luaL_checkstring(L, 2);
	char *path = SDL_GetPrefPath(org, app);
	if (path == NULL)
		return push_error(L, "SDL_GetPrefPath() failed: %s", SDL_GetError());

	lua_pushstring(L, path);
	SDL_free(path);
	return 1;
}


/*----------------------------------------------------------------------------*/
static const luaL_Reg nox_system__funcs[] = {
	{ "get_base_path", f_nox_system_get_base_path },
	{ "get_pref_path", f_nox_system_get_pref_path },
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static int open_module_nox_system(lua_State *L) {
	luaL_newlib(L, nox_system__funcs);
	return 1;
}


/*
--------------------------------------------------------------------------------

		Module: nox.video

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int f_nox_video_get_blend_mode(lua_State *L) {
	const char *name;
	switch (blend_mode) {
		case SDL_BLENDMODE_NONE:	name = "none"; break;
		case SDL_BLENDMODE_BLEND:	name = "blend"; break;
		case SDL_BLENDMODE_ADD:		name = "add"; break;
		case SDL_BLENDMODE_MOD:		name = "mod"; break;
		default:					name = NULL; break;
	}
	lua_pushstring(L, name);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_set_blend_mode(lua_State *L) {
	static const char *names[] = { "none", "blend", "add", "mod", NULL };
	static const SDL_BlendMode modes[] = {
		SDL_BLENDMODE_NONE, SDL_BLENDMODE_BLEND, SDL_BLENDMODE_ADD, SDL_BLENDMODE_MOD
	};

	blend_mode = modes[luaL_checkoption(L, 1, NULL, names)];

	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_get_draw_color(lua_State *L) {
	lua_pushinteger(L, draw_color.r);
	lua_pushinteger(L, draw_color.g);
	lua_pushinteger(L, draw_color.b);
	lua_pushinteger(L, draw_color.a);
	return 4;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_set_draw_color(lua_State *L) {
	int r = (int)luaL_checknumber(L, 1);
	int g = (int)luaL_checknumber(L, 2);
	int b = (int)luaL_checknumber(L, 3);
	int a = (int)luaL_optnumber(L, 4, 255.0);

	draw_color.r = (Uint8)clamp(r, 0, 255);
	draw_color.g = (Uint8)clamp(g, 0, 255);
	draw_color.b = (Uint8)clamp(b, 0, 255);
	draw_color.a = (Uint8)clamp(a, 0, 255);

	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_get_render_target(lua_State *L) {
	lua_rawgeti(L, LUA_REGISTRYINDEX, render_target_ref);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_set_render_target(lua_State *L) {
	if (lua_isnil(L, 1)) {
		reset_render_target(L);
	} else {
		image_t *self = check_image(L, 1);
		luaL_argcheck(L, self->root == self, 1, "cannot use child images as render target");

		reset_render_target(L);

		if (SDL_SetRenderTarget(renderer, self->texture))
			luaL_error(L, "SDL_SetRenderTarget() failed: %s", SDL_GetError());

		render_target = self->texture;
		render_target_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_clear(lua_State *L) {
	set_draw_parameters(L);
	if (SDL_RenderClear(renderer))
		luaL_error(L, "SDL_RenderClear() failed: %s", SDL_GetError());
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_destroy_image(lua_State *L) {
	image_t *self = luaL_checkudata(L, 1, "nox_image");
	if (self->texture != NULL) {
		if (self->texture == render_target)
			reset_render_target(L);

		SDL_DestroyTexture(self->texture);
		self->texture = NULL;
	}
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_load_image(lua_State *L) {
	size_t length;
	const char *binary = luaL_checklstring(L, 1, &length);
	image_t *new = push_object(L, "nox_image", sizeof(image_t));

	new->rect.x = 0;
	new->rect.y = 0;
	new->root = new;
	new->texture = STBIMG_LoadTextureFromMemory(renderer, (const unsigned char*)binary, (int)length);
	if (new->texture == NULL)
		return push_error(L, "STBIMG_LoadTextureFromMemory() failed: %s", SDL_GetError());
	SDL_QueryTexture(new->texture, NULL, NULL, &new->rect.w, &new->rect.h);

	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_create_image(lua_State *L) {
	Uint32 pixel_format;
	int w = (int)luaL_checkinteger(L, 1);
	int h = (int)luaL_checkinteger(L, 2);
	image_t *new = push_object(L, "nox_image", sizeof(image_t));

	new->root = new;
	new->texture = NULL;
	new->rect.x = 0;
	new->rect.y = 0;
	new->rect.w = w;
	new->rect.h = h;

	if ((pixel_format = SDL_GetWindowPixelFormat(window)) == SDL_PIXELFORMAT_UNKNOWN)
		return push_error(L, "SDL_GetWindowPixelFormat() failed: %s", SDL_GetError());

	if ((new->texture = SDL_CreateTexture(renderer, pixel_format, SDL_TEXTUREACCESS_TARGET, w, h)) == NULL)
		return push_error(L, "SDL_CreateTexture(%d, %d) failed: %s", w, h, SDL_GetError());

	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_create_child_image(lua_State *L) {
	image_t *self = check_image(L, 1);
	int x = (int)luaL_checknumber(L, 2);
	int y = (int)luaL_checknumber(L, 3);
	int w = (int)luaL_checknumber(L, 4);
	int h = (int)luaL_checknumber(L, 5);
	image_t *new = push_object(L, "nox_image", sizeof(image_t));

	x = maximum(0, x + self->rect.x);
	y = maximum(0, y + self->rect.y);

	new->root = self;
	new->texture = NULL;
	new->rect.x = clamp(x, 0, self->rect.w - 1);
	new->rect.y = clamp(y, 0, self->rect.h - 1);
	new->rect.w = clamp(w, 0, self->rect.w - x);
	new->rect.h = clamp(h, 0, self->rect.h - y);

	lua_pushvalue(L, 1);
	lua_setuservalue(L, -2);

	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_is_image_valid(lua_State *L) {
	image_t *self = luaL_checkudata(L, 1, "nox_image");
	lua_pushboolean(L, self->root->texture != NULL);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_is_child_image(lua_State *L) {
	image_t *self = check_image(L, 1);
	lua_pushboolean(L, self->root != self);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_get_image_size(lua_State *L) {
	image_t *self = check_image(L, 1);
	lua_pushinteger(L, self->rect.w);
	lua_pushinteger(L, self->rect.h);
	return 2;
}


/*----------------------------------------------------------------------------*/
static int f_nox_video_draw_image(lua_State *L) {
	SDL_Rect dst;
	image_t *self = check_image(L, 1);
	dst.x = (int)luaL_checknumber(L, 2);
	dst.y = (int)luaL_checknumber(L, 3);
	dst.w = self->rect.w;
	dst.h = self->rect.h;

	if (SDL_SetTextureColorMod(self->root->texture, draw_color.r, draw_color.g, draw_color.b))
		luaL_error(L, "SDL_SetTextureColorMod() failed: %s", SDL_GetError());
	if (SDL_SetTextureAlphaMod(self->root->texture, draw_color.a))
		luaL_error(L, "SDL_SetTextureAlphaMod() failed: %s", SDL_GetError());
	if (SDL_SetTextureBlendMode(self->root->texture, blend_mode))
		luaL_error(L, "SDL_SetTextureBlendMode() failed: %s", SDL_GetError());
	if (SDL_RenderCopy(renderer, self->root->texture, &self->rect, &dst))
		luaL_error(L, "SDL_RenderCopy() failed: %s", SDL_GetError());

	return 0;
}


/*----------------------------------------------------------------------------*/
static const luaL_Reg nox_video__funcs[] = {
	{ "get_blend_mode", f_nox_video_get_blend_mode },
	{ "set_blend_mode", f_nox_video_set_blend_mode },
	{ "get_draw_color", f_nox_video_get_draw_color },
	{ "set_draw_color", f_nox_video_set_draw_color },
	{ "get_render_target", f_nox_video_get_render_target },
	{ "set_render_target", f_nox_video_set_render_target },
	{ "clear", f_nox_video_clear },
	{ "destroy_image", f_nox_video_destroy_image },
	{ "load_image", f_nox_video_load_image },
	{ "create_image", f_nox_video_create_image },
	{ "create_child_image", f_nox_video_create_child_image },
	{ "is_image_valid", f_nox_video_is_image_valid },
	{ "is_child_image", f_nox_video_is_child_image },
	{ "get_image_size", f_nox_video_get_image_size },
	{ "draw_image", f_nox_video_draw_image },
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static const luaL_Reg nox_image__funcs[] = {
	{ "__gc", f_nox_video_destroy_image },
	{ "destroy", f_nox_video_destroy_image },
	{ "create_child", f_nox_video_create_child_image },
	{ "is_valid", f_nox_video_is_image_valid },
	{ "is_child", f_nox_video_is_child_image },
	{ "get_size", f_nox_video_get_image_size },
	{ "draw", f_nox_video_draw_image },
	{ "set_render_target", f_nox_video_set_render_target },
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static int open_module_nox_video(lua_State *L) {
	register_metatable(L, "nox_image", nox_image__funcs);
	luaL_newlib(L, nox_video__funcs);
	return 1;
}


/*
--------------------------------------------------------------------------------

		Module: nox.window

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int f_nox_window_close(lua_State *L) {
	(void)L;
	event_loop_running = 0;
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_window_get_title(lua_State *L) {
	const char *title = SDL_GetWindowTitle(window);
	lua_pushstring(L, title);
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_window_set_title(lua_State *L) {
	const char *title = luaL_checkstring(L, 1);
	SDL_SetWindowTitle(window, title);
	return 0;
}


/*----------------------------------------------------------------------------*/
static int f_nox_window_is_fullscreen(lua_State *L) {
	Uint32 flags = SDL_GetWindowFlags(window);
	lua_pushboolean(L, flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP));
	return 1;
}


/*----------------------------------------------------------------------------*/
static int f_nox_window_set_fullscreen(lua_State *L) {
	int fs = lua_toboolean(L, 1);
	SDL_SetWindowFullscreen(window, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP :  0);
	return 0;
}


/*----------------------------------------------------------------------------*/
static const luaL_Reg nox_window__funcs[] = {
	{ "close", f_nox_window_close },
	{ "get_title", f_nox_window_get_title },
	{ "set_title", f_nox_window_set_title },
	{ "is_fullscreen", f_nox_window_is_fullscreen },
	{ "set_fullscreen", f_nox_window_set_fullscreen },
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static int open_module_nox_window(lua_State *L) {
	luaL_newlib(L, nox_window__funcs);
	return 1;
}


/*
--------------------------------------------------------------------------------

		Module: nox

--------------------------------------------------------------------------------
*/
/*----------------------------------------------------------------------------*/
static int open_module_nox(lua_State *L) {
	lua_newtable(L);

	lua_pushstring(L, NOX_AUTHOR);
	lua_setfield(L, -2, "_AUTHOR");

	lua_pushstring(L, NOX_VERSION);
	lua_setfield(L, -2, "_VERSION");

	luaL_requiref(L, "nox.audio", open_module_nox_audio, 0);
	lua_setfield(L, -2, "audio");

	luaL_requiref(L, "nox.events", open_module_nox_events, 0);
	lua_setfield(L, -2, "events");

	luaL_requiref(L, "nox.math", open_module_nox_math, 0);
	lua_setfield(L, -2, "math");

	luaL_requiref(L, "nox.system", open_module_nox_system, 0);
	lua_setfield(L, -2, "system");

	luaL_requiref(L, "nox.video", open_module_nox_video, 0);
	lua_setfield(L, -2, "video");

	luaL_requiref(L, "nox.window", open_module_nox_window, 0);
	lua_setfield(L, -2, "window");

	return 1;
}


/*
================================================================================

		EVENT LOOP

================================================================================
*/
/*----------------------------------------------------------------------------*/
static void handle_SDL_events(lua_State *L) {
	SDL_Event ev;

	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
			case SDL_QUIT:
				event_loop_running = 0;
				break;

			case SDL_KEYDOWN:
				if (push_callback(L, "on_key_down")) {
					lua_pushstring(L, SDL_GetKeyName(ev.key.keysym.sym));
					lua_call(L, 1, 0);
				}
				break;

			case SDL_KEYUP:
				if (push_callback(L, "on_key_up")) {
					lua_pushstring(L, SDL_GetKeyName(ev.key.keysym.sym));
					lua_call(L, 1, 0);
				}
				break;

			case SDL_TEXTINPUT:
				if (push_callback(L, "on_text_input")) {
					lua_pushstring(L, ev.text.text);
					lua_call(L, 1, 0);
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
				if (push_callback(L, "on_mouse_down")) {
					lua_pushinteger(L, ev.button.button);
					lua_call(L, 1, 0);
				}
				break;

			case SDL_MOUSEBUTTONUP:
				if (push_callback(L, "on_mouse_up")) {
					lua_pushinteger(L, ev.button.button);
					lua_call(L, 1, 0);
				}
				break;

			case SDL_MOUSEMOTION:
				if (push_callback(L, "on_mouse_moved")) {
					lua_pushinteger(L, ev.motion.x);
					lua_pushinteger(L, ev.motion.y);
					lua_call(L, 2, 0);
				}
				break;

			case SDL_CONTROLLERDEVICEADDED:
				if (SDL_GameControllerOpen(ev.cdevice.which) != NULL) {
					if (push_callback(L, "on_controller_added")) {
						lua_pushinteger(L, ev.cdevice.which);
						lua_call(L, 1, 0);
					}
				}
				break;

			case SDL_CONTROLLERDEVICEREMOVED:
				if (push_callback(L, "on_controller_removed")) {
					lua_pushinteger(L, ev.cdevice.which);
					lua_call(L, 1, 0);
				}
				break;

			case SDL_CONTROLLERBUTTONDOWN:
				if (push_callback(L, "on_controller_down")) {
					lua_pushinteger(L, ev.cbutton.which);
					lua_pushstring(L, SDL_GameControllerGetStringForButton(ev.cbutton.button));
					lua_call(L, 2, 0);
				}
				break;

			case SDL_CONTROLLERBUTTONUP:
				if (push_callback(L, "on_controller_up")) {
					lua_pushinteger(L, ev.cbutton.which);
					lua_pushstring(L, SDL_GameControllerGetStringForButton(ev.cbutton.button));
					lua_call(L, 2, 0);
				}
				break;

			case SDL_CONTROLLERAXISMOTION:
				if (push_callback(L, "on_controller_moved")) {
					lua_pushinteger(L, ev.caxis.which);
					lua_pushstring(L, SDL_GameControllerGetStringForAxis(ev.caxis.axis));
					lua_pushnumber(L, (lua_Number)ev.caxis.value / 32768.0);
					lua_call(L, 3, 0);
				}
				break;
		}
	}
}


/*----------------------------------------------------------------------------*/
static void run_event_loop(lua_State *L) {
	Uint32 last_tick, current_tick, delta_ticks;

	if (push_callback(L, "on_init"))
		lua_call(L, 0, 0);

	last_tick = SDL_GetTicks();

	while (event_loop_running) {
		purge_voices(L);
		handle_SDL_events(L);

		current_tick = SDL_GetTicks();
		delta_ticks = current_tick - last_tick;
		last_tick = current_tick;

		if (push_callback(L, "on_update")) {
			lua_pushnumber(L, (lua_Number)delta_ticks / 1000.0);
			lua_call(L, 1, 0);
		}
		SDL_RenderPresent(renderer);
	}

	if (push_callback(L, "on_quit"))
		lua_call(L, 0, 0);
}


/*
================================================================================

		INIT / SHUTDOWN

================================================================================
*/
/*----------------------------------------------------------------------------*/
static int init_nox(lua_State *L) {
	int i;
	SDL_AudioSpec want, have;

	if (SDL_Init(SDL_INIT_EVERYTHING))
		luaL_error(L, "SDL_Init() failed: %s", SDL_GetError());

	if ((window = SDL_CreateWindow(WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE)) == NULL)
		luaL_error(L, "SDL_CreateWindow() failed: %s", SDL_GetError());
	if ((renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)) == NULL)
		luaL_error(L, "SDL_CreateRenderer() failed: %s", SDL_GetError());

	SDL_zero(want); SDL_zero(have);
	want.freq = AUDIO_FREQUENCY;
	want.format = AUDIO_F32SYS;
	want.channels = 2;
	want.samples = 1024 * 4;
	want.callback = mix_audio_voices;

	if ((audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0)) == 0)
		luaL_error(L, "SDL_OpenAudioDevice() failed: %s", SDL_GetError());
	if (have.format != AUDIO_F32SYS)
		luaL_error(L, "SDL_OpenAudioDevice() returned wrong audio format");
	if (have.channels != 2)
		luaL_error(L, "SDL_OpenAudioDevice() returned wrong number of channels");

	audio_freq = have.freq;
	for (i = 0; i < AUDIO_VOICES; ++i) {
		audio_voices[i].sample = NULL;
		audio_voices[i].sample_ref = LUA_NOREF;
	}

	SDL_PauseAudioDevice(audio_device, SDL_FALSE);

	if (luaL_loadfile(L, "test.lua") != LUA_OK)
		lua_error(L);
	lua_call(L, 0, 0);

	run_event_loop(L);

	return 0;
}


/*----------------------------------------------------------------------------*/
static void shutdown_nox() {
	if (audio_device != 0)
		SDL_CloseAudioDevice(audio_device);
	if (renderer != NULL)
		SDL_DestroyRenderer(renderer);
	if (window != NULL)
		SDL_DestroyWindow(window);

	SDL_Quit();
}


/*----------------------------------------------------------------------------*/
int main() {
	lua_State *L = luaL_newstate();

	luaL_openlibs(L);
	luaL_requiref(L, "nox", open_module_nox, 1);
	lua_pop(L, 1);

	lua_getglobal(L, "debug");
	lua_getfield(L, -1, "traceback");
	lua_remove(L, -2);

	lua_pushcfunction(L, init_nox);
	if (lua_pcall(L, 0, 0, -2) != LUA_OK) {
		fprintf(stderr, "%s\n", lua_tostring(L, -1));
	}

	SDL_PauseAudioDevice(audio_device, SDL_FALSE); /* stop audio thread before closing Lua state */

	lua_close(L);
	shutdown_nox();

	return 0;
}
