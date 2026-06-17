/**************************************************************************/
/*  beef_compiler.cpp                                                     */
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

#include "beef_compiler.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/print_string.h"

#ifdef WINDOWS_ENABLED
#include <windows.h>
#endif

// On the web (Emscripten) dynamic-linking build, the Beef scripts ship as a wasm SIDE_MODULE that the
// engine dlopen()s at runtime — the same role LoadLibrary plays on Windows. WEB_DLINK_ENABLED is
// defined by platform/web/detect.py only when scons is built with dlink_enabled=yes.
#ifdef WEB_DLINK_ENABLED
#include <dlfcn.h>
#include <stdio.h>
#endif

BeefCompiler *BeefCompiler::_singleton = nullptr;
void (*BeefCompiler::s_pre_unload_callback)() = nullptr;

static Callable s_beef_log_sink;

void beef_set_log_sink(const Callable &p_sink) {
	s_beef_log_sink = p_sink;
}

void beef_log(const String &p_msg) {
	// Editor: route to the Beef bottom-panel Output tab. Otherwise (game / headless / non-editor):
	// fall back to the engine log so output still appears on stdout / the general log.
	if (s_beef_log_sink.is_valid()) {
		s_beef_log_sink.call(p_msg);
	} else {
		print_line(p_msg);
	}
}

bool BeefCompiler::find_beef_tools(const String &p_override_path) {
	// 0. Explicit override (from EditorSettings: beef/editor/beef_build_path)
	if (!p_override_path.is_empty()) {
		if (FileAccess::exists(p_override_path)) {
			beef_build_path = p_override_path;
			found = true;
			_singleton = this;
			beef_log("BeefCompiler: using configured BeefBuild");
			return true;
		} else {
			WARN_PRINT("BeefCompiler: configured beef_build_path not found: " + p_override_path);
		}
	}

	// 1. Bundled toolchain next to the editor (self-contained — no Beef installation required). The
	// bundle ships a Beef-install-like tree (bin/BeefBuild.exe + BeefLibs/corlib + bin/llvm/lld-link.exe
	// + the RT libs) built from the SAME submodule as IDEHelper64.dll, so its version matches — which is
	// required for in-editor hot reload (Debugger_HotLoad resolves deltas only against a same-version DLL).
	// Preferred over any installed Beef so the shipped editor is deterministic.
	{
		String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
		String candidate = exe_dir.path_join("BeefBundle/bin").path_join("BeefBuild.exe");
		if (FileAccess::exists(candidate)) {
			beef_build_path = candidate;
			found = true;
			_singleton = this;
			beef_log("BeefCompiler: using bundled BeefBuild");
			return true;
		}
	}

	// No installed-Beef / BEEF_PATH / PATH fallback by design: the toolchain that builds the loaded DLL
	// MUST be the same version as the editor's IDEHelper (hot-reload requirement), and an arbitrary
	// installed Beef would silently break that. The bundle is the default; the only override is the
	// explicit EditorSetting (beef/editor/beef_build_path) handled above.
	_singleton = this; // register even when not found so get_singleton() always works
	WARN_PRINT("BeefCompiler: BeefBuild not found. Build the bundle with make_beef_bundle.bat (bin/BeefBundle) or set beef/editor/beef_build_path to a custom BeefBuild.exe.");
	return false;
}

