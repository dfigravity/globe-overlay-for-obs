/*
Globe Overlay for OBS — a check-in globe source.
Copyright (C) 2026 Vince Olshove

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

/*
 * How it works
 * ------------
 * The source owns a private obs-browser "browser_source" child and renders it.
 * The look (one of data/looks/<name>.html, single-file builds of the web overlay)
 * is loaded through obs-browser's own local-file scheme:
 *     http://absolute/<percent-encoded path>?settings=<json>&channel=<name>
 * The scheme handler opens the file from the URL *path* only, so the query
 * string reaches the page (verified against obs-browser/browser-scheme.cpp).
 * Settings changes that the page can apply live are pushed with the browser
 * source's "javascript_event" proc (a DOM CustomEvent "globeSettings" with the
 * JSON as event.detail). Structural changes (look, channel, ocean particles…)
 * rebuild the URL, which reloads the page; those are debounced in video_tick
 * so dragging a slider does not reload ten times a second.
 *
 * The property sheet is generated from globe-schema.h, which is generated from
 * the web project's tweaks-schema.js, so the OBS UI, the in-page panel and the
 * file settings block always list the same controls.
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <obs-module.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>
#include <plugin-support.h>
#include "globe-schema.h"

#define S_LOOK "look"
#define S_CHANNEL "channel"
#define S_WIDTH "width"
#define S_HEIGHT "height"
#define S_FPS "fps"
#define S_SHOW_PANEL "show_panel"
#define DEFAULT_LOOK "wind-ink"
#define URL_DEBOUNCE_NS (400ull * 1000000ull)

struct globe_source {
	obs_source_t *source;
	obs_source_t *browser;

	uint32_t width, height;
	int fps;
	char *look;
	char *channel;
	char *tweaks_json; /* last JSON built from settings */
	struct dstr url;   /* child's current URL */

	/* debounced URL rebuild (set on the UI thread, applied on the graphics thread) */
	pthread_mutex_t mutex;
	bool url_dirty;
	uint64_t dirty_at;

	bool showing;
	bool active;

	/* Randomize / Reset buttons: one context per settings group (+ one for "all", index GT_MAX_GROUPS) */
	struct group_btn {
		struct globe_source *s;
		int group;
	} btn[33];
};
#define GT_MAX_GROUPS 32

/* ---------- looks manifest (data/looks/manifest.json) ---------- */

static obs_data_t *manifest = NULL; /* { looks: [ {id,title,family,file}, ... ] } */

static void load_manifest(void)
{
	if (manifest)
		return;
	char *path = obs_module_file("looks/manifest.json");
	if (path) {
		manifest = obs_data_create_from_json_file(path);
		bfree(path);
	}
	if (!manifest) {
		obs_log(LOG_WARNING, "looks/manifest.json missing; only the default look is offered");
		manifest = obs_data_create();
	}
}

static const char *look_family(const char *look)
{
	static char family[64];
	family[0] = 0;
	load_manifest();
	obs_data_array_t *arr = obs_data_get_array(manifest, "looks");
	if (!arr)
		return "";
	size_t n = obs_data_array_count(arr);
	for (size_t i = 0; i < n; i++) {
		obs_data_t *l = obs_data_array_item(arr, i);
		if (strcmp(obs_data_get_string(l, "id"), look) == 0)
			snprintf(family, sizeof(family), "%s", obs_data_get_string(l, "family"));
		obs_data_release(l);
		if (family[0])
			break;
	}
	obs_data_array_release(arr);
	if (!family[0]) { /* fall back to the id prefix, same rule as lookOf() in the web code */
		const char *dash = strchr(look, '-');
		size_t len = dash ? (size_t)(dash - look) : strlen(look);
		snprintf(family, sizeof(family), "%.*s", (int)len, look);
	}
	return family;
}

/* ---------- helpers ---------- */

static bool item_is_color_auto_name(const struct gt_item *it, struct dstr *name)
{
	dstr_copy(name, it->setting);
	dstr_cat(name, "_auto");
	return it->type == GT_COLOR;
}

static void color_to_hex(long long c, char out[8])
{
	/* obs colours are 0xAABBGGRR */
	snprintf(out, 8, "#%02x%02x%02x", (unsigned)(c & 0xff), (unsigned)((c >> 8) & 0xff),
		 (unsigned)((c >> 16) & 0xff));
}

