/**************************************************************************/
/*  beef_ide_helper.h                                                     */
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
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

// Thin wrapper around Beef's IDEHelper (IDEHelper64.dll) — the same parser/semantic engine BeefIDE
// uses. We load it at runtime (it already ships next to godot.exe) and drive its C ABI to provide
// language services for .bf scripts: syntax diagnostics today, with classification/autocomplete/
// navigation to follow. Windows-only for now (matches the rest of the toolchain).
class BeefIDEHelper {
public:
	struct ParseError {
		String message;
		int line = 0; // 1-based
		int column = 0; // 1-based
		bool is_warning = false;
	};

	// Token classes (subset of Beef's BfSourceElementType) for syntax highlighting.
	enum TokenKind {
		TK_NORMAL = 0,
		TK_KEYWORD = 1,
		TK_LITERAL = 2,
		TK_COMMENT = 3,
		TK_IDENTIFIER = 4,
		TK_LOCAL = 5,
		TK_PARAMETER = 6,
		TK_MEMBER = 7,
		TK_METHOD = 8,
		TK_TYPE = 9,
		TK_PRIMITIVE_TYPE = 10,
		TK_STRUCT = 11,
		TK_GENERIC_PARAM = 12,
		TK_REF_TYPE = 13,
		TK_INTERFACE = 14,
		TK_NAMESPACE = 15,
	};

private:
	void *_dll = nullptr;
	void *_system = nullptr; // BfSystem*

	// Resolved IDEHelper exports (C ABI; the original C++ reference params are plain pointers here).
	typedef void *(*PFN_System_Create)();
	typedef void (*PFN_System_Delete)(void *system);
	typedef void *(*PFN_CreatePassInstance)(void *system);
	typedef void (*PFN_PassInstance_Delete)(void *pass);
	typedef void *(*PFN_CreateParser)(void *system, void *project);
	typedef void (*PFN_Parser_Delete)(void *parser);
	typedef void (*PFN_Parser_SetSource)(void *parser, const char *data, int length, const char *file_name, int text_version);
	typedef bool (*PFN_Parser_Parse)(void *parser, void *pass, bool compat_mode);
	typedef bool (*PFN_Parser_Reduce)(void *parser, void *pass);
	typedef void (*PFN_Parser_ClassifySource)(void *parser, void *char_data, bool preserve_flags);
	typedef void (*PFN_Parser_SetIsClassifying)(void *parser);
	typedef bool (*PFN_Parser_BuildDefs)(void *parser, void *pass, void *resolve_data, bool full_refresh);
	typedef void (*PFN_Parser_CreateClassifier)(void *parser, void *pass, void *resolve_data, void *char_data);
	typedef void (*PFN_Parser_FinishClassifier)(void *parser, void *resolve_data);
	typedef void (*PFN_Parser_SetNextRevision)(void *parser, void *next_revision);
	typedef void (*PFN_System_RemoveOldParsers)(void *system);
	typedef void (*PFN_System_RemoveOldData)(void *system);
	typedef int (*PFN_PassInstance_GetErrorCount)(void *pass);
	typedef const char *(*PFN_PassInstance_GetErrorData)(void *pass, int error_idx, int *out_code, bool *out_is_warning,
			bool *out_is_after, bool *out_is_deferred, int *out_is_while_specializing, bool *out_is_persistent,
			char **project_name, char **file_name, int *out_src_start, int *out_src_end, int *out_line, int *out_column,
			int *out_more_info_count);

	PFN_System_Create _system_create = nullptr;
	PFN_System_Delete _system_delete = nullptr;
	PFN_CreatePassInstance _create_pass = nullptr;
	PFN_PassInstance_Delete _pass_delete = nullptr;
	PFN_CreateParser _create_parser = nullptr;
	PFN_Parser_Delete _parser_delete = nullptr;
	PFN_Parser_SetSource _parser_set_source = nullptr;
	PFN_Parser_Parse _parser_parse = nullptr;
	PFN_Parser_Reduce _parser_reduce = nullptr;
	PFN_Parser_ClassifySource _parser_classify = nullptr;
	PFN_Parser_SetIsClassifying _parser_set_is_classifying = nullptr;
	PFN_Parser_BuildDefs _parser_build_defs = nullptr;
	PFN_Parser_CreateClassifier _parser_create_classifier = nullptr;
	PFN_Parser_FinishClassifier _parser_finish_classifier = nullptr;
	PFN_Parser_SetNextRevision _parser_set_next_revision = nullptr;
	PFN_System_RemoveOldParsers _system_remove_old_parsers = nullptr;
	PFN_System_RemoveOldData _system_remove_old_data = nullptr;
	PFN_PassInstance_GetErrorCount _get_error_count = nullptr;
	PFN_PassInstance_GetErrorData _get_error_data = nullptr;

