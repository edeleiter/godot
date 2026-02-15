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
#include "core/input/input_event.h"
#include "core/input/input_map.h"
#include "core/os/keyboard.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_undo_redo_manager.h"
#endif

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


Dictionary GodotMCPServer::_tool_input_map(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String action = p_args.get("action", "");

	if (action.is_empty()) {
		return _error_result("Missing required 'action' parameter. Use: 'list', 'add_action', 'remove_action', 'add_binding', or 'remove_binding'");
	}

	InputMap *im = InputMap::get_singleton();
	if (!im) {
		return _error_result("InputMap not available");
	}

	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (!ps) {
		return _error_result("ProjectSettings not available");
	}

	if (action == "list") {
		Array actions_out;
		List<PropertyInfo> pinfo;
		ps->get_property_list(&pinfo);

		for (const PropertyInfo &pi : pinfo) {
			if (!pi.name.begins_with("input/")) {
				continue;
			}

			String action_name = pi.name.substr(6); // Remove "input/"
			// Skip built-in UI actions.
			if (action_name.begins_with("ui_")) {
				continue;
			}

			Dictionary action_info;
			action_info["name"] = action_name;

			Dictionary action_dict = ps->get_setting(pi.name);
			if (action_dict.has("deadzone")) {
				action_info["deadzone"] = action_dict["deadzone"];
			}

			Array bindings;
			if (action_dict.has("events")) {
				Array events = action_dict["events"];
				for (int i = 0; i < events.size(); i++) {
					Ref<InputEvent> event = events[i];
					if (event.is_null()) {
						continue;
					}

					Dictionary binding;
					Ref<InputEventKey> key = event;
					Ref<InputEventMouseButton> mouse = event;
					Ref<InputEventJoypadButton> joy_btn = event;
					Ref<InputEventJoypadMotion> joy_axis = event;

					if (key.is_valid()) {
						binding["type"] = "key";
						Key kc = key->get_physical_keycode();
						if (kc == Key::NONE) {
							kc = key->get_keycode();
						}
						const char *name = find_keycode_name(kc);
						binding["key"] = name ? String(name) : String("Unknown");
					} else if (mouse.is_valid()) {
						binding["type"] = "mouse_button";
						binding["button"] = (int)mouse->get_button_index();
					} else if (joy_btn.is_valid()) {
						binding["type"] = "joypad_button";
						binding["button"] = (int)joy_btn->get_button_index();
					} else if (joy_axis.is_valid()) {
						binding["type"] = "joypad_motion";
						binding["axis"] = (int)joy_axis->get_axis();
						binding["axis_value"] = joy_axis->get_axis_value();
					}

					if (!binding.is_empty()) {
						bindings.push_back(binding);
					}
				}
			}
			action_info["bindings"] = bindings;
			actions_out.push_back(action_info);
		}

		return _success_result("Listed " + itos(actions_out.size()) + " input actions",
				Dictionary{ { "actions", actions_out }, { "count", actions_out.size() } });

	} else if (action == "add_action") {
		String action_name = p_args.get("action_name", "");
		if (action_name.is_empty()) {
			return _error_result("'add_action' requires 'action_name' parameter");
		}

		if (im->has_action(action_name)) {
			return _error_result("Action already exists: " + action_name);
		}

		float deadzone = p_args.get("deadzone", 0.5);
		String setting = "input/" + action_name;
		Dictionary action_dict;
		action_dict["deadzone"] = deadzone;
		action_dict["events"] = Array();

		EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
		ur->create_action("MCP: Add input action '" + action_name + "'");
		ur->add_do_method(im, "add_action", action_name, deadzone);
		ur->add_do_method(ps, "set_setting", setting, action_dict);
		ur->add_do_method(ps, "save");
		ur->add_undo_method(im, "erase_action", action_name);
		ur->add_undo_method(ps, "set_setting", setting, Variant());
		ur->add_undo_method(ps, "save");
		ur->commit_action();

		return _success_result("Added input action: " + action_name,
				Dictionary{ { "action_name", action_name }, { "deadzone", deadzone } });

	} else if (action == "remove_action") {
		String action_name = p_args.get("action_name", "");
		if (action_name.is_empty()) {
			return _error_result("'remove_action' requires 'action_name' parameter");
		}

		if (!im->has_action(action_name)) {
			return _error_result("Action not found: " + action_name);
		}

		// Capture full state before erasing for undo.
		String setting = "input/" + action_name;
		float old_deadzone = im->action_get_deadzone(action_name);
		Array old_events;
		const List<Ref<InputEvent>> *action_events = im->action_get_events(action_name);
		if (action_events) {
			for (const Ref<InputEvent> &ev : *action_events) {
				old_events.push_back(ev);
			}
		}
		Dictionary old_dict;
		old_dict["deadzone"] = old_deadzone;
		old_dict["events"] = old_events;

		EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
		ur->create_action("MCP: Remove input action '" + action_name + "'");
		ur->add_do_method(im, "erase_action", action_name);
		ur->add_do_method(ps, "set_setting", setting, Variant());
		ur->add_do_method(ps, "save");
		ur->add_undo_method(im, "add_action", action_name, old_deadzone);
		for (int i = 0; i < old_events.size(); i++) {
			ur->add_undo_method(im, "action_add_event", action_name, old_events[i]);
		}
		ur->add_undo_method(ps, "set_setting", setting, old_dict);
		ur->add_undo_method(ps, "save");
		ur->commit_action();

		return _success_result("Removed input action: " + action_name);

	} else if (action == "add_binding") {
		String action_name = p_args.get("action_name", "");
		if (action_name.is_empty()) {
			return _error_result("'add_binding' requires 'action_name' parameter");
		}

		if (!im->has_action(action_name)) {
			return _error_result("Action not found: " + action_name + ". Create it first with 'add_action'.");
		}

		Dictionary binding = p_args.get("binding", Dictionary());
		if (binding.is_empty()) {
			return _error_result("'add_binding' requires 'binding' parameter: {type, key/button/axis}");
		}

		String type = binding.get("type", "");
		Ref<InputEvent> event;

		if (type == "key") {
			String key_name = binding.get("key", "");
			if (key_name.is_empty()) {
				return _error_result("Key binding requires 'key' parameter (e.g., 'W', 'Space', 'Escape')");
			}

			Key keycode = find_keycode(key_name);
			if (keycode == Key::NONE) {
				return _error_result("Unknown key: " + key_name);
			}

			Ref<InputEventKey> key_event;
			key_event.instantiate();
			key_event->set_physical_keycode(keycode);
			event = key_event;

		} else if (type == "mouse_button") {
			int button = binding.get("button", 0);
			if (button <= 0) {
				return _error_result("Mouse binding requires 'button' parameter (1=left, 2=right, 3=middle)");
			}

			Ref<InputEventMouseButton> mouse_event;
			mouse_event.instantiate();
			mouse_event->set_button_index((MouseButton)button);
			event = mouse_event;

		} else if (type == "joypad_button") {
			int button = binding.get("button", -1);
			if (button < 0) {
				return _error_result("Joypad button binding requires 'button' parameter (0=A, 1=B, 2=X, 3=Y, etc.)");
			}

			Ref<InputEventJoypadButton> joy_event;
			joy_event.instantiate();
			joy_event->set_button_index((JoyButton)button);
			event = joy_event;

		} else if (type == "joypad_motion") {
			int axis = binding.get("axis", -1);
			if (axis < 0) {
				return _error_result("Joypad motion binding requires 'axis' (0=LeftX, 1=LeftY, 2=RightX, 3=RightY) and 'axis_value' (-1 or 1)");
			}

			float axis_value = binding.get("axis_value", 1.0);

			Ref<InputEventJoypadMotion> joy_event;
			joy_event.instantiate();
			joy_event->set_axis((JoyAxis)axis);
			joy_event->set_axis_value(axis_value);
			event = joy_event;

		} else {
			return _error_result("Unknown binding type: " + type + ". Use: 'key', 'mouse_button', 'joypad_button', or 'joypad_motion'");
		}

		// Capture old ProjectSettings state for undo.
		String setting = "input/" + action_name;
		Dictionary old_dict;
		old_dict["deadzone"] = im->action_get_deadzone(action_name);
		Array old_events;
		const List<Ref<InputEvent>> *existing_events = im->action_get_events(action_name);
		if (existing_events) {
			for (const Ref<InputEvent> &ev : *existing_events) {
				old_events.push_back(ev);
			}
		}
		old_dict["events"] = old_events;

		// Build new dict with the added event.
		Dictionary new_dict = old_dict.duplicate();
		Array new_events = old_events.duplicate();
		new_events.push_back(event);
		new_dict["events"] = new_events;

		EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
		ur->create_action("MCP: Add " + type + " binding to '" + action_name + "'");
		ur->add_do_method(im, "action_add_event", action_name, event);
		ur->add_do_method(ps, "set_setting", setting, new_dict);
		ur->add_do_method(ps, "save");
		ur->add_undo_method(im, "action_erase_event", action_name, event);
		ur->add_undo_method(ps, "set_setting", setting, old_dict);
		ur->add_undo_method(ps, "save");
		ur->commit_action();

		return _success_result("Added " + type + " binding to " + action_name,
				Dictionary{ { "action_name", action_name }, { "binding_type", type } });

	} else if (action == "remove_binding") {
		String action_name = p_args.get("action_name", "");
		if (action_name.is_empty()) {
			return _error_result("'remove_binding' requires 'action_name' parameter");
		}

		if (!im->has_action(action_name)) {
			return _error_result("Action not found: " + action_name);
		}

		Dictionary binding = p_args.get("binding", Dictionary());
		if (binding.is_empty()) {
			return _error_result("'remove_binding' requires 'binding' parameter");
		}

		String type = binding.get("type", "");
		if (type.is_empty()) {
			return _error_result("'binding' requires a 'type' field: 'key', 'mouse_button', 'joypad_button', or 'joypad_motion'");
		}

		// Find matching event.
		const List<Ref<InputEvent>> *events = im->action_get_events(action_name);
		if (!events) {
			return _error_result("No events found for action: " + action_name);
		}

		Ref<InputEvent> matched_event;
		for (const Ref<InputEvent> &ev : *events) {
			bool matches = false;

			if (type == "key") {
				Ref<InputEventKey> key = ev;
				if (key.is_valid()) {
					String key_name = binding.get("key", "");
					Key keycode = find_keycode(key_name);
					Key ev_code = key->get_physical_keycode();
					if (ev_code == Key::NONE) {
						ev_code = key->get_keycode();
					}
					matches = (ev_code == keycode);
				}
			} else if (type == "mouse_button") {
				Ref<InputEventMouseButton> mouse = ev;
				if (mouse.is_valid()) {
					matches = ((int)mouse->get_button_index() == (int)binding.get("button", 0));
				}
			} else if (type == "joypad_button") {
				Ref<InputEventJoypadButton> joy = ev;
				if (joy.is_valid()) {
					matches = ((int)joy->get_button_index() == (int)binding.get("button", -1));
				}
			} else if (type == "joypad_motion") {
				Ref<InputEventJoypadMotion> joy = ev;
				if (joy.is_valid()) {
					matches = ((int)joy->get_axis() == (int)binding.get("axis", -1));
				}
			}

			if (matches) {
				matched_event = ev;
				break;
			}
		}

		if (matched_event.is_null()) {
			return _error_result("No matching binding found for " + type + " on " + action_name);
		}

		// Capture old ProjectSettings state for undo.
		String setting = "input/" + action_name;
		Dictionary old_dict;
		old_dict["deadzone"] = im->action_get_deadzone(action_name);
		Array old_events;
		for (const Ref<InputEvent> &ev : *events) {
			old_events.push_back(ev);
		}
		old_dict["events"] = old_events;

		// Build new dict without the matched event.
		Dictionary new_dict = old_dict.duplicate();
		Array new_events;
		for (const Ref<InputEvent> &ev : *events) {
			if (ev != matched_event) {
				new_events.push_back(ev);
			}
		}
		new_dict["events"] = new_events;

		EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
		ur->create_action("MCP: Remove " + type + " binding from '" + action_name + "'");
		ur->add_do_method(im, "action_erase_event", action_name, matched_event);
		ur->add_do_method(ps, "set_setting", setting, new_dict);
		ur->add_do_method(ps, "save");
		ur->add_undo_method(im, "action_add_event", action_name, matched_event);
		ur->add_undo_method(ps, "set_setting", setting, old_dict);
		ur->add_undo_method(ps, "save");
		ur->commit_action();

		return _success_result("Removed " + type + " binding from " + action_name,
				Dictionary{ { "action_name", action_name }, { "binding_type", type } });

	} else {
		return _error_result("Unknown action: " + action + ". Use: 'list', 'add_action', 'remove_action', 'add_binding', or 'remove_binding'");
	}
#else
	return _error_result("Editor functionality not available");
#endif
}