void BeefCompiler::ensure_project_files(const String &p_workspace_dir, const String &p_project_name) {
	String src_dir = p_workspace_dir.path_join("src");

	Error err = DirAccess::make_dir_recursive_absolute(p_workspace_dir);
	if (err != OK && err != ERR_ALREADY_EXISTS) {
		ERR_PRINT("BeefCompiler: failed to create workspace dir (error " + itos(err) + ")");
	}

	// Pin corlib to the SAME toolchain as the resolved BeefBuild (the bundle's BeefLibs, or the install's
	// for a dev fallback). Without this, BeefBuild resolves the `corlib = "*"` dependency from its global/
	// install config — which on a machine with a different Beef installed is a VERSION MISMATCH (the
	// compiler injects System.Compiler.Options.* types a mismatched corlib lacks). A workspace
	// BeefConfig.toml's UnversionedLibDirs takes precedence, so corlib always matches BeefBuild — and is
	// what lets hot reload work. Rewritten each call so it tracks the current toolchain location.
	if (beef_build_path.contains("/") || beef_build_path.contains("\\")) {
		String toolchain_libs = beef_build_path.get_base_dir().get_base_dir().path_join("BeefLibs");
		if (FileAccess::exists(toolchain_libs.path_join("corlib").path_join("BeefProj.toml"))) {
			Ref<FileAccess> cf = FileAccess::open(p_workspace_dir.path_join("BeefConfig.toml"), FileAccess::WRITE);
			if (cf.is_valid()) {
				cf->store_string("Version = 1\nUnversionedLibDirs = [\"" + toolchain_libs.replace("\\", "/") + "\"]\n");
			}
		}
	}

	err = DirAccess::make_dir_recursive_absolute(src_dir);
	if (err != OK && err != ERR_ALREADY_EXISTS) {
		ERR_PRINT("BeefCompiler: failed to create src dir (error " + itos(err) + ")");
	}

	// --- src/GodotBridge.bf: placeholder so BeefBuild never sees an empty project ---
	// BeefBuild aborts with "no source code" if src/ is empty. This file only satisfies that
	// requirement until src/ has real content; once any other .bf exists (the generated
	// GodotRuntime.bf/GodotScript.bf, or the user's scripts) it is unnecessary and is not
	// (re)created, so it doesn't clutter the project or reappear after deletion.
	String bridge_file = src_dir.path_join("GodotBridge.bf");
	bool src_has_other_bf = false;
	if (Ref<DirAccess> da = DirAccess::open(src_dir); da.is_valid()) {
		da->list_dir_begin();
		for (String f = da->get_next(); !f.is_empty(); f = da->get_next()) {
			if (!da->current_is_dir() && f.get_extension() == "bf" && f != "GodotBridge.bf") {
				src_has_other_bf = true;
				break;
			}
		}
		da->list_dir_end();
	}
	if (!src_has_other_bf && !FileAccess::exists(bridge_file)) {
		// Sanitize project name to a valid Beef identifier (replace hyphens/spaces)
		String safe_ns = p_project_name.replace("-", "_").replace(" ", "_");
		Ref<FileAccess> f = FileAccess::open(bridge_file, FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string(
					"// Auto-generated by Godot-Beef. Safe to delete once you have your own .bf files here.\n"
					"// Place your Beef script classes in this directory (res://beef/src/).\n"
					"namespace " +
					safe_ns + ";\n");
		}
	}

	// --- BeefSpace.toml: workspace with user scripts project + GodotBindings ---
	// Debug disables Beef's OBJECT-level leak detection (object-debug-flags + realtime scanner) but keeps
	// the Debug allocator, so raw-allocation leaks are still caught at shutdown. The object scanner is
	// incompatible with this interop: live objects live behind a C++ void* it can't see, engine wrappers
	// are retained for the process lifetime by design, and it access-violates on corlib's own FFIType*
	// statistics — so it crashes on by-design retention, not on real leaks of ours.
	String space_toml = p_workspace_dir.path_join("BeefSpace.toml");
	// AllocType=CRT + CLibType=Dynamic + no object-debug-flags also matches the hot-reload baseline's
	// codegen, so IDEHelper deltas resolve against the loaded DLL. (Release stays static for export.)
	const char *leak_check_configs =
			"\n"
			"[Configs.Debug.Win64]\n"
			"EnableObjectDebugFlags = false\n"
			"EnableRealtimeLeakCheck = false\n"
			"AllocType = \"CRT\"\n"
			"CLibType = \"Dynamic\"\n"
			"\n"
			"[Configs.Debug.Win32]\n"
			"EnableObjectDebugFlags = false\n"
			"EnableRealtimeLeakCheck = false\n"
			"\n"
			"[Configs.Debug.wasm32]\n"
			"EnableObjectDebugFlags = false\n"
			"EnableRealtimeLeakCheck = false\n"
			"\n"
			"[Configs.Release.wasm32]\n"
			"EnableObjectDebugFlags = false\n"
			"EnableRealtimeLeakCheck = false\n";
	if (!FileAccess::exists(space_toml)) {
		Ref<FileAccess> f = FileAccess::open(space_toml, FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string(
					"FileVersion = 1\n"
					"Projects = {" +
					p_project_name +
					" = {Path = \".\"}, GodotBindings = {Path = \"GodotBindings\"}}\n"
					"\n"
					"[Workspace]\n"
					"StartupProject = \"" +
					p_project_name + "\"\n" + leak_check_configs);
		}
	} else {
		// Heal pre-existing workspaces that predate the leak-check config: append it if absent.
		Ref<FileAccess> rf = FileAccess::open(space_toml, FileAccess::READ);
		if (rf.is_valid()) {
			String existing = rf->get_as_text();
			rf.unref();
			if (!existing.contains("EnableRealtimeLeakCheck")) {
				Ref<FileAccess> af = FileAccess::open(space_toml, FileAccess::READ_WRITE);
				if (af.is_valid()) {
					af->seek_end();
					af->store_string(leak_check_configs);
				}
				// Changing the Debug allocator/object-debug-flags invalidates every previously
				// built .obj/.lib in build/: llvm-ar fails to combine the mismatched archive
				// ("could not parse library"). Wipe the Debug intermediates so the first compile
				// after the heal is fully fresh. Fires once (next ensure sees the config present).
				clean_build(p_workspace_dir, "Debug");
			}
		}
	}

	// --- BeefProj.toml: user scripts project (DynamicLib) ---
	// Both configs link the Beef runtime statically (BeefLibType = "Static") so the scripts DLL is
	// self-contained: an exported game ships just this one DLL, with no Beef runtime DLLs and (Release)
	// no leak scanner to FatalExit on shutdown. The Debug config also carries a DebugCommand pointing at
	// this Godot exe + project, so the workspace can be opened in BeefIDE and Run to hot-patch the live
	// DLL; unused by the Godot-driven workflow.
	String proj_toml = p_workspace_dir.path_join("BeefProj.toml");
	if (!FileAccess::exists(proj_toml)) {
		String godot_exe = OS::get_singleton()->get_executable_path().replace("/", "\\\\");
		String proj_root = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/").replace("/", "\\\\");
		Ref<FileAccess> f = FileAccess::open(proj_toml, FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string(
					"FileVersion = 1\n"
					"Dependencies = {corlib = \"*\", GodotBindings = \"*\"}\n"
					"\n"
					"[Project]\n"
					"Name = \"" +
					p_project_name +
					"\"\n"
					"TargetType = \"BeefLib\"\n"
					"\n"
					// Debug links the Beef runtime DYNAMICALLY (BeefLibType=Dynamic) + CRT alloc so its
					// codegen matches the IDEHelper hot-reload baseline (enabling live in-editor patching);
					// Beef042RT64.dll is copied next to the built DLL. Release stays Static/self-contained.
					"[Configs.Debug.Win64]\n"
					"BuildKind = \"DynamicLib\"\n"
					"BeefLibType = \"Dynamic\"\n"
					"DebugCommand = \"" +
					godot_exe +
					"\"\n"
					"DebugCommandArguments = \"--path " +
					proj_root +
					"\"\n"
					"DebugWorkingDirectory = \"" +
					proj_root +
					"\"\n"
					"\n"
					"[Configs.Release.Win64]\n"
					"BuildKind = \"DynamicLib\"\n"
					"BeefLibType = \"Static\"\n"
					"\n"
					// Web (wasm32): the scripts compile to a position-independent (PIC) wasm SIDE_MODULE.
					// ReflectAlwaysInclude=IncludeAll forces the corlib/binding types the reflection
					// metadata references to actually be compiled, so the side module has no unresolved
					// data symbols when the engine dlopen()s it.
					"[Configs.Debug.wasm32]\n"
					"BuildKind = \"DynamicLib\"\n"
					"BeefLibType = \"Static\"\n"
					"RelocType = \"PIC\"\n"
					"PICLevel = \"Big\"\n"
					"\n"
					"[Configs.Release.wasm32]\n"
					"BuildKind = \"DynamicLib\"\n"
					"BeefLibType = \"Static\"\n"
					"RelocType = \"PIC\"\n"
					"PICLevel = \"Big\"\n"
					"PreprocessorMacros = [\"RELEASE\"]\n"
					"ReflectAlwaysInclude = \"IncludeAll\"\n"
					"ReflectNonStaticMethods = true\n"
					"ReflectStaticMethods = true\n"
					"ReflectConstructors = true\n");
		}
	} else {
		// Heal pre-existing workspaces whose configs predate static linking: ensure every
		// [Configs.*.Win64] block that sets BuildKind = "DynamicLib" also pins BeefLibType = "Static",
		// so older projects (and ones whose Release config was scaffolded bare) become self-contained.
		Ref<FileAccess> rf = FileAccess::open(proj_toml, FileAccess::READ);
		if (rf.is_valid()) {
			String existing = rf->get_as_text();
			rf.unref();
			PackedStringArray lines = existing.split("\n");
			String healed;
			bool changed = false;
			for (int i = 0; i < lines.size(); i++) {
				healed += lines[i] + "\n";
				if (lines[i].strip_edges() == "BuildKind = \"DynamicLib\"") {
					// Does this config block already declare BeefLibType (before the next section)?
					bool has_lib_type = false;
					for (int j = i + 1; j < lines.size(); j++) {
						String peek = lines[j].strip_edges();
						if (peek.begins_with("[")) {
							break;
						}
						if (peek.begins_with("BeefLibType")) {
							has_lib_type = true;
							break;
						}
					}
					if (!has_lib_type) {
						healed += "BeefLibType = \"Static\"\n";
						changed = true;
					}
				}
			}
			if (changed) {
				healed = healed.trim_suffix("\n"); // split/join added one trailing newline
				Ref<FileAccess> wf = FileAccess::open(proj_toml, FileAccess::WRITE);
				if (wf.is_valid()) {
					wf->store_string(healed);
				}
				// Switching the runtime link type invalidates previously built intermediates; wipe both
				// config build dirs so the next compile of each is fully fresh.
				clean_build(p_workspace_dir, "Debug");
				clean_build(p_workspace_dir, "Release");
			}
		}
	}

	// --- beef/build/.gdignore: prevent Godot from importing BeefBuild intermediates ---
	// BeefBuild emits .obj, .lib, .dll, .pdb etc. into build/. Without .gdignore Godot's
	// importer tries to parse vdata.obj as a 3D mesh and emits errors every startup.
	String build_dir = p_workspace_dir.path_join("build");
	DirAccess::make_dir_recursive_absolute(build_dir);
	String build_gdignore = build_dir.path_join(".gdignore");
	if (!FileAccess::exists(build_gdignore)) {
		Ref<FileAccess> f = FileAccess::open(build_gdignore, FileAccess::WRITE);
		// .gdignore file is intentionally empty — its presence is all that matters.
	}

	// --- GodotBindings sub-project (StaticLib, holds generated Godot API bindings) ---
	String bindings_dir = p_workspace_dir.path_join("GodotBindings");
	DirAccess::make_dir_recursive_absolute(bindings_dir);
	DirAccess::make_dir_recursive_absolute(bindings_dir.path_join("src"));

	// --- beef/GodotBindings/.gdignore: prevent Godot from scanning ~1027 auto-generated .bf files ---
	// Without this, Godot's script class scanner tries to register every generated binding file,
	// causing a very long startup delay and noisy "global class" errors in the editor.
	String bindings_gdignore = bindings_dir.path_join(".gdignore");
	if (!FileAccess::exists(bindings_gdignore)) {
		FileAccess::open(bindings_gdignore, FileAccess::WRITE);
	}

	String bindings_toml = bindings_dir.path_join("BeefProj.toml");
	if (!FileAccess::exists(bindings_toml)) {
		Ref<FileAccess> f = FileAccess::open(bindings_toml, FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string(
					"FileVersion = 1\n"
					"Dependencies = {corlib = \"*\"}\n"
					"\n"
					"[Project]\n"
					"Name = \"GodotBindings\"\n"
					"TargetType = \"BeefLib\"\n"
					"\n"
					"[Configs.Debug.Win64]\n"
					"BuildKind = \"StaticLib\"\n"
					"\n"
					"[Configs.Release.Win64]\n"
					"BuildKind = \"StaticLib\"\n"
					"\n"
					"[Configs.Debug.wasm32]\n"
					"BuildKind = \"StaticLib\"\n"
					"RelocType = \"PIC\"\n"
					"PICLevel = \"Big\"\n"
					"\n"
					"[Configs.Release.wasm32]\n"
					"BuildKind = \"StaticLib\"\n"
					"RelocType = \"PIC\"\n"
					"PICLevel = \"Big\"\n");
		}
	}
}