/* percent-encode for a URL path (keeps '/') or a query value */
static void url_encode(struct dstr *out, const char *s, bool keep_slash)
{
	static const char *hex = "0123456789ABCDEF";
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		unsigned char c = *p;
		bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
			  c == '_' || c == '.' || c == '~' || (keep_slash && c == '/');
		if (ok) {
			dstr_ncat(out, (const char *)&c, 1);
		} else {
			char buf[4] = {'%', hex[c >> 4], hex[c & 15], 0};
			dstr_cat(out, buf);
		}
	}
}

/* nested obs_data setters for "a.b" keys */
static obs_data_t *nested_parent(obs_data_t *root, const char *key, const char **leaf)
{
	const char *dot = strchr(key, '.');
	if (!dot) {
		*leaf = key;
		obs_data_addref(root);
		return root;
	}
	char head[64];
	snprintf(head, sizeof(head), "%.*s", (int)(dot - key), key);
	obs_data_t *obj = obs_data_get_obj(root, head);
	if (!obj) {
		obj = obs_data_create();
		obs_data_set_obj(root, head, obj);
	}
	*leaf = dot + 1;
	return obj; /* caller releases */
}

/* Build the page's tweaks JSON from the OBS settings. */
static char *build_tweaks_json(obs_data_t *settings)
{
	obs_data_t *root = obs_data_create();
	struct dstr autoname = {0};
	for (size_t i = 0; i < GT_COUNT; i++) {
		const struct gt_item *it = &GT_ITEMS[i];
		const char *leaf;
		obs_data_t *parent = nested_parent(root, it->key, &leaf);
		switch (it->type) {
		case GT_RANGE:
			obs_data_set_double(parent, leaf, obs_data_get_double(settings, it->setting));
			break;
		case GT_TOGGLE:
			obs_data_set_bool(parent, leaf, obs_data_get_bool(settings, it->setting));
			break;
		case GT_COLOR: {
			item_is_color_auto_name(it, &autoname);
			if (obs_data_get_bool(settings, autoname.array)) {
				obs_data_set_string(parent, leaf, "");
			} else {
				char hex[8];
				color_to_hex(obs_data_get_int(settings, it->setting), hex);
				obs_data_set_string(parent, leaf, hex);
			}
			break;
		}
		case GT_SELECT:
			if (it->int_options)
				obs_data_set_int(parent, leaf, obs_data_get_int(settings, it->setting));
			else
				obs_data_set_string(parent, leaf, obs_data_get_string(settings, it->setting));
			break;
		case GT_TEXT:
			obs_data_set_string(parent, leaf, obs_data_get_string(settings, it->setting));
			break;
		}
		obs_data_release(parent);
	}
	dstr_free(&autoname);
	char *json = bstrdup(obs_data_get_json(root));
	obs_data_release(root);
	return json;
}

static void build_url(struct globe_source *s)
{
	struct dstr file = {0};
	dstr_printf(&file, "looks/%s.html", s->look);
	char *path = obs_module_file(file.array);
	dstr_free(&file);
	dstr_copy(&s->url, "http://absolute/");
	if (path) {
		/* obs-browser does CefURIEncode(path) on "local file" mode; mirror that. Windows paths use '/' too. */
		for (char *p = path; *p; p++)
			if (*p == '\\')
				*p = '/';
		url_encode(&s->url, path, true);
		bfree(path);
	} else {
		obs_log(LOG_ERROR, "look '%s' not found in plugin data", s->look);
	}
	dstr_cat(&s->url, "?settings=");
	url_encode(&s->url, s->tweaks_json ? s->tweaks_json : "{}", false);
	if (s->channel && *s->channel) {
		dstr_cat(&s->url, "&channel=");
		url_encode(&s->url, s->channel, false);
	}
	/* OBS's property sheet is the UI; never draw the in-page panel or its gear in the source */
	dstr_cat(&s->url, "&nopanel=1");
}

static void apply_child_settings(struct globe_source *s, bool with_url)
{
	if (!s->browser)
		return;
	obs_data_t *cs = obs_source_get_settings(s->browser);
	obs_data_set_bool(cs, "is_local_file", false);
	if (with_url)
		obs_data_set_string(cs, "url", s->url.array);
	obs_data_set_int(cs, "width", s->width);
	obs_data_set_int(cs, "height", s->height);
	obs_data_set_bool(cs, "fps_custom", true);
	obs_data_set_int(cs, "fps", s->fps);
	obs_data_set_bool(cs, "shutdown", false);
	obs_data_set_bool(cs, "restart_when_active", false);
	obs_data_set_bool(cs, "reroute_audio", false);
	/* body transparent like the stock browser source; the pages are transparent themselves */
	obs_data_set_string(cs, "css",
			    "body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }");
	obs_source_update(s->browser, cs);
	obs_data_release(cs);
}

