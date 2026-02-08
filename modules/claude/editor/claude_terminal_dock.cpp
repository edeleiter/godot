/**************************************************************************/
/*  claude_terminal_dock.cpp                                              */
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

#include "claude_terminal_dock.h"

#ifdef WINDOWS_ENABLED

#include "editor/editor_string_names.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/main/timer.h"

///////////////////////////////////////////////////////////////////////////////
// TerminalView
///////////////////////////////////////////////////////////////////////////////

void TerminalView::_bind_methods() {
}

TerminalView::TerminalView() {
	set_focus_mode(FOCUS_CLICK);
	set_clip_contents(true);

	// Cursor blink timer.
	blink_timer = memnew(Timer);
	blink_timer->set_wait_time(0.6);
	blink_timer->connect("timeout", callable_mp(this, &TerminalView::_on_blink_timer));
	add_child(blink_timer);

	// Scrollbar.
	scrollbar = memnew(VScrollBar);
	scrollbar->set_anchors_and_offsets_preset(PRESET_RIGHT_WIDE);
	scrollbar->connect(SceneStringName(value_changed), callable_mp(this, &TerminalView::_on_scrollbar_changed));
	add_child(scrollbar);
}

void TerminalView::_update_font() {
	font = get_theme_font(SNAME("output_source"), EditorStringName(EditorFonts));
	bold_font = get_theme_font(SNAME("output_source_bold"), EditorStringName(EditorFonts));
	italic_font = get_theme_font(SNAME("output_source_italic"), EditorStringName(EditorFonts));
	bold_italic_font = get_theme_font(SNAME("output_source_bold_italic"), EditorStringName(EditorFonts));

	int custom_size = EDITOR_GET("network/claude_mcp/terminal_font_size");
	if (custom_size > 0) {
		font_size = custom_size * EDSCALE;
	} else {
		font_size = get_theme_font_size(SNAME("output_source_size"), EditorStringName(EditorFonts));
	}

	if (font.is_valid()) {
		cell_width = font->get_char_size('M', font_size).x;
		cell_height = font->get_height(font_size);
		ascent = font->get_ascent(font_size);
	}
}

void TerminalView::_recompute_grid_size() {
	if (cell_width <= 0 || cell_height <= 0 || !state.is_valid()) {
		return;
	}

	float scrollbar_width = scrollbar->is_visible() ? scrollbar->get_size().x : 0;
	float available_width = get_size().x - scrollbar_width;
	float available_height = get_size().y;

	int new_cols = MAX(1, (int)(available_width / cell_width));
	int new_rows = MAX(1, (int)(available_height / cell_height));

	if (new_cols != state->get_cols() || new_rows != state->get_rows()) {
		state->resize(new_cols, new_rows);
		if (pty.is_valid() && pty->is_running()) {
			pty->resize(new_cols, new_rows);
		}
	}
}

void TerminalView::_on_blink_timer() {
	cursor_blink_on = !cursor_blink_on;
	queue_redraw();
}

void TerminalView::_on_scrollbar_changed(double p_value) {
	if (!state.is_valid()) {
		return;
	}
	int max_scroll = state->get_scrollback_count();
	scroll_offset = max_scroll - (int)p_value;
	queue_redraw();
}

void TerminalView::update_scrollbar() {
	if (!state.is_valid()) {
		return;
	}
	int scrollback_count = state->get_scrollback_count();
	int visible_rows = state->get_rows();
	int total_lines = scrollback_count + visible_rows;

	// Clamp scroll_offset to valid range (guards against resize edge cases).
	scroll_offset = CLAMP(scroll_offset, 0, scrollback_count);

	// Block signals to prevent feedback loop — programmatic changes to
	// max/page/value can trigger value_changed via Range clamping.
	scrollbar->set_block_signals(true);
	scrollbar->set_max(total_lines);
	scrollbar->set_page(visible_rows);
	scrollbar->set_value(scrollback_count - scroll_offset);
	scrollbar->set_block_signals(false);

	scrollbar->set_visible(total_lines > visible_rows);
}

Vector2i TerminalView::_screen_to_cell(const Vector2 &p_pos) const {
	if (cell_width <= 0 || cell_height <= 0) {
		return Vector2i(0, 0);
	}
	int col = CLAMP((int)(p_pos.x / cell_width), 0, state.is_valid() ? state->get_cols() - 1 : 0);
	int row = CLAMP((int)(p_pos.y / cell_height), 0, state.is_valid() ? state->get_rows() - 1 : 0);
	return Vector2i(col, row);
}