bool BeefCompiler::compile(const String &p_workspace_dir, const String &p_project_name,
		const String &p_config, String &r_dll_path, Vector<String> &r_errors, String *r_output) {
	if (!found) {
		String msg = "BeefBuild.exe not found. Install Beef IDE from https://www.beeflang.org/";
		r_errors.push_back(msg);
		if (r_output) {
			*r_output = msg;
		}
		return false;
	}

	if (_compile_blocked) {
		// Silent — caller already saw the original errors. Unblock with unblock_compile().
		if (r_output) {
			*r_output = "Build skipped: a previous build failed. Fix the errors and build again.";
		}
		return false;
	}

	ensure_project_files(p_workspace_dir, p_project_name);

	List<String> args;
	args.push_back("-proddir=" + p_workspace_dir);
	args.push_back("-config=" + p_config);
	// Append any user-configured extra BeefBuild args (beef/build/extra_args), space-separated.
	String extra_args = ProjectSettings::get_singleton()->get_setting("beef/build/extra_args");
	for (const String &a : extra_args.split(" ", false)) {
		if (!a.strip_edges().is_empty()) {
			args.push_back(a);
		}
	}

	String output;
	int exit_code = 0;
	Error err = OS::get_singleton()->execute(beef_build_path, args, &output, &exit_code, true);
	if (r_output) {
		*r_output = output;
	}
	if (err != OK) {
		r_errors.push_back("Failed to run BeefBuild: " + beef_build_path);
		_compile_blocked = true;
		return false;
	}

	if (exit_code != 0) {
		PackedStringArray lines = output.split("\n");
		for (int i = 0; i < lines.size(); i++) {
			String line = lines[i].strip_edges();
			if (!line.is_empty()) {
				r_errors.push_back(line);
				ERR_PRINT("Beef: " + line);
			}
		}
		_compile_blocked = true;
		return false;
	}

	r_dll_path = get_dll_path(p_workspace_dir, p_project_name, p_config);

	beef_log("BeefCompiler: compile OK");
	return true;
}

