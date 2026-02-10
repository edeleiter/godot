/**************************************************************************/
/*  godot_mcp_tools_animation.cpp                                         */
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

#include "scene/animation/animation_mixer.h"
#include "scene/animation/animation_node_state_machine.h"
#include "scene/animation/animation_player.h"
#include "scene/animation/animation_tree.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_undo_redo_manager.h"
#endif

// Helper to parse loop mode string.
static Animation::LoopMode _parse_loop_mode(const String &p_mode) {
	if (p_mode == "linear") {
		return Animation::LOOP_LINEAR;
	} else if (p_mode == "ping_pong") {
		return Animation::LOOP_PINGPONG;
	}
	return Animation::LOOP_NONE;
}

// Helper to convert loop mode to string.
static String _loop_mode_to_string(Animation::LoopMode p_mode) {
	switch (p_mode) {
		case Animation::LOOP_LINEAR:
			return "linear";
		case Animation::LOOP_PINGPONG:
			return "ping_pong";
		default:
			return "none";
	}
}

// Helper to convert track type to string.
static String _track_type_to_string(Animation::TrackType p_type) {
	switch (p_type) {
		case Animation::TYPE_VALUE:
			return "value";
		case Animation::TYPE_POSITION_3D:
			return "position_3d";
		case Animation::TYPE_ROTATION_3D:
			return "rotation_3d";
		case Animation::TYPE_SCALE_3D:
			return "scale_3d";
		case Animation::TYPE_BLEND_SHAPE:
			return "blend_shape";
		case Animation::TYPE_METHOD:
			return "method";
		case Animation::TYPE_BEZIER:
			return "bezier";
		case Animation::TYPE_AUDIO:
			return "audio";
		case Animation::TYPE_ANIMATION:
			return "animation";
		default:
			return "unknown";
	}
}

// Sentinel value for unrecognized track type strings.
static const Animation::TrackType TRACK_TYPE_INVALID = (Animation::TrackType)-1;

static Animation::TrackType _parse_track_type(const String &p_type) {
	if (p_type == "value") {
		return Animation::TYPE_VALUE;
	} else if (p_type == "position_3d") {
		return Animation::TYPE_POSITION_3D;
	} else if (p_type == "rotation_3d") {
		return Animation::TYPE_ROTATION_3D;
	} else if (p_type == "scale_3d") {
		return Animation::TYPE_SCALE_3D;
	} else if (p_type == "blend_shape") {
		return Animation::TYPE_BLEND_SHAPE;
	} else if (p_type == "method") {
		return Animation::TYPE_METHOD;
	} else if (p_type == "bezier") {
		return Animation::TYPE_BEZIER;
	}
	return TRACK_TYPE_INVALID;
}

