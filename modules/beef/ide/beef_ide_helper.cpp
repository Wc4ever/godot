#include "beef_ide_helper.h"

#include "../compiler/beef_compiler.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/templates/hash_set.h"

#include <cstdio>
#include <cstdlib>

#ifdef WINDOWS_ENABLED
#include <windows.h>
#endif

BeefIDEHelper *BeefIDEHelper::_singleton = nullptr;

BeefIDEHelper *BeefIDEHelper::get_singleton() {
	if (!_singleton) {
		_singleton = memnew(BeefIDEHelper);
	}
	return _singleton;
}

void BeefIDEHelper::free_singleton() {
	if (_singleton) {
		memdelete(_singleton);
		_singleton = nullptr;
	}
}

#ifdef WINDOWS_ENABLED
bool BeefIDEHelper::_ensure_dll() {
	if (_dll) {
		return true;
	}
	// IDEHelper64.dll lives in the self-contained BeefBundle (alongside the version-matched BeefBuild —
	// they MUST be the same Beef version for hot reload). Prefer the bundle, then exe-adjacent, then the
	// default search path.
	String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	const char *candidates[] = { "BeefBundle/bin/IDEHelper64.dll", "IDEHelper64.dll", nullptr };
	for (int i = 0; candidates[i]; i++) {
		String dll_path = exe_dir.path_join(candidates[i]);
		_dll = (void *)LoadLibraryW((LPCWSTR)dll_path.utf16().get_data());
		if (_dll) {
			return true;
		}
	}
	_dll = (void *)LoadLibraryW(L"IDEHelper64.dll"); // default search path
	return _dll != nullptr;
}
#endif

bool BeefIDEHelper::_load() {
	if (_system) {
		return true; // already loaded
	}
#ifdef WINDOWS_ENABLED
	if (!_ensure_dll()) {
		return false;
	}

	HMODULE mod = (HMODULE)_dll;
	_system_create = (PFN_System_Create)(void *)GetProcAddress(mod, "BfSystem_Create");
	_system_delete = (PFN_System_Delete)(void *)GetProcAddress(mod, "BfSystem_Delete");
	_create_pass = (PFN_CreatePassInstance)(void *)GetProcAddress(mod, "BfSystem_CreatePassInstance");
	_pass_delete = (PFN_PassInstance_Delete)(void *)GetProcAddress(mod, "BfPassInstance_Delete");
	_create_parser = (PFN_CreateParser)(void *)GetProcAddress(mod, "BfSystem_CreateParser");
	_parser_delete = (PFN_Parser_Delete)(void *)GetProcAddress(mod, "BfParser_Delete");
	_parser_set_source = (PFN_Parser_SetSource)(void *)GetProcAddress(mod, "BfParser_SetSource");
	_parser_parse = (PFN_Parser_Parse)(void *)GetProcAddress(mod, "BfParser_Parse");
	_parser_reduce = (PFN_Parser_Reduce)(void *)GetProcAddress(mod, "BfParser_Reduce");
	_parser_classify = (PFN_Parser_ClassifySource)(void *)GetProcAddress(mod, "BfParser_ClassifySource");
	_parser_set_is_classifying = (PFN_Parser_SetIsClassifying)(void *)GetProcAddress(mod, "BfParser_SetIsClassifying");
	_parser_build_defs = (PFN_Parser_BuildDefs)(void *)GetProcAddress(mod, "BfParser_BuildDefs");
	_parser_create_classifier = (PFN_Parser_CreateClassifier)(void *)GetProcAddress(mod, "BfParser_CreateClassifier");
	_parser_finish_classifier = (PFN_Parser_FinishClassifier)(void *)GetProcAddress(mod, "BfParser_FinishClassifier");
	_parser_set_next_revision = (PFN_Parser_SetNextRevision)(void *)GetProcAddress(mod, "BfParser_SetNextRevision");
	_system_remove_old_parsers = (PFN_System_RemoveOldParsers)(void *)GetProcAddress(mod, "BfSystem_RemoveOldParsers");
	_system_remove_old_data = (PFN_System_RemoveOldData)(void *)GetProcAddress(mod, "BfSystem_RemoveOldData");
	_get_error_count = (PFN_PassInstance_GetErrorCount)(void *)GetProcAddress(mod, "BfPassInstance_GetErrorCount");
	_get_error_data = (PFN_PassInstance_GetErrorData)(void *)GetProcAddress(mod, "BfPassInstance_GetErrorData");

	if (!_system_create || !_create_pass || !_create_parser || !_parser_set_source ||
			!_parser_parse || !_parser_reduce || !_get_error_count || !_get_error_data) {
		return false; // unexpected IDEHelper build; bail rather than risk a bad call
	}

	_system = _system_create();
	return _system != nullptr;
#else
	return false;
#endif
}

bool BeefIDEHelper::is_available() {
	return _load();
}

bool BeefIDEHelper::get_parse_errors(const String &p_source, const String &p_file_name, Vector<ParseError> &r_errors) {
	if (!_load()) {
		return false;
	}
#ifdef WINDOWS_ENABLED
	CharString src = p_source.utf8();
	CharString file = p_file_name.utf8();

	void *parser = _create_parser(_system, nullptr);
	if (!parser) {
		return false;
	}
	void *pass = _create_pass(_system);
	if (!pass) {
		_parser_delete(parser);
		return false;
	}

	_parser_set_source(parser, src.get_data(), src.length(), file.get_data(), 0);
	_parser_parse(parser, pass, false);
	_parser_reduce(parser, pass);

	int count = _get_error_count(pass);
	for (int i = 0; i < count; i++) {
		int code = 0, src_start = 0, src_end = 0, line = 0, column = 0, more = 0, while_spec = 0;
		bool is_warning = false, is_after = false, is_deferred = false, is_persistent = false;
		char *project_name = nullptr, *file_name = nullptr;
		const char *msg = _get_error_data(pass, i, &code, &is_warning, &is_after, &is_deferred,
				&while_spec, &is_persistent, &project_name, &file_name, &src_start, &src_end,
				&line, &column, &more);

		ParseError e;
		e.message = msg ? String::utf8(msg) : String();
		e.line = line + 1; // IDEHelper line/column are 0-based; Godot expects 1-based.
		e.column = column + 1;
		e.is_warning = is_warning;
		r_errors.push_back(e);
	}

	_pass_delete(pass);
	_parser_delete(parser);
	return true;
#else
	return false;
#endif
}

bool BeefIDEHelper::classify(const String &p_source, const String &p_file_name, Vector<uint8_t> &r_kinds) {
	if (!_load() || !_parser_classify) {
		return false;
	}
#ifdef WINDOWS_ENABLED
	CharString src = p_source.utf8();
	CharString file = p_file_name.utf8();
	int len = src.length();

	void *parser = _create_parser(_system, nullptr);
	if (!parser) {
		return false;
	}
	void *pass = _create_pass(_system);
	if (!pass) {
		_parser_delete(parser);
		return false;
	}

	_parser_set_source(parser, src.get_data(), len, file.get_data(), 0);
	_parser_parse(parser, pass, false);
	_parser_reduce(parser, pass);

	// BfSourceClassifier::CharData is 4 bytes: { char mChar; uint8 mDisplayPassId; uint8
	// mDisplayTypeId; uint8 mDisplayFlags; } (INCLUDE_CHARDATA_CHARID is off in the shipped DLL).
	const int kCharDataSize = 4;
	Vector<uint8_t> buf;
	buf.resize(len * kCharDataSize);
	uint8_t *b = buf.ptrw();
	const char *s = src.get_data();
	for (int i = 0; i < len; i++) {
		b[i * kCharDataSize + 0] = (uint8_t)s[i]; // mChar
		b[i * kCharDataSize + 1] = 0;
		b[i * kCharDataSize + 2] = 0; // mDisplayTypeId (filled by the classifier)
		b[i * kCharDataSize + 3] = 0;
	}

	_parser_classify(parser, b, false);

	r_kinds.resize(len);
	uint8_t *out = r_kinds.ptrw();
	for (int i = 0; i < len; i++) {
		out[i] = b[i * kCharDataSize + 2]; // mDisplayTypeId == BfSourceElementType
	}

	_pass_delete(pass);
	_parser_delete(parser);
	return true;
#else
	return false;
#endif
}