void BeefCompiler::clean_build(const String &p_workspace_dir, const String &p_config) {
	// Remove <workspace>/build/<config>_Win64 so the next compile is from scratch.
	String build_out = p_workspace_dir.path_join("build").path_join(p_config + "_Win64");
	if (!DirAccess::exists(build_out)) {
		return;
	}
	Ref<DirAccess> da = DirAccess::open(build_out);
	if (da.is_valid()) {
		Error err = da->erase_contents_recursive();
		if (err != OK) {
			WARN_PRINT("BeefCompiler: clean_build could not fully clear the build dir (err " + itos(err) + ")");
		}
	}
}

String BeefCompiler::get_dll_path(const String &p_workspace_dir, const String &p_project_name, const String &p_config) {
	// Convention: <workspace>/build/<config>_Win64/<project_name>/<project_name>.dll
	return p_workspace_dir
			.path_join("build")
			.path_join(p_config + "_Win64")
			.path_join(p_project_name)
			.path_join(p_project_name + ".dll");
}

String BeefCompiler::get_wasm_side_module_path(const String &p_workspace_dir, const String &p_project_name, const String &p_config) {
	return p_workspace_dir
			.path_join("build")
			.path_join(p_config + "_wasm32")
			.path_join(p_project_name)
			.path_join(p_project_name + ".side.wasm");
}