Dictionary GodotMCPServer::_tool_create_animation(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String node_path = p_args.get("node_path", "");
	String animation_name = p_args.get("animation_name", "");
	double length = p_args.get("length", 1.0);
	String library_name = p_args.get("library_name", "");
	String loop_mode_str = p_args.get("loop_mode", "none");
	Array tracks = p_args.get("tracks", Array());

	if (animation_name.is_empty()) {
		return _error_result("Missing required 'animation_name' parameter");
	}

	String error;
	if (!_validate_node_path(node_path, error)) {
		return _error_result(error);
	}

	Node *node = _resolve_node_path(node_path);
	if (!node) {
		return _error_result("Node not found: " + node_path);
	}

	AnimationMixer *mixer = Object::cast_to<AnimationMixer>(node);
	if (!mixer) {
		return _error_result("Node is not an AnimationPlayer or AnimationMixer: " + node_path);
	}

	// Get or create the animation library.
	Ref<AnimationLibrary> library;
	if (mixer->has_animation_library(library_name)) {
		library = mixer->get_animation_library(library_name);
	} else {
		library.instantiate();
		Error err = mixer->add_animation_library(library_name, library);
		if (err != OK) {
			return _error_result("Failed to create animation library: " + library_name);
		}
	}

	if (library->has_animation(animation_name)) {
		return _error_result("Animation '" + animation_name + "' already exists in library '" + library_name + "'");
	}

	// Create the animation.
	Ref<Animation> anim;
	anim.instantiate();
	anim->set_length(length);
	anim->set_loop_mode(_parse_loop_mode(loop_mode_str));

	// Add tracks if provided.
	int tracks_added = 0;
	for (int i = 0; i < tracks.size(); i++) {
		Dictionary track_def = tracks[i];
		String type_str = track_def.get("type", "value");
		String path = track_def.get("path", "");
		Array keys = track_def.get("keys", Array());

		if (path.is_empty()) {
			continue;
		}

		Animation::TrackType track_type = _parse_track_type(type_str);
		if (track_type == TRACK_TYPE_INVALID) {
			return _error_result("Unknown track type: " + type_str + ". Use: 'value', 'position_3d', 'rotation_3d', 'scale_3d', 'blend_shape', 'method', or 'bezier'");
		}
		int track_idx = anim->add_track(track_type);
		anim->track_set_path(track_idx, NodePath(path));

		// Insert keyframes.
		for (int k = 0; k < keys.size(); k++) {
			Dictionary key = keys[k];
			double time = key.get("time", 0.0);

			switch (track_type) {
				case Animation::TYPE_POSITION_3D: {
					Dictionary val = key.get("value", Dictionary());
					Vector3 pos(val.get("x", 0.0), val.get("y", 0.0), val.get("z", 0.0));
					anim->position_track_insert_key(track_idx, time, pos);
				} break;
				case Animation::TYPE_ROTATION_3D: {
					Dictionary val = key.get("value", Dictionary());
					Quaternion quat;
					if (val.has("x") && !val.has("w")) {
						// Euler degrees -> radians -> quaternion.
						Vector3 deg(val.get("x", 0.0), val.get("y", 0.0), val.get("z", 0.0));
						Vector3 rad(Math::deg_to_rad((double)deg.x),
								Math::deg_to_rad((double)deg.y),
								Math::deg_to_rad((double)deg.z));
						quat = Quaternion::from_euler(rad);
					} else {
						quat = Quaternion(val.get("x", 0.0), val.get("y", 0.0), val.get("z", 0.0), val.get("w", 1.0));
					}
					anim->rotation_track_insert_key(track_idx, time, quat);
				} break;
				case Animation::TYPE_SCALE_3D: {
					Dictionary val = key.get("value", Dictionary());
					Vector3 scale(val.get("x", 1.0), val.get("y", 1.0), val.get("z", 1.0));
					anim->scale_track_insert_key(track_idx, time, scale);
				} break;
				case Animation::TYPE_BLEND_SHAPE: {
					float blend_val = key.get("value", 0.0);
					anim->blend_shape_track_insert_key(track_idx, time, blend_val);
				} break;
				case Animation::TYPE_METHOD: {
					Dictionary method_dict;
					method_dict["method"] = key.get("method", "");
					method_dict["args"] = key.get("args", Array());
					anim->track_insert_key(track_idx, time, method_dict);
				} break;
				default: {
					// VALUE track and others — pass value directly.
					Variant value = key.get("value", Variant());
					anim->track_insert_key(track_idx, time, value);
				} break;
			}
		}

		tracks_added++;
	}

	// Add animation to library with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Create animation '" + animation_name + "'");
	ur->add_do_method(library.ptr(), "add_animation", animation_name, anim);
	ur->add_undo_method(library.ptr(), "remove_animation", animation_name);
	ur->commit_action();

	Dictionary data;
	data["animation_name"] = animation_name;
	data["library"] = library_name;
	data["length"] = length;
	data["loop_mode"] = loop_mode_str;
	data["tracks_added"] = tracks_added;
	return _success_result("Created animation '" + animation_name + "'", data);
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_get_animation_info(const Dictionary &p_args) {
	String node_path = p_args.get("node_path", "");
	String animation_name = p_args.get("animation_name", "");

	String error;
	if (!_validate_node_path(node_path, error)) {
		return _error_result(error);
	}

	Node *node = _resolve_node_path(node_path);
	if (!node) {
		return _error_result("Node not found: " + node_path);
	}

	AnimationMixer *mixer = Object::cast_to<AnimationMixer>(node);
	if (!mixer) {
		return _error_result("Node is not an AnimationPlayer/AnimationMixer/AnimationTree: " + node_path);
	}

	Dictionary data;
	data["node_path"] = node_path;
	data["class"] = node->get_class();

	// List all libraries and their animations.
	Array libraries_out;
	List<StringName> library_list;
	mixer->get_animation_library_list(&library_list);

	for (const StringName &lib_name : library_list) {
		Ref<AnimationLibrary> lib = mixer->get_animation_library(lib_name);
		if (!lib.is_valid()) {
			continue;
		}

		Dictionary lib_info;
		lib_info["name"] = String(lib_name);

		Array anims_out;
		List<StringName> anim_list;
		lib->get_animation_list(&anim_list);

		for (const StringName &anim_name : anim_list) {
			Ref<Animation> anim = lib->get_animation(anim_name);
			if (!anim.is_valid()) {
				continue;
			}

			Dictionary anim_info;
			anim_info["name"] = String(anim_name);
			anim_info["length"] = anim->get_length();
			anim_info["loop_mode"] = _loop_mode_to_string(anim->get_loop_mode());
			anim_info["track_count"] = anim->get_track_count();

			// If this is the requested animation, include detailed track info.
			if (!animation_name.is_empty() && String(anim_name) == animation_name) {
				Array tracks_out;
				for (int t = 0; t < anim->get_track_count(); t++) {
					Dictionary track_info;
					track_info["index"] = t;
					track_info["type"] = _track_type_to_string(anim->track_get_type(t));
					track_info["path"] = String(anim->track_get_path(t));
					track_info["key_count"] = anim->track_get_key_count(t);
					track_info["enabled"] = anim->track_is_enabled(t);

					// Include keyframe times and values.
					Array keys_out;
					for (int k = 0; k < anim->track_get_key_count(t); k++) {
						Dictionary key_info;
						key_info["time"] = anim->track_get_key_time(t, k);

						Animation::TrackType tt = anim->track_get_type(t);
						switch (tt) {
							case Animation::TYPE_POSITION_3D: {
								Vector3 pos;
								anim->position_track_get_key(t, k, &pos);
								Dictionary val;
								val["x"] = pos.x;
								val["y"] = pos.y;
								val["z"] = pos.z;
								key_info["value"] = val;
							} break;
							case Animation::TYPE_ROTATION_3D: {
								Quaternion rot;
								anim->rotation_track_get_key(t, k, &rot);
								Vector3 euler = rot.get_euler();
								Dictionary val;
								val["x"] = Math::rad_to_deg(euler.x);
								val["y"] = Math::rad_to_deg(euler.y);
								val["z"] = Math::rad_to_deg(euler.z);
								key_info["value"] = val;
							} break;
							case Animation::TYPE_SCALE_3D: {
								Vector3 scale;
								anim->scale_track_get_key(t, k, &scale);
								Dictionary val;
								val["x"] = scale.x;
								val["y"] = scale.y;
								val["z"] = scale.z;
								key_info["value"] = val;
							} break;
							case Animation::TYPE_BLEND_SHAPE: {
								float blend;
								anim->blend_shape_track_get_key(t, k, &blend);
								key_info["value"] = blend;
							} break;
							default: {
								key_info["value"] = anim->track_get_key_value(t, k);
							} break;
						}

						keys_out.push_back(key_info);
					}
					track_info["keys"] = keys_out;
					tracks_out.push_back(track_info);
				}
				anim_info["tracks"] = tracks_out;
			}

			anims_out.push_back(anim_info);
		}

		lib_info["animations"] = anims_out;
		lib_info["animation_count"] = anims_out.size();
		libraries_out.push_back(lib_info);
	}
	data["libraries"] = libraries_out;

	// For AnimationTree, also report the tree root type and state machine info.
	AnimationTree *tree = Object::cast_to<AnimationTree>(node);
	if (tree) {
		Ref<AnimationRootNode> root = tree->get_root_animation_node();
		if (root.is_valid()) {
			data["tree_root_type"] = root->get_class();

			// If the root is a state machine, list states and transitions.
			Ref<AnimationNodeStateMachine> sm = root;
			if (sm.is_valid()) {
				Dictionary sm_info;

				Array states;
				TypedArray<StringName> node_list = sm->get_node_list_as_typed_array();
				for (int i = 0; i < node_list.size(); i++) {
					StringName state_name = node_list[i];
					Dictionary state;
					state["name"] = String(state_name);
					Ref<AnimationNode> state_node = sm->get_node(state_name);
					if (state_node.is_valid()) {
						state["type"] = state_node->get_class();
					}
					states.push_back(state);
				}
				sm_info["states"] = states;

				Array transitions;
				for (int i = 0; i < sm->get_transition_count(); i++) {
					Dictionary trans;
					trans["from"] = String(sm->get_transition_from(i));
					trans["to"] = String(sm->get_transition_to(i));
					Ref<AnimationNodeStateMachineTransition> t = sm->get_transition(i);
					if (t.is_valid()) {
						trans["switch_mode"] = (int)t->get_switch_mode();
						trans["advance_mode"] = (int)t->get_advance_mode();
					}
					transitions.push_back(trans);
				}
				sm_info["transitions"] = transitions;
				data["state_machine"] = sm_info;
			}
		}
	}

	return _success_result("Animation info for " + node_path, data);
}