	// --- Resolve compiler (semantic autocomplete) exports ---
	typedef void *(*PFN_CreateCompiler)(void *system, bool resolve_only);
	typedef void *(*PFN_CreateProject)(void *system, const char *name, const char *dir);
	typedef void (*PFN_ProjectSetOptions)(void *project, int target_type, const char *startup_object, const char *macros, int opt_level, int lto, int reloc, int pic, int flags);
	typedef void (*PFN_ProjectAddDependency)(void *project, void *dep);
	typedef void (*PFN_ParserSetAutocomplete)(void *parser, int cursor_idx);
	typedef void *(*PFN_CreateResolvePassData)(void *parser, int resolve_type, bool fuzzy);
	typedef bool (*PFN_CompilerClassify)(void *compiler, void *pass, void *resolve_data);
	typedef const char *(*PFN_CompilerGetAutocompleteInfo)(void *compiler);
	typedef bool (*PFN_CompilerCompile)(void *compiler, void *pass, const char *output_path);
	typedef void (*PFN_CompilerSetOptions)(void *compiler, void *hot_project, int hot_idx, const char *target_triple, const char *target_cpu, int toolset_type, int simd_setting, int alloc_stack_count, int max_worker_threads, int option_flags, const char *malloc_name, const char *free_name);
	typedef const char *(*PFN_CompilerGetUsedOutputFileNames)(void *compiler, void *project, int flags, bool *had_changes);

	PFN_CreateCompiler _create_compiler = nullptr;
	PFN_CreateProject _create_project = nullptr;
	PFN_ProjectSetOptions _project_set_options = nullptr;
	PFN_ProjectAddDependency _project_add_dependency = nullptr;
	PFN_ParserSetAutocomplete _parser_set_autocomplete = nullptr;
	PFN_CreateResolvePassData _create_resolve_pass_data = nullptr;
	PFN_CompilerClassify _compiler_classify = nullptr;
	PFN_CompilerGetAutocompleteInfo _compiler_get_autocomplete = nullptr;
	PFN_CompilerCompile _compiler_compile = nullptr;
	PFN_CompilerSetOptions _compiler_set_options = nullptr;
	PFN_CompilerGetUsedOutputFileNames _compiler_get_used_output = nullptr;
	typedef void (*PFN_CompilerHotCommit)(void *compiler);
	PFN_CompilerHotCommit _compiler_hot_commit = nullptr;
	typedef void (*PFN_SystemLock)(void *system, int priority);
	typedef void (*PFN_SystemUnlock)(void *system);
	PFN_SystemLock _system_lock = nullptr;
	PFN_SystemUnlock _system_unlock = nullptr;

	void *_resolve_compiler = nullptr;
	void *_proj_corlib = nullptr;
	void *_proj_bindings = nullptr;
	void *_proj_game = nullptr;
	void *_autocomplete_parser = nullptr; // parser for the file being completed (recreated per request)
	bool _resolve_ready = false; // persistent resolve system built + bootstrapped for _resolve_workspace
	bool _program_started = false; // IDEHelper_ProgramStart (LLVM init) done once, shared with the debugger
	String _resolve_workspace; // workspace the persistent system was built for (rebuild if it changes)
	HashMap<String, void *> _file_parsers; // normalized game-file path -> current BfParser* (for revisioning)
	bool _resolve_load_compiler();
	// Recursively add .bf parsers + build their defs. Skips p_skip_file; if p_track is set, records each
	// added parser keyed by its normalized full path (so it can later be revisioned in place).
	void _add_source_dir(void *p_system, const String &p_dir, void *p_project, void *p_pass, void *p_resolve, const String &p_skip_file, HashMap<String, void *> *p_track = nullptr);
	bool _resolve_build(const String &p_workspace_dir, const String &p_project_name, const String &p_corlib_src, bool p_verbose); // build the persistent system once
	void _resolve_ensure_built(const String &p_workspace_dir, const String &p_project_name, bool p_verbose); // build if not already built for this workspace (caller holds the lock)
	void _resolve_reset(); // drop the persistent system (recreate BfSystem) so a new workspace can be built

