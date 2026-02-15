/**************************************************************************/
/*  con_pty_process.cpp                                                   */
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

#include "con_pty_process.h"

#ifdef WINDOWS_ENABLED

// Ensure Windows 10+ APIs (ConPTY) are available in MinGW headers.
// The ConPTY functions are guarded by NTDDI_VERSION >= NTDDI_WIN10_RS5.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#elif _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#elif NTDDI_VERSION < 0x0A000006
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE = ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)
// = 0x00020016
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

#include "core/os/os.h"
#include "core/string/print_string.h"

#define INVALID_HANDLE ((void *)INVALID_HANDLE_VALUE)

void ConPtyProcess::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "command", "cols", "rows"), &ConPtyProcess::start, DEFVAL(80), DEFVAL(24));
	ClassDB::bind_method(D_METHOD("stop"), &ConPtyProcess::stop);
	ClassDB::bind_method(D_METHOD("is_running"), &ConPtyProcess::is_running);
	ClassDB::bind_method(D_METHOD("write_input", "data"), &ConPtyProcess::write_input);
	ClassDB::bind_method(D_METHOD("drain_output"), &ConPtyProcess::drain_output);
	ClassDB::bind_method(D_METHOD("resize", "cols", "rows"), &ConPtyProcess::resize);
	ClassDB::bind_method(D_METHOD("get_cols"), &ConPtyProcess::get_cols);
	ClassDB::bind_method(D_METHOD("get_rows"), &ConPtyProcess::get_rows);
}

void ConPtyProcess::_read_thread_func(void *p_userdata) {
	ConPtyProcess *pty = static_cast<ConPtyProcess *>(p_userdata);
	const int BUFFER_SIZE = 4096;
	uint8_t buffer[BUFFER_SIZE];

	while (!pty->exit_flag.is_set()) {
		DWORD bytes_read = 0;
		BOOL success = ReadFile((HANDLE)pty->pipe_out, buffer, BUFFER_SIZE, &bytes_read, nullptr);

		if (!success || bytes_read == 0) {
			// Pipe closed or error - process likely exited.
			break;
		}

		pty->output_mutex.lock();
		int old_size = pty->output_buffer.size();
		pty->output_buffer.resize(old_size + bytes_read);
		memcpy(pty->output_buffer.ptrw() + old_size, buffer, bytes_read);
		pty->output_mutex.unlock();
	}

	pty->running = false;
}

Error ConPtyProcess::start(const String &p_command, int p_cols, int p_rows) {
	if (running) {
		return ERR_ALREADY_IN_USE;
	}

	cols = p_cols;
	rows = p_rows;

	// Create pipes for ConPTY I/O.
	HANDLE pipe_in_read = INVALID_HANDLE_VALUE;
	HANDLE pipe_out_write = INVALID_HANDLE_VALUE;

	if (!CreatePipe(&pipe_in_read, (HANDLE *)&pipe_in, nullptr, 0)) {
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "ConPTY: Failed to create input pipe.");
	}

	if (!CreatePipe((HANDLE *)&pipe_out, &pipe_out_write, nullptr, 0)) {
		CloseHandle(pipe_in_read);
		CloseHandle((HANDLE)pipe_in);
		pipe_in = nullptr;
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "ConPTY: Failed to create output pipe.");
	}

	pipe_pty_in = (void *)pipe_in_read;
	pipe_pty_out = (void *)pipe_out_write;

	// Create the pseudoconsole.
	COORD size;
	size.X = (SHORT)cols;
	size.Y = (SHORT)rows;

	HPCON hpc = nullptr;
	HRESULT hr = CreatePseudoConsole(size, (HANDLE)pipe_pty_in, (HANDLE)pipe_pty_out, 0, &hpc);
	if (FAILED(hr)) {
		_cleanup();
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("ConPTY: CreatePseudoConsole failed with HRESULT 0x%08x.", (uint32_t)hr));
	}
	pty_handle = (void *)hpc;

	// Close the PTY-side pipe handles now that the pseudoconsole owns them.
	CloseHandle((HANDLE)pipe_pty_in);
	pipe_pty_in = nullptr;
	CloseHandle((HANDLE)pipe_pty_out);
	pipe_pty_out = nullptr;

	// Setup startup info with pseudoconsole attribute.
	STARTUPINFOEXW si = {};
	si.StartupInfo.cb = sizeof(si);

	SIZE_T attr_list_size = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_list_size);

	si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)memalloc(attr_list_size);
	ERR_FAIL_NULL_V_MSG(si.lpAttributeList, ERR_OUT_OF_MEMORY, "ConPTY: Failed to allocate attribute list.");

	if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_list_size)) {
		memfree(si.lpAttributeList);
		_cleanup();
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "ConPTY: InitializeProcThreadAttributeList failed.");
	}

	if (!UpdateProcThreadAttribute(
				si.lpAttributeList,
				0,
				PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
				(HPCON)pty_handle,
				sizeof(HPCON),
				nullptr,
				nullptr)) {
		DeleteProcThreadAttributeList(si.lpAttributeList);
		memfree(si.lpAttributeList);
		_cleanup();
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "ConPTY: UpdateProcThreadAttribute failed.");
	}

	// Build command line.
	Char16String cmd_utf16 = p_command.utf16();

	DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT;

	PROCESS_INFORMATION pi = {};

	BOOL created = CreateProcessW(
			nullptr,
			(LPWSTR)cmd_utf16.ptrw(),
			nullptr,
			nullptr,
			FALSE,
			creation_flags,
			nullptr,
			nullptr,
			&si.StartupInfo,
			&pi);

	DeleteProcThreadAttributeList(si.lpAttributeList);
	memfree(si.lpAttributeList);

	if (!created) {
		DWORD err = GetLastError();
		_cleanup();
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("ConPTY: CreateProcessW failed with error %d.", (uint32_t)err));
	}

	process_handle = (void *)pi.hProcess;
	thread_handle = (void *)pi.hThread;

	running = true;
	exit_flag.clear();

	// Start background read thread.
	read_thread.start(_read_thread_func, this);

	return OK;
}