static bool _is_ident_char(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

int BeefIDEHelper::find_function(const String &p_name, const String &p_source) {
	Vector<uint8_t> kinds;
	if (p_name.is_empty() || !classify(p_source, "find.bf", kinds)) {
		return -1;
	}
	CharString src = p_source.utf8();
	CharString want = p_name.utf8();
	const char *s = src.get_data();
	const int len = src.length();
	const int want_len = want.length();

	// The classifier tags both declarations and call sites as Method, so a name match alone could land
	// on a call. Confirm it is a declaration by checking the name is followed by `(...)` then a body
	// (`{` or `=>`), which a call site (`name(...);`) is not.
	int line = 0;
	for (int i = 0; i < len; i++) {
		if (s[i] == '\n') {
			line++;
			continue;
		}
		const bool at_ident_start = (i == 0) || !_is_ident_char(s[i - 1]);
		if (!(at_ident_start && i < kinds.size() && kinds[i] == TK_METHOD)) {
			continue;
		}
		int j = i;
		while (j < len && _is_ident_char(s[j])) {
			j++;
		}
		if (j - i != want_len || memcmp(s + i, want.get_data(), want_len) != 0) {
			continue;
		}

		// Expect `(` (optionally after whitespace), then balance to the matching `)`.
		int k = j;
		while (k < len && (s[k] == ' ' || s[k] == '\t')) {
			k++;
		}
		if (k >= len || s[k] != '(') {
			continue;
		}
		int depth = 0;
		while (k < len) {
			if (s[k] == '(') {
				depth++;
			} else if (s[k] == ')') {
				depth--;
				if (depth == 0) {
					k++;
					break;
				}
			}
			k++;
		}
		// A body follows a declaration: `{` or an expression body `=>`. A call is `name(...);`.
		while (k < len && (s[k] == ' ' || s[k] == '\t' || s[k] == '\r' || s[k] == '\n')) {
			k++;
		}
		if (k < len && (s[k] == '{' || (s[k] == '=' && k + 1 < len && s[k + 1] == '>'))) {
			return line;
		}
	}
	return -1;
}

// ─── Semantic code completion (resolve compiler) ─────────────────────────────

bool BeefIDEHelper::_resolve_load_compiler() {
#ifdef WINDOWS_ENABLED
	if (_create_compiler) {
		return true; // already resolved
	}
	if (!_load()) {
		return false;
	}
	HMODULE mod = (HMODULE)_dll;
	_create_compiler = (PFN_CreateCompiler)(void *)GetProcAddress(mod, "BfSystem_CreateCompiler");
	_create_project = (PFN_CreateProject)(void *)GetProcAddress(mod, "BfSystem_CreateProject");
	_project_set_options = (PFN_ProjectSetOptions)(void *)GetProcAddress(mod, "BfProject_SetOptions");
	_project_add_dependency = (PFN_ProjectAddDependency)(void *)GetProcAddress(mod, "BfProject_AddDependency");
	_parser_set_autocomplete = (PFN_ParserSetAutocomplete)(void *)GetProcAddress(mod, "BfParser_SetAutocomplete");
	_create_resolve_pass_data = (PFN_CreateResolvePassData)(void *)GetProcAddress(mod, "BfParser_CreateResolvePassData");
	_compiler_classify = (PFN_CompilerClassify)(void *)GetProcAddress(mod, "BfCompiler_ClassifySource");
	_compiler_get_autocomplete = (PFN_CompilerGetAutocompleteInfo)(void *)GetProcAddress(mod, "BfCompiler_GetAutocompleteInfo");
	_compiler_compile = (PFN_CompilerCompile)(void *)GetProcAddress(mod, "BfCompiler_Compile");
	_compiler_set_options = (PFN_CompilerSetOptions)(void *)GetProcAddress(mod, "BfCompiler_SetOptions");
	_compiler_get_used_output = (PFN_CompilerGetUsedOutputFileNames)(void *)GetProcAddress(mod, "BfCompiler_GetUsedOutputFileNames");
	_compiler_hot_commit = (PFN_CompilerHotCommit)(void *)GetProcAddress(mod, "BfCompiler_HotCommit");
	_system_lock = (PFN_SystemLock)(void *)GetProcAddress(mod, "BfSystem_Lock");
	_system_unlock = (PFN_SystemUnlock)(void *)GetProcAddress(mod, "BfSystem_Unlock");
	if (!_pfn_program_start) {
		_pfn_program_start = (void *)GetProcAddress(mod, "IDEHelper_ProgramStart");
	}
	if (!_pfn_vssupport_find) {
		_pfn_vssupport_find = (void *)GetProcAddress(mod, "VSSupport_Find");
	}

	if (!_create_compiler || !_create_project || !_project_set_options || !_project_add_dependency ||
			!_system_lock || !_system_unlock ||
			!_parser_set_autocomplete || !_create_resolve_pass_data || !_compiler_classify || !_compiler_get_autocomplete) {
		return false;
	}
	return true;
#else
	return false;
#endif
}

void BeefIDEHelper::_add_source_dir(void *p_system, const String &p_dir, void *p_project, void *p_pass, void *p_resolve, const String &p_skip_file, HashMap<String, void *> *p_track) {
#ifdef WINDOWS_ENABLED
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	da->list_dir_begin();
	for (String name = da->get_next(); !name.is_empty(); name = da->get_next()) {
		if (name == "." || name == "..") {
			continue;
		}
		String full = p_dir.path_join(name);
		if (da->current_is_dir()) {
			_add_source_dir(p_system, full, p_project, p_pass, p_resolve, p_skip_file, p_track);
			continue;
		}
		if (!name.to_lower().ends_with(".bf")) {
			continue;
		}
		String norm = full.replace_char('\\', '/').to_lower();
		if (!p_skip_file.is_empty() && norm == p_skip_file) {
			continue; // represented by the autocomplete parser instead
		}
		Ref<FileAccess> f = FileAccess::open(full, FileAccess::READ);
		if (f.is_null()) {
			continue;
		}
		CharString src = f->get_as_utf8_string().utf8();
		CharString file_utf8 = full.utf8();
		void *parser = _create_parser(p_system, p_project);
		if (!parser) {
			continue;
		}
		_parser_set_source(parser, src.get_data(), src.length(), file_utf8.get_data(), 0);
		_parser_parse(parser, p_pass, false);
		_parser_reduce(parser, p_pass);
		// Build the type defs into the system. BfCompiler::Compile does NOT do this — def building
		// is driven externally per-parser (mirrors BeefIDE's BfParser_BuildDefs), and without it the
		// type system is empty and autocomplete resolves nothing.
		if (_parser_build_defs) {
			_parser_build_defs(parser, p_pass, p_resolve, true);
		}
		if (p_track) {
			(*p_track)[norm] = parser; // remember so this file can be revisioned in place later
		}
	}
	da->list_dir_end();
#endif
}

void BeefIDEHelper::_resolve_reset() {
#ifdef WINDOWS_ENABLED
	// Drop the persistent system so a different workspace can be built cleanly. The BfSystem owns its
	// parsers/typedefs/projects/compiler, so deleting and recreating it frees them all. ProgramStart
	// (LLVM init) is process-global and is intentionally NOT reset.
	_file_parsers.clear();
	_resolve_compiler = nullptr;
	_proj_corlib = _proj_bindings = _proj_game = nullptr;
	_autocomplete_parser = nullptr;
	if (_system && _system_delete) {
		_system_delete(_system);
		_system = nullptr;
	}
	if (!_system && _system_create) {
		_system = _system_create();
	}
	_resolve_ready = false;
	_resolve_workspace = String();
#endif
}

bool BeefIDEHelper::_resolve_build(const String &p_workspace_dir, const String &p_project_name, const String &p_corlib_src, bool p_verbose) {
#ifdef WINDOWS_ENABLED
	// Build the full type system ONCE: parse + build defs for corlib, the generated bindings and all
	// game scripts, then bootstrap-compile so types are populated. This is the expensive part (~1.3s);
	// keeping it resident lets each later completion reuse it and only revise the edited file.
	// Caller holds the system lock.
	const char *macros = "DEBUG\nBF_DEBUG\nBF_PLATFORM_WINDOWS\nBF_64_BIT\n";
	const int TARGET_BEEFLIB = 2; // BfTargetType_BeefLib

	void *pass = _create_pass(_system);

	// Project graph: corlib <- GodotBindings <- <game scripts>.
	_proj_corlib = _create_project(_system, "corlib", p_corlib_src.utf8().get_data());
	_project_set_options(_proj_corlib, TARGET_BEEFLIB, "", macros, 0, 0, 0, 0, 0);

	String bindings_dir = p_workspace_dir.path_join("GodotBindings");
	_proj_bindings = _create_project(_system, "GodotBindings", bindings_dir.utf8().get_data());
	_project_set_options(_proj_bindings, TARGET_BEEFLIB, "", macros, 0, 0, 0, 0, 0);
	_project_add_dependency(_proj_bindings, _proj_corlib);

	_proj_game = _create_project(_system, p_project_name.utf8().get_data(), p_workspace_dir.utf8().get_data());
	_project_set_options(_proj_game, TARGET_BEEFLIB, "", macros, 0, 0, 0, 0, 0);
	_project_add_dependency(_proj_game, _proj_corlib);
	_project_add_dependency(_proj_game, _proj_bindings);

	// Non-autocomplete pass for normal def building (null parser -> empty parser list, mAutoComplete
	// null, so each BuildDefs inserts a normal type def rather than an autocomplete temp type).
	void *bg_resolve = _create_resolve_pass_data(nullptr, 0 /*BfResolveType_None*/, false);

	uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	if (!p_corlib_src.is_empty()) {
		_add_source_dir(_system, p_corlib_src, _proj_corlib, pass, bg_resolve, String());
	}
	uint64_t t1 = OS::get_singleton()->get_ticks_usec();
	_add_source_dir(_system, bindings_dir.path_join("src"), _proj_bindings, pass, bg_resolve, String());
	uint64_t t2 = OS::get_singleton()->get_ticks_usec();
	// Track game-file parsers by path so each can be revisioned in place when its buffer changes.
	_add_source_dir(_system, p_workspace_dir.path_join("src"), _proj_game, pass, bg_resolve, String(), &_file_parsers);
	uint64_t t3 = OS::get_singleton()->get_ticks_usec();

	if (_pfn_program_start && !_program_started) {
		((void (*)())_pfn_program_start)(); // LLVM/program init (once), as the debugger path also requires
		_program_started = true;
	}
	_resolve_compiler = _create_compiler(_system, true);
	// Configure the compiler target. Without this the compiler has no machine type, so the pointer size
	// is wrong (int defaults to 32-bit), corlib fails to resolve (int64->int casts, var misuse in
	// Enum.bf) and the injected System.Compiler.Options.* types are never built. maxWorkerThreads=1
	// keeps the resolve fully synchronous.
	if (_compiler_set_options) {
		_compiler_set_options(_resolve_compiler, nullptr, 0, "x86_64-pc-windows-msvc", "",
				0 /*BfToolsetType_Microsoft*/, 0 /*SIMD none*/, 0 /*allocStack*/, 1 /*maxWorkerThreads*/,
				0 /*optionFlags*/, "malloc", "free");
	}
	// Bootstrap so mContext->mBfObjectType etc. are initialized (else ProcessAutocompleteTempType bails
	// out "Not initialized yet"). A resolve-only compiler must NOT be bare-compiled (its
	// ResolveTypeResult path dereferences mResolvePassData and crashes on null); route through
	// ClassifySource with a non-autocomplete pass, which sets mResolvePassData then Compiles internally.
	_compiler_classify(_resolve_compiler, pass, bg_resolve);
	uint64_t t4 = OS::get_singleton()->get_ticks_usec();

	if (_pass_delete) {
		_pass_delete(pass);
	}
	_resolve_ready = true;
	_resolve_workspace = p_workspace_dir;
	if (p_verbose) {
		beef_log(vformat("[AC] build: corlib=%dms bindings=%dms game=%dms bootstrap=%dms (%d game files)",
				(int)((t1 - t0) / 1000), (int)((t2 - t1) / 1000), (int)((t3 - t2) / 1000), (int)((t4 - t3) / 1000), _file_parsers.size()));
	}
	return true;
#else
	return false;
#endif
}

#ifdef WINDOWS_ENABLED
// Locate a corlib source dir matching the IDEHelper64.dll we drive: the compiler injects
// System.Compiler.Options.* types newer corlibs declare, and it MUST be the same version BeefBuild builds
// the loaded DLL with. Prefer the resolved BeefBuild's own BeefLibs (bundle or custom toolchain), then the
// exe-adjacent bundle. Never the thirdparty/ submodule.
static String _find_corlib_src() {
	String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	Vector<String> candidates;
	if (BeefCompiler *bc = BeefCompiler::get_singleton()) {
		String bb = bc->get_beef_build_path();
		if (!bb.is_empty() && (bb.contains("/") || bb.contains("\\"))) {
			candidates.push_back(bb.get_base_dir().get_base_dir().path_join("BeefLibs/corlib/src"));
		}
	}
	candidates.push_back(exe_dir.path_join("BeefBundle/BeefLibs/corlib/src"));
	candidates.push_back(exe_dir.path_join("BeefLibs/corlib/src")); // flat bundle layout
	for (int i = 0; i < candidates.size(); i++) {
		if (FileAccess::exists(candidates[i].path_join("Compiler.bf"))) {
			return candidates[i];
		}
	}
	return String();
}
#endif

void BeefIDEHelper::_resolve_ensure_built(const String &p_workspace_dir, const String &p_project_name, bool p_verbose) {
#ifdef WINDOWS_ENABLED
	// Build (or rebuild, on workspace change) the persistent type system on first use. Caller holds the
	// system lock.
	if (_resolve_ready && _resolve_workspace == p_workspace_dir) {
		return;
	}
	String corlib_src = _find_corlib_src();
	if (_resolve_ready) {
		_resolve_reset();
	}
	_resolve_build(p_workspace_dir, p_project_name, corlib_src, p_verbose);
#endif
}

void BeefIDEHelper::_extract_pass_errors(void *p_pass, bool p_beefbuild_format, Vector<String> &r_errors) {
#ifdef WINDOWS_ENABLED
	if (!_get_error_count || !_get_error_data) {
		return;
	}
	int n = _get_error_count(p_pass);
	for (int i = 0; i < n && i < 50; i++) {
		int code = 0;
		bool is_warn = false, is_after = false, is_deferred = false, is_persistent = false;
		int while_spec = 0, ss = 0, se = 0, line = 0, col = 0, more = 0;
		char *proj = nullptr, *file = nullptr;
		const char *msg = _get_error_data(p_pass, i, &code, &is_warn, &is_after, &is_deferred,
				&while_spec, &is_persistent, &proj, &file, &ss, &se, &line, &col, &more);
		if (!msg || is_warn) {
			continue;
		}
		if (p_beefbuild_format) {
			String e = "ERROR: " + String::utf8(msg);
			if (file) {
				e += " at line " + itos(line + 1) + ":" + itos(col + 1) + " in " + String::utf8(file);
			}
			r_errors.push_back(e);
		} else {
			r_errors.push_back(String::utf8(msg) + (file ? (" (" + String::utf8(file) + ":" + itos(line + 1) + ")") : String()));
		}
	}
#endif
}

bool BeefIDEHelper::_build_emit_objects(const String &p_workspace_dir, const String &p_project_name, const String &p_corlib_src, const String &p_build_dir, bool p_hot_swap, Vector<String> &r_objs, Vector<String> &r_errors) {
#ifdef WINDOWS_ENABLED
	if (!_resolve_load_compiler() || !_compiler_compile || !_compiler_get_used_output || !_create_pass || !_create_resolve_pass_data) {
		r_errors.push_back("IDEHelper compiler exports unavailable (need a newer IDEHelper64.dll).");
		return false;
	}
	if (p_corlib_src.is_empty()) {
		r_errors.push_back("corlib source not found next to the executable or in the Beef install.");
		return false;
	}

	// One-time LLVM/program init (shared with the resolve + debugger paths).
	if (_pfn_program_start && !_program_started) {
		((void (*)())_pfn_program_start)();
		_program_started = true;
	}

	const char *macros = "DEBUG\nBF_DEBUG\nBF_PLATFORM_WINDOWS\nBF_64_BIT\n";
	const int TARGET_BEEFLIB = 2; // BfTargetType_BeefLib
	String bindings_dir = p_workspace_dir.path_join("GodotBindings");

	// Build a fresh, dedicated system+compiler+project graph for codegen, separate from the resolve
	// system. This is ALWAYS a clean baseline build: a later hot compile (hot_reload) reuses _build_compiler
	// to emit deltas, but each build_dll/build_hot_baseline establishes a NEW baseline, so the compiler must
	// start fresh. Reusing one whose hot state was advanced by a previous debug session's deltas (e.g. after
	// Stop + restart) yields an inconsistent baseline and the next session's hot reload fails to resolve.
	// Tear down any prior build system first (frees its compiler/projects/parsers — no leak).
	{
		if (_build_system && _system_delete) {
			_system_delete(_build_system);
		}
		_build_system = _system_create();
		_build_proj_corlib = _create_project(_build_system, "corlib", p_corlib_src.utf8().get_data());
		_project_set_options(_build_proj_corlib, TARGET_BEEFLIB, "", macros, 0, 0, 0, 0, 0);
		_build_proj_bindings = _create_project(_build_system, "GodotBindings", bindings_dir.utf8().get_data());
		_project_set_options(_build_proj_bindings, TARGET_BEEFLIB, "", macros, 0, 0, 0, 0, 0);
		_project_add_dependency(_build_proj_bindings, _build_proj_corlib);
		_build_proj_game = _create_project(_build_system, p_project_name.utf8().get_data(), p_workspace_dir.utf8().get_data());
		// The game project must be a DYNAMIC lib (BfTargetType_BeefLib_DynamicLib = 10), matching what
		// BeefBuild assigns for a DynamicLib build kind. Plain BfTargetType_BeefLib (2) compiles fine but
		// the comptime [GodotScript]/[GodotRegister] registrar emit (IComptimeTypeApply.ApplyToType -> the
		// per-class BeefGodot_* [Export] entry points) does not land in the output — verified empirically:
		// target type 2 yields 0 BeefGodot_* symbols, target type 10 emits them. BfProjectFlags_AlwaysIncludeAll
		// (0x80) force-reifies the otherwise-unreferenced registrar types.
		const int TARGET_BEEFLIB_DYNAMIC = 10; // BfTargetType_BeefLib_DynamicLib
		_project_set_options(_build_proj_game, TARGET_BEEFLIB_DYNAMIC, "", macros, 0, 0, 0, 0, 0x80);
		_project_add_dependency(_build_proj_game, _build_proj_corlib);
		_project_add_dependency(_build_proj_game, _build_proj_bindings);

		_build_compiler = _create_compiler(_build_system, false); // false => full codegen, not resolve-only
		int flags = 1 | 2 | 8; // EmitDebugInfo | EmitLineInfo | GenerateOBJ
		if (p_hot_swap) {
			flags |= 0x2000; // EnableHotSwapping
		}
		// Toolset MUST be Microsoft (1), not GNU (0): BfToolsetType is { GNU=0, Microsoft=1, LLVM=2 }, and
		// the mangler (BfCompiler.cpp GetMangleKind) uses Itanium/GNU mangling (_ZN2bf...) for GNU and MSVC
		// mangling (?...@@) otherwise. The Beef RT DLL (Beef042RT64.dll) exports its native corlib functions
		// (Math::Sqrt, Runtime::Init, Double::ToString, FFI::*, ...) with MSVC mangling, so a GNU toolset
		// makes our objects reference _ZN2bf-style names that never resolve against the RT import lib.
		_compiler_set_options(_build_compiler, nullptr, 0, "x86_64-pc-windows-msvc", "",
				1 /*BfToolsetType_Microsoft*/, 0 /*SIMD none*/, 0 /*allocStack*/, 0 /*maxWorkerThreads=auto*/,
				flags, "malloc", "free");
		_build_ready = true;
		_build_hot_swap = p_hot_swap;
		_build_workspace = p_workspace_dir;
		_build_hot_idx = 0;
	}

	_system_lock(_build_system, 0);
	void *pass = _create_pass(_build_system);
	// Real build: BuildDefs with resolveData=NULL (a resolve pass data would mark the type defs
	// resolve-mode and suppress comptime EmitTypeBody — which is what generates the [Export] entry points).
	_add_source_dir(_build_system, p_corlib_src, _build_proj_corlib, pass, nullptr, String());
	_add_source_dir(_build_system, bindings_dir.path_join("src"), _build_proj_bindings, pass, nullptr, String());
	// Track the game-project parsers so hot_reload can revision each file in place against this baseline.
	_build_game_parsers.clear();
	_add_source_dir(_build_system, p_workspace_dir.path_join("src"), _build_proj_game, pass, nullptr, String(), &_build_game_parsers);

	String obj_dir = p_build_dir.path_join("obj");
	_build_obj_dir = obj_dir; // hot deltas must land in the same tree as this baseline
	// The compiler writes <obj_dir>/<project>/<module>.obj but does not create the per-project
	// subdirectories itself, so pre-create them (BeefBuild does the same).
	DirAccess::make_dir_recursive_absolute(obj_dir.path_join("corlib"));
	DirAccess::make_dir_recursive_absolute(obj_dir.path_join("GodotBindings"));
	DirAccess::make_dir_recursive_absolute(obj_dir.path_join(p_project_name));
	bool ok = _compiler_compile(_build_compiler, pass, obj_dir.utf8().get_data());

	if (!ok) {
		_extract_pass_errors(pass, /*beefbuild_format=*/false, r_errors);
		if (r_errors.is_empty()) {
			r_errors.push_back("BfCompiler_Compile failed (no error detail).");
		}
		if (_pass_delete) {
			_pass_delete(pass);
		}
		_system_unlock(_build_system);
		return false;
	}

	// Collect link inputs. Driven this way the compiler emits one loose .obj per module into
	// <obj_dir>/<project>/ (the per-project <project>__.lib archives it also writes come out EMPTY), so
	// link every .obj from all three projects — exactly the object set BeefBuild feeds the linker. This is
	// also what makes the comptime-emitted registrar objects (GodotScriptRegistrations_*Reg.obj, holding
	// the BeefGodot_* [Export] entry points) reach the DLL. vdata.obj (reflection metadata) is in the set.
	Vector<String> projects;
	projects.push_back(p_project_name);
	projects.push_back("GodotBindings");
	projects.push_back("corlib");
	int obj_count = 0;
	for (const String &pn : projects) {
		String pdir = obj_dir.path_join(pn);
		Ref<DirAccess> da = DirAccess::open(pdir);
		if (da.is_null()) {
			r_errors.push_back("Project object dir not produced: " + pdir);
			continue;
		}
		da->list_dir_begin();
		for (String f = da->get_next(); !f.is_empty(); f = da->get_next()) {
			if (!da->current_is_dir() && f.get_extension().to_lower() == "obj") {
				r_objs.push_back(pdir.path_join(f));
				obj_count++;
			}
		}
		da->list_dir_end();
	}
	if (obj_count == 0) {
		r_errors.push_back("No object files were emitted by the in-process compile.");
	}
	if (_pass_delete) {
		_pass_delete(pass);
	}
	_system_unlock(_build_system);
	return true;
#else
	return false;
#endif
}

bool BeefIDEHelper::build_dll(const String &p_workspace_dir, const String &p_project_name, const String &p_out_dll, bool p_hot_swap, Vector<String> &r_errors) {
#ifdef WINDOWS_ENABLED
	String corlib = _find_corlib_src();
	String build_dir = p_out_dll.get_base_dir();
	DirAccess::make_dir_recursive_absolute(build_dir);

	// 1. Compile the whole workspace to per-project objects/archives in-process.
	Vector<String> objs;
	if (!_build_emit_objects(p_workspace_dir, p_project_name, corlib, build_dir, p_hot_swap, objs, r_errors)) {
		return false;
	}
	if (objs.is_empty()) {
		r_errors.push_back("In-process compile produced no link inputs.");
		return false;
	}

	// 2. Locate the toolchain pieces from the Beef install (bin dir holds the RT libs + llvm/bin/lld-link).
	String beef_bin;
	if (BeefCompiler *bc = BeefCompiler::get_singleton()) {
		beef_bin = bc->get_beef_build_path().get_base_dir();
	}
	String lld = beef_bin.path_join("llvm/bin/lld-link.exe");
	if (!FileAccess::exists(lld)) {
		r_errors.push_back("lld-link.exe not found (looked in " + lld + ").");
		return false;
	}
	// Dynamic Beef runtime (import lib for Beef042RT64.dll, shipped in the bundle). This matches the
	// BeefLibType=Dynamic config the hot-swap baseline requires, and the DLL is what the game loads at
	// runtime. corlib is still compiled to objects and linked in (that is per-project Beef code either way).
	String rt_lib = "Beef042RT64.lib";
	if (!FileAccess::exists(beef_bin.path_join(rt_lib))) {
		r_errors.push_back("Beef runtime lib not found: " + beef_bin.path_join(rt_lib));
		return false;
	}

	// 3. Build the lld-link command. Dynamic CRT + the dynamic Beef runtime. Hot-swap builds get a fixed
	// base with room after it for the debugger's hot heap.
	String out_base = p_out_dll.substr(0, p_out_dll.rfind(".")); // strip .dll
	List<String> args;
	args.push_back("-out:" + p_out_dll);
	args.push_back("-dll");
	args.push_back("-implib:" + out_base + ".lib");
	for (const String &o : objs) {
		args.push_back(o);
	}
	args.push_back(rt_lib);
	args.push_back("kernel32.lib");
	args.push_back("-defaultlib:msvcrt");
	args.push_back("-nologo");
	args.push_back("-incremental:no");
	args.push_back("-debug");
	args.push_back("-opt:noref");
	args.push_back("-pdb:" + out_base + "_lld.pdb");
	if (p_hot_swap) {
		// Fixed base + ASLR off so the loaded image leaves predictable space after it for hot patching
		// (mirrors BeefIDE's -base:(((hash & 0x3FFFF)+0x10)<<28)). A high base avoids colliding with the
		// host's already-loaded modules under -dynamicbase:no.
		uint64_t want = (((uint64_t)(p_out_dll.hash() & 0x3FFFF) + 0x10) << 28);
		args.push_back("-base:0x" + String::num_uint64(want, 16));
		args.push_back("-dynamicbase:no");
	}
	args.push_back("-libpath:" + beef_bin);
	args.push_back("-libpath:" + build_dir);
	// MSVC + Windows SDK lib paths. Discover them via IDEHelper's VSSupport_Find() (the same COM-based
	// VS/SDK finder BeefIDE uses — no vswhere.exe and no developer command prompt required). It returns
	// tab/newline-separated TOOL32/TOOL64/LIB32/LIB64 lines; we take the LIB64 dirs. Fall back to / also
	// honor the LIB env if it happens to be set (a dev prompt still works).
	int lib_paths_added = 0;
	if (_pfn_vssupport_find) {
		const char *vs_info = ((const char *(*)())_pfn_vssupport_find)();
		if (vs_info) {
			for (const String &line : String::utf8(vs_info).split("\n", false)) {
				Vector<String> cols = line.split("\t", false);
				if (cols.size() >= 2 && cols[0] == "LIB64") {
					args.push_back("-libpath:" + cols[1].strip_edges());
					lib_paths_added++;
				}
			}
		}
	}
	String lib_env = OS::get_singleton()->get_environment("LIB");
	for (const String &p : lib_env.split(";", false)) {
		if (!p.strip_edges().is_empty()) {
			args.push_back("-libpath:" + p.strip_edges());
			lib_paths_added++;
		}
	}
	if (lib_paths_added == 0) {
		r_errors.push_back("Could not locate the MSVC/Windows SDK lib paths (VSSupport_Find found none and "
						   "LIB is unset). Install Visual Studio with the C++ workload + Windows SDK.");
		return false;
	}

	// Pass everything through a response file: with ~1300 object inputs the argument vector blows past
	// Windows' ~32 KB command-line limit ("Could not create child process"). lld-link reads @file like
	// BeefBuild does. One argument per line; quote any that contain spaces.
	String rsp_path = build_dir.path_join(p_project_name + "_lld.rsp");
	{
		String rsp;
		for (const String &a : args) {
			rsp += (a.contains(" ") ? ("\"" + a + "\"") : a) + "\n";
		}
		Ref<FileAccess> rf = FileAccess::open(rsp_path, FileAccess::WRITE);
		if (rf.is_null()) {
			r_errors.push_back("Could not write linker response file: " + rsp_path);
			return false;
		}
		rf->store_string(rsp);
	}
	List<String> lld_args;
	lld_args.push_back("@" + rsp_path);

	String output;
	int exit_code = 0;
	beef_log("BeefIDEHelper: linking " + p_out_dll.get_file() + " via lld-link (" + itos(objs.size()) + " inputs)...");
	OS::get_singleton()->execute(lld, lld_args, &output, &exit_code, true);
	if (exit_code != 0 || !FileAccess::exists(p_out_dll)) {
		for (const String &line : output.split("\n", false)) {
			String t = line.strip_edges();
			if (!t.is_empty()) {
				r_errors.push_back(t);
			}
		}
		r_errors.push_back("lld-link failed (exit " + itos(exit_code) + ").");
		return false;
	}
	beef_log("BeefIDEHelper: in-process build OK");
	return true;
#else
	(void)p_workspace_dir;
	(void)p_project_name;
	(void)p_out_dll;
	(void)p_hot_swap;
	r_errors.push_back("In-process build is Windows-only.");
	return false;
#endif
}

bool BeefIDEHelper::build_hot_baseline(const String &p_workspace_dir, const String &p_project_name, const String &p_build_dir, Vector<String> &r_errors) {
#ifdef WINDOWS_ENABLED
	String corlib = _find_corlib_src();
	Vector<String> objs; // discarded — the baseline is for the compiler's hot state, not a linked DLL
	return _build_emit_objects(p_workspace_dir, p_project_name, corlib, p_build_dir, /*hot_swap=*/true, objs, r_errors);
#else
	(void)p_workspace_dir;
	(void)p_project_name;
	(void)p_build_dir;
	r_errors.push_back("Hot baseline is Windows-only.");
	return false;
#endif
}

bool BeefIDEHelper::hot_reload(const String &p_workspace_dir, const String &p_project_name, const String &p_build_dir, const Vector<String> &p_changed_files, Vector<String> &r_errors) {
#ifdef WINDOWS_ENABLED
	if (!_build_ready || !_build_compiler) {
		r_errors.push_back("No build baseline — call build_dll(hot_swap=true) before hot_reload.");
		return false;
	}
	if (!_compiler_hot_commit || !_pfn_hot_load) {
		r_errors.push_back("IDEHelper is missing hot-reload exports (BfCompiler_HotCommit / Debugger_HotLoad).");
		return false;
	}

	_system_lock(_build_system, 0);
	void *pass = _create_pass(_build_system);

	// Revision only the changed game-file parsers (minimal delta): a smaller delta references far fewer
	// symbols, which is what lets Debugger_HotLoad resolve them against the loaded DLL. Empty list ->
	// revision all (fallback). Paths are matched normalized (lowercase, forward slashes).
	HashSet<String> changed;
	for (const String &cf : p_changed_files) {
		changed.insert(cf.replace_char('\\', '/').to_lower());
	}
	for (const KeyValue<String, void *> &kv : _build_game_parsers) {
		const String &norm = kv.key;
		if (!changed.is_empty() && !changed.has(norm)) {
			continue;
		}
		Ref<FileAccess> f = FileAccess::open(norm, FileAccess::READ);
		if (f.is_null()) {
			continue;
		}
		CharString src = f->get_as_utf8_string().utf8();
		CharString file_utf8 = norm.utf8();
		void *np = _create_parser(_build_system, _build_proj_game);
		if (!np) {
			continue;
		}
		if (kv.value && _parser_set_next_revision) {
			_parser_set_next_revision(kv.value, np); // old parser stays alive until RemoveOldParsers
		}
		_parser_set_source(np, src.get_data(), src.length(), file_utf8.get_data(), 0);
		_parser_parse(np, pass, false);
		_parser_reduce(np, pass);
		if (_parser_build_defs) {
			_parser_build_defs(np, pass, nullptr, true);
		}
		_build_game_parsers[norm] = np;
	}

	// Hot compile: hotProject = the game project, incrementing hotIdx. The compiler emits a delta
	// (only changed method bodies, as new hot versions) instead of a full rebuild.
	_build_hot_idx++;
	int flags = 1 | 2 | 8 | 0x2000; // EmitDebugInfo | EmitLineInfo | GenerateOBJ | EnableHotSwapping
	// Toolset 1 = Microsoft (NOT 0 = GNU) — must match the baseline so the delta uses the same MSVC
	// mangling, else its references to RT/baseline symbols won't resolve in the loaded module.
	_compiler_set_options(_build_compiler, _build_proj_game, _build_hot_idx, "x86_64-pc-windows-msvc", "",
			1 /*BfToolsetType_Microsoft*/, 0, 0, 0, flags, "malloc", "free");

	// Write deltas into the SAME object tree the baseline used (the compiler's hot state references it).
	String obj_dir = _build_obj_dir.is_empty() ? p_build_dir.path_join("obj") : _build_obj_dir;
	DirAccess::make_dir_recursive_absolute(obj_dir.path_join("corlib"));
	DirAccess::make_dir_recursive_absolute(obj_dir.path_join("GodotBindings"));
	DirAccess::make_dir_recursive_absolute(obj_dir.path_join(p_project_name));
	uint64_t ref_time = OS::get_singleton()->get_unix_time() - 2; // delta = objs rewritten from here on
	uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	bool ok = _compiler_compile(_build_compiler, pass, obj_dir.utf8().get_data());
	uint64_t t1 = OS::get_singleton()->get_ticks_usec();
	if (!ok) {
		_extract_pass_errors(pass, /*beefbuild_format=*/true, r_errors);
		if (r_errors.is_empty()) {
			r_errors.push_back("ERROR: Hot compile failed.");
		}
		if (_pass_delete) {
			_pass_delete(pass);
		}
		_system_unlock(_build_system);
		return false;
	}

	if (_pass_delete) {
		_pass_delete(pass);
	}

	// Gather the delta objects this hot compile produced. GetUsedOutputFileNames(.FlushQueuedHotFiles)
	// would be the official source but returns nothing when driven this way, so detect the objects the
	// compiler just (re)wrote by modification time across the object tree.
	Vector<String> delta;
	{
		List<String> dirs;
		dirs.push_back(obj_dir);
		while (!dirs.is_empty()) {
			String d = dirs.front()->get();
			dirs.pop_front();
			Ref<DirAccess> da = DirAccess::open(d);
			if (da.is_null()) {
				continue;
			}
			da->list_dir_begin();
			for (String n = da->get_next(); !n.is_empty(); n = da->get_next()) {
				if (n == "." || n == "..") {
					continue;
				}
				String full = d.path_join(n);
				if (da->current_is_dir()) {
					dirs.push_back(full);
				} else if (n.to_lower().ends_with(".obj") && FileAccess::get_modified_time(full) >= ref_time) {
					delta.push_back(full);
				}
			}
			da->list_dir_end();
		}
	}

	if (delta.is_empty()) {
		_system_unlock(_build_system);
		r_errors.push_back("Hot compile produced no delta objects (no changes detected, or FlushQueuedHotFiles returned none).");
		return false;
	}

	// Commit the hot versions in the compiler, then patch them into the live debuggee.
	_compiler_hot_commit(_build_compiler);
	String joined;
	for (int i = 0; i < delta.size(); i++) {
		if (i) {
			joined += "\n";
		}
		joined += delta[i];
	}
	CharString joined_utf8 = joined.utf8();
	typedef bool (*PFN_HotLoad)(const char *, int);
	bool loaded = ((PFN_HotLoad)_pfn_hot_load)(joined_utf8.get_data(), _build_hot_idx);
	_system_unlock(_build_system);

	beef_log(vformat("BeefIDEHelper: hot compile %dms, %d delta obj(s), HotLoad(idx=%d) %s",
			(int)((t1 - t0) / 1000), delta.size(), _build_hot_idx, loaded ? "OK" : "FAILED"));
	if (!loaded) {
		r_errors.push_back("Debugger_HotLoad failed (is the game running under a hot-swap debug session?).");
		return false;
	}
	return true;
#else
	(void)p_workspace_dir;
	(void)p_project_name;
	(void)p_build_dir;
	r_errors.push_back("Hot reload is Windows-only.");
	return false;
#endif
}

void BeefIDEHelper::prewarm(const String &p_workspace_dir, const String &p_project_name) {
#ifdef WINDOWS_ENABLED
	// Build the persistent resolve system ahead of the first completion so that request is instant
	// instead of paying the one-time ~1.4s build. Runs on the calling (editor main) thread.
	// TODO: move pre-warm (and the resolve system) onto a background worker thread so the build never
	// stalls the editor at all. That requires serializing every IDEHelper entry point that touches the
	// shared BfSystem (get_parse_errors / classify / find_function / get_completions / prewarm) plus the
	// one-time IDEHelper_ProgramStart behind a single mutex, since today only the resolve path takes
	// BfSystem_Lock and the syntax highlighter calls classify on nearly every edit.
	if (!_resolve_load_compiler()) {
		return;
	}
	const bool verbose = OS::get_singleton()->get_environment("BEEF_AC_VERBOSE") == "1";
	_system_lock(_system, 0);
	_resolve_ensure_built(p_workspace_dir, p_project_name, verbose);
	_system_unlock(_system);
#endif
}

// Reformat a Beef method signature ("void AddChild(Godot.Node node, [bool x = false], ...)") into
// Godot's call-hint form: the argument at p_arg_idx is wrapped in 0xFFFF markers (which the editor
// highlights), optional-arg brackets and verbatim-'@' markers are stripped.
static String _format_call_hint(const String &p_sig, int p_arg_idx) {
	int open = p_sig.find_char('(');
	if (open < 0) {
		return p_sig;
	}
	int close = p_sig.rfind(")");
	if (close < 0) {
		close = p_sig.length();
	}
	String head = p_sig.substr(0, open + 1); // e.g. "void AddChild("
	String args_str = p_sig.substr(open + 1, close - open - 1);
	if (args_str.strip_edges().is_empty()) {
		return head + ")";
	}
	// Depth-aware split on top-level commas (defaults can contain (), [], <>).
	Vector<String> args;
	int depth = 0, start = 0;
	for (int i = 0; i < args_str.length(); i++) {
		char32_t c = args_str[i];
		if (c == '(' || c == '[' || c == '<') {
			depth++;
		} else if (c == ')' || c == ']' || c == '>') {
			depth--;
		} else if (c == ',' && depth == 0) {
			args.push_back(args_str.substr(start, i - start));
			start = i + 1;
		}
	}
	args.push_back(args_str.substr(start));

	const String marker = String::chr(0xFFFF);
	String out = head;
	for (int i = 0; i < args.size(); i++) {
		String a = args[i].strip_edges();
		if (a.begins_with("[") && a.ends_with("]")) {
			a = a.substr(1, a.length() - 2).strip_edges(); // optional-arg bracket
		}
		a = a.replace("@", ""); // verbatim-identifier prefix
		if (i > 0) {
			out += ", ";
		}
		out += (i == p_arg_idx) ? (marker + a + marker) : a;
	}
	out += ")";
	return out;
}

bool BeefIDEHelper::get_completions(const String &p_workspace_dir, const String &p_project_name, const String &p_file, const String &p_source, int p_cursor, Vector<Completion> &r_out, String *r_call_hint) {
#ifdef WINDOWS_ENABLED
	if (!_resolve_load_compiler()) {
		return false;
	}
	// Verbose tracing is opt-in (the headless --beef-complete verifier sets BEEF_AC_VERBOSE) so the live
	// editor completion path stays quiet.
	const bool verbose = OS::get_singleton()->get_environment("BEEF_AC_VERBOSE") == "1";

	_system_lock(_system, 0); // ClassifySource / BuildDefs assert the system lock is held
	_resolve_ensure_built(p_workspace_dir, p_project_name, verbose);

	void *pass = _create_pass(_system);
	CharString src = p_source.utf8();
	CharString file_utf8 = p_file.utf8();
	String norm_file = p_file.replace_char('\\', '/').to_lower();

	// Bring the edited file's actual type def up to date with the editor buffer by creating a new parser
	// revision in place (mirrors BeefIDE's CreateNewParserRevision): link it after the previous parser,
	// re-parse from the buffer and rebuild its defs. The old revision's typedefs are retired by the def
	// builder and swept by RemoveOldData/RemoveOldParsers at the end.
	void *bg_resolve = _create_resolve_pass_data(nullptr, 0 /*BfResolveType_None*/, false);
	{
		void *norm_parser = _create_parser(_system, _proj_game);
		void **prev = _file_parsers.getptr(norm_file);
		if (prev && *prev && _parser_set_next_revision) {
			_parser_set_next_revision(*prev, norm_parser); // *prev stays alive until RemoveOldParsers
		}
		_parser_set_source(norm_parser, src.get_data(), src.length(), file_utf8.get_data(), 0);
		_parser_parse(norm_parser, pass, false);
		_parser_reduce(norm_parser, pass);
		if (_parser_build_defs) {
			_parser_build_defs(norm_parser, pass, bg_resolve, true);
		}
		_file_parsers[norm_file] = norm_parser;
	}
	// Repopulate types changed by the revision above (incremental compile; cheap once bootstrapped).
	_compiler_classify(_resolve_compiler, pass, bg_resolve);

	// A SEPARATE parser carries the autocomplete cursor. SetAutocomplete asserts the parser is fresh
	// (refCount == -1), so it must not be the revised normal parser above. BuildDefs with this resolve
	// pass active is what creates the autocomplete temp type (BfDefBuilder), which ClassifySource ->
	// ProcessAutocompleteTempType then resolves to produce the member list.
	uint64_t t_ac0 = OS::get_singleton()->get_ticks_usec();
	_autocomplete_parser = _create_parser(_system, _proj_game);
	// SetIsClassifying BEFORE SetSource: it sets a parser flag that disables the parser cache. Without
	// it, SetSource would hash the (filename, source) and reuse the revised parser's cached parserData
	// (whose mUniqueParser is null), so every node's GetParser() returns null and BuildDefs crashes.
	if (_parser_set_is_classifying) {
		_parser_set_is_classifying(_autocomplete_parser);
	}
	_parser_set_source(_autocomplete_parser, src.get_data(), src.length(), file_utf8.get_data(), 0);
	_parser_set_autocomplete(_autocomplete_parser, p_cursor);
	void *resolve_data = _create_resolve_pass_data(_autocomplete_parser, 3 /*BfResolveType_Autocomplete*/, true);
	_parser_parse(_autocomplete_parser, pass, false);
	_parser_reduce(_autocomplete_parser, pass);
	if (_parser_build_defs) {
		_parser_build_defs(_autocomplete_parser, pass, resolve_data, false);
	}
	if (_parser_create_classifier) {
		_parser_create_classifier(_autocomplete_parser, pass, resolve_data, nullptr);
	}
	_compiler_classify(_resolve_compiler, pass, resolve_data);
	if (_parser_finish_classifier) {
		_parser_finish_classifier(_autocomplete_parser, resolve_data);
	}
	if (verbose) {
		beef_log(vformat("[AC] autocomplete=%dms", (int)((OS::get_singleton()->get_ticks_usec() - t_ac0) / 1000)));
	}

	// Dump any resolve-pass diagnostics: a failed type/member resolution explains empty completions.
	if (verbose && _get_error_count && _get_error_data) {
		int ec = _get_error_count(pass);
		beef_log(vformat("[AC] pass errors=%d", ec));
		for (int i = 0; i < ec && i < 20; i++) {
			int code = 0, src_start = 0, src_end = 0, line = 0, col = 0, more = 0, while_spec = 0;
			bool is_warn = false, is_after = false, is_deferred = false, is_persistent = false;
			char *proj = nullptr, *fname = nullptr;
			const char *msg = _get_error_data(pass, i, &code, &is_warn, &is_after, &is_deferred,
					&while_spec, &is_persistent, &proj, &fname, &src_start, &src_end, &line, &col, &more);
			beef_log(vformat("[AC]   err[%d] %s (%s:%d)", i, msg ? String::utf8(msg) : String("?"),
					fname ? String::utf8(fname).get_file() : String("?"), line));
		}
	}

	const char *info = _compiler_get_autocomplete(_resolve_compiler);
	String all = info ? String::utf8(info) : String();
	if (verbose) {
		beef_log(vformat("[AC] raw len=%d >>>%s<<<", all.length(), all.left(400)));
	}

	// Each line is "kind\t<display>[\x02<hex match indices>,…X][\x03<documentation>]", and the display
	// itself may carry a second tab for separate insert text. Mirror BeefIDE's AutoComplete parser: take
	// the display, strip the \x02 match-highlight block and \x03 doc, and skip the control/hint kinds
	// that aren't completion items.
	PackedStringArray lines = all.split("\n", false);
	for (int i = 0; i < lines.size(); i++) {
		int tab = lines[i].find_char('\t');
		if (tab < 0) {
			continue;
		}
		String kind = lines[i].substr(0, tab);
		// The "invoke" line is the active call's signature hint: "invoke\t<signature>\t<argIdx>".
		// Route the first one to the caller's call hint rather than dropping it.
		if (kind == "invoke" && r_call_hint && r_call_hint->is_empty()) {
			String rest = lines[i].substr(tab + 1);
			int last_tab = rest.rfind("\t");
			int arg_idx = 0;
			String sig = rest;
			if (last_tab >= 0) {
				arg_idx = rest.substr(last_tab + 1).to_int();
				sig = rest.substr(0, last_tab);
			}
			*r_call_hint = _format_call_hint(sig, arg_idx);
			continue;
		}
		if (kind == "insertRange" || kind == "select" || kind == "uncertain" || kind == "invoke" ||
				kind == "invoke_cur" || kind == "isMember" || kind == "invokeInfo" || kind == "invokeLeftParen" ||
				kind == "typeRef" || kind == "methodRef" || kind == "fieldRef" || kind == "propertyRef" ||
				kind == "localId" || kind == "typeGenericParam" || kind == "methodGenericParam") {
			continue; // protocol directives / call-signature hints, not completion items
		}
		String display = lines[i].substr(tab + 1);
		// Cut at the first of: \x02 (match data), \x03 (documentation), or a second \t (insert text).
		// TODO: capture the \x03 documentation text instead of discarding it, and surface it as the
		// completion item's tooltip/hint (would need a doc field on Completion + plumbing through
		// BeefLanguage::complete_code). The \x02 match indices stay discarded — Godot recomputes its own.
		int cut = -1;
		for (const char32_t sep : { (char32_t)0x02, (char32_t)0x03, (char32_t)'\t' }) {
			int p = display.find_char(sep);
			if (p >= 0 && (cut < 0 || p < cut)) {
				cut = p;
			}
		}
		if (cut >= 0) {
			display = display.substr(0, cut);
		}
		if (display.is_empty()) {
			continue;
		}
		Completion c;
		c.kind = kind;
		c.name = display;
		r_out.push_back(c);
	}

	// Retire the autocomplete parser and any old file revisions/temp types created this pass.
	if (_autocomplete_parser && _parser_delete) {
		_parser_delete(_autocomplete_parser);
		_autocomplete_parser = nullptr;
	}
	if (_pass_delete) {
		_pass_delete(pass);
	}
	if (_system_remove_old_parsers) {
		_system_remove_old_parsers(_system);
	}
	if (_system_remove_old_data) {
		_system_remove_old_data(_system);
	}
	_system_unlock(_system);
	return true;
#else
	return false;
#endif
}

bool BeefIDEHelper::resolve_symbol(const String &p_workspace_dir, const String &p_project_name, const String &p_file, const String &p_source, int p_cursor, String &r_type_full, String &r_kind) {
#ifdef WINDOWS_ENABLED
	if (!_resolve_load_compiler()) {
		return false;
	}
	_system_lock(_system, 0);
	_resolve_ensure_built(p_workspace_dir, p_project_name, false);

	void *pass = _create_pass(_system);
	CharString src = p_source.utf8();
	CharString file_utf8 = p_file.utf8();
	String norm_file = p_file.replace_char('\\', '/').to_lower();

	// Bring the edited file's type def up to date (same revision dance as get_completions).
	void *bg_resolve = _create_resolve_pass_data(nullptr, 0 /*BfResolveType_None*/, false);
	{
		void *norm_parser = _create_parser(_system, _proj_game);
		void **prev = _file_parsers.getptr(norm_file);
		if (prev && *prev && _parser_set_next_revision) {
			_parser_set_next_revision(*prev, norm_parser);
		}
		_parser_set_source(norm_parser, src.get_data(), src.length(), file_utf8.get_data(), 0);
		_parser_parse(norm_parser, pass, false);
		_parser_reduce(norm_parser, pass);
		if (_parser_build_defs) {
			_parser_build_defs(norm_parser, pass, bg_resolve, true);
		}
		_file_parsers[norm_file] = norm_parser;
	}
	_compiler_classify(_resolve_compiler, pass, bg_resolve);

	// Resolve at the cursor in GetSymbolInfo mode: the compiler emits "<kind>Ref\t<project>:<TypeFullName>[\t<idx>]"
	// for the symbol under the cursor (see BfCompiler.cpp _EncodeTypeDef).
	_autocomplete_parser = _create_parser(_system, _proj_game);
	if (_parser_set_is_classifying) {
		_parser_set_is_classifying(_autocomplete_parser);
	}
	_parser_set_source(_autocomplete_parser, src.get_data(), src.length(), file_utf8.get_data(), 0);
	_parser_set_autocomplete(_autocomplete_parser, p_cursor);
	void *resolve_data = _create_resolve_pass_data(_autocomplete_parser, 6 /*BfResolveType_GetSymbolInfo*/, true);
	_parser_parse(_autocomplete_parser, pass, false);
	_parser_reduce(_autocomplete_parser, pass);
	if (_parser_build_defs) {
		_parser_build_defs(_autocomplete_parser, pass, resolve_data, false);
	}
	if (_parser_create_classifier) {
		_parser_create_classifier(_autocomplete_parser, pass, resolve_data, nullptr);
	}
	_compiler_classify(_resolve_compiler, pass, resolve_data);
	if (_parser_finish_classifier) {
		_parser_finish_classifier(_autocomplete_parser, resolve_data);
	}

	const char *info = _compiler_get_autocomplete(_resolve_compiler);
	String all = info ? String::utf8(info) : String();
	bool found = false;
	for (const String &line : all.split("\n", false)) {
		int tab = line.find_char('\t');
		if (tab < 0) {
			continue;
		}
		String kind = line.substr(0, tab);
		if (kind == "methodRef" || kind == "ctorRef" || kind == "fieldRef" || kind == "propertyRef" || kind == "typeRef") {
			String rest = line.substr(tab + 1);
			int t2 = rest.find_char('\t');
			String enc = (t2 >= 0) ? rest.substr(0, t2) : rest; // "<project>:Godot.Node3D"
			int colon = enc.find(":");
			r_type_full = (colon >= 0) ? enc.substr(colon + 1) : enc; // "Godot.Node3D"
			r_kind = kind;
			found = true;
			break;
		}
	}

	if (_autocomplete_parser && _parser_delete) {
		_parser_delete(_autocomplete_parser);
		_autocomplete_parser = nullptr;
	}
	if (_pass_delete) {
		_pass_delete(pass);
	}
	if (_system_remove_old_parsers) {
		_system_remove_old_parsers(_system);
	}
	if (_system_remove_old_data) {
		_system_remove_old_data(_system);
	}
	_system_unlock(_system);
	return found;
#else
	return false;
#endif
}

bool BeefIDEHelper::run_leak_check(const String &p_exe, const String &p_args, const String &p_working_dir, Vector<String> &r_messages) {
#ifdef WINDOWS_ENABLED
	// Load the DLL directly (do NOT call _load(): it creates a BfSystem, and the debugger expects
	// IDEHelper_ProgramStart()'s LLVM StaticInit to run before any system/debugger is created).
	if (!_ensure_dll()) {
		ERR_PRINT("BeefIDEHelper: IDEHelper64.dll not available for leak check.");
		return false;
	}
	HMODULE mod = (HMODULE)_dll;

	typedef void (*PFN_ProgramStart)();
	typedef void (*PFN_TargetsCreate)();
	typedef void (*PFN_DbgCreate)();
	typedef bool (*PFN_OpenFile)(const char *launch_path, const char *target_path, const char *args, const char *working_dir, void *env_block, int env_block_size, bool hot_swap, int flags);
	typedef void (*PFN_Run)();
	typedef void (*PFN_Update)();
	typedef int (*PFN_GetRunState)();
	typedef const char *(*PFN_PopMessage)();
	typedef void (*PFN_Continue)();
	typedef void (*PFN_Detach)();
	typedef const char *(*PFN_GetCurrentException)();
	typedef const char *(*PFN_GetDbgAllocInfo)();

	PFN_ProgramStart program_start = (PFN_ProgramStart)(void *)GetProcAddress(mod, "IDEHelper_ProgramStart");
	PFN_TargetsCreate targets_create = (PFN_TargetsCreate)(void *)GetProcAddress(mod, "Targets_Create");
	PFN_DbgCreate dbg_create = (PFN_DbgCreate)(void *)GetProcAddress(mod, "Debugger_Create");
	PFN_OpenFile open_file = (PFN_OpenFile)(void *)GetProcAddress(mod, "Debugger_OpenFile");
	PFN_Run dbg_run = (PFN_Run)(void *)GetProcAddress(mod, "Debugger_Run");
	PFN_Update dbg_update = (PFN_Update)(void *)GetProcAddress(mod, "Debugger_Update");
	PFN_GetRunState get_run_state = (PFN_GetRunState)(void *)GetProcAddress(mod, "Debugger_GetRunState");
	PFN_PopMessage pop_message = (PFN_PopMessage)(void *)GetProcAddress(mod, "Debugger_PopMessage");
	PFN_Continue dbg_continue = (PFN_Continue)(void *)GetProcAddress(mod, "Debugger_Continue");
	PFN_Detach dbg_detach = (PFN_Detach)(void *)GetProcAddress(mod, "Debugger_Detach");
	PFN_GetCurrentException get_exception = (PFN_GetCurrentException)(void *)GetProcAddress(mod, "Debugger_GetCurrentException");
	PFN_GetDbgAllocInfo get_alloc_info = (PFN_GetDbgAllocInfo)(void *)GetProcAddress(mod, "Debugger_GetDbgAllocInfo");

	if (!program_start || !dbg_create || !open_file || !dbg_run || !dbg_update || !get_run_state || !pop_message || !dbg_continue) {
		ERR_PRINT("BeefIDEHelper: IDEHelper is missing required debugger exports.");
		return false;
	}

	program_start();
	if (targets_create) {
		targets_create(); // initializes gX86Target — the WinDebugger ctor dereferences it
	}
	dbg_create();

	CharString exe_utf8 = p_exe.utf8();
	CharString args_utf8 = p_args.utf8();
	CharString wd_utf8 = p_working_dir.utf8();
	if (!open_file(exe_utf8.get_data(), exe_utf8.get_data(), args_utf8.get_data(), wd_utf8.get_data(), nullptr, 0, false, 0)) {
		ERR_PRINT("BeefIDEHelper: Debugger_OpenFile failed for " + p_exe);
		return false;
	}

	dbg_run();

	const uint64_t start_ms = OS::get_singleton()->get_ticks_msec();
	const uint64_t max_ms = 300000; // 5 min wall-clock safety cap
	while (true) {
		dbg_update();
		for (const char *m = pop_message(); m != nullptr; m = pop_message()) {
			String msg = String::utf8(m);
			r_messages.push_back(msg);
			beef_log("[beef-dbg] " + msg);
		}
		int state = get_run_state();
		if (state == RS_TERMINATED) {
			break;
		}
		if (state == RS_PAUSED || state == RS_BREAKPOINT || state == RS_EXCEPTION) {
			if (get_exception) {
				const char *exc = get_exception();
				if (exc != nullptr && exc[0] != 0) {
					String e = String::utf8(exc);
					r_messages.push_back(e);
					beef_log("[beef-dbg] break: " + e);
				}
			}
			// While paused at the break, ask the debugger for the debuggee's tracked-allocation report.
			// At the runtime's shutdown leak break this is the leaked-object list WITH symbolicated
			// allocation stack traces (what BeefIDE's leak inspector shows) — far richer than the
			// debuggee's own generic "(System.Object)0x.." console line.
			if (get_alloc_info) {
				const char *info = get_alloc_info();
				if (info != nullptr && info[0] != 0) {
					String s = String::utf8(info);
					r_messages.push_back(s);
					beef_log("[beef-dbg] alloc-info >>>\n" + s + "\n<<< alloc-info");
				}
			}
			// Resume so the process can finish terminating.
			dbg_continue();
		} else {
			// Running/idle: Debugger_Update doesn't block, so yield to avoid a busy spin while the
			// debuggee runs (the game animates for a few seconds before its --autoquit shutdown).
			OS::get_singleton()->delay_usec(1000);
		}
		if (OS::get_singleton()->get_ticks_msec() - start_ms > max_ms) {
			beef_log("BeefIDEHelper: leak-check timed out waiting for the game to exit.");
			break;
		}
	}
	if (dbg_detach) {
		dbg_detach();
	}
	beef_log("BeefIDEHelper: leak-check run complete (" + itos(r_messages.size()) + " debugger messages).");
	return true;
#else
	return false;
#endif
}

// ─── Native breakpoint debug session ─────────────────────────────────────────
// IDEHelper WinDebugger C ABI typedefs (intptr == 64-bit on the only platform this builds for).
#ifdef WINDOWS_ENABLED
typedef void (*PFN_Dbg_Void)();
typedef bool (*PFN_Dbg_OpenFile)(const char *launch_path, const char *target_path, const char *args, const char *working_dir, void *env_block, int env_block_size, bool hot_swap, int flags);
typedef int (*PFN_Dbg_GetRunState)();
typedef const char *(*PFN_Dbg_PopMessage)();
typedef void (*PFN_Dbg_StepBool)(bool in_assembly);
typedef void *(*PFN_Dbg_CreateBreakpoint)(const char *file_name, int line_num, int want_column, int instr_offset);
typedef int64_t (*PFN_Dbg_BreakpointGetLineNum)(void *breakpoint);
typedef void (*PFN_Dbg_BreakpointSetCondition)(void *breakpoint, const char *condition);
typedef int (*PFN_Dbg_BreakpointGetHitCount)(void *breakpoint);
typedef void (*PFN_Dbg_BreakpointDelete)(void *breakpoint);
typedef void (*PFN_Dbg_BreakpointCheck)(void *breakpoint);
typedef int (*PFN_Dbg_GetProcessId)();
typedef void *(*PFN_Dbg_GetActiveBreakpoint)();
typedef int (*PFN_Dbg_CallStackGetCount)();
typedef const char *(*PFN_Dbg_CallStackGetFrameInfo)(int stack_frame_idx, int64_t *addr, const char **out_file,
		int32_t *out_hot_idx, int32_t *out_def_line_start, int32_t *out_def_line_end, int32_t *out_line,
		int32_t *out_column, int32_t *out_language, int32_t *out_stack_size, int8_t *out_flags);
// Beef's StringView ({ const char* mPtr; intptr mLength; } == 16 bytes) is returned by value, which on
// x64 means the hidden-pointer ABI; declaring the typedef with the matching by-value return lets the
// compiler generate the correct call.
struct BfStringView {
	const char *mPtr;
	int64_t mLength;
};
typedef BfStringView (*PFN_Dbg_Evaluate)(const char *expr, int call_stack_idx, int cursor_pos, int32_t language, uint16_t expression_flags);
typedef const char *(*PFN_Dbg_EvaluateContinue)();
typedef const char *(*PFN_Dbg_GetAutoLocals)(int call_stack_idx, bool show_regs);
#endif

bool BeefIDEHelper::_load_debugger() {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved) {
		return true;
	}
	// Load IDEHelper64.dll directly (NOT _load(): that creates a BfSystem, but the debugger needs
	// IDEHelper_ProgramStart()'s LLVM StaticInit to run before any system/debugger is created).
	if (!_ensure_dll()) {
		return false;
	}
	HMODULE mod = (HMODULE)_dll;
	_pfn_program_start = (void *)GetProcAddress(mod, "IDEHelper_ProgramStart");
	_pfn_targets_create = (void *)GetProcAddress(mod, "Targets_Create");
	_pfn_dbg_create = (void *)GetProcAddress(mod, "Debugger_Create");
	_pfn_open_file = (void *)GetProcAddress(mod, "Debugger_OpenFile");
	_pfn_run = (void *)GetProcAddress(mod, "Debugger_Run");
	_pfn_update = (void *)GetProcAddress(mod, "Debugger_Update");
	_pfn_get_run_state = (void *)GetProcAddress(mod, "Debugger_GetRunState");
	_pfn_pop_message = (void *)GetProcAddress(mod, "Debugger_PopMessage");
	_pfn_continue = (void *)GetProcAddress(mod, "Debugger_Continue");
	_pfn_detach = (void *)GetProcAddress(mod, "Debugger_Detach");
	_pfn_create_breakpoint = (void *)GetProcAddress(mod, "Debugger_CreateBreakpoint");
	_pfn_bp_get_line = (void *)GetProcAddress(mod, "Breakpoint_GetLineNum");
	_pfn_bp_set_condition = (void *)GetProcAddress(mod, "Breakpoint_SetCondition");
	_pfn_bp_get_hit_count = (void *)GetProcAddress(mod, "Breakpoint_GetHitCount");
	_pfn_bp_delete = (void *)GetProcAddress(mod, "Breakpoint_Delete");
	_pfn_bp_check = (void *)GetProcAddress(mod, "Breakpoint_Check");
	_pfn_stop_debugging = (void *)GetProcAddress(mod, "Debugger_StopDebugging");
	_pfn_get_process_id = (void *)GetProcAddress(mod, "Debugger_GetProcessId");
	_pfn_active_bp = (void *)GetProcAddress(mod, "Debugger_GetActiveBreakpoint");
	_pfn_step_over = (void *)GetProcAddress(mod, "Debugger_StepOver");
	_pfn_step_into = (void *)GetProcAddress(mod, "Debugger_StepInto");
	_pfn_step_out = (void *)GetProcAddress(mod, "Debugger_StepOut");
	_pfn_cs_update = (void *)GetProcAddress(mod, "CallStack_Update");
	_pfn_cs_count = (void *)GetProcAddress(mod, "CallStack_GetCount");
	_pfn_cs_frame_info = (void *)GetProcAddress(mod, "CallStack_GetStackFrameInfo");
	_pfn_evaluate = (void *)GetProcAddress(mod, "Debugger_Evaluate");
	_pfn_evaluate_continue = (void *)GetProcAddress(mod, "Debugger_EvaluateContinue");
	_pfn_get_auto_locals = (void *)GetProcAddress(mod, "Debugger_GetAutoLocals");
	_pfn_hot_load = (void *)GetProcAddress(mod, "Debugger_HotLoad");

	if (!_pfn_program_start || !_pfn_dbg_create || !_pfn_open_file || !_pfn_run || !_pfn_update ||
			!_pfn_get_run_state || !_pfn_pop_message || !_pfn_continue || !_pfn_create_breakpoint ||
			!_pfn_cs_update || !_pfn_cs_count || !_pfn_cs_frame_info) {
		ERR_PRINT("BeefIDEHelper: IDEHelper is missing required debugger exports.");
		return false;
	}
	_dbg_resolved = true;
	return true;
#else
	return false;
#endif
}