	// --- In-process full build (codegen) — separate, persistent BfSystem/compiler from the resolve one.
	// This compiler OWNS the produced DLL's baseline, so it can later emit hot-compile deltas (Path B).
	void *_build_system = nullptr;
	void *_build_compiler = nullptr;
	void *_build_proj_corlib = nullptr;
	void *_build_proj_bindings = nullptr;
	void *_build_proj_game = nullptr;
	bool _build_ready = false;
	bool _build_hot_swap = false; // the build compiler was configured with EnableHotSwapping
	int _build_hot_idx = 0; // monotonically increasing hot-compile index (baseline = 0)
	String _build_workspace;
	String _build_obj_dir; // where the baseline wrote objects; hot deltas must use the same dir
	HashMap<String, void *> _build_game_parsers; // game-file path -> current BfParser* (for hot revisioning)
	// Emit objects for the whole workspace into <build_dir>/obj via the in-process IDEHelper compiler.
	// r_objs receives the linker input list. Returns false (and fills r_errors) on a compile error.
	bool _build_emit_objects(const String &p_workspace_dir, const String &p_project_name, const String &p_corlib_src, const String &p_build_dir, bool p_hot_swap, Vector<String> &r_objs, Vector<String> &r_errors);

	// Append the (non-warning, capped) compile errors from a finished pass to r_errors. p_beefbuild_format
	// emits BeefBuild's "ERROR: <msg> at line L:C in <file>" shape (parsed into the Problems tab); otherwise
	// a plain "<msg> (<file>:L)".
	void _extract_pass_errors(void *p_pass, bool p_beefbuild_format, Vector<String> &r_errors);

	bool _load();

#ifdef WINDOWS_ENABLED
	// Load IDEHelper64.dll into _dll (exe-adjacent first, then the default search path). Returns true
	// if _dll is loaded. Shared by _load(), _load_debugger() and run_leak_check().
	bool _ensure_dll();
#endif

	// --- Native debug session state (Windows-only) ---
	// Debugger exports are stored as void* and cast in the .cpp to keep this header free of the
	// IDEHelper ABI typedefs. Resolved once by _load_debugger(); the one-time engine init
	// (ProgramStart/Targets_Create/Debugger_Create) runs once per process, guarded by _dbg_initialized.
	bool _dbg_resolved = false;
	bool _dbg_program_started = false; // LLVM/target one-time init done (debugger is recreated per run)
	void *_pfn_program_start = nullptr;
	void *_pfn_vssupport_find = nullptr; // VSSupport_Find() — discovers MSVC/Windows-SDK lib paths (no vswhere.exe / dev prompt needed)
	void *_pfn_targets_create = nullptr;
	void *_pfn_dbg_create = nullptr;
	void *_pfn_open_file = nullptr;
	void *_pfn_run = nullptr;
	void *_pfn_update = nullptr;
	void *_pfn_get_run_state = nullptr;
	void *_pfn_pop_message = nullptr;
	void *_pfn_continue = nullptr;
	void *_pfn_detach = nullptr;
	void *_pfn_create_breakpoint = nullptr;
	void *_pfn_bp_get_line = nullptr;
	void *_pfn_bp_set_condition = nullptr;
	void *_pfn_bp_get_hit_count = nullptr;
	void *_pfn_bp_delete = nullptr;
	void *_pfn_bp_check = nullptr;
	void *_pfn_stop_debugging = nullptr;
	void *_pfn_get_process_id = nullptr;
	void *_pfn_active_bp = nullptr;
	void *_pfn_step_over = nullptr;
	void *_pfn_step_into = nullptr;
	void *_pfn_step_out = nullptr;
	void *_pfn_cs_update = nullptr;
	void *_pfn_cs_count = nullptr;
	void *_pfn_cs_frame_info = nullptr;
	void *_pfn_evaluate = nullptr;
	void *_pfn_evaluate_continue = nullptr;
	void *_pfn_get_auto_locals = nullptr;
	void *_pfn_hot_load = nullptr; // Debugger_HotLoad(fileNames, hotIdx) — patches the live process
	bool _load_debugger();

	static BeefIDEHelper *_singleton;

public:
	static BeefIDEHelper *get_singleton();
	static void free_singleton();

	// Build the Beef scripts DynamicLib fully in-process: IDEHelper compiles the workspace to objects,
	// then a bundled lld-link.exe links them (+ the Beef runtime lib) into p_out_dll. The build compiler
	// persists so it can later emit hot-compile deltas. p_hot_swap links the DLL hot-patchable
	// (-base/-dynamicbase:no) and enables the EnableHotSwapping codegen flag. Returns false + r_errors on
	// failure. Windows-only (returns false elsewhere). An alternative to shelling out to BeefBuild.exe.
	bool build_dll(const String &p_workspace_dir, const String &p_project_name, const String &p_out_dll, bool p_hot_swap, Vector<String> &r_errors);