// Locate the emcc launcher. Prefer an explicit emscripten dir, else fall back to PATH.
static String _find_emcc(const String &p_emscripten_dir) {
	// Try the explicit setting first, then the EMSDK env var (set by emsdk_env), so a user who has
	// activated emsdk doesn't have to also set beef/editor/web/emscripten_dir by hand.
	const char *candidates[] = { "upstream/emscripten/emcc.bat", "emcc.bat", "emcc", nullptr };
	String dirs[] = { p_emscripten_dir, OS::get_singleton()->get_environment("EMSDK") };
	for (int d = 0; d < 2; d++) {
		if (dirs[d].is_empty()) {
			continue;
		}
		for (int i = 0; candidates[i] != nullptr; i++) {
			String c = dirs[d].path_join(candidates[i]);
			if (FileAccess::exists(c)) {
				return c;
			}
		}
	}
#ifdef WINDOWS_ENABLED
	return "emcc.bat"; // rely on PATH
#else
	return "emcc";
#endif
}

// Recursively copy a directory tree (used to seed a workspace-local corlib from the toolchain).
static void _copy_dir_recursive(const String &p_from, const String &p_to) {
	Ref<DirAccess> src = DirAccess::open(p_from);
	if (src.is_null()) {
		return;
	}
	DirAccess::make_dir_recursive_absolute(p_to);
	src->list_dir_begin();
	for (String f = src->get_next(); !f.is_empty(); f = src->get_next()) {
		if (f == "." || f == "..") {
			continue; // DirAccess yields navigational entries; recursing into "." would loop forever
		}
		String sp = p_from.path_join(f);
		String dp = p_to.path_join(f);
		if (src->current_is_dir()) {
			_copy_dir_recursive(sp, dp);
		} else if (!FileAccess::exists(dp)) {
			Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
			if (d.is_valid()) {
				d->copy(sp, dp);
			}
		}
	}
	src->list_dir_end();
}

// The wasm SIDE_MODULE must be fully position-independent, including corlib — but Beef's default
// corlib (from the toolchain) has no wasm32 config, so it would compile non-PIC (absolute data
// addresses) and the side module would mis-resolve at load. Mirror what a hand-set-up web workspace
// does: seed a workspace-local corlib copy with a wasm32 PIC config and reference it from BeefSpace,
// so BeefBuild compiles corlib PIC for this workspace. Idempotent; only touches things once.
static bool _ensure_pic_corlib(const String &p_beef_build_path, const String &p_workspace_dir, Vector<String> &r_errors) {
	String local = p_workspace_dir.path_join("corlib");
	String local_toml = local.path_join("BeefProj.toml");
	const char *wasm_cfg =
			"\n[Configs.Debug.wasm32]\nRelocType = \"PIC\"\nPICLevel = \"Big\"\n"
			"\n[Configs.Release.wasm32]\nRelocType = \"PIC\"\nPICLevel = \"Big\"\n";

	// Already a local corlib with a wasm32 PIC config? Nothing to do. Match the config-section header
	// and the RelocType line specifically so an unrelated mention of "wasm32"/"PIC" (e.g. a comment)
	// doesn't false-positive.
	if (FileAccess::exists(local_toml)) {
		String t = FileAccess::get_file_as_string(local_toml);
		if (t.find(".wasm32]") != -1 && t.find("RelocType = \"PIC\"") != -1) {
			return true;
		}
	}

	// Need the toolchain corlib to seed from. BeefBuild on PATH (no directory) can't be traced to it.
	if (p_beef_build_path.find("/") == -1 && p_beef_build_path.find("\\") == -1) {
		r_errors.push_back("Cannot locate the Beef toolchain corlib (BeefBuild resolved from PATH). "
						   "Set up a workspace-local corlib with a wasm32 PIC config manually.");
		return false;
	}
	String toolchain_corlib = p_beef_build_path.get_base_dir().get_base_dir().path_join("BeefLibs").path_join("corlib");
	if (!FileAccess::exists(toolchain_corlib.path_join("BeefProj.toml"))) {
		r_errors.push_back("Beef toolchain corlib not found at " + toolchain_corlib + " — cannot prepare a PIC corlib for web.");
		return false;
	}

	// Seed the local copy (skips files that already exist, so it's safe to re-run).
	if (!FileAccess::exists(local_toml)) {
		beef_log("BeefCompiler[web]: seeding workspace-local PIC corlib");
		_copy_dir_recursive(toolchain_corlib, local);
	}

	// Ensure the local corlib's BeefProj.toml carries the wasm32 PIC config.
	if (FileAccess::exists(local_toml)) {
		String t = FileAccess::get_file_as_string(local_toml);
		if (t.find(".wasm32]") == -1) {
			Ref<FileAccess> f = FileAccess::open(local_toml, FileAccess::READ_WRITE);
			if (f.is_valid()) {
				f->seek_end();
				f->store_string(wasm_cfg);
			}
		}
	} else {
		r_errors.push_back("Failed to seed local corlib at " + local);
		return false;
	}

	// Reference the local corlib from BeefSpace.toml so BeefBuild uses it (not the toolchain one).
	String space = p_workspace_dir.path_join("BeefSpace.toml");
	if (FileAccess::exists(space)) {
		String s = FileAccess::get_file_as_string(space);
		PackedStringArray lines = s.split("\n");
		String out;
		bool changed = false;
		for (int i = 0; i < lines.size(); i++) {
			String line = lines[i];
			// Inject corlib into the Projects map unless it's already a key there. Test for the key at a
			// brace/comma boundary so a project NAMED something like "mycorlib" doesn't false-match.
			bool has_corlib = line.find("{corlib ") != -1 || line.find(", corlib ") != -1 ||
					line.find("{corlib=") != -1 || line.find(", corlib=") != -1;
			if (line.find("Projects =") != -1 && !has_corlib) {
				int last = line.rfind("}");
				if (last != -1) {
					line = line.substr(0, last) + ", corlib = {Path = \"corlib\"}" + line.substr(last);
					changed = true;
				}
			}
			out += line;
			if (i < lines.size() - 1) {
				out += "\n";
			}
		}
		if (changed) {
			Ref<FileAccess> wf = FileAccess::open(space, FileAccess::WRITE);
			if (wf.is_valid()) {
				wf->store_string(out);
			}
		}
	}
	return true;
}