bool BeefIDEHelper::debug_start(const String &p_exe, const String &p_args, const String &p_working_dir, bool p_hot_swap, const String &p_target_dll) {
#ifdef WINDOWS_ENABLED
	if (!_load_debugger()) {
		return false;
	}
	// LLVM StaticInit + disassembler target are process-global one-time setup. gX86Target must exist
	// before the WinDebugger ctor dereferences it, so Targets_Create comes before Debugger_Create.
	if (!_program_started) {
		((PFN_Dbg_Void)_pfn_program_start)();
		_program_started = true;
	}
	if (!_dbg_program_started) {
		if (_pfn_targets_create) {
			((PFN_Dbg_Void)_pfn_targets_create)();
		}
		_dbg_program_started = true;
	}
	// The debugger itself is (re)created per session: debug_detach() tears down the previous one (and
	// its background thread) at session end, so a fresh one is needed for each run.
	((PFN_Dbg_Void)_pfn_dbg_create)();

	CharString exe_utf8 = p_exe.utf8();
	CharString args_utf8 = p_args.utf8();
	CharString wd_utf8 = p_working_dir.utf8();
	// launch_path = the process we spawn (the host exe). target_path = the module the hot-swap engine
	// identifies as mTargetBinary and resolves delta symbols against — that MUST be the Beef scripts DLL
	// (it carries the Beef symbols + the linked .libs). When debugging a host app that loads the Beef code
	// as a secondary DLL, these differ. p_hot_swap reserves the runtime hot-swap heap (launch-not-attach).
	// The hot-swap engine identifies the target module by matching this path against the loaded module's
	// path via PathEquals, which is case- AND slash-insensitive (verified: it lowercases and maps \\ -> /).
	// The debugger records the module path from the debug event's lpImageName — i.e. the SAME string the
	// game passed to LoadLibraryW, which is get_dll_path — NOT a canonicalized "\\?\" form. So pass the
	// plain DLL path as-is; do NOT add a "\\?\" prefix or canonicalize (that makes PathEquals mismatch and
	// mTargetBinary stays NULL -> "hot target binary has not yet been loaded").
	String target_path = p_target_dll.is_empty() ? p_exe : p_target_dll;
	CharString target_utf8 = target_path.utf8();
	if (!((PFN_Dbg_OpenFile)_pfn_open_file)(exe_utf8.get_data(), target_utf8.get_data(), args_utf8.get_data(),
				wd_utf8.get_data(), nullptr, 0, p_hot_swap, 0)) {
		ERR_PRINT("BeefIDEHelper: Debugger_OpenFile failed for " + p_exe);
		return false;
	}
	beef_log(vformat("BeefIDEHelper: debug session opened (hot-swap %s)",
			p_hot_swap ? "ON" : "off"));
	return true;
#else
	return false;
#endif
}