static void push_js_event(struct globe_source *s, const char *name, const char *json)
{
	if (!s->browser)
		return;
	proc_handler_t *ph = obs_source_get_proc_handler(s->browser);
	if (!ph) {
		obs_log(LOG_WARNING, "no proc handler on browser child");
		return;
	}
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_string(&cd, "eventName", name);
	calldata_set_string(&cd, "jsonString", json ? json : "{}");
	bool ok = proc_handler_call(ph, "javascript_event", &cd);
	if (!ok)
		obs_log(LOG_WARNING, "javascript_event proc not found on browser child");
	calldata_free(&cd);
}

/* ---------- obs_source_info callbacks ---------- */

static const char *globe_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("GlobeOverlay");
}

static void globe_update(void *data, obs_data_t *settings);

static void *globe_create(obs_data_t *settings, obs_source_t *source)
{
	struct globe_source *s = bzalloc(sizeof(*s));
	s->source = source;
	pthread_mutex_init(&s->mutex, NULL);
	dstr_init(&s->url);

	s->browser = obs_source_create_private("browser_source", obs_source_get_name(source), NULL);
	if (!s->browser)
		obs_log(LOG_ERROR, "could not create browser_source child; is the obs-browser plugin installed?");

	globe_update(s, settings);
	return s;
}

static void globe_destroy(void *data)
{
	struct globe_source *s = data;
	if (s->browser) {
		if (s->active)
			obs_source_remove_active_child(s->source, s->browser);
		if (s->showing)
			obs_source_dec_showing(s->browser);
		obs_source_release(s->browser);
	}
	bfree(s->look);
	bfree(s->channel);
	bfree(s->tweaks_json);
	dstr_free(&s->url);
	pthread_mutex_destroy(&s->mutex);
	bfree(s);
}