// Append every *.obj / *.o / *.a in p_dir to r_objs (absolute, globalized paths).
static void _gather_objects(const String &p_dir, Vector<String> &r_objs) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	da->list_dir_begin();
	for (String f = da->get_next(); !f.is_empty(); f = da->get_next()) {
		if (da->current_is_dir()) {
			continue;
		}
		if (f.ends_with(".obj") || f.ends_with(".o") || f.ends_with(".a")) {
			r_objs.push_back(p_dir.path_join(f));
		}
	}
	da->list_dir_end();
}

bool BeefCompiler::build_wasm_side_module(const String &p_workspace_dir, const String &p_project_name,
		const String &p_config, const String &p_emscripten_dir, const String &p_wasm_runtime_dir,
		String &r_out_path, Vector<String> &r_errors) {
	if (!found) {
		r_errors.push_back("BeefBuild.exe not found — cannot compile the Beef scripts for web.");
		return false;
	}

	ensure_project_files(p_workspace_dir, p_project_name);

	// 0. Make sure corlib will compile position-independent for wasm (the toolchain corlib has no wasm32
	// config and would build non-PIC, breaking the side module's data relocations at load). Seeds a
	// workspace-local PIC corlib the first time; idempotent thereafter.
	if (!_ensure_pic_corlib(beef_build_path, p_workspace_dir, r_errors)) {
		return false;
	}

	// 1. Compile the scripts for wasm32 with BeefBuild. Its own (non-SIDE_MODULE) link step may fail —
	// we only need the per-translation-unit objects, which it writes before linking — so a nonzero
	// exit is tolerated as long as the project's vdata.obj landed. A genuine compile error leaves it
	// absent, which we detect and report.
	String wasm_build_dir = p_workspace_dir.path_join("build").path_join(p_config + "_wasm32");
	{
		List<String> args;
		args.push_back("-proddir=" + p_workspace_dir);
		args.push_back("-config=" + p_config);
		args.push_back("-platform=wasm32");
		beef_log("BeefCompiler[web]: compiling scripts for wasm32 (" + p_config + ")...");
		String output;
		int exit_code = 0;
		OS::get_singleton()->execute(beef_build_path, args, &output, &exit_code, true);
		String vdata = wasm_build_dir.path_join(p_project_name).path_join("vdata.obj");
		if (!FileAccess::exists(vdata)) {
			// No objects produced -> a real compile failure (not just the ignorable link step).
			PackedStringArray lines = output.split("\n");
			for (int i = 0; i < lines.size(); i++) {
				String line = lines[i].strip_edges();
				if (line.find("error") != -1 || line.find("ERROR") != -1) {
					r_errors.push_back(line);
				}
			}
			r_errors.push_back("BeefBuild produced no wasm objects. Is the Beef wasm toolchain set up "
							   "(Emscripten configured in BeefIDE + wasm runtime built via wasm/build_wasm.bat)?");
			return false;
		}
	}

	// 2. Gather everything to link: the workspace's compiled objects (corlib + GodotBindings + the user
	// scripts project) plus the Beef wasm runtime objects/lib.
	Vector<String> objs;
	_gather_objects(wasm_build_dir.path_join("corlib"), objs);
	_gather_objects(wasm_build_dir.path_join("GodotBindings"), objs);
	_gather_objects(wasm_build_dir.path_join(p_project_name), objs);
	int script_objs = objs.size();
	if (!p_wasm_runtime_dir.is_empty()) {
		_gather_objects(p_wasm_runtime_dir, objs);
	}
	if (objs.size() == script_objs) {
		r_errors.push_back("No Beef wasm runtime objects found in '" + p_wasm_runtime_dir.get_file() +
				"'. Set beef/editor/web/wasm_runtime_dir to the folder holding the Beef wasm runtime "
				"(.o/.a built via Beef's wasm/build_wasm.bat).");
		return false;
	}

	String emcc = _find_emcc(p_emscripten_dir);

	// 3. A small BeefStop stub the Beef runtime references (a trap on fatal error). Generate + compile it
	// so the web build needs no extra shipped artifact; --allow-multiple-definition tolerates the RT
	// also providing one.
	String stub_c = wasm_build_dir.path_join("beefstop_stub.c");
	String stub_o = wasm_build_dir.path_join("beefstop_stub.o");
	{
		Ref<FileAccess> f = FileAccess::open(stub_c, FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string("void BeefStop(void){__builtin_trap();}\n");
			f->close();
			List<String> cargs;
			cargs.push_back("-fPIC");
			cargs.push_back("-O2");
			cargs.push_back("-c");
			cargs.push_back(stub_c);
			cargs.push_back("-o");
			cargs.push_back(stub_o);
			int ec = 0;
			OS::get_singleton()->execute(emcc, cargs, nullptr, &ec, true);
			if (!FileAccess::exists(stub_o)) {
				r_errors.push_back("emcc failed to compile the BeefStop stub (exit " + itos(ec) + ", emcc: " +
						emcc + "). The wasm link would fail with 'undefined symbol BeefStop'.");
				return false;
			}
			objs.push_back(stub_o);
		} else {
			r_errors.push_back("Could not write the BeefStop stub source at " + stub_c + ".");
			return false;
		}
	}

	// 4. Write the object list to a response file (1000+ objects overflow the command line).
	String rsp = wasm_build_dir.path_join("_side_link.rsp");
	{
		Ref<FileAccess> f = FileAccess::open(rsp, FileAccess::WRITE);
		if (f.is_null()) {
			r_errors.push_back("Could not write link response file: " + rsp);
			return false;
		}
		for (int i = 0; i < objs.size(); i++) {
			f->store_string("\"" + objs[i] + "\"\n");
		}
		f->close();
	}

	r_out_path = get_wasm_side_module_path(p_workspace_dir, p_project_name, p_config);

	// 5. Link the SIDE_MODULE. SIDE_MODULE=1 auto-exports every symbol — both the BeefGodot_* entry
	// points the engine dlsym()s AND the module's own data symbols (__bfStrData/__bfStrObj) that its
	// GOT.mem entries must self-resolve at load. (An explicit export list switches wasm-ld to
	// export-only-these and drops that auto-export, so the loader can't resolve the GOT and dlopen
	// fails at load.) -Wl,-O0 disables wasm-ld's SHF_STRINGS merge, which mis-relocates string-literal
	// data under PIC — the core web blocker; -O2 still optimizes via wasm-opt. Data symbols collide
	// harmlessly across the per-project vdata copies, so allow multiple definitions.
	List<String> args;
	args.push_back("@" + rsp);
	args.push_back("-sSIDE_MODULE=1");
	args.push_back("-O2");
	args.push_back("-Wl,-O0");
	args.push_back("-Wl,--allow-multiple-definition");
	args.push_back("-o");
	args.push_back(r_out_path);

	beef_log("BeefCompiler[web]: linking SIDE_MODULE");
	String output;
	int exit_code = 0;
	Error err = OS::get_singleton()->execute(emcc, args, &output, &exit_code, true);
	if (err != OK || exit_code != 0 || !FileAccess::exists(r_out_path)) {
		PackedStringArray lines = output.split("\n");
		for (int i = 0; i < lines.size(); i++) {
			String line = lines[i].strip_edges();
			if (!line.is_empty()) {
				r_errors.push_back(line);
			}
		}
		r_errors.push_back("emcc failed to link the wasm SIDE_MODULE (emcc: " + emcc + ").");
		return false;
	}
	beef_log("BeefCompiler[web]: built side module");
	return true;
}