void *BeefIDEHelper::debug_add_breakpoint(const String &p_file, int p_line, const String &p_condition) {
#ifdef WINDOWS_ENABLED
	if (!_dbg_resolved || !_pfn_create_breakpoint) {
		return nullptr;
	}
	CharString file_utf8 = p_file.utf8();
	// IDEHelper line numbers are 0-based (see get_parse_errors), so a 1-based Godot line maps to line-1.
	// wantColumn=-1 (any column), instrOffset=-1 (resolve to the line's first instruction).
	void *bp = ((PFN_Dbg_CreateBreakpoint)_pfn_create_breakpoint)(file_utf8.get_data(), p_line - 1, -1, -1);
	if (bp) {
		if (!p_condition.is_empty() && _pfn_bp_set_condition) {
			// A Beef expression evaluated in the breakpoint's frame; the debuggee only stops when true.
			CharString cond_utf8 = p_condition.utf8();
			((PFN_Dbg_BreakpointSetCondition)_pfn_bp_set_condition)(bp, cond_utf8.get_data());
			beef_log(vformat("BeefIDEHelper: breakpoint at %s:%d (conditional)", String(p_file).get_file(), p_line));
		} else {
			beef_log(vformat("BeefIDEHelper: breakpoint at %s:%d", String(p_file).get_file(), p_line));
		}
		// Bind the breakpoint to the loaded module now. At session start this also happens as modules
		// load, but for a breakpoint added mid-session the module is already loaded, so without this
		// explicit Check the physical breakpoint is never set and it would never fire (matches how
		// BeefIDE calls Breakpoint_Check after Debugger_CreateBreakpoint).
		if (_pfn_bp_check) {
			((PFN_Dbg_BreakpointCheck)_pfn_bp_check)(bp);
		}
	}
	return bp;
#else
	return nullptr;
#endif
}

