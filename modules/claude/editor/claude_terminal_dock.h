/**************************************************************************/
/*  claude_terminal_dock.h                                                */
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

#pragma once

#ifdef WINDOWS_ENABLED

#include "../terminal/ansi_terminal_state.h"
#include "../terminal/con_pty_process.h"
#include "editor/docks/editor_dock.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/tab_bar.h"

class Timer;

// Renders a terminal character grid via custom _draw().
class TerminalView : public Control {
	GDCLASS(TerminalView, Control);

private:
	Ref<ConPtyProcess> pty;
	Ref<AnsiTerminalState> state;

	// Rendering metrics.
	Ref<Font> font;
	Ref<Font> bold_font;
	Ref<Font> italic_font;
	Ref<Font> bold_italic_font;
	int font_size = 14;
	float cell_width = 0;
	float cell_height = 0;
	float ascent = 0;

	// Cursor blinking.
	bool cursor_blink_on = true;
	Timer *blink_timer = nullptr;

	// Scrollback viewing.
	int scroll_offset = 0; // 0 = at bottom (live), positive = scrolled up.
	VScrollBar *scrollbar = nullptr;

	// Selection state.
	bool selecting = false;
	Vector2i selection_start; // (col, row) in screen+scrollback space.
	Vector2i selection_end;
	bool has_selection = false;

	// Process state.
	bool process_exited = false;

	void _update_font();
	void _recompute_grid_size();
	void _on_blink_timer();
	void _on_scrollbar_changed(double p_value);

	void _draw_cell(const AnsiTerminalState::Cell &p_cell, int p_x, int p_y, bool p_is_cursor, bool p_is_selected);
	String _get_selected_text() const;
	Vector2i _screen_to_cell(const Vector2 &p_pos) const;
	bool _is_cell_selected(int p_col, int p_row_in_view) const;

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();

public:
	void set_pty(const Ref<ConPtyProcess> &p_pty) { pty = p_pty; }
	void set_state(const Ref<AnsiTerminalState> &p_state) { state = p_state; }
	Ref<ConPtyProcess> get_pty() const { return pty; }
	Ref<AnsiTerminalState> get_state() const { return state; }

	bool is_process_exited() const { return process_exited; }
	void set_process_exited(bool p_exited) { process_exited = p_exited; }

	void clear_selection();
	void update_scrollbar();

	TerminalView();
};

// Bottom dock panel with tabbed terminal instances.
class ClaudeTerminalDock : public EditorDock {
	GDCLASS(ClaudeTerminalDock, EditorDock);

private:
	struct TerminalTab {
		Ref<ConPtyProcess> pty;
		Ref<AnsiTerminalState> state;
		TerminalView *view = nullptr;
		int tab_index = 0;
	};

	TabBar *tab_bar = nullptr;
	Button *new_tab_button = nullptr;
	VBoxContainer *main_vbox = nullptr;
	Vector<TerminalTab> tabs;
	int active_tab = -1;

	String terminal_command;
	int scrollback_limit = 10000;

	void _add_terminal_tab();
	void _close_terminal_tab(int p_index);
	void _tab_changed(int p_index);
	void _tab_close_requested(int p_index);
	void _new_tab_pressed();
	void _show_tab(int p_index);
	void _poll_terminals();
	void _update_tab_titles();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	ClaudeTerminalDock();
	~ClaudeTerminalDock();
};

#endif // WINDOWS_ENABLED