bool BeefCompiler::load_dll(const String &p_path, bool p_quiet, bool p_shadow) {
	unload_dll();
#ifdef WINDOWS_ENABLED
	String load_path = p_path;
	if (p_shadow) {
		// Load a private copy so the real output DLL stays unlocked: an external builder (or our own
		// in-editor build) can then relink it without us calling FreeLibrary first — FreeLibrary on
		// the Beef debug runtime is unsafe (its background scan thread). A fixed shadow path per file
		// is reused (overwritten) each run, so copies don't accumulate.
		String shadow_dir = OS::get_singleton()->get_cache_path().path_join("godot_beef_loaded");
		DirAccess::make_dir_recursive_absolute(shadow_dir);
		String shadow = shadow_dir.path_join(p_path.get_file());
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (da.is_valid() && da->copy(p_path, shadow) == OK) {
			load_path = shadow;
			_shadow_path = shadow;
		} else {
			WARN_PRINT("BeefCompiler: could not shadow-copy the DLL; loading it in place (the output file will be locked).");
		}
	}
	dll_handle = (void *)LoadLibraryW((LPCWSTR)load_path.utf16().get_data());
	if (!dll_handle) {
		if (!_shadow_path.is_empty()) {
			DirAccess::remove_absolute(_shadow_path); // don't orphan the staged copy on a failed load
			_shadow_path = String();
		}
		if (!p_quiet) {
			ERR_PRINT("BeefCompiler: failed to load DLL '" + load_path.get_file() + "' (error " + itos((int64_t)GetLastError()) + ")");
		}
		return false;
	}
	beef_log(_shadow_path.is_empty() ? "BeefCompiler: DLL loaded OK" : "BeefCompiler: DLL loaded OK (shadow copy)");
	return true;
#elif defined(WEB_DLINK_ENABLED)
	// Web: dlopen the Beef scripts wasm SIDE_MODULE. No shadow copy / FreeLibrary dance — that exists to
	// dodge the Beef debug runtime's background scan thread on Windows, which the single-threaded web
	// build doesn't run. The module is loaded in place.
	(void)p_shadow;
	// The side module ships inside the .pck (Godot's virtual FS), but Emscripten's synchronous dlopen
	// reads its own in-memory MEMFS. Stage the bytes from res:// into a MEMFS path first, then dlopen it.
	String mem_path = p_path;
	if (p_path.begins_with("res://") || p_path.begins_with("user://")) {
		Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path);
		if (bytes.is_empty()) {
			if (!p_quiet) {
				ERR_PRINT("BeefCompiler: bundled side module not found in package: " + p_path.get_file());
			}
			return false;
		}
		mem_path = String("/") + p_path.get_file(); // e.g. /godot-beef-test.side.wasm in MEMFS
		CharString mp = mem_path.utf8();
		FILE *out = fopen(mp.get_data(), "wb");
		if (out == nullptr) {
			if (!p_quiet) {
				ERR_PRINT("BeefCompiler: could not stage side module '" + mem_path.get_file() + "' into MEMFS");
			}
			return false;
		}
		fwrite(bytes.ptr(), 1, bytes.size(), out);
		fclose(out);
	}
	CharString path_utf8 = mem_path.utf8();
	dll_handle = dlopen(path_utf8.get_data(), RTLD_NOW | RTLD_LOCAL);
	if (!dll_handle) {
		if (!p_quiet) {
			const char *err = dlerror();
			ERR_PRINT("BeefCompiler: dlopen failed for '" + p_path.get_file() + "'" + (err ? (String(": ") + err) : String()));
		}
		return false;
	}
	// NOTE: do NOT call __wasm_call_ctors here. Emscripten already runs the side module's ctors
	// (which apply data relocations) when it preloads the module via dynamicLibraries; calling it a
	// second time re-applies the data relocs, double-offsetting static pointers (e.g. string-literal
	// char data) into garbage.
	beef_log("BeefCompiler: side module loaded OK (web dlink)");
	return true;
