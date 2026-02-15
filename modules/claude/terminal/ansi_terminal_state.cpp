/**************************************************************************/
/*  ansi_terminal_state.cpp                                               */
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

#include "ansi_terminal_state.h"

#include "core/string/print_string.h"

// Standard ANSI 8-color palette + bright variants.
const Color AnsiTerminalState::ansi_colors[16] = {
	Color(0.0f, 0.0f, 0.0f),       // 0: Black
	Color(0.67f, 0.0f, 0.0f),      // 1: Red
	Color(0.0f, 0.67f, 0.0f),      // 2: Green
	Color(0.67f, 0.67f, 0.0f),     // 3: Yellow
	Color(0.0f, 0.0f, 0.67f),      // 4: Blue
	Color(0.67f, 0.0f, 0.67f),     // 5: Magenta
	Color(0.0f, 0.67f, 0.67f),     // 6: Cyan
	Color(0.75f, 0.75f, 0.75f),    // 7: White
	Color(0.33f, 0.33f, 0.33f),    // 8: Bright Black
	Color(1.0f, 0.33f, 0.33f),     // 9: Bright Red
	Color(0.33f, 1.0f, 0.33f),     // 10: Bright Green
	Color(1.0f, 1.0f, 0.33f),      // 11: Bright Yellow
	Color(0.33f, 0.33f, 1.0f),     // 12: Bright Blue
	Color(1.0f, 0.33f, 1.0f),      // 13: Bright Magenta
	Color(0.33f, 1.0f, 1.0f),      // 14: Bright Cyan
	Color(1.0f, 1.0f, 1.0f),       // 15: Bright White
};

void AnsiTerminalState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("feed_bytes", "data"), &AnsiTerminalState::feed_bytes);
	ClassDB::bind_method(D_METHOD("resize", "cols", "rows"), &AnsiTerminalState::resize);
	ClassDB::bind_method(D_METHOD("get_cols"), &AnsiTerminalState::get_cols);
	ClassDB::bind_method(D_METHOD("get_rows"), &AnsiTerminalState::get_rows);
	ClassDB::bind_method(D_METHOD("get_cursor_pos"), &AnsiTerminalState::get_cursor_pos);
	ClassDB::bind_method(D_METHOD("is_cursor_visible"), &AnsiTerminalState::is_cursor_visible);
	ClassDB::bind_method(D_METHOD("is_dirty"), &AnsiTerminalState::is_dirty);
	ClassDB::bind_method(D_METHOD("clear_dirty"), &AnsiTerminalState::clear_dirty);
	ClassDB::bind_method(D_METHOD("is_using_alt_screen"), &AnsiTerminalState::is_using_alt_screen);
	ClassDB::bind_method(D_METHOD("get_scrollback_count"), &AnsiTerminalState::get_scrollback_count);
	ClassDB::bind_method(D_METHOD("set_scrollback_limit", "limit"), &AnsiTerminalState::set_scrollback_limit);
	ClassDB::bind_method(D_METHOD("get_scrollback_limit"), &AnsiTerminalState::get_scrollback_limit);
}

AnsiTerminalState::AnsiTerminalState() {
	scroll_bottom = _rows - 1;
	_ensure_screen_size(primary_screen);
	_ensure_screen_size(alt_screen);
}

void AnsiTerminalState::set_default_fg(const Color &p_color) {
	default_fg = p_color;
	current_fg = p_color;
}

void AnsiTerminalState::set_default_bg(const Color &p_color) {
	default_bg = p_color;
	current_bg = p_color;
}

int AnsiTerminalState::_cell_index(int p_row, int p_col) const {
	return p_row * _cols + p_col;
}

void AnsiTerminalState::_ensure_screen_size(Vector<Cell> &p_screen) {
	int needed = _cols * _rows;
	if (p_screen.size() != needed) {
		int old_size = p_screen.size();
		p_screen.resize(needed);
		for (int i = old_size; i < needed; i++) {
			_clear_cell(p_screen.write[i]);
		}
	}
}

void AnsiTerminalState::_clear_cell(Cell &p_cell) {
	p_cell.ch = ' ';
	p_cell.fg = default_fg;
	p_cell.bg = default_bg;
	p_cell.bold = false;
	p_cell.dim = false;
	p_cell.italic = false;
	p_cell.underline = false;
	p_cell.inverse = false;
}