static void globe_update(void *data, obs_data_t *settings)
{
	struct globe_source *s = data;

	const char *look = obs_data_get_string(settings, S_LOOK);
	/* one Channel field in the sheet: scenes saved before may hold the link in the schema's (now hidden) field — move it once */
	const char *old_link = obs_data_get_string(settings, "gt_channel");
	if (old_link && *old_link) {
		const char *cur = obs_data_get_string(settings, S_CHANNEL);
		if (!cur || !*cur)
			obs_data_set_string(settings, S_CHANNEL, old_link);
		obs_data_unset_user_value(settings, "gt_channel");
	}
	const char *channel = obs_data_get_string(settings, S_CHANNEL);
	uint32_t width = (uint32_t)obs_data_get_int(settings, S_WIDTH);
	uint32_t height = (uint32_t)obs_data_get_int(settings, S_HEIGHT);
	int fps = (int)obs_data_get_int(settings, S_FPS);
	if (!look || !*look)
		look = DEFAULT_LOOK;

	bool first = s->look == NULL;
	bool structural = first || strcmp(look, s->look) != 0 ||
			  strcmp(channel ? channel : "", s->channel ? s->channel : "") != 0;
	bool size_changed = width != s->width || height != s->height || fps != s->fps;

	char *json = build_tweaks_json(settings);
	bool json_changed = !s->tweaks_json || strcmp(json, s->tweaks_json) != 0;

	bool live_only = !structural;
	if (json_changed && !structural) {
		/* decide with the schema: any non-live item differing between the previous JSON and the new one? */
		obs_data_t *prev = s->tweaks_json ? obs_data_create_from_json(s->tweaks_json) : obs_data_create();
		obs_data_t *next = obs_data_create_from_json(json);
		for (size_t i = 0; i < GT_COUNT && live_only; i++) {
			const struct gt_item *it = &GT_ITEMS[i];
			if (it->live)
				continue;
			const char *leaf;
			obs_data_t *pa = nested_parent(prev, it->key, &leaf);
			obs_data_t *pb = nested_parent(next, it->key, &leaf);
			obs_data_t *tmp_a = obs_data_create(), *tmp_b = obs_data_create();
			/* compare via JSON of the single leaf value */
			obs_data_item_t *ia = obs_data_item_byname(pa, leaf), *ib = obs_data_item_byname(pb, leaf);
			const char *ja = "", *jb = "";
			if (ia) {
				switch (obs_data_item_gettype(ia)) {
				case OBS_DATA_NUMBER:
					obs_data_set_double(tmp_a, "v", obs_data_item_get_double(ia));
					break;
				case OBS_DATA_BOOLEAN:
					obs_data_set_bool(tmp_a, "v", obs_data_item_get_bool(ia));
					break;
				case OBS_DATA_STRING:
					obs_data_set_string(tmp_a, "v", obs_data_item_get_string(ia));
					break;
				default:
					break;
				}
				obs_data_item_release(&ia);
			}
			if (ib) {
				switch (obs_data_item_gettype(ib)) {
				case OBS_DATA_NUMBER:
					obs_data_set_double(tmp_b, "v", obs_data_item_get_double(ib));
					break;
				case OBS_DATA_BOOLEAN:
					obs_data_set_bool(tmp_b, "v", obs_data_item_get_bool(ib));
					break;
				case OBS_DATA_STRING:
					obs_data_set_string(tmp_b, "v", obs_data_item_get_string(ib));
					break;
				default:
					break;
				}
				obs_data_item_release(&ib);
			}
			ja = obs_data_get_json(tmp_a);
			jb = obs_data_get_json(tmp_b);
			if (strcmp(ja, jb) != 0)
				live_only = false;
			obs_data_release(tmp_a);
			obs_data_release(tmp_b);
			obs_data_release(pa);
			obs_data_release(pb);
		}
		obs_data_release(prev);
		obs_data_release(next);
	}

	bfree(s->look);
	s->look = bstrdup(look);
	bfree(s->channel);
	s->channel = bstrdup(channel ? channel : "");
	bfree(s->tweaks_json);
	s->tweaks_json = json;
	s->width = width ? width : 1920;
	s->height = height ? height : 1080;
	s->fps = fps > 0 ? fps : 60;

	if (first) {
		build_url(s);
		apply_child_settings(s, true);
		obs_log(LOG_INFO, "update: first load, look=%s, url %zu chars", s->look, s->url.len);
		return;
	}
	if (size_changed)
		apply_child_settings(s, false);

	obs_log(LOG_INFO, "update: json_changed=%d structural=%d live_only=%d size_changed=%d", json_changed,
		structural, live_only, size_changed);
	if (structural || (json_changed && !live_only)) {
		/* debounce: dragging a structural slider rebuilds the URL once it settles */
		pthread_mutex_lock(&s->mutex);
		s->url_dirty = true;
		s->dirty_at = os_gettime_ns();
		pthread_mutex_unlock(&s->mutex);
	} else if (json_changed) {
		obs_log(LOG_INFO, "update: pushing live settings (%zu bytes)", strlen(json));
		push_js_event(s, "globeSettings", json);
	}
}

static void globe_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct globe_source *s = data;
	bool apply = false;
	pthread_mutex_lock(&s->mutex);
	if (s->url_dirty && os_gettime_ns() - s->dirty_at > URL_DEBOUNCE_NS) {
		s->url_dirty = false;
		apply = true;
	}
	pthread_mutex_unlock(&s->mutex);
	if (apply) {
		/* NOTE: obs_source_update on the child from the graphics thread. obs-browser's Update()
		 * only posts work to the CEF thread, and other plugins (source-clone, browser-transition)
		 * do the same; verify in the test build that no deadlock occurs with the rendering lock. */
		build_url(s);
		apply_child_settings(s, true);
		obs_log(LOG_INFO, "tick: rebuilt url (%zu chars) after structural change", s->url.len);
	}
}

static void globe_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct globe_source *s = data;
	if (s->browser)
		obs_source_video_render(s->browser);
}

static uint32_t globe_get_width(void *data)
{
	return ((struct globe_source *)data)->width;
}

static uint32_t globe_get_height(void *data)
{
	return ((struct globe_source *)data)->height;
}

static void globe_activate(void *data)
{
	struct globe_source *s = data;
	if (s->browser && !s->active) {
		obs_source_add_active_child(s->source, s->browser);
		s->active = true;
	}
}

static void globe_deactivate(void *data)
{
	struct globe_source *s = data;
	if (s->browser && s->active) {
		obs_source_remove_active_child(s->source, s->browser);
		s->active = false;
	}
}