void BeefIDEHelper::debug_set_breakpoint_condition(void *p_breakpoint, const String &p_condition) {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_bp_set_condition && p_breakpoint) {
		// Mirrors BeefIDE: an empty string clears the condition; the expression is re-evaluated on the
		// next hit, so no rebind (Breakpoint_Check) is needed for a condition change mid-session.
		CharString cond_utf8 = p_condition.utf8();
		((PFN_Dbg_BreakpointSetCondition)_pfn_bp_set_condition)(p_breakpoint, cond_utf8.get_data());
	}
#endif
}

int BeefIDEHelper::debug_breakpoint_hit_count(void *p_breakpoint) {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_bp_get_hit_count && p_breakpoint) {
		return ((PFN_Dbg_BreakpointGetHitCount)_pfn_bp_get_hit_count)(p_breakpoint);
	}
#endif
	return 0;
}

void BeefIDEHelper::debug_delete_breakpoint(void *p_breakpoint) {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_bp_delete && p_breakpoint) {
		((PFN_Dbg_BreakpointDelete)_pfn_bp_delete)(p_breakpoint);
	}
#endif
}

void BeefIDEHelper::debug_begin_run() {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_run) {
		((PFN_Dbg_Void)_pfn_run)();
	}
#endif
}

void BeefIDEHelper::debug_continue() {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_continue) {
		((PFN_Dbg_Void)_pfn_continue)();
	}
