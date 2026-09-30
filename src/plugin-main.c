/*
Globe Overlay for OBS
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

#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern struct obs_source_info globe_source_info;
extern void globe_source_free_module_data(void);

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Live viewer check-in globe overlay with selectable looks (Triode check-in)";
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

/* Register after all modules loaded so obs-browser is guaranteed to be there. */
void obs_module_post_load(void)
{
	if (!obs_is_source_configurable("browser_source")) {
		obs_log(LOG_ERROR, "obs-browser (browser_source) is not available; Globe Overlay source not registered");
		return;
	}
	obs_register_source(&globe_source_info);
	obs_log(LOG_INFO, "registered source '%s'", globe_source_info.id);
}

void obs_module_unload(void)
{
	globe_source_free_module_data();
	obs_log(LOG_INFO, "plugin unloaded");
}