	// Establish the hot-reload baseline (hotIdx 0) by compiling the workspace in-process — NO link, so
	// it needs no linker / Windows SDK (LIB). This is all hot_reload requires: the loaded DLL is built by
	// BeefBuild, and Debugger_HotLoad consumes the .obj deltas directly. Windows-only.
	bool build_hot_baseline(const String &p_workspace_dir, const String &p_project_name, const String &p_build_dir, Vector<String> &r_errors);

	// In-editor HOT RELOAD: re-reads the workspace's game .bf files, hot-compiles a delta against the
	// persistent build baseline (so only changed method bodies are emitted), and patches them into the
	// live debuggee via Debugger_HotLoad. Requires: a prior build_dll(p_hot_swap=true) (baseline at
	// hotIdx 0) and an active hot-swap debug session (debug_start(hot_swap=true) + run). Windows-only.
	bool hot_reload(const String &p_workspace_dir, const String &p_project_name, const String &p_build_dir, const Vector<String> &p_changed_files, Vector<String> &r_errors);

	// IDEHelper WinDebugger RunState values (Debugger.h). Exposed so callers can interpret debug_wait().
	enum RunState {
		RS_NOT_STARTED = 0,
		RS_RUNNING = 1,
		RS_RUNNING_TO_TEMP_BREAKPOINT = 2,
		RS_PAUSED = 3,
		RS_BREAKPOINT = 4,
		RS_DEBUG_EVAL = 5,
		RS_DEBUG_EVAL_DONE = 6,
		RS_HOT_STEP = 7,
		RS_EXCEPTION = 8,
		RS_TERMINATING = 9,
		RS_TERMINATED = 10,
	};

	// One frame of the debuggee call stack at a stop.
	struct DebugFrame {
		String description; // method/frame text from the debugger
		String file; // source file path, if known
		int line = 0; // 1-based source line, 0 if unknown
		uint64_t address = 0;
	};

	// --- Native breakpoint debug session (Windows-only). The engine layer beneath any debugger UI. ---
	// Typical flow: debug_start() -> debug_add_breakpoint()* -> debug_begin_run() -> loop {
	//   debug_wait() -> inspect via debug_call_stack()/debug_active_breakpoint_line() ->
	//   debug_continue()/debug_step_*() } until RS_TERMINATED -> debug_stop().
	// Launch the target under the debugger WITHOUT running it yet (so breakpoints can be set first).
	// p_target_dll: the module the hot-swap engine treats as the "target binary" — it MUST be the module
	// that contains the Beef code (the scripts DLL), because Debugger_HotLoad resolves a delta's symbols
	// only against the target binary's symbol map + its linked .libs. When debugging a host app (godot.exe)
	// that loads the Beef code as a secondary DLL, the launch exe and the target module differ; pass the
	// scripts DLL here. Empty => target == launch exe (only correct when the exe itself is the Beef binary).
	bool debug_start(const String &p_exe, const String &p_args, const String &p_working_dir, bool p_hot_swap = false, const String &p_target_dll = String());
	// Add a source-line breakpoint (p_file should be an absolute .bf path; p_line is 1-based). Returns
	// the opaque Breakpoint* (non-null) on success. Bind happens when the debuggee's symbols load.
	// p_condition, if non-empty, is a Beef expression evaluated in the breakpoint's frame each time the
	// line is reached; the debuggee only stops when it evaluates true (e.g. "mX > 300").
	void *debug_add_breakpoint(const String &p_file, int p_line, const String &p_condition = String());
	// Update a live breakpoint's condition (empty clears it); takes effect on the next hit.
	void debug_set_breakpoint_condition(void *p_breakpoint, const String &p_condition);
	// Number of times the breakpoint has actually broken (condition-true hits), or 0.
	int debug_breakpoint_hit_count(void *p_breakpoint);
	// Delete a breakpoint (removes it from the debugger). Must be done before ending a session so the
	// debugger's teardown doesn't assert on leftover breakpoints.
	void debug_delete_breakpoint(void *p_breakpoint);
	void debug_begin_run(); // Debugger_Run — start the debuggee after breakpoints are set
	void debug_continue();
	void debug_step_over();
	void debug_step_into();
	void debug_step_out();
	// Pump Debugger_Update until the debuggee stops (breakpoint/paused/exception) or terminates, or the
	// timeout elapses. Drained debugger messages are appended to r_messages. Returns the RunState.
	int debug_wait(Vector<String> &r_messages, int p_timeout_ms = 30000);
	// Non-blocking single pump: one Debugger_Update, drain messages into r_messages, return RunState.
	// Call this once per editor frame so the UI stays responsive while the debuggee runs in its own
	// process (the caller reacts to the returned state, e.g. refreshes the stack on a stop).
	int debug_poll(Vector<String> &r_messages);
	int debug_run_state();
	int debug_process_id(); // OS process id of the debuggee (0 if none), for a guaranteed OS-level kill
	int debug_active_breakpoint_line(); // 1-based source line of the active breakpoint, 0 if none
	Vector<DebugFrame> debug_call_stack();
	// Evaluate a Beef expression in frame p_frame_idx (0 == top) at the current stop and return its
	// displayed value. Resolves the debugger's pending/continue protocol internally. On error the
	// result begins with '!' (e.g. "!Not paused", or "!<message>"). Empty if eval is unavailable.
	String debug_evaluate(const String &p_expr, int p_frame_idx = 0);
	// Names of the local variables in scope at frame p_frame_idx (includes "$this"). Evaluate each with
	// debug_evaluate to get its value. Empty if not paused / unavailable.
	Vector<String> debug_locals(int p_frame_idx = 0);
	void debug_stop(); // terminate the debuggee (StopDebugging)
	void debug_detach(); // tear down the debugger + its background thread at session end (recreated next run)