#else
	ERR_PRINT("BeefCompiler: DLL loading is Windows-only for now");
	return false;
#endif
}

void BeefCompiler::unload_dll(bool p_free_library) {
	if (dll_handle) {
		// Destroy all live Beef objects BEFORE FreeLibrary.
		// Beef's debug runtime checks for un-deleted objects at DLL_PROCESS_DETACH;
		// if any beef_obj is still alive when FreeLibrary fires, it triggers a DebugBreak crash.
		if (s_pre_unload_callback) {
			s_pre_unload_callback();
		} else {
			WARN_PRINT("BeefCompiler: unload_dll: no pre-unload callback registered — live Beef objects may leak");
		}
		// At final process shutdown we deliberately skip FreeLibrary: the Beef debug runtime runs
		// a background leak-scan thread, and unmapping the DLL out from under that still-running
		// thread access-violates. Letting the OS unload the library at process exit (after it has
		// terminated the worker threads) avoids the crash. Mid-session reloads still FreeLibrary so
		// a freshly recompiled DLL can replace the old one.
		if (p_free_library) {
#ifdef WINDOWS_ENABLED
			FreeLibrary((HMODULE)dll_handle);
			// The shadow copy is no longer mapped; remove it (best-effort).
			if (!_shadow_path.is_empty()) {
				Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
				if (da.is_valid()) {
					da->remove(_shadow_path);
				}
			}
#elif defined(WEB_DLINK_ENABLED)
			dlclose(dll_handle);
#endif
		}
		_shadow_path = String();
		dll_handle = nullptr;
		_compile_blocked = false; // allow a fresh compile on next reload
	}
}

void *BeefCompiler::get_proc(const char *p_name) {
#ifdef WINDOWS_ENABLED
	if (!dll_handle) {
		return nullptr;
	}
	return (void *)GetProcAddress((HMODULE)dll_handle, p_name);
#elif defined(WEB_DLINK_ENABLED)
	if (!dll_handle) {
		return nullptr;
	}
	return dlsym(dll_handle, p_name);
#else
	return nullptr;
#endif
}

void BeefCompiler::cleanup() {
	// Final shutdown: destroy live objects but DON'T FreeLibrary (see unload_dll) — avoids the
	// debug-runtime scan-thread crash. The OS unloads the DLL when the process exits.
	unload_dll(false);
	if (_singleton == this) {
		_singleton = nullptr;
	}
}