bool TerminalView::_is_cell_selected(int p_col, int p_row_in_view) const {
	if (!has_selection) {
		return false;
	}

	// Convert to linear position for comparison.
	int cols = state.is_valid() ? state->get_cols() : 80;
	int sel_start_linear = selection_start.y * cols + selection_start.x;
	int sel_end_linear = selection_end.y * cols + selection_end.x;
	int cell_linear = p_row_in_view * cols + p_col;

	int min_linear = MIN(sel_start_linear, sel_end_linear);
	int max_linear = MAX(sel_start_linear, sel_end_linear);

	return cell_linear >= min_linear && cell_linear <= max_linear;
}

void TerminalView::clear_selection() {
	has_selection = false;
	selecting = false;
	queue_redraw();
}

String TerminalView::_get_selected_text() const {
	if (!has_selection || !state.is_valid()) {
		return String();
	}

	int cols = state->get_cols();
	int sel_start_linear = selection_start.y * cols + selection_start.x;
	int sel_end_linear = selection_end.y * cols + selection_end.x;
	int min_linear = MIN(sel_start_linear, sel_end_linear);
	int max_linear = MAX(sel_start_linear, sel_end_linear);

	String result;
	int last_row = -1;

	for (int pos = min_linear; pos <= max_linear; pos++) {
		int row = pos / cols;
		int col = pos % cols;

		if (row != last_row && last_row >= 0) {
			result += "\n";
		}
		last_row = row;

		// Only get cells from the visible screen area.
		if (row >= 0 && row < state->get_rows()) {
			const AnsiTerminalState::Cell &cell = state->get_cell(row, col);
			if (cell.ch >= 0x20) {
				result += String::chr(cell.ch);
			}
		}
	}

	return result;
}

void TerminalView::refresh_font() {
	_update_font();
	_recompute_grid_size();
	queue_redraw();
}

void TerminalView::_draw_cell(const AnsiTerminalState::Cell &p_cell, int p_x, int p_y, bool p_is_cursor, bool p_is_selected) {
	Color fg = p_cell.fg;
	Color bg = p_cell.bg;

	if (p_cell.inverse) {
		SWAP(fg, bg);
		if (bg.a < 0.01f) {
			bg = Color(1, 1, 1);
		}
		if (fg.a < 0.01f) {
			fg = Color(0, 0, 0);
		}
	}

	if (p_cell.dim) {
		fg.a *= 0.5f;
	}

	if (p_is_selected) {
		bg = Color(0.3f, 0.5f, 0.8f, 0.5f);
		fg = Color(1, 1, 1);
	}

	// Draw background.
	if (bg.a > 0.01f) {
		draw_rect(Rect2(p_x, p_y, cell_width, cell_height), bg);
	}

	// Draw cursor.
	if (p_is_cursor && cursor_blink_on) {
		Color cursor_color = Color(0.8f, 0.8f, 0.8f, 0.8f);
		if (has_focus()) {
			draw_rect(Rect2(p_x, p_y, cell_width, cell_height), cursor_color);
			fg = Color(0, 0, 0); // Text under solid cursor.
		} else {
			draw_rect(Rect2(p_x, p_y, cell_width, cell_height), cursor_color, false, 1.0f);
		}
	}

	// Draw character.
	if (p_cell.ch > 0x20 && p_cell.ch <= 0x10FFFF && font.is_valid()) {
		Ref<Font> draw_font = font;
		if (p_cell.bold && p_cell.italic && bold_italic_font.is_valid()) {
			draw_font = bold_italic_font;
		} else if (p_cell.bold && bold_font.is_valid()) {
			draw_font = bold_font;
		} else if (p_cell.italic && italic_font.is_valid()) {
			draw_font = italic_font;
		}

		draw_font->draw_char(get_canvas_item(), Point2(p_x, p_y + ascent), p_cell.ch, font_size, fg);
	}

	// Draw underline.
	if (p_cell.underline) {
		float underline_y = p_y + ascent + 2;
		draw_line(Point2(p_x, underline_y), Point2(p_x + cell_width, underline_y), fg, 1.0f);
	}
}