int AnsiTerminalState::_csi_param(int p_index, int p_default) const {
	if (p_index >= csi_params.size() || csi_params[p_index] <= 0) {
		return p_default;
	}
	return csi_params[p_index];
}

void AnsiTerminalState::resize(int p_cols, int p_rows) {
	if (p_cols == _cols && p_rows == _rows) {
		return;
	}

	int old_cols = _cols;
	int old_rows = _rows;

	_cols = MAX(1, p_cols);
	_rows = MAX(1, p_rows);
	scroll_top = 0;
	scroll_bottom = _rows - 1;

	// Resize both screens, preserving content where possible.
	auto resize_screen = [&](Vector<Cell> &p_screen) {
		Vector<Cell> new_screen;
		new_screen.resize(_cols * _rows);
		for (int i = 0; i < new_screen.size(); i++) {
			_clear_cell(new_screen.write[i]);
		}

		int copy_rows = MIN(old_rows, _rows);
		int copy_cols = MIN(old_cols, _cols);
		for (int r = 0; r < copy_rows; r++) {
			for (int c = 0; c < copy_cols; c++) {
				new_screen.write[r * _cols + c] = p_screen[r * old_cols + c];
			}
		}

		p_screen = new_screen;
	};

	resize_screen(primary_screen);
	resize_screen(alt_screen);

	cursor_row = CLAMP(cursor_row, 0, _rows - 1);
	cursor_col = CLAMP(cursor_col, 0, _cols - 1);

	dirty = true;
}

const AnsiTerminalState::Cell &AnsiTerminalState::get_cell(int p_row, int p_col) const {
	static Cell empty_cell;
	if (p_row < 0 || p_row >= _rows || p_col < 0 || p_col >= _cols) {
		return empty_cell;
	}
	const Vector<Cell> &screen = *active_screen;
	return screen[_cell_index(p_row, p_col)];
}

Vector2i AnsiTerminalState::get_cursor_pos() const {
	return Vector2i(cursor_col, cursor_row);
}

const Vector<AnsiTerminalState::Cell> &AnsiTerminalState::get_scrollback_line(int p_index) const {
	static Vector<Cell> empty_line;
	if (p_index < 0 || p_index >= scrollback.size()) {
		return empty_line;
	}
	return scrollback[p_index];
}

void AnsiTerminalState::feed_bytes(const PackedByteArray &p_data) {
	feed(p_data.ptr(), p_data.size());
}