static void globe_show(void *data)
{
	struct globe_source *s = data;
	if (s->browser && !s->showing) {
		obs_source_inc_showing(s->browser);
		s->showing = true;
	}
}

static void globe_hide(void *data)
{
	struct globe_source *s = data;
	if (s->browser && s->showing) {
		obs_source_dec_showing(s->browser);
		s->showing = false;
	}
}

static void globe_enum_active_sources(void *data, obs_source_enum_proc_t cb, void *param)
{
	struct globe_source *s = data;
	if (s->browser)
		cb(s->source, s->browser, param);
}

/* Interact window → forward to the page (so the in-page ⚙ panel works) */
static void globe_mouse_click(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up,
			      uint32_t click_count)
{
	struct globe_source *s = data;
	if (s->browser)
		obs_source_send_mouse_click(s->browser, event, type, mouse_up, click_count);
}

static void globe_mouse_move(void *data, const struct obs_mouse_event *event, bool mouse_leave)
{
	struct globe_source *s = data;
	if (s->browser)
		obs_source_send_mouse_move(s->browser, event, mouse_leave);
}

static void globe_mouse_wheel(void *data, const struct obs_mouse_event *event, int x_delta, int y_delta)
{
	struct globe_source *s = data;
	if (s->browser)
		obs_source_send_mouse_wheel(s->browser, event, x_delta, y_delta);
}

static void globe_focus(void *data, bool focus)
{
	struct globe_source *s = data;
	if (s->browser)
		obs_source_send_focus(s->browser, focus);
}

static void globe_key_click(void *data, const struct obs_key_event *event, bool key_up)
{
	struct globe_source *s = data;
	if (s->browser)
		obs_source_send_key_click(s->browser, event, key_up);
}

/* ---------- defaults & properties ---------- */

static void globe_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, S_LOOK, DEFAULT_LOOK);
	obs_data_set_default_string(settings, S_CHANNEL, "");
	obs_data_set_default_int(settings, S_WIDTH, 1920);
	obs_data_set_default_int(settings, S_HEIGHT, 1080);
	obs_data_set_default_int(settings, S_FPS, 60);
	struct dstr autoname = {0};
	for (size_t i = 0; i < GT_COUNT; i++) {
		const struct gt_item *it = &GT_ITEMS[i];
		switch (it->type) {
		case GT_RANGE:
			obs_data_set_default_double(settings, it->setting, it->def_num);
			break;
		case GT_TOGGLE:
			obs_data_set_default_bool(settings, it->setting, it->def_bool);
			break;
		case GT_COLOR:
			item_is_color_auto_name(it, &autoname);
			obs_data_set_default_bool(settings, autoname.array, it->def_str[0] == 0);
			obs_data_set_default_int(settings, it->setting, 0xFFFFFFFF);
			break;
		case GT_SELECT:
			if (it->int_options)
				obs_data_set_default_int(settings, it->setting, (long long)it->def_num);
			else
				obs_data_set_default_string(settings, it->setting, it->def_str);
			break;
		case GT_TEXT:
			obs_data_set_default_string(settings, it->setting, it->def_str);
			break;
		}
	}
	dstr_free(&autoname);
}

static bool refresh_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct globe_source *s = data;
	if (s && s->browser) {
		build_url(s);
		apply_child_settings(s, true);
	}
	return false;
}

/* ---------- Randomize / Reset ---------- */
static double rnd01(void)
{
	return (double)rand() / ((double)RAND_MAX + 1.0);
}

/* items a Randomize/Reset button acts on: its group (or every group for -1), never the link or Invert */
static bool button_targets(const struct gt_item *it, int group)
{
	if (group >= 0 && it->group_index != group)
		return false;
	return strcmp(it->key, "channel") != 0 && strcmp(it->key, "invert") != 0 && it->type != GT_TEXT;
}

static void apply_and_refresh(struct globe_source *s, obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
	obs_source_update(s->source,
			  NULL); /* settings were edited in place; this runs globe_update → live push / URL rebuild */
}