void TerminalView::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_THEME_CHANGED: {
			_update_font();
			_recompute_grid_size();
		} break;

		case NOTIFICATION_RESIZED: {
			_recompute_grid_size();
			update_scrollbar();
		} break;

		case NOTIFICATION_DRAW: {
			if (!state.is_valid()) {
				return;
			}

			// Draw terminal background.
			Color term_bg = Color(0.1f, 0.1f, 0.1f);
			draw_rect(Rect2(Point2(), get_size()), term_bg);

			int rows = state->get_rows();
			int cols = state->get_cols();
			Vector2i cursor_pos = state->get_cursor_pos();
			bool show_cursor = state->is_cursor_visible() && scroll_offset == 0;

			// Draw scrollback lines if scrolled up.
			if (scroll_offset > 0 && !state->is_using_alt_screen()) {
				int sb_count = state->get_scrollback_count();
				int sb_start = sb_count - scroll_offset;

				for (int r = 0; r < rows; r++) {
					int sb_line = sb_start + r;
					if (sb_line >= 0 && sb_line < sb_count) {
						// Scrollback line.
						const Vector<AnsiTerminalState::Cell> &line = state->get_scrollback_line(sb_line);
						for (int c = 0; c < cols && c < line.size(); c++) {
							_draw_cell(line[c], (int)(c * cell_width), (int)(r * cell_height), false, _is_cell_selected(c, r));
						}
					} else if (sb_line >= sb_count) {
						// Screen line.
						int screen_row = sb_line - sb_count;
						if (screen_row >= 0 && screen_row < rows) {
							for (int c = 0; c < cols; c++) {
								const AnsiTerminalState::Cell &cell = state->get_cell(screen_row, c);
								_draw_cell(cell, (int)(c * cell_width), (int)(r * cell_height), false, _is_cell_selected(c, r));
							}
						}
					}
				}
			} else {
				// Normal view - draw active screen.
				for (int r = 0; r < rows; r++) {
					for (int c = 0; c < cols; c++) {
						const AnsiTerminalState::Cell &cell = state->get_cell(r, c);
						bool is_cursor = show_cursor && r == cursor_pos.y && c == cursor_pos.x;
						_draw_cell(cell, (int)(c * cell_width), (int)(r * cell_height), is_cursor, _is_cell_selected(c, r));
					}
				}
			}

			// Draw focus border.
			if (has_focus()) {
				Color focus_color = Color(0.4f, 0.6f, 1.0f, 0.5f);
				draw_rect(Rect2(Point2(), get_size()), focus_color, false, 2.0f);
			}

			// Draw "process exited" overlay.
			if (process_exited) {
				String msg = "[Process exited - press Enter to restart]";
				if (font.is_valid()) {
					Vector2 text_size = font->get_string_size(msg, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
					Vector2 pos = (get_size() - text_size) * 0.5f;
					pos.y += ascent;
					font->draw_string(get_canvas_item(), pos, msg, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0.7f, 0.7f, 0.7f));
				}
			}
		} break;

		case NOTIFICATION_FOCUS_ENTER: {
			blink_timer->start();
			cursor_blink_on = true;
			queue_redraw();
		} break;

		case NOTIFICATION_FOCUS_EXIT: {
			blink_timer->stop();
			cursor_blink_on = true;
			queue_redraw();
		} break;
	}
}