void AnsiTerminalState::feed(const uint8_t *p_data, int p_len) {
	for (int i = 0; i < p_len; i++) {
		uint8_t byte = p_data[i];

		// UTF-8 multi-byte continuation.
		if (utf8_remaining > 0) {
			if ((byte & 0xC0) == 0x80) {
				utf8_codepoint = (utf8_codepoint << 6) | (byte & 0x3F);
				utf8_remaining--;
				if (utf8_remaining == 0) {
					if (parser_state == STATE_GROUND) {
						_put_char(utf8_codepoint);
					}
				}
				continue;
			} else {
				// Invalid continuation - reset and reprocess.
				utf8_remaining = 0;
			}
		}

		// UTF-8 start byte detection (only in ground state).
		if (parser_state == STATE_GROUND && byte >= 0xC0) {
			if ((byte & 0xE0) == 0xC0) {
				utf8_codepoint = byte & 0x1F;
				utf8_remaining = 1;
				continue;
			} else if ((byte & 0xF0) == 0xE0) {
				utf8_codepoint = byte & 0x0F;
				utf8_remaining = 2;
				continue;
			} else if ((byte & 0xF8) == 0xF0) {
				utf8_codepoint = byte & 0x07;
				utf8_remaining = 3;
				continue;
			}
		}

		switch (parser_state) {
			case STATE_GROUND: {
				if (byte == 0x1B) {
					parser_state = STATE_ESCAPE;
				} else if (byte == '\n' || byte == 0x0B || byte == 0x0C) {
					_new_line();
				} else if (byte == '\r') {
					_carriage_return();
				} else if (byte == '\b') {
					_backspace();
				} else if (byte == '\t') {
					_tab();
				} else if (byte == 0x07) {
					// BEL - ignore.
				} else if (byte >= 0x20 && byte < 0x7F) {
					_put_char((char32_t)byte);
				}
				// Other C0 controls ignored.
			} break;

			case STATE_ESCAPE: {
				if (byte == '[') {
					parser_state = STATE_CSI_ENTRY;
					csi_params.clear();
					csi_intermediate.clear();
					current_param = -1;
				} else if (byte == ']') {
					parser_state = STATE_OSC;
					osc_string.clear();
				} else if (byte == '7') {
					// DECSC - save cursor.
					saved_cursor_row = cursor_row;
					saved_cursor_col = cursor_col;
					parser_state = STATE_GROUND;
				} else if (byte == '8') {
					// DECRC - restore cursor.
					cursor_row = saved_cursor_row;
					cursor_col = saved_cursor_col;
					parser_state = STATE_GROUND;
					dirty = true;
				} else if (byte == 'D') {
					// IND - index (move down, scroll if at bottom).
					_new_line();
					parser_state = STATE_GROUND;
				} else if (byte == 'M') {
					// RI - reverse index (move up, scroll down if at top).
					if (cursor_row == scroll_top) {
						_scroll_down(1);
					} else if (cursor_row > 0) {
						cursor_row--;
					}
					parser_state = STATE_GROUND;
					dirty = true;
				} else if (byte == 'c') {
					// RIS - full reset.
					resize(_cols, _rows);
					cursor_row = 0;
					cursor_col = 0;
					cursor_visible = true;
					current_fg = default_fg;
					current_bg = default_bg;
					current_bold = false;
					current_dim = false;
					current_italic = false;
					current_underline = false;
					current_inverse = false;
					using_alt_screen = false;
					active_screen = &primary_screen;
					parser_state = STATE_GROUND;
					dirty = true;
				} else {
					// Unknown escape sequence.
					parser_state = STATE_GROUND;
				}
			} break;

			case STATE_CSI_ENTRY:
			case STATE_CSI_PARAM: {
				if (byte >= '0' && byte <= '9') {
					if (current_param < 0) {
						current_param = 0;
					}
					current_param = current_param * 10 + (byte - '0');
					parser_state = STATE_CSI_PARAM;
				} else if (byte == ';') {
					csi_params.push_back(current_param < 0 ? 0 : current_param);
					current_param = -1;
					parser_state = STATE_CSI_PARAM;
				} else if (byte == '?' || byte == '>' || byte == '!') {
					csi_intermediate += (char32_t)byte;
					parser_state = STATE_CSI_INTERMEDIATE;
				} else if (byte >= 0x20 && byte <= 0x2F) {
					csi_intermediate += (char32_t)byte;
					parser_state = STATE_CSI_INTERMEDIATE;
				} else if (byte >= 0x40 && byte <= 0x7E) {
					// Final byte - execute.
					csi_params.push_back(current_param < 0 ? 0 : current_param);
					_execute_csi((char32_t)byte);
					parser_state = STATE_GROUND;
				} else {
					// Invalid byte, abort.
					parser_state = STATE_GROUND;
				}
			} break;

			case STATE_CSI_INTERMEDIATE: {
				if (byte >= '0' && byte <= '9') {
					if (current_param < 0) {
						current_param = 0;
					}
					current_param = current_param * 10 + (byte - '0');
				} else if (byte == ';') {
					csi_params.push_back(current_param < 0 ? 0 : current_param);
					current_param = -1;
				} else if (byte >= 0x40 && byte <= 0x7E) {
					csi_params.push_back(current_param < 0 ? 0 : current_param);
					_execute_csi((char32_t)byte);
					parser_state = STATE_GROUND;
				} else {
					parser_state = STATE_GROUND;
				}
			} break;

			case STATE_OSC: {
				if (byte == 0x07 || byte == 0x1B) {
					// OSC terminated by BEL or ESC.
					// We ignore OSC sequences for now (window title, etc.).
					if (byte == 0x1B) {
						// Consume the following '\' if present.
						// We'll just reset to ground; the '\' will be handled as ground.
					}
					parser_state = STATE_GROUND;
				} else {
					osc_string += (char32_t)byte;
				}
			} break;
		}
	}
}