static bool randomize_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct group_btn *b = data;
	if (!b || !b->s)
		return false;
	static bool seeded = false;
	if (!seeded) {
		srand((unsigned)os_gettime_ns());
		seeded = true;
	}
	obs_data_t *settings = obs_source_get_settings(b->s->source);
	struct dstr autoname = {0};
	for (size_t i = 0; i < GT_COUNT; i++) {
		const struct gt_item *it = &GT_ITEMS[i];
		if (!button_targets(it, b->group))
			continue;
		switch (it->type) {
		case GT_RANGE: {
			/* biased toward the default: up to 70% of the way to either end, so results stay usable */
			double u = rnd01(), d = it->def_num;
			double v = d + (u < 0.5 ? (it->min - d) : (it->max - d)) * fabs(2.0 * u - 1.0) * 0.7;
			if (it->step > 0)
				v = it->min + floor((v - it->min) / it->step + 0.5) * it->step;
			obs_data_set_double(settings, it->setting, fmin(it->max, fmax(it->min, v)));
			break;
		}
		case GT_TOGGLE:
			obs_data_set_bool(settings, it->setting, rnd01() < 0.6 ? it->def_bool : !it->def_bool);
			break;
		case GT_COLOR: {
			/* a vivid colour: random hue, high saturation and value (OBS stores 0xAABBGGRR) */
			double h = rnd01() * 6.0, sat = 0.55 + 0.45 * rnd01(), val = 0.8 + 0.2 * rnd01();
			double c = val * sat, x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0)), m = val - c, r = 0, g = 0,
			       bl = 0;
			int hi = (int)h;
			if (hi == 0) {
				r = c;
				g = x;
			} else if (hi == 1) {
				r = x;
				g = c;
			} else if (hi == 2) {
				g = c;
				bl = x;
			} else if (hi == 3) {
				g = x;
				bl = c;
			} else if (hi == 4) {
				r = x;
				bl = c;
			} else {
				r = c;
				bl = x;
			}
			long long col = 0xFF000000LL | ((long long)((bl + m) * 255) << 16) |
					((long long)((g + m) * 255) << 8) | (long long)((r + m) * 255);
			obs_data_set_int(settings, it->setting, col);
			item_is_color_auto_name(it, &autoname);
			obs_data_set_bool(settings, autoname.array, rnd01() < 0.3); /* sometimes keep the look's own */
			break;
		}
		case GT_SELECT: {
			int n = 1;
			for (const char *c = it->options; *c; c++)
				n += *c == '|';
			int pick = (int)(rnd01() * n), idx = 0;
			const char *cur = it->options;
			while (idx < pick && cur) {
				cur = strchr(cur, '|');
				if (cur)
					cur++;
				idx++;
			}
			if (!cur)
				break;
			const char *bar = strchr(cur, '|');
			char val[48];
			snprintf(val, sizeof(val), "%.*s", (int)(bar ? (size_t)(bar - cur) : strlen(cur)), cur);
			if (it->int_options)
				obs_data_set_int(settings, it->setting, atoll(val));
			else
				obs_data_set_string(settings, it->setting, val);
			break;
		}
		default:
			break;
		}
	}
	dstr_free(&autoname);
	apply_and_refresh(b->s, settings);
	obs_data_release(settings);
	return true; /* redraw the sheet so the sliders show the new values */
}

static bool reset_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct group_btn *b = data;
	if (!b || !b->s)
		return false;
	obs_data_t *settings = obs_source_get_settings(b->s->source);
	struct dstr autoname = {0};
	for (size_t i = 0; i < GT_COUNT; i++) {
		const struct gt_item *it = &GT_ITEMS[i];
		if (!button_targets(it, b->group) && !(b->group < 0 && strcmp(it->key, "invert") == 0))
			continue;
		obs_data_unset_user_value(settings, it->setting);
		if (it->type == GT_COLOR) {
			item_is_color_auto_name(it, &autoname);
			obs_data_unset_user_value(settings, autoname.array);
		}
	}
	dstr_free(&autoname);
	apply_and_refresh(b->s, settings);
	obs_data_release(settings);
	return true;
}

/* picking a colour switches off its "use the look's own colour" box, so the picker just works */
static bool color_modified(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	const struct gt_item *it = priv;
	struct dstr autoname = {0};
	item_is_color_auto_name(it, &autoname);
	/* the picker starts at white (0xFFFFFFFF); any other value means the user chose a colour */
	if (obs_data_get_int(settings, it->setting) != 0xFFFFFFFF && obs_data_get_bool(settings, autoname.array))
		obs_data_set_bool(settings, autoname.array, false);
	dstr_free(&autoname);
	UNUSED_PARAMETER(p);
	return true;
}