#endif
}

void BeefIDEHelper::debug_step_over() {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_step_over) {
		((PFN_Dbg_StepBool)_pfn_step_over)(false);
	}
#endif
}

void BeefIDEHelper::debug_step_into() {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_step_into) {
		((PFN_Dbg_StepBool)_pfn_step_into)(false);
	}
#endif
}

void BeefIDEHelper::debug_step_out() {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_step_out) {
		((PFN_Dbg_StepBool)_pfn_step_out)(false);
	}
#endif
}

int BeefIDEHelper::debug_run_state() {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_get_run_state) {
		return ((PFN_Dbg_GetRunState)_pfn_get_run_state)();
	}
#endif
	return RS_TERMINATED;
}

int BeefIDEHelper::debug_process_id() {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_get_process_id) {
		return ((PFN_Dbg_GetProcessId)_pfn_get_process_id)();
	}
#endif
	return 0;
}

int BeefIDEHelper::debug_active_breakpoint_line() {
#ifdef WINDOWS_ENABLED
	if (_dbg_resolved && _pfn_active_bp && _pfn_bp_get_line) {
		void *bp = ((PFN_Dbg_GetActiveBreakpoint)_pfn_active_bp)();
		if (bp) {
			// 0-based internally; report 1-based to match Godot/editor line conventions.
			return (int)((PFN_Dbg_BreakpointGetLineNum)_pfn_bp_get_line)(bp) + 1;
		}
	}
#endif
	return 0;
}