void AnsiTerminalState::_put_char(char32_t p_ch) {
	if (cursor_col >= _cols) {
		// Line wrap.
		_carriage_return();
		_new_line();
	}

	int idx = _cell_index(cursor_row, cursor_col);
	Cell &cell = active_screen->write[idx];
	cell.ch = p_ch;
	cell.fg = current_fg;
	cell.bg = current_bg;
	cell.bold = current_bold;
	cell.dim = current_dim;
	cell.italic = current_italic;
	cell.underline = current_underline;
	cell.inverse = current_inverse;

	cursor_col++;
	dirty = true;
}

void AnsiTerminalState::_new_line() {
	if (cursor_row == scroll_bottom) {
		_scroll_up(1);
	} else if (cursor_row < _rows - 1) {
		cursor_row++;
	}
	dirty = true;
}

void AnsiTerminalState::_carriage_return() {
	cursor_col = 0;
	dirty = true;
}

void AnsiTerminalState::_backspace() {
	if (cursor_col > 0) {
		cursor_col--;
		dirty = true;
	}
}

void AnsiTerminalState::_tab() {
	// Advance to next tab stop (every 8 columns).
	int next_tab = ((cursor_col / 8) + 1) * 8;
	cursor_col = MIN(next_tab, _cols - 1);
	dirty = true;
}

void AnsiTerminalState::_scroll_up(int p_count) {
	for (int n = 0; n < p_count; n++) {
		// Save scrolled-off line to scrollback (primary screen only).
		if (active_screen == &primary_screen && scroll_top == 0) {
			Vector<Cell> line;
			line.resize(_cols);
			for (int c = 0; c < _cols; c++) {
				line.write[c] = active_screen->get(_cell_index(scroll_top, c));
			}
			scrollback.push_back(line);
			if (scrollback.size() > scrollback_limit) {
				scrollback.remove_at(0);
			}
		}

		// Move lines up within scroll region.
		for (int r = scroll_top; r < scroll_bottom; r++) {
			for (int c = 0; c < _cols; c++) {
				active_screen->write[_cell_index(r, c)] = active_screen->get(_cell_index(r + 1, c));
			}
		}

		// Clear the bottom line of scroll region.
		for (int c = 0; c < _cols; c++) {
			_clear_cell(active_screen->write[_cell_index(scroll_bottom, c)]);
		}
	}
	dirty = true;
}

void AnsiTerminalState::_scroll_down(int p_count) {
	for (int n = 0; n < p_count; n++) {
		// Move lines down within scroll region.
		for (int r = scroll_bottom; r > scroll_top; r--) {
			for (int c = 0; c < _cols; c++) {
				active_screen->write[_cell_index(r, c)] = active_screen->get(_cell_index(r - 1, c));
			}
		}

		// Clear the top line of scroll region.
		for (int c = 0; c < _cols; c++) {
			_clear_cell(active_screen->write[_cell_index(scroll_top, c)]);
		}
	}
	dirty = true;
}

void AnsiTerminalState::_erase_in_display(int p_mode) {
	switch (p_mode) {
		case 0: {
			// Erase from cursor to end of screen.
			for (int c = cursor_col; c < _cols; c++) {
				_clear_cell(active_screen->write[_cell_index(cursor_row, c)]);
			}
			for (int r = cursor_row + 1; r < _rows; r++) {
				for (int c = 0; c < _cols; c++) {
					_clear_cell(active_screen->write[_cell_index(r, c)]);
				}
			}
		} break;
		case 1: {
			// Erase from start of screen to cursor.
			for (int r = 0; r < cursor_row; r++) {
				for (int c = 0; c < _cols; c++) {
					_clear_cell(active_screen->write[_cell_index(r, c)]);
				}
			}
			for (int c = 0; c <= cursor_col; c++) {
				_clear_cell(active_screen->write[_cell_index(cursor_row, c)]);
			}
		} break;
		case 2:
		case 3: {
			// Erase entire screen (3 also clears scrollback, but we handle that separately).
			for (int i = 0; i < active_screen->size(); i++) {
				_clear_cell(active_screen->write[i]);
			}
			if (p_mode == 3) {
				scrollback.clear();
			}
		} break;
	}
	dirty = true;
}

void AnsiTerminalState::_erase_in_line(int p_mode) {
	switch (p_mode) {
		case 0: {
			// Erase from cursor to end of line.
			for (int c = cursor_col; c < _cols; c++) {
				_clear_cell(active_screen->write[_cell_index(cursor_row, c)]);
			}
		} break;
		case 1: {
			// Erase from start of line to cursor.
			for (int c = 0; c <= cursor_col; c++) {
				_clear_cell(active_screen->write[_cell_index(cursor_row, c)]);
			}
		} break;
		case 2: {
			// Erase entire line.
			for (int c = 0; c < _cols; c++) {
				_clear_cell(active_screen->write[_cell_index(cursor_row, c)]);
			}
		} break;
	}
	dirty = true;
}

