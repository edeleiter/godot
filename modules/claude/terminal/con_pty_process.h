/**************************************************************************/
/*  con_pty_process.h                                                     */
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

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"

class ConPtyProcess : public RefCounted {
	GDCLASS(ConPtyProcess, RefCounted);

private:
	// Win32 handles stored as void* to avoid including <windows.h> in the header
	// (windows.h defines macros like IGNORE that break Godot headers).
	void *pty_handle = nullptr;
	void *pipe_in = nullptr; // Parent writes to child stdin.
	void *pipe_out = nullptr; // Parent reads from child stdout.
	void *pipe_pty_in = nullptr; // PTY side of stdin pipe.
	void *pipe_pty_out = nullptr; // PTY side of stdout pipe.
	void *process_handle = nullptr; // Child process handle.
	void *thread_handle = nullptr; // Child primary thread handle.

	Thread read_thread;
	Mutex output_mutex;
	Vector<uint8_t> output_buffer;
	SafeFlag exit_flag;

	int cols = 80;
	int rows = 24;
	bool running = false;

	static void _read_thread_func(void *p_userdata);
	void _cleanup();

protected:
	static void _bind_methods();

public:
	Error start(const String &p_command, int p_cols = 80, int p_rows = 24);
	void stop();
	bool is_running() const { return running; }

	void write_input(const PackedByteArray &p_data);
	PackedByteArray drain_output();

	Error resize(int p_cols, int p_rows);

	int get_cols() const { return cols; }
	int get_rows() const { return rows; }

	ConPtyProcess();
	~ConPtyProcess();
};

#endif // WINDOWS_ENABLED