void ConPtyProcess::stop() {
	if (!running) {
		return;
	}

	exit_flag.set();

	// Close the pseudoconsole first - this will signal EOF to the child
	// and unblock the ReadFile in the read thread.
	if (pty_handle) {
		ClosePseudoConsole((HPCON)pty_handle);
		pty_handle = nullptr;
	}

	// Terminate the child process if still running.
	if (process_handle) {
		DWORD exit_code = 0;
		if (GetExitCodeProcess((HANDLE)process_handle, &exit_code) && exit_code == STILL_ACTIVE) {
			TerminateProcess((HANDLE)process_handle, 1);
		}
	}

	if (read_thread.is_started()) {
		read_thread.wait_to_finish();
	}

	_cleanup();
	running = false;
}

void ConPtyProcess::write_input(const PackedByteArray &p_data) {
	if (!running || !pipe_in) {
		return;
	}

	DWORD bytes_written = 0;
	WriteFile((HANDLE)pipe_in, p_data.ptr(), p_data.size(), &bytes_written, nullptr);
}

PackedByteArray ConPtyProcess::drain_output() {
	PackedByteArray result;

	output_mutex.lock();
	if (output_buffer.size() > 0) {
		result.resize(output_buffer.size());
		memcpy(result.ptrw(), output_buffer.ptr(), output_buffer.size());
		output_buffer.clear();
	}
	output_mutex.unlock();

	return result;
}

Error ConPtyProcess::resize(int p_cols, int p_rows) {
	if (!running || !pty_handle) {
		return ERR_UNCONFIGURED;
	}

	cols = p_cols;
	rows = p_rows;

	COORD size;
	size.X = (SHORT)p_cols;
	size.Y = (SHORT)p_rows;

	HRESULT hr = ResizePseudoConsole((HPCON)pty_handle, size);
	if (FAILED(hr)) {
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("ConPTY: ResizePseudoConsole failed with HRESULT 0x%08x.", (uint32_t)hr));
	}

	return OK;
}

void ConPtyProcess::_cleanup() {
	if (pipe_in) {
		CloseHandle((HANDLE)pipe_in);
		pipe_in = nullptr;
	}
	if (pipe_out) {
		CloseHandle((HANDLE)pipe_out);
		pipe_out = nullptr;
	}
	if (pipe_pty_in) {
		CloseHandle((HANDLE)pipe_pty_in);
		pipe_pty_in = nullptr;
	}
	if (pipe_pty_out) {
		CloseHandle((HANDLE)pipe_pty_out);
		pipe_pty_out = nullptr;
	}
	if (process_handle) {
		CloseHandle((HANDLE)process_handle);
		process_handle = nullptr;
	}
	if (thread_handle) {
		CloseHandle((HANDLE)thread_handle);
		thread_handle = nullptr;
	}
	if (pty_handle) {
		ClosePseudoConsole((HPCON)pty_handle);
		pty_handle = nullptr;
	}
}

ConPtyProcess::ConPtyProcess() {
}

ConPtyProcess::~ConPtyProcess() {
	stop();
}

#endif // WINDOWS_ENABLED
