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
#include "SDL.h"


/*
================================================================================

		DEFINES & TYPES

================================================================================
*/
/*----------------------------------------------------------------------------*/
#define WINDOW_TITLE		"Nox Window"
#define WINDOW_WIDTH		1280
#define WINDOW_HEIGHT		720
#define WINDOW_PADDING		64


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
	if (lua_getfield(L, -1, name) != LUA_TTABLE) {
		lua_pop(L, 2);
		return 0;
	}
	lua_remove(L, -2);
	return -1;
}


/*
================================================================================

		LUA API

================================================================================
*/
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
static const luaL_Reg nox_video__funcs[] = {
	{ "get_blend_mode", f_nox_video_get_blend_mode },
	{ "set_blend_mode", f_nox_video_set_blend_mode },
	{ "get_draw_color", f_nox_video_get_draw_color },
	{ "set_draw_color", f_nox_video_set_draw_color },
	{ NULL, NULL }
};


/*----------------------------------------------------------------------------*/
static int open_module_nox_video(lua_State *L) {
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

	luaL_requiref(L, "nox.events", open_module_nox_events, 0);
	lua_setfield(L, -2, "events");

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
	if (SDL_Init(SDL_INIT_EVERYTHING))
		luaL_error(L, "SDL_Init() failed: %s", SDL_GetError());

	if ((window = SDL_CreateWindow(WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE)) == NULL)
		luaL_error(L, "SDL_CreateWindow() failed: %s", SDL_GetError());
	if ((renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)) == NULL)
		luaL_error(L, "SDL_CreateRenderer() failed: %s", SDL_GetError());

	return 0;
}


/*----------------------------------------------------------------------------*/
static void shutdown_nox() {
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

	lua_close(L);
	shutdown_nox();

	return 0;
}
