#ifndef BEEF_COMPILER_H
#define BEEF_COMPILER_H

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
	bool compile(const String &p_workspace_dir, const String &p_project_name,
			const String &p_config, String &r_dll_path, Vector<String> &r_errors, String *r_output = nullptr);

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

#endif // BEEF_COMPILER_H