/* show/hide family-specific groups when the look changes */
static bool look_modified(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(p);
	const char *family = look_family(obs_data_get_string(settings, S_LOOK));
	char name[32];
	for (int g = 0; g < GT_GROUP_COUNT; g++) {
		const char *looks = "";
		for (size_t i = 0; i < GT_COUNT; i++)
			if (GT_ITEMS[i].group_index == g) {
				looks = GT_ITEMS[i].looks;
				break;
			}
		snprintf(name, sizeof(name), "grp_%d", g);
		obs_property_t *grp = obs_properties_get(props, name);
		if (!grp)
			continue;
		bool visible = looks[0] == 0;
		if (!visible) { /* "a|b" list */
			const char *cur = looks;
			while (*cur && !visible) {
				const char *bar = strchr(cur, '|');
				size_t len = bar ? (size_t)(bar - cur) : strlen(cur);
				visible = strlen(family) == len && strncmp(family, cur, len) == 0;
				cur = bar ? bar + 1 : cur + len;
			}
		}
		obs_property_set_visible(grp, visible);
	}
	return true;
}

static void split_iter(const char *list, void (*fn)(const char *tok, size_t len, int idx, void *ctx), void *ctx)
{
	int idx = 0;
	const char *cur = list;
	while (cur && *cur) {
		const char *bar = strchr(cur, '|');
		size_t len = bar ? (size_t)(bar - cur) : strlen(cur);
		fn(cur, len, idx++, ctx);
		cur = bar ? bar + 1 : cur + len;
	}
}

struct opt_ctx {
	obs_property_t *list;
	const struct gt_item *it;
	char labels[16][48];
	int nlabels;
};
static void collect_label(const char *tok, size_t len, int idx, void *ctx)
{
	struct opt_ctx *c = ctx;
	if (idx < 16) {
		snprintf(c->labels[idx], sizeof(c->labels[idx]), "%.*s", (int)len, tok);
		c->nlabels = idx + 1;
	}
}
static void add_option(const char *tok, size_t len, int idx, void *ctx)
{
	struct opt_ctx *c = ctx;
	char val[48];
	snprintf(val, sizeof(val), "%.*s", (int)len, tok);
	const char *label = idx < c->nlabels && c->labels[idx][0] ? c->labels[idx] : val;
	if (c->it->int_options)
		obs_property_list_add_int(c->list, label, atoll(val));
	else
		obs_property_list_add_string(c->list, label, val);
}

