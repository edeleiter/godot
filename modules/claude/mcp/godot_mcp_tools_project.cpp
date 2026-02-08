/**************************************************************************/
/*  godot_mcp_tools_project.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "godot_mcp_server.h"

#include "core/config/project_settings.h"

Dictionary GodotMCPServer::_tool_project_settings(const Dictionary &p_args) {
	String action = p_args.get("action", "");

	if (action.is_empty()) {
		return _error_result("Missing required 'action' parameter. Use: 'get', 'set', or 'list'");
	}

	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (!ps) {
		return _error_result("ProjectSettings not available");
	}

	if (action == "get") {
		String setting = p_args.get("setting", "");
		if (setting.is_empty()) {
			return _error_result("'get' action requires 'setting' parameter");
		}

		if (!ps->has_setting(setting)) {
			return _error_result("Setting not found: " + setting);
		}

		Variant value = ps->get_setting(setting);
		return _success_result("Setting retrieved",
				Dictionary{ { "setting", setting }, { "value", value },
						{ "type", Variant::get_type_name(value.get_type()) } });

	} else if (action == "set") {
		String setting = p_args.get("setting", "");
		if (setting.is_empty()) {
			return _error_result("'set' action requires 'setting' parameter");
		}

		if (!p_args.has("value")) {
			return _error_result("'set' action requires 'value' parameter");
		}

		Variant value = p_args["value"];

		// If the setting already exists, coerce value to match the existing type.
		if (ps->has_setting(setting)) {
			Variant existing = ps->get_setting(setting);
			if (existing.get_type() != Variant::NIL) {
				value = _coerce_value(value, existing.get_type());
			}
		}

		ps->set_setting(setting, value);
		Error err = ps->save();
		if (err != OK) {
			return _error_result("Setting updated but failed to save project.godot (error " + itos(err) + ")");
		}

		// Check if the setting requires a restart.
		bool needs_restart = setting.begins_with("rendering/") ||
				setting.begins_with("display/window/size/") ||
				setting.begins_with("application/config/");

		Dictionary result;
		result["setting"] = setting;
		result["value"] = value;
		if (needs_restart) {
			result["note"] = "This setting may require an editor restart to take full effect.";
		}
		return _success_result("Setting updated: " + setting, result);

	} else if (action == "list") {
		String prefix = p_args.get("prefix", "");

		// Common settings categories to show when no prefix is given.
		static const char *common_prefixes[] = {
			"application/config/",
			"display/window/size/",
			"physics/2d/",
			"physics/3d/",
			"rendering/renderer/",
			"rendering/environment/",
			"input/",
			nullptr
		};

		Array settings_list;
		List<PropertyInfo> props;
		ps->get_property_list(&props);

		for (const PropertyInfo &pi : props) {
			if (pi.name.begins_with("_") || pi.name.is_empty()) {
				continue;
			}

			bool include = false;
			if (!prefix.is_empty()) {
				include = pi.name.begins_with(prefix);
			} else {
				for (int i = 0; common_prefixes[i]; i++) {
					if (pi.name.begins_with(common_prefixes[i])) {
						include = true;
						break;
					}
				}
			}

			if (include) {
				Dictionary entry;
				entry["name"] = pi.name;
				entry["type"] = Variant::get_type_name(pi.type);
				entry["value"] = ps->get_setting(pi.name);
				settings_list.push_back(entry);
			}
		}

		return _success_result("Listed " + itos(settings_list.size()) + " settings",
				Dictionary{ { "settings", settings_list }, { "count", settings_list.size() },
						{ "prefix", prefix.is_empty() ? String("(common)") : prefix } });

	} else {
		return _error_result("Unknown action: " + action + ". Use: 'get', 'set', or 'list'");
	}
}