// Debugger messages can carry OS-localized text in the system code page (non-UTF8), which trips
// Godot's strict UTF-8 parser. The frame/breakpoint text we care about is ASCII; replace any high
// byte with '?' so the message is clean ASCII and never spams "Unicode parsing error".
static String _sanitize_dbg_msg(const char *p_msg) {
	CharString out;
	for (const char *c = p_msg; *c; c++) {
		out += ((unsigned char)*c < 0x80) ? *c : '?';
	}
	return String(out.get_data());
}

int BeefIDEHelper::debug_wait(Vector<String> &r_messages, int p_timeout_ms) {
#ifdef WINDOWS_ENABLED
	if (!_dbg_resolved) {
		return RS_TERMINATED;
	}
	const uint64_t start_ms = OS::get_singleton()->get_ticks_msec();
	while (true) {
		((PFN_Dbg_Void)_pfn_update)();
		for (const char *m = ((PFN_Dbg_PopMessage)_pfn_pop_message)(); m != nullptr; m = ((PFN_Dbg_PopMessage)_pfn_pop_message)()) {
			r_messages.push_back(_sanitize_dbg_msg(m));
		}
		int state = ((PFN_Dbg_GetRunState)_pfn_get_run_state)();
		// A stop (breakpoint, paused, or exception) or termination ends the wait; running keeps pumping.
		if (state == RS_PAUSED || state == RS_BREAKPOINT || state == RS_EXCEPTION || state == RS_TERMINATED) {
			return state;
		}
		OS::get_singleton()->delay_usec(1000); // Update doesn't block; yield to avoid a busy spin
		if ((int)(OS::get_singleton()->get_ticks_msec() - start_ms) > p_timeout_ms) {
			beef_log("BeefIDEHelper: debug_wait timed out.");
			return state;
		}
	}
#else
	return RS_TERMINATED;
#endif
}