	// True once IDEHelper is loaded and a BfSystem is available.
	bool is_available();

	// Parse a single .bf source and collect lexer/parser diagnostics (line/column 1-based). Returns
	// false if IDEHelper is unavailable. Syntax-level only; full semantic errors come from BeefBuild.
	bool get_parse_errors(const String &p_source, const String &p_file_name, Vector<ParseError> &r_errors);

	// Classify each byte of the source into a TokenKind for syntax highlighting. r_kinds ends up with
	// one entry per source byte (1 byte == 1 column for ASCII .bf). Returns false if unavailable.
	bool classify(const String &p_source, const String &p_file_name, Vector<uint8_t> &r_kinds);

	// 0-based line of the declaration of method p_name in p_source, or -1 if not found / unavailable.
	int find_function(const String &p_name, const String &p_source);

	// Semantic code completion via the IDEHelper resolve compiler. Builds an in-process resolve compiler
	// over the whole Beef workspace (corlib + GodotBindings + user scripts) on first use, then runs an
	// autocomplete resolve pass at p_cursor (byte offset) in p_file's p_source. Returns false if
	// unavailable. Each entry has a kind tag (e.g. "method", "field", "class") and a name.
	struct Completion {
		String name;
		String kind;
	};
	// Resolve completions at p_cursor. If r_call_hint is non-null and the cursor is inside a call's
	// argument list, it receives the active method's signature formatted for Godot's call hint (the
	// current argument wrapped in 0xFFFF markers).
	bool get_completions(const String &p_workspace_dir, const String &p_project_name, const String &p_file, const String &p_source, int p_cursor, Vector<Completion> &r_out, String *r_call_hint = nullptr);

	// Resolve the symbol under p_cursor to its definition (BfResolveType_GetSymbolInfo). On success
	// r_type_full is the owning type's full name (e.g. "Godot.Node3D") and r_kind is the reference kind
	// ("methodRef"/"ctorRef"/"fieldRef"/"propertyRef"/"typeRef"). Used by lookup_code to map a hovered
	// Beef binding member back to its Godot class so the editor can show the engine documentation.
	bool resolve_symbol(const String &p_workspace_dir, const String &p_project_name, const String &p_file, const String &p_source, int p_cursor, String &r_type_full, String &r_kind);

	// Build the persistent resolve system ahead of time so the first get_completions is instant rather
	// than paying the one-time ~1.4s build cost. Runs synchronously on the calling thread.
	void prewarm(const String &p_workspace_dir, const String &p_project_name);

	// Launch p_exe under IDEHelper's debugger and pump it to completion, draining debugger messages
	// (including the Beef runtime's shutdown leak report, which the debugger receives + symbolicates
	// instead of the process FatalExit-ing). Each message is appended to r_messages and printed.
	// This is the headless equivalent of "run the game under BeefIDE" for leak inspection.
	bool run_leak_check(const String &p_exe, const String &p_args, const String &p_working_dir, Vector<String> &r_messages);

	// Immediately terminate this process WITHOUT running DLL detach / atexit — needed after a
	// leak-check run because IDEHelper's background threads hang any orderly exit. One-shot only.
	static void hard_exit(int p_code);

	void cleanup();

	~BeefIDEHelper();
};