void TerminalView::gui_input(const Ref<InputEvent> &p_event) {
	// Mouse input for selection.
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		if (mb->get_button_index() == MouseButton::LEFT) {
			if (mb->is_pressed()) {
				grab_focus();
				selecting = true;
				has_selection = false;
				selection_start = _screen_to_cell(mb->get_position());
				selection_end = selection_start;
				queue_redraw();
			} else {
				if (selecting && selection_start != selection_end) {
					has_selection = true;
				}
				selecting = false;
			}
			accept_event();
		} else if (mb->get_button_index() == MouseButton::WHEEL_UP && mb->is_pressed()) {
			scroll_offset = MIN(scroll_offset + 3, state.is_valid() ? state->get_scrollback_count() : 0);
			update_scrollbar();
			queue_redraw();
			accept_event();
		} else if (mb->get_button_index() == MouseButton::WHEEL_DOWN && mb->is_pressed()) {
			scroll_offset = MAX(scroll_offset - 3, 0);
			update_scrollbar();
			queue_redraw();
			accept_event();
		}
		return;
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && selecting) {
		selection_end = _screen_to_cell(mm->get_position());
		has_selection = (selection_start != selection_end);
		queue_redraw();
		accept_event();
		return;
	}

	// Keyboard input.
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed()) {
		// Handle copy (Ctrl+Shift+C).
		if (k->get_keycode() == Key::C && k->is_ctrl_pressed() && k->is_shift_pressed()) {
			if (has_selection) {
				DisplayServer::get_singleton()->clipboard_set(_get_selected_text());
			}
			accept_event();
			return;
		}

		// Handle paste (Ctrl+Shift+V).
		if (k->get_keycode() == Key::V && k->is_ctrl_pressed() && k->is_shift_pressed()) {
			String clipboard = DisplayServer::get_singleton()->clipboard_get();
			if (!clipboard.is_empty() && pty.is_valid() && pty->is_running()) {
				CharString utf8 = clipboard.utf8();
				PackedByteArray key_data;
				key_data.resize(utf8.length());
				memcpy(key_data.ptrw(), utf8.ptr(), utf8.length());
				pty->write_input(key_data);
			}
			accept_event();
			return;
		}

		// If process exited, Enter restarts.
		if (process_exited && k->get_keycode() == Key::ENTER) {
			// Will be handled by the dock's poll logic.
			accept_event();
			return;
		}

		if (!pty.is_valid() || !pty->is_running()) {
			return;
		}

		// Scroll back to bottom on any keypress.
		if (scroll_offset != 0) {
			scroll_offset = 0;
			update_scrollbar();
			queue_redraw();
		}

		clear_selection();

		PackedByteArray key_data;

		// Translate key to ANSI/xterm escape sequence.
		bool ctrl = k->is_ctrl_pressed();
		bool shift = k->is_shift_pressed();
		Key keycode = k->get_keycode();

		if (ctrl && !shift) {
			// Ctrl+letter → control character.
			if (keycode >= Key::A && keycode <= Key::Z) {
				uint8_t ctrl_char = (uint8_t)(keycode) - (uint8_t)(Key::A) + 1;
				key_data.resize(1);
				key_data.write[0] = ctrl_char;
			}
		}

		if (key_data.size() == 0) {
			switch (keycode) {
				case Key::ENTER:
				case Key::KP_ENTER: {
					key_data.resize(1);
					key_data.write[0] = '\r';
				} break;
				case Key::BACKSPACE: {
					key_data.resize(1);
					key_data.write[0] = 0x7f;
				} break;
				case Key::TAB: {
					key_data.resize(1);
					key_data.write[0] = '\t';
				} break;
				case Key::ESCAPE: {
					key_data.resize(1);
					key_data.write[0] = 0x1b;
				} break;
				case Key::UP: {
					key_data.resize(3);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = 'A';
				} break;
				case Key::DOWN: {
					key_data.resize(3);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = 'B';
				} break;
				case Key::RIGHT: {
					key_data.resize(3);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = 'C';
				} break;
				case Key::LEFT: {
					key_data.resize(3);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = 'D';
				} break;
				case Key::HOME: {
					key_data.resize(3);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = 'H';
				} break;
				case Key::END: {
					key_data.resize(3);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = 'F';
				} break;
				case Key::INSERT: {
					key_data.resize(4);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = '2';
					key_data.write[3] = '~';
				} break;
				case Key::KEY_DELETE: {
					key_data.resize(4);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = '3';
					key_data.write[3] = '~';
				} break;
				case Key::PAGEUP: {
					key_data.resize(4);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = '5';
					key_data.write[3] = '~';
				} break;
				case Key::PAGEDOWN: {
					key_data.resize(4);
					key_data.write[0] = 0x1b;
					key_data.write[1] = '[';
					key_data.write[2] = '6';
					key_data.write[3] = '~';
				} break;
				case Key::F1:
				case Key::F2:
				case Key::F3:
				case Key::F4: {
					// F1-F4: ESC O P/Q/R/S.
					key_data.resize(3);
					key_data.write[0] = 0x1b;
					key_data.write[1] = 'O';
					key_data.write[2] = 'P' + ((uint8_t)(keycode) - (uint8_t)(Key::F1));
				} break;
				case Key::F5:
				case Key::F6:
				case Key::F7:
				case Key::F8:
				case Key::F9:
				case Key::F10:
				case Key::F11:
				case Key::F12: {
					// F5-F12 use CSI sequences.
					static const char *f_codes[] = { "15", "17", "18", "19", "20", "21", "23", "24" };
					int f_idx = (int)(keycode) - (int)(Key::F5);
					String seq = vformat("\x1b[%s~", f_codes[f_idx]);
					CharString utf8 = seq.utf8();
					key_data.resize(utf8.length());
					memcpy(key_data.ptrw(), utf8.ptr(), utf8.length());
				} break;
				default: {
					// Printable character - use Unicode.
					char32_t unicode = k->get_unicode();
					if (unicode >= 0x20) {
						CharString utf8 = String::chr(unicode).utf8();
						key_data.resize(utf8.length());
						memcpy(key_data.ptrw(), utf8.ptr(), utf8.length());
					}
				} break;
			}
		}

		if (key_data.size() > 0) {
			pty->write_input(key_data);
			accept_event();
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// ClaudeTerminalDock
///////////////////////////////////////////////////////////////////////////////

void ClaudeTerminalDock::_bind_methods() {
}

ClaudeTerminalDock::ClaudeTerminalDock() {
	set_name(TTRC("Terminal"));
	set_icon_name("Terminal");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_terminal_dock", TTRC("Toggle Terminal Dock"), KeyModifierMask::ALT | Key::T));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);

	main_vbox = memnew(VBoxContainer);
	main_vbox->set_v_size_flags(SIZE_EXPAND_FILL);
	main_vbox->set_h_size_flags(SIZE_EXPAND_FILL);
	add_child(main_vbox);

	// Tab bar + new tab button.
	HBoxContainer *tab_hbox = memnew(HBoxContainer);
	main_vbox->add_child(tab_hbox);

	tab_bar = memnew(TabBar);
	tab_bar->set_h_size_flags(SIZE_EXPAND_FILL);
	tab_bar->set_tab_close_display_policy(TabBar::CLOSE_BUTTON_SHOW_ALWAYS);
	tab_bar->connect("tab_changed", callable_mp(this, &ClaudeTerminalDock::_tab_changed));
	tab_bar->connect("tab_close_pressed", callable_mp(this, &ClaudeTerminalDock::_tab_close_requested));
	tab_hbox->add_child(tab_bar);

	new_tab_button = memnew(Button);
	new_tab_button->set_text("+");
	new_tab_button->set_tooltip_text(TTR("New Terminal"));
	new_tab_button->set_theme_type_variation(SceneStringName(FlatButton));
	new_tab_button->connect(SceneStringName(pressed), callable_mp(this, &ClaudeTerminalDock::_new_tab_pressed));
	tab_hbox->add_child(new_tab_button);

	settings_button = memnew(Button);
	settings_button->set_tooltip_text(TTR("Terminal Settings"));
	settings_button->set_theme_type_variation(SceneStringName(FlatButton));
	settings_button->connect(SceneStringName(pressed), callable_mp(this, &ClaudeTerminalDock::_on_settings_pressed));
	tab_hbox->add_child(settings_button);

	set_process_internal(true);
}

ClaudeTerminalDock::~ClaudeTerminalDock() {
	for (int i = 0; i < tabs.size(); i++) {
		if (tabs[i].pty.is_valid() && tabs[i].pty->is_running()) {
			tabs[i].pty->stop();
		}
	}
}

void ClaudeTerminalDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			terminal_command = EDITOR_GET("network/claude_mcp/terminal_command");
			scrollback_limit = EDITOR_GET("network/claude_mcp/terminal_scrollback_lines");
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			if (settings_button) {
				settings_button->set_button_icon(get_editor_theme_icon(SNAME("Tools")));
			}
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			_poll_terminals();
		} break;
	}
}