void AnsiTerminalState::_execute_csi(char32_t p_final) {
	bool is_private = csi_intermediate.contains("?");

	if (is_private) {
		// Private mode sequences (DEC).
		int mode = _csi_param(0, 0);

		if (p_final == 'h') {
			// Set mode.
			switch (mode) {
				case 25: {
					// Show cursor.
					cursor_visible = true;
					dirty = true;
				} break;
				case 1049: {
					// Switch to alternate screen buffer.
					if (!using_alt_screen) {
						saved_cursor_row = cursor_row;
						saved_cursor_col = cursor_col;
						using_alt_screen = true;
						active_screen = &alt_screen;
						// Clear alt screen.
						for (int i = 0; i < alt_screen.size(); i++) {
							_clear_cell(alt_screen.write[i]);
						}
						cursor_row = 0;
						cursor_col = 0;
						dirty = true;
					}
				} break;
				case 1: {
					// Application cursor keys - noted but no action needed on our side.
				} break;
				case 2004: {
					// Bracketed paste mode enable - noted.
				} break;
			}
		} else if (p_final == 'l') {
			// Reset mode.
			switch (mode) {
				case 25: {
					// Hide cursor.
					cursor_visible = false;
					dirty = true;
				} break;
				case 1049: {
					// Switch back to primary screen.
					if (using_alt_screen) {
						using_alt_screen = false;
						active_screen = &primary_screen;
						cursor_row = saved_cursor_row;
						cursor_col = saved_cursor_col;
						dirty = true;
					}
				} break;
				case 1: {
					// Normal cursor keys.
				} break;
				case 2004: {
					// Bracketed paste mode disable.
				} break;
			}
		}
		return;
	}

	switch (p_final) {
		case 'A': {
			// CUU - Cursor Up.
			int n = _csi_param(0, 1);
			cursor_row = MAX(scroll_top, cursor_row - n);
			dirty = true;
		} break;
		case 'B': {
			// CUD - Cursor Down.
			int n = _csi_param(0, 1);
			cursor_row = MIN(scroll_bottom, cursor_row + n);
			dirty = true;
		} break;
		case 'C': {
			// CUF - Cursor Forward.
			int n = _csi_param(0, 1);
			cursor_col = MIN(_cols - 1, cursor_col + n);
			dirty = true;
		} break;
		case 'D': {
			// CUB - Cursor Back.
			int n = _csi_param(0, 1);
			cursor_col = MAX(0, cursor_col - n);
			dirty = true;
		} break;
		case 'E': {
			// CNL - Cursor Next Line.
			int n = _csi_param(0, 1);
			cursor_row = MIN(scroll_bottom, cursor_row + n);
			cursor_col = 0;
			dirty = true;
		} break;
		case 'F': {
			// CPL - Cursor Previous Line.
			int n = _csi_param(0, 1);
			cursor_row = MAX(scroll_top, cursor_row - n);
			cursor_col = 0;
			dirty = true;
		} break;
		case 'G': {
			// CHA - Cursor Horizontal Absolute.
			int col = _csi_param(0, 1) - 1;
			cursor_col = CLAMP(col, 0, _cols - 1);
			dirty = true;
		} break;
		case 'H':
		case 'f': {
			// CUP / HVP - Cursor Position.
			int row = _csi_param(0, 1) - 1;
			int col = _csi_param(1, 1) - 1;
			cursor_row = CLAMP(row, 0, _rows - 1);
			cursor_col = CLAMP(col, 0, _cols - 1);
			dirty = true;
		} break;
		case 'J': {
			// ED - Erase in Display.
			_erase_in_display(_csi_param(0, 0));
		} break;
		case 'K': {
			// EL - Erase in Line.
			_erase_in_line(_csi_param(0, 0));
		} break;
		case 'L': {
			// IL - Insert Lines.
			int n = _csi_param(0, 1);
			if (cursor_row >= scroll_top && cursor_row <= scroll_bottom) {
				int old_top = scroll_top;
				scroll_top = cursor_row;
				_scroll_down(n);
				scroll_top = old_top;
			}
		} break;
		case 'M': {
			// DL - Delete Lines.
			int n = _csi_param(0, 1);
			if (cursor_row >= scroll_top && cursor_row <= scroll_bottom) {
				int old_top = scroll_top;
				scroll_top = cursor_row;
				_scroll_up(n);
				scroll_top = old_top;
			}
		} break;
		case 'P': {
			// DCH - Delete Characters.
			int n = _csi_param(0, 1);
			int max_shift = _cols - cursor_col;
			n = MIN(n, max_shift);
			for (int c = cursor_col; c < _cols - n; c++) {
				active_screen->write[_cell_index(cursor_row, c)] = active_screen->get(_cell_index(cursor_row, c + n));
			}
			for (int c = _cols - n; c < _cols; c++) {
				_clear_cell(active_screen->write[_cell_index(cursor_row, c)]);
			}
			dirty = true;
		} break;
		case 'S': {
			// SU - Scroll Up.
			int n = _csi_param(0, 1);
			_scroll_up(n);
		} break;
		case 'T': {
			// SD - Scroll Down.
			int n = _csi_param(0, 1);
			_scroll_down(n);
		} break;
		case 'X': {
			// ECH - Erase Characters.
			int n = _csi_param(0, 1);
			for (int i = 0; i < n && (cursor_col + i) < _cols; i++) {
				_clear_cell(active_screen->write[_cell_index(cursor_row, cursor_col + i)]);
			}
			dirty = true;
		} break;
		case '@': {
			// ICH - Insert Characters.
			int n = _csi_param(0, 1);
			int max_shift = _cols - cursor_col;
			n = MIN(n, max_shift);
			// Shift right.
			for (int c = _cols - 1; c >= cursor_col + n; c--) {
				active_screen->write[_cell_index(cursor_row, c)] = active_screen->get(_cell_index(cursor_row, c - n));
			}
			for (int c = cursor_col; c < cursor_col + n && c < _cols; c++) {
				_clear_cell(active_screen->write[_cell_index(cursor_row, c)]);
			}
			dirty = true;
		} break;
		case 'd': {
			// VPA - Vertical Position Absolute.
			int row = _csi_param(0, 1) - 1;
			cursor_row = CLAMP(row, 0, _rows - 1);
			dirty = true;
		} break;
		case 'm': {
			// SGR - Select Graphic Rendition.
			_execute_sgr();
		} break;
		case 'r': {
			// DECSTBM - Set Scrolling Region.
			int top = _csi_param(0, 1) - 1;
			int bottom = _csi_param(1, _rows) - 1;
			scroll_top = CLAMP(top, 0, _rows - 1);
			scroll_bottom = CLAMP(bottom, 0, _rows - 1);
			if (scroll_top >= scroll_bottom) {
				scroll_top = 0;
				scroll_bottom = _rows - 1;
			}
			cursor_row = 0;
			cursor_col = 0;
			dirty = true;
		} break;
		case 's': {
			// SCP - Save Cursor Position.
			saved_cursor_row = cursor_row;
			saved_cursor_col = cursor_col;
		} break;
		case 'u': {
			// RCP - Restore Cursor Position.
			cursor_row = saved_cursor_row;
			cursor_col = saved_cursor_col;
			dirty = true;
		} break;
		case 'n': {
			// DSR - Device Status Report (we don't respond, just acknowledge).
		} break;
		case 'c': {
			// DA - Device Attributes (we don't respond).
		} break;
		default: {
			// Unrecognized CSI sequence.
#ifdef DEBUG_ENABLED
			String params_str;
			for (int j = 0; j < csi_params.size(); j++) {
				if (j > 0) {
					params_str += ";";
				}
				params_str += itos(csi_params[j]);
			}
			print_verbose(vformat("Terminal: Unrecognized CSI sequence: %s%s%c", csi_intermediate, params_str, (char)p_final));
#endif
		} break;
	}
}

