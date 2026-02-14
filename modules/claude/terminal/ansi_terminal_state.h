/**************************************************************************/
/*  ansi_terminal_state.h                                                 */
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

#include "core/math/color.h"
#include "core/object/ref_counted.h"
#include "core/templates/vector.h"

class AnsiTerminalState : public RefCounted {
	GDCLASS(AnsiTerminalState, RefCounted);

public:
	struct Cell {
		char32_t ch = ' ';
		Color fg = Color(1, 1, 1);
		Color bg = Color(0, 0, 0, 0);
		bool bold = false;
		bool dim = false;
		bool italic = false;
		bool underline = false;
		bool inverse = false;
	};

private:
	enum ParserState {
		STATE_GROUND,
		STATE_ESCAPE,
		STATE_CSI_ENTRY,
		STATE_CSI_PARAM,
		STATE_CSI_INTERMEDIATE,
		STATE_OSC,
	};

	int _cols = 80;
	int _rows = 24;

	Vector<Cell> primary_screen;
	Vector<Cell> alt_screen;
	Vector<Cell> *active_screen = &primary_screen;
	bool using_alt_screen = false;

	// Scrollback for primary screen.
	Vector<Vector<Cell>> scrollback;
	int scrollback_limit = 10000;

	int cursor_row = 0;
	int cursor_col = 0;
	bool cursor_visible = true;

	// Saved cursor state.
	int saved_cursor_row = 0;
	int saved_cursor_col = 0;

	// Current SGR attributes.
	Color current_fg = Color(1, 1, 1);
	Color current_bg = Color(0, 0, 0, 0);
	bool current_bold = false;
	bool current_dim = false;
	bool current_italic = false;
	bool current_underline = false;
	bool current_inverse = false;

	// Configurable default colors (set by the view to match editor theme).
	Color default_fg = Color(1, 1, 1);
	Color default_bg = Color(0, 0, 0, 0);

	// Parser state.
	ParserState parser_state = STATE_GROUND;
	Vector<int> csi_params;
	String csi_intermediate;
	int current_param = -1; // -1 means no param started.
	String osc_string;

	// UTF-8 decode state.
	char32_t utf8_codepoint = 0;
	int utf8_remaining = 0;

	bool dirty = true;

	// Scroll region (top and bottom rows, inclusive).
	int scroll_top = 0;
	int scroll_bottom = 0; // Set to _rows - 1.

	// Standard ANSI 8-color palette + bright variants.
	static const Color ansi_colors[16];

	// Internal helpers.
	void _put_char(char32_t p_ch);
	void _execute_csi(char32_t p_final);
	void _execute_sgr();
	void _scroll_up(int p_count = 1);
	void _scroll_down(int p_count = 1);
	void _erase_in_display(int p_mode);
	void _erase_in_line(int p_mode);
	void _new_line();
	void _carriage_return();
	void _backspace();
	void _tab();
	int _cell_index(int p_row, int p_col) const;
	void _ensure_screen_size(Vector<Cell> &p_screen);
	void _clear_cell(Cell &p_cell);
	int _csi_param(int p_index, int p_default) const;

protected:
	static void _bind_methods();

public:
	void feed(const uint8_t *p_data, int p_len);
	void feed_bytes(const PackedByteArray &p_data);
	void resize(int p_cols, int p_rows);

	const Cell &get_cell(int p_row, int p_col) const;
	Vector2i get_cursor_pos() const;
	bool is_cursor_visible() const { return cursor_visible; }
	bool is_dirty() const { return dirty; }
	void clear_dirty() { dirty = false; }

	int get_cols() const { return _cols; }
	int get_rows() const { return _rows; }
	bool is_using_alt_screen() const { return using_alt_screen; }

	int get_scrollback_count() const { return scrollback.size(); }
	const Vector<Cell> &get_scrollback_line(int p_index) const;

	void set_scrollback_limit(int p_limit) { scrollback_limit = p_limit; }
	int get_scrollback_limit() const { return scrollback_limit; }

	void set_default_fg(const Color &p_color);
	void set_default_bg(const Color &p_color);
	Color get_default_fg() const { return default_fg; }
	Color get_default_bg() const { return default_bg; }

	AnsiTerminalState();
};