void ClaudeTerminalDock::_add_terminal_tab() {
	TerminalTab tab;
	tab.pty.instantiate();
	tab.state.instantiate();
	tab.state->set_scrollback_limit(scrollback_limit);

	tab.view = memnew(TerminalView);
	tab.view->set_pty(tab.pty);
	tab.view->set_state(tab.state);
	tab.view->set_v_size_flags(SIZE_EXPAND_FILL);
	tab.view->set_h_size_flags(SIZE_EXPAND_FILL);
	tab.view->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
	tab.view->set_visible(false);
	main_vbox->add_child(tab.view);

	tab.tab_index = tabs.size();
	tabs.push_back(tab);

	int tab_idx = tab_bar->get_tab_count();
	tab_bar->add_tab(vformat("Terminal %d", tabs.size()));

	// Start the terminal process.
	String cmd = terminal_command;
	if (cmd.is_empty()) {
		cmd = "wsl.exe -d Ubuntu";
	}
	Error err = tab.pty->start(cmd, tab.state->get_cols(), tab.state->get_rows());
	if (err != OK) {
		print_line(vformat("Terminal: Failed to start process '%s': error %d", cmd, err));
	}

	// Switch to the new tab.
	tab_bar->set_current_tab(tab_idx);
	_show_tab(tab_idx);
}