int BeefIDEHelper::debug_poll(Vector<String> &r_messages) {
#ifdef WINDOWS_ENABLED
	if (!_dbg_resolved) {
		return RS_TERMINATED;
	}
	((PFN_Dbg_Void)_pfn_update)();
	for (const char *m = ((PFN_Dbg_PopMessage)_pfn_pop_message)(); m != nullptr; m = ((PFN_Dbg_PopMessage)_pfn_pop_message)()) {
		r_messages.push_back(_sanitize_dbg_msg(m));
	}
	return ((PFN_Dbg_GetRunState)_pfn_get_run_state)();
#else
	return RS_TERMINATED;
#endif
}

Vector<BeefIDEHelper::DebugFrame> BeefIDEHelper::debug_call_stack() {
	Vector<DebugFrame> frames;
#ifdef WINDOWS_ENABLED
	if (!_dbg_resolved || !_pfn_cs_update || !_pfn_cs_count || !_pfn_cs_frame_info) {
		return frames;
	}
	((PFN_Dbg_Void)_pfn_cs_update)();
	int count = ((PFN_Dbg_CallStackGetCount)_pfn_cs_count)();
	for (int i = 0; i < count; i++) {
		int64_t addr = 0;
		const char *file = nullptr;
		int32_t hot_idx = 0, def_start = 0, def_end = 0, line = 0, column = 0, language = 0, stack_size = 0;
		int8_t flags = 0;
		const char *desc = ((PFN_Dbg_CallStackGetFrameInfo)_pfn_cs_frame_info)(i, &addr, &file, &hot_idx,
				&def_start, &def_end, &line, &column, &language, &stack_size, &flags);
		DebugFrame f;
		f.description = desc ? String::utf8(desc) : String();
		// Beef appends a "#<source-hash>" suffix to the file path in its debug info; strip it so the
		// path is a plain filesystem path consumers can resolve/open.
		f.file = file ? String::utf8(file) : String();
		int hash_pos = f.file.find("#");
		if (hash_pos >= 0) {
			f.file = f.file.substr(0, hash_pos);
		}
		f.line = line + 1; // 0-based -> 1-based
		f.address = (uint64_t)addr;
		frames.push_back(f);
	}
#endif
	return frames;
}

String BeefIDEHelper::debug_evaluate(const String &p_expr, int p_frame_idx) {
#ifdef WINDOWS_ENABLED
	if (!_dbg_resolved || !_pfn_evaluate) {
		return String();
	}
	CharString expr_utf8 = p_expr.utf8();
	// cursorPos=-1 (no autocomplete cursor), language=-1 (infer from the frame), flags=0 (plain value).
	BfStringView sv = ((PFN_Dbg_Evaluate)_pfn_evaluate)(expr_utf8.get_data(), p_frame_idx, -1, -1, 0);
	String result = (sv.mPtr && sv.mLength > 0) ? String::utf8(sv.mPtr, (int)sv.mLength) : String();

	// The evaluator may defer (function calls, visualizers): "!pending" means drive EvaluateContinue
	// until it resolves. Bound the loop so a stuck evaluation can't hang the session.
	if (result == "!pending" && _pfn_evaluate_continue) {
		for (int i = 0; i < 1000; i++) {
			if (_pfn_update) {
				((PFN_Dbg_Void)_pfn_update)();
			}
			const char *cont = ((PFN_Dbg_EvaluateContinue)_pfn_evaluate_continue)();
			result = cont ? String::utf8(cont) : String();
			if (result != "!pending") {
				break;
			}
			OS::get_singleton()->delay_usec(1000);
		}
	}

	// The result is "value\n<type/metadata...>"; return just the displayed value (first line). Error
	// and status results ("!Not paused", "!<message>") begin with '!' and are returned verbatim.
	int nl = result.find("\n");
	if (nl != -1) {
		result = result.substr(0, nl);
	}
	return result;
#else
	return String();
#endif
}

Vector<String> BeefIDEHelper::debug_locals(int p_frame_idx) {
	Vector<String> locals;
#ifdef WINDOWS_ENABLED
	if (!_dbg_resolved || !_pfn_get_auto_locals) {
		return locals;
	}
	// Newline-separated local names in scope at the frame ("$this" represents the receiver). Evaluate
	// each with debug_evaluate to get its value.
	const char *raw = ((PFN_Dbg_GetAutoLocals)_pfn_get_auto_locals)(p_frame_idx, false);
	if (raw) {
		String all = _sanitize_dbg_msg(raw);
		PackedStringArray names = all.split("\n", false);
		for (int i = 0; i < names.size(); i++) {
			String n = names[i].strip_edges();
			if (!n.is_empty()) {
				locals.push_back(n);
			}
		}
	}
#endif
	return locals;
}

void BeefIDEHelper::debug_stop() {
#ifdef WINDOWS_ENABLED
	// Terminate the debuggee. StopDebugging is a no-op if it already exited, and (unlike Detach) does
	// not assert on remaining breakpoints. Callers should debug_delete_breakpoint() their breakpoints
	// first so they don't accumulate across sessions.
	if (_dbg_resolved && _pfn_stop_debugging) {
		((PFN_Dbg_Void)_pfn_stop_debugging)();
	}
#endif
}

void BeefIDEHelper::debug_detach() {
#ifdef WINDOWS_ENABLED
	// Detach tears down the debugger (stops its background thread, resets shutdown state) and frees
	// gDebugger. This MUST be done at session end: the background thread only exits on Detach, otherwise
	// a second session spawns a second thread that fights the first over shared state. debug_start
	// recreates the debugger for the next run. Breakpoints must already be deleted (Detach asserts on
	// leftovers).
	if (_dbg_resolved && _pfn_detach) {
		((PFN_Dbg_Void)_pfn_detach)();
	}
#endif
}

void BeefIDEHelper::hard_exit(int p_code) {
#ifdef WINDOWS_ENABLED
	fflush(stdout);
	// TerminateProcess does NOT run DLL_PROCESS_DETACH, so it skips IDEHelper's thread-join hang.
	TerminateProcess(GetCurrentProcess(), (UINT)p_code);
#endif
	::_exit(p_code);
}

void BeefIDEHelper::cleanup() {
	// Deliberately do NOT BfSystem_Delete or FreeLibrary the IDEHelper module. IDEHelper is a full
	// compiler with background worker threads; tearing it down (joining threads / unmapping the DLL)
	// at process shutdown hangs — the same failure mode as the Beef runtime. We only ever clean up at
	// process exit, so leaking it is harmless: the OS reclaims everything after the worker threads are
	// terminated. (Per-parse BfParser_Delete during the session is fine; that is not the hang.)
	_system = nullptr;
	_dll = nullptr;
}

BeefIDEHelper::~BeefIDEHelper() {
	cleanup();
}
