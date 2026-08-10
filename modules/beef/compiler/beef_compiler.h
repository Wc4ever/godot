/**************************************************************************/
/*  beef_compiler.h                                                       */
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

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/callable.h"

// Diagnostic logging for the Beef module (the "BeefCompiler: ..." / "BeefIDEHelper: ..." lines).
// Routed to the editor's Beef bottom-panel Output tab when a sink is registered (by BeefEditorPlugin);
// otherwise falls back to print_line (stdout / the general editor log) — e.g. in the running game,
// headless tools, or non-editor builds.
void beef_log(const String &p_msg);
void beef_set_log_sink(const Callable &p_sink);

// Wrapper around the user-installed Beef dev tools (BeefBuild.exe).
// Mirrors the C# module approach: external compiler, results loaded at runtime.
// Requires Beef IDE installed: https://www.beeflang.org/

class BeefCompiler {
	String beef_build_path;
	bool found = false;
	bool _compile_blocked = false; // true after a failed compile to prevent error spam

	void *dll_handle = nullptr;
	String _shadow_path; // when non-empty, the loaded DLL is a private copy (real output stays unlocked)

	// Resident builder (GodotBeefBuild.dll) — a warm, in-process BeefBuild whose type system stays
	// loaded across builds, so an incremental edit skips the ~1.5s frontend (measured ~40ms vs ~3.6s).
	// Optional and best-effort: if the DLL is absent, the config/workspace differs from what it was
	// initialized with, or a resident build fails, compile() transparently falls back to spawning
	// BeefBuild.exe (which also yields full diagnostics). Windows-only for now.
	void *_resident_handle = nullptr;
	bool _resident_init_done = false;
	String _resident_workspace; // workspace the resident builder was Init'd against
	String _resident_config; // config it was Init'd against
	typedef int32_t (*ResidentInitFn)(const char *, const char *);
	typedef int32_t (*ResidentCompileFn)();
	typedef void (*ResidentMarkChangedFn)(const char *);
	ResidentInitFn _resident_init = nullptr;
	ResidentCompileFn _resident_compile = nullptr;
	ResidentMarkChangedFn _resident_mark_changed = nullptr;

	// Pin our Beef DLLs by full path so the process uses them, not a stray installed Beef on PATH
	// (Windows resolves DLLs by base name). No-op off Windows.
	void _pin_toolchain_runtime();
	// Load GodotBeefBuild.dll (next to BeefBuild.exe) and resolve its exports. Idempotent; returns
	// true once the resident builder is available.
	bool _load_resident_builder();
	// Fast path: Init-once + mark user sources changed + Compile via the resident DLL. Returns true
	// ONLY on a successful resident build; false means "use the spawn fallback" (absent / config
	// mismatch / build failed).
	bool _try_resident_compile(const String &p_workspace_dir, const String &p_project_name,
			const String &p_config, String &r_dll_path);
	// A build cache from a different BeefBuild crashes the resident's in-process clean-rebuild; clean
	// it (once) when a toolchain-version marker mismatches, then record the marker after a good build.
	void _clean_if_foreign_cache(const String &p_workspace_dir, const String &p_config);
	void _write_cache_marker(const String &p_workspace_dir, const String &p_config);
	// Recursively mark every user src/*.bf as changed so the warm compiler reparses them (bindings
	// stay warm). The engine regenerates registrars + the user edits scripts before each build, so
	// marking all user sources is robust without tracking individual edits.
	void _mark_user_sources_changed(const String &p_workspace_dir);

	static BeefCompiler *_singleton;

	// Called immediately before FreeLibrary so live Beef objects can be destroyed first.
	// Without this, Beef's debug runtime reports them as memory leaks at DLL_PROCESS_DETACH.
	static void (*s_pre_unload_callback)();

public:
	static BeefCompiler *get_singleton() { return _singleton; }

	// Register a callback invoked immediately before every FreeLibrary call.
	// BeefLanguage sets this in init() to call _pre_unload_destroy_instances().
	void set_pre_unload_callback(void (*cb)()) { s_pre_unload_callback = cb; }

	// Search for BeefBuild.exe. p_override_path skips auto-detection if non-empty and valid.
	bool find_beef_tools(const String &p_override_path = String());