void ClaudeTerminalDock::_close_terminal_tab(int p_index) {
	if (p_index < 0 || p_index >= tabs.size()) {
		return;
	}

	TerminalTab &tab = tabs.write[p_index];

	if (tab.pty.is_valid() && tab.pty->is_running()) {
		tab.pty->stop();
	}

	if (tab.view) {
		main_vbox->remove_child(tab.view);
		memdelete(tab.view);
	}

	tabs.remove_at(p_index);
	tab_bar->remove_tab(p_index);

	// Update tab indices.
	for (int i = 0; i < tabs.size(); i++) {
		tabs.write[i].tab_index = i;
	}

	_update_tab_titles();

	if (tabs.size() > 0) {
		int new_active = CLAMP(p_index, 0, tabs.size() - 1);
		tab_bar->set_current_tab(new_active);
		_show_tab(new_active);
	} else {
		active_tab = -1;
	}
}

void ClaudeTerminalDock::_tab_changed(int p_index) {
	_show_tab(p_index);
}

void ClaudeTerminalDock::_tab_close_requested(int p_index) {
	_close_terminal_tab(p_index);
}

void ClaudeTerminalDock::_new_tab_pressed() {
	_add_terminal_tab();
}

void ClaudeTerminalDock::_show_tab(int p_index) {
	// Hide all views.
	for (int i = 0; i < tabs.size(); i++) {
		if (tabs[i].view) {
			tabs[i].view->set_visible(false);
		}
	}

	// Show the selected one.
	if (p_index >= 0 && p_index < tabs.size()) {
		active_tab = p_index;
		if (tabs[p_index].view) {
			tabs[p_index].view->set_visible(true);
			tabs[p_index].view->grab_focus();
		}
	}
}

void ClaudeTerminalDock::_poll_terminals() {
	for (int i = 0; i < tabs.size(); i++) {
		TerminalTab &tab = tabs.write[i];

		if (tab.pty.is_valid()) {
			PackedByteArray output = tab.pty->drain_output();
			if (output.size() > 0 && tab.state.is_valid()) {
				tab.state->feed(output.ptr(), output.size());
			}

			// Check if process exited.
			if (!tab.pty->is_running() && !tab.view->is_process_exited()) {
				tab.view->set_process_exited(true);
			}
		}

		if (tab.state.is_valid() && tab.state->is_dirty()) {
			tab.state->clear_dirty();
			if (tab.view) {
				tab.view->update_scrollbar();
				tab.view->queue_redraw();
			}
		}
	}
}

void ClaudeTerminalDock::_update_tab_titles() {
	for (int i = 0; i < tabs.size(); i++) {
		tab_bar->set_tab_title(i, vformat("Terminal %d", i + 1));
	}
}

void ClaudeTerminalDock::_on_settings_pressed() {
	if (!settings_dialog) {
		_build_settings_dialog();
	}

	// Populate controls from current settings (block signals to avoid
	// triggering saves during population).
	terminal_cmd_edit->set_text(EDITOR_GET("network/claude_mcp/terminal_command"));
	font_size_spinbox->set_block_signals(true);
	font_size_spinbox->set_value((int)EDITOR_GET("network/claude_mcp/terminal_font_size"));
	font_size_spinbox->set_block_signals(false);
	scrollback_spinbox->set_block_signals(true);
	scrollback_spinbox->set_value((int)EDITOR_GET("network/claude_mcp/terminal_scrollback_lines"));
	scrollback_spinbox->set_block_signals(false);

	settings_dialog->popup_centered(Size2(420, 0));
}

void ClaudeTerminalDock::_on_settings_confirmed() {
	// Save terminal command from LineEdit (which only fires text_submitted on Enter, not on OK click).
	_on_terminal_cmd_changed(terminal_cmd_edit->get_text());
}