static obs_properties_t *globe_get_properties(void *data)
{
	struct globe_source *s = data;
	obs_properties_t *props = obs_properties_create();

	/* Look picker from the manifest */
	obs_property_t *look = obs_properties_add_list(props, S_LOOK, obs_module_text("Look"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	load_manifest();
	obs_data_array_t *arr = obs_data_get_array(manifest, "looks");
	size_t n = arr ? obs_data_array_count(arr) : 0;
	for (size_t i = 0; i < n; i++) {
		obs_data_t *l = obs_data_array_item(arr, i);
		obs_property_list_add_string(look, obs_data_get_string(l, "title"), obs_data_get_string(l, "id"));
		obs_data_release(l);
	}
	if (arr)
		obs_data_array_release(arr);
	if (!n)
		obs_property_list_add_string(look, "Ink on Paper", DEFAULT_LOOK);
	obs_property_set_modified_callback2(look, look_modified, NULL);

	obs_property_t *ch = obs_properties_add_text(props, S_CHANNEL, obs_module_text("Channel"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(ch, obs_module_text("Channel.Help"));

	obs_properties_add_int(props, S_WIDTH, obs_module_text("Width"), 320, 7680, 1);
	obs_properties_add_int(props, S_HEIGHT, obs_module_text("Height"), 180, 4320, 1);
	obs_properties_add_int_slider(props, S_FPS, obs_module_text("FPS"), 15, 60, 1);

	/* Generated groups */
	struct dstr autoname = {0};
	for (int g = 0; g < GT_GROUP_COUNT; g++) {
		obs_properties_t *gp = obs_properties_create();
		const char *title = "";
		int n_items = 0;
		for (size_t i = 0; i < GT_COUNT; i++) {
			const struct gt_item *it = &GT_ITEMS[i];
			if (it->group_index != g)
				continue;
			if (strcmp(it->key, "channel") == 0)
				continue; /* the top-level Channel field is the one place for the link */
			title = it->group;
			n_items++;
			obs_property_t *p = NULL;
			switch (it->type) {
			case GT_RANGE:
				p = obs_properties_add_float_slider(gp, it->setting, it->label, it->min, it->max,
								    it->step);
				break;
			case GT_TOGGLE:
				p = obs_properties_add_bool(gp, it->setting, it->label);
				break;
			case GT_COLOR: {
				item_is_color_auto_name(it, &autoname);
				p = obs_properties_add_color(gp, it->setting, it->label);
				obs_property_set_modified_callback2(p, color_modified, (void *)it);
				struct dstr lbl = {0};
				dstr_printf(&lbl, "%s: %s", it->label, obs_module_text("UseLookColour"));
				obs_properties_add_bool(gp, autoname.array, lbl.array);
				dstr_free(&lbl);
				break;
			}
			case GT_SELECT: {
				p = obs_properties_add_list(gp, it->setting, it->label, OBS_COMBO_TYPE_LIST,
							    it->int_options ? OBS_COMBO_FORMAT_INT
									    : OBS_COMBO_FORMAT_STRING);
				struct opt_ctx ctx = {.list = p, .it = it, .nlabels = 0};
				split_iter(it->labels, collect_label, &ctx);
				split_iter(it->options, add_option, &ctx);
				break;
			}
			case GT_TEXT:
				p = obs_properties_add_text(gp, it->setting, it->label, OBS_TEXT_DEFAULT);
				break;
			}
			if (p && it->note && it->note[0])
				obs_property_set_long_description(p, it->note);
		}
		if (!n_items) {
			obs_properties_destroy(gp);
			continue;
		}
		bool has_targets = false; /* only groups a button can change get Randomize / Reset */
		for (size_t i = 0; i < GT_COUNT && !has_targets; i++)
			has_targets = button_targets(&GT_ITEMS[i], g);
		if (s && g < GT_MAX_GROUPS && has_targets) {
			char bn[40];
			s->btn[g] = (struct group_btn){s, g};
			snprintf(bn, sizeof(bn), "rand_%d", g);
			obs_properties_add_button2(gp, bn, obs_module_text("Randomize"), randomize_clicked, &s->btn[g]);
			snprintf(bn, sizeof(bn), "reset_%d", g);
			obs_properties_add_button2(gp, bn, obs_module_text("Reset"), reset_clicked, &s->btn[g]);
		}
		char name[32];
		snprintf(name, sizeof(name), "grp_%d", g);
		obs_properties_add_group(props, name, title, OBS_GROUP_NORMAL, gp);
	}
	dstr_free(&autoname);

	if (s) {
		s->btn[GT_MAX_GROUPS] = (struct group_btn){s, -1};
		obs_properties_add_button2(props, "rand_all", obs_module_text("RandomizeAll"), randomize_clicked,
					   &s->btn[GT_MAX_GROUPS]);
		obs_properties_add_button2(props, "reset_all", obs_module_text("ResetAll"), reset_clicked,
					   &s->btn[GT_MAX_GROUPS]);
	}
	obs_properties_add_button2(props, "refresh", obs_module_text("Refresh"), refresh_clicked, s);

	/* apply family visibility for the current look */
	if (s) {
		obs_data_t *cur = obs_source_get_settings(s->source);
		look_modified(NULL, props, look, cur);
		obs_data_release(cur);
	}
	return props;
}

struct obs_source_info globe_source_info = {
	.id = "globe_overlay_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION |
			OBS_SOURCE_DO_NOT_DUPLICATE,
	.get_name = globe_get_name,
	.create = globe_create,
	.destroy = globe_destroy,
	.update = globe_update,
	.get_defaults = globe_get_defaults,
	.get_properties = globe_get_properties,
	.get_width = globe_get_width,
	.get_height = globe_get_height,
	.video_tick = globe_video_tick,
	.video_render = globe_video_render,
	.activate = globe_activate,
	.deactivate = globe_deactivate,
	.show = globe_show,
	.hide = globe_hide,
	.enum_active_sources = globe_enum_active_sources,
	.mouse_click = globe_mouse_click,
	.mouse_move = globe_mouse_move,
	.mouse_wheel = globe_mouse_wheel,
	.focus = globe_focus,
	.key_click = globe_key_click,
	.icon_type = OBS_ICON_TYPE_BROWSER,
};

void globe_source_free_module_data(void)
{
	if (manifest) {
		obs_data_release(manifest);
		manifest = NULL;
	}
}