void AnsiTerminalState::_execute_sgr() {
	if (csi_params.size() == 0 || (csi_params.size() == 1 && csi_params[0] == 0)) {
		// Reset all attributes.
		current_fg = default_fg;
		current_bg = default_bg;
		current_bold = false;
		current_dim = false;
		current_italic = false;
		current_underline = false;
		current_inverse = false;
		return;
	}

	for (int i = 0; i < csi_params.size(); i++) {
		int p = csi_params[i];

		switch (p) {
			case 0: {
				current_fg = default_fg;
				current_bg = default_bg;
				current_bold = false;
				current_dim = false;
				current_italic = false;
				current_underline = false;
				current_inverse = false;
			} break;
			case 1:
				current_bold = true;
				break;
			case 2:
				current_dim = true;
				break;
			case 3:
				current_italic = true;
				break;
			case 4:
				current_underline = true;
				break;
			case 7:
				current_inverse = true;
				break;
			case 21:
			case 22:
				current_bold = false;
				current_dim = false;
				break;
			case 23:
				current_italic = false;
				break;
			case 24:
				current_underline = false;
				break;
			case 27:
				current_inverse = false;
				break;

			// Foreground colors (standard 8).
			case 30:
			case 31:
			case 32:
			case 33:
			case 34:
			case 35:
			case 36:
			case 37:
				current_fg = ansi_colors[p - 30];
				break;

			case 38: {
				// Extended foreground color.
				if (i + 1 < csi_params.size()) {
					if (csi_params[i + 1] == 5 && i + 2 < csi_params.size()) {
						// 256-color mode.
						int color_idx = csi_params[i + 2];
						if (color_idx >= 0 && color_idx < 16) {
							current_fg = ansi_colors[color_idx];
						} else if (color_idx >= 16 && color_idx < 232) {
							// 6x6x6 color cube.
							int idx = color_idx - 16;
							int r = idx / 36;
							int g = (idx % 36) / 6;
							int b = idx % 6;
							current_fg = Color(r / 5.0f, g / 5.0f, b / 5.0f);
						} else if (color_idx >= 232 && color_idx < 256) {
							// Grayscale ramp.
							float v = (color_idx - 232) * 10.0f / 255.0f + 8.0f / 255.0f;
							current_fg = Color(v, v, v);
						}
						i += 2;
					} else if (csi_params[i + 1] == 2 && i + 4 < csi_params.size()) {
						// 24-bit RGB.
						float r = csi_params[i + 2] / 255.0f;
						float g = csi_params[i + 3] / 255.0f;
						float b = csi_params[i + 4] / 255.0f;
						current_fg = Color(r, g, b);
						i += 4;
					}
				}
			} break;

			case 39:
				// Default foreground.
				current_fg = default_fg;
				break;

			// Background colors (standard 8).
			case 40:
			case 41:
			case 42:
			case 43:
			case 44:
			case 45:
			case 46:
			case 47:
				current_bg = ansi_colors[p - 40];
				break;

			case 48: {
				// Extended background color.
				if (i + 1 < csi_params.size()) {
					if (csi_params[i + 1] == 5 && i + 2 < csi_params.size()) {
						// 256-color mode.
						int color_idx = csi_params[i + 2];
						if (color_idx >= 0 && color_idx < 16) {
							current_bg = ansi_colors[color_idx];
						} else if (color_idx >= 16 && color_idx < 232) {
							int idx = color_idx - 16;
							int r = idx / 36;
							int g = (idx % 36) / 6;
							int b = idx % 6;
							current_bg = Color(r / 5.0f, g / 5.0f, b / 5.0f);
						} else if (color_idx >= 232 && color_idx < 256) {
							float v = (color_idx - 232) * 10.0f / 255.0f + 8.0f / 255.0f;
							current_bg = Color(v, v, v);
						}
						i += 2;
					} else if (csi_params[i + 1] == 2 && i + 4 < csi_params.size()) {
						float r = csi_params[i + 2] / 255.0f;
						float g = csi_params[i + 3] / 255.0f;
						float b = csi_params[i + 4] / 255.0f;
						current_bg = Color(r, g, b);
						i += 4;
					}
				}
			} break;

			case 49:
				// Default background.
				current_bg = default_bg;
				break;

			// Bright foreground colors.
			case 90:
			case 91:
			case 92:
			case 93:
			case 94:
			case 95:
			case 96:
			case 97:
				current_fg = ansi_colors[p - 90 + 8];
				break;

			// Bright background colors.
			case 100:
			case 101:
			case 102:
			case 103:
			case 104:
			case 105:
			case 106:
			case 107:
				current_bg = ansi_colors[p - 100 + 8];
				break;
		}
	}
}