void ClaudeTerminalDock::_build_settings_dialog() {
	settings_dialog = memnew(AcceptDialog);
	settings_dialog->set_title(TTR("Terminal Settings"));
	settings_dialog->connect("confirmed", callable_mp(this, &ClaudeTerminalDock::_on_settings_confirmed));
	add_child(settings_dialog);

	VBoxContainer *vbox = memnew(VBoxContainer);
	settings_dialog->add_child(vbox);

	// Terminal command.
	HBoxContainer *cmd_hbox = memnew(HBoxContainer);
	vbox->add_child(cmd_hbox);

	Label *cmd_label = memnew(Label);
	cmd_label->set_text(TTR("Command:"));
	cmd_label->set_custom_minimum_size(Size2(130, 0));
	cmd_hbox->add_child(cmd_label);

	terminal_cmd_edit = memnew(LineEdit);
	terminal_cmd_edit->set_h_size_flags(SIZE_EXPAND_FILL);
	terminal_cmd_edit->connect("text_submitted", callable_mp(this, &ClaudeTerminalDock::_on_terminal_cmd_changed));
	cmd_hbox->add_child(terminal_cmd_edit);

	// Font size.
	HBoxContainer *font_hbox = memnew(HBoxContainer);
	vbox->add_child(font_hbox);

	Label *font_label = memnew(Label);
	font_label->set_text(TTR("Font Size:"));
	font_label->set_custom_minimum_size(Size2(130, 0));
	font_hbox->add_child(font_label);

	font_size_spinbox = memnew(SpinBox);
	font_size_spinbox->set_min(0);
	font_size_spinbox->set_max(72);
	font_size_spinbox->set_step(1);
	font_size_spinbox->set_suffix(" (0 = default)");
	font_size_spinbox->set_h_size_flags(SIZE_EXPAND_FILL);
	font_size_spinbox->connect(SceneStringName(value_changed), callable_mp(this, &ClaudeTerminalDock::_on_font_size_changed));
	font_hbox->add_child(font_size_spinbox);

	// Scrollback lines.
	HBoxContainer *scroll_hbox = memnew(HBoxContainer);
	vbox->add_child(scroll_hbox);

	Label *scroll_label = memnew(Label);
	scroll_label->set_text(TTR("Scrollback:"));
	scroll_label->set_custom_minimum_size(Size2(130, 0));
	scroll_hbox->add_child(scroll_label);

	scrollback_spinbox = memnew(SpinBox);
	scrollback_spinbox->set_min(100);
	scrollback_spinbox->set_max(100000);
	scrollback_spinbox->set_step(100);
	scrollback_spinbox->set_h_size_flags(SIZE_EXPAND_FILL);
	scrollback_spinbox->connect(SceneStringName(value_changed), callable_mp(this, &ClaudeTerminalDock::_on_scrollback_changed));
	scroll_hbox->add_child(scrollback_spinbox);

	// Hint.
	vbox->add_child(memnew(HSeparator));

	Label *hint_label = memnew(Label);
	hint_label->set_text(TTR("Command and scrollback changes apply to new tabs only."));
	hint_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	hint_label->add_theme_color_override("font_color", Color(0.7, 0.7, 0.7));
	vbox->add_child(hint_label);
}

void ClaudeTerminalDock::_on_terminal_cmd_changed(const String &p_text) {
	_set_setting("network/claude_mcp/terminal_command", p_text);
	terminal_command = p_text;
}

void ClaudeTerminalDock::_on_font_size_changed(double p_value) {
	_set_setting("network/claude_mcp/terminal_font_size", (int)p_value);

	// Live-update all open terminal views.
	for (int i = 0; i < tabs.size(); i++) {
		if (tabs[i].view) {
			tabs[i].view->refresh_font();
		}
	}
}

void ClaudeTerminalDock::_on_scrollback_changed(double p_value) {
	_set_setting("network/claude_mcp/terminal_scrollback_lines", (int)p_value);
	scrollback_limit = (int)p_value;
}

void ClaudeTerminalDock::_set_setting(const String &p_key, const Variant &p_value) {
	EditorSettings::get_singleton()->set(p_key, p_value);
	EditorSettings::get_singleton()->notify_changes();
	EditorSettings::get_singleton()->save();
}

#endif // WINDOWS_ENABLED