	// Create BeefSpace.toml, BeefProj.toml, GodotBindings/ sub-project and src/ in
	// p_workspace_dir if they don't exist yet.
	void ensure_project_files(const String &p_workspace_dir, const String &p_project_name);
	bool is_found() const { return found; }
	String get_beef_build_path() const { return beef_build_path; }

	// After a failed compile the compiler is "blocked" to avoid spamming errors on every
	// script reload. Call unblock_compile() to allow the next attempt (e.g. after the
	// user fixes their code or adds source files).
	bool is_compile_blocked() const { return _compile_blocked; }
	void unblock_compile() { _compile_blocked = false; }

	// Returns the path where generated Godot API bindings (.bf files) should live.
	static String get_bindings_src_dir(const String &p_workspace_dir) {
		return p_workspace_dir.path_join("GodotBindings").path_join("src");
	}

	// Deterministic output DLL path for a workspace/project/config (no compile). Lets the engine
	// load an externally-built DLL (e.g. from BeefIDE) without invoking BeefBuild itself.
	// Convention: <workspace>/build/<config>_Win64/<project>/<project>.dll
	static String get_dll_path(const String &p_workspace_dir, const String &p_project_name, const String &p_config);

	// Run BeefBuild on p_workspace_dir (auto-creates BeefSpace.toml + BeefProj.toml if missing).
	// On success, r_dll_path holds the output DLL path. If r_output is non-null it receives the full
	// (raw) BeefBuild stdout/stderr text regardless of success, for display in a build panel.
	// p_prefer_spawn forces the robust spawned BeefBuild.exe and skips the in-process resident builder
	// (used for a clean rebuild, whose delete would corrupt the warm resident's view of the cache).
	bool compile(const String &p_workspace_dir, const String &p_project_name,
			const String &p_config, String &r_dll_path, Vector<String> &r_errors, String *r_output = nullptr,
			bool p_prefer_spawn = false);

	// Deterministic path of the wasm SIDE_MODULE the web export ships:
	// <workspace>/build/<config>_wasm32/<project>/<project>.side.wasm
	static String get_wasm_side_module_path(const String &p_workspace_dir, const String &p_project_name, const String &p_config);

	// Build the Beef scripts as an Emscripten wasm SIDE_MODULE for web export. Runs BeefBuild with
	// -platform=wasm32 to compile the objects, then links them with emcc into a position-independent
	// side module the (wasm) engine dlopen()s at runtime. Passes -Wl,-O0 to disable wasm-ld's
	// SHF_STRINGS merge, which mis-relocates string-literal data under PIC (see WEB_EXPORT_FEASIBILITY).
	// Linked as SIDE_MODULE=1 so every symbol is auto-exported — both the BeefGodot_* entry points the
	// engine dlsym()s and the module's own data symbols its GOT entries must self-resolve at load.
	// p_emscripten_dir / p_wasm_runtime_dir override auto-detection of the emcc SDK and the Beef wasm
	// runtime objects. On success r_out_path holds the produced .side.wasm.
	bool build_wasm_side_module(const String &p_workspace_dir, const String &p_project_name,
			const String &p_config, const String &p_emscripten_dir, const String &p_wasm_runtime_dir,
			String &r_out_path, Vector<String> &r_errors);

	// Delete the build output directory for a config (forces a from-scratch rebuild). Safe no-op if
	// the directory does not exist. The DLL must be unloaded first (Windows locks a loaded DLL).
	void clean_build(const String &p_workspace_dir, const String &p_config);

	// Shared DLL management — one DLL covers all Beef scripts in a Godot project.
	// p_quiet downgrades a load failure from an error to no print (the caller reports it).
	// p_shadow loads a private copy of the DLL so the real output file stays unlocked — then an
	// external build (or our own) can relink it without us first calling FreeLibrary, which is unsafe
	// with the Beef debug runtime. Used for the editor's load; the running game loads in place.
	bool load_dll(const String &p_path, bool p_quiet = false, bool p_shadow = false);
	// p_free_library=false skips the FreeLibrary call (used at process shutdown to avoid the Beef
	// debug-runtime scan-thread crash); the OS reclaims the DLL on exit.
	void unload_dll(bool p_free_library = true);
	bool is_dll_loaded() const { return dll_handle != nullptr; }
	void *get_proc(const char *p_name);

	void cleanup();
};
