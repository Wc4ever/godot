#include "beef_export_plugin.h"

#ifdef TOOLS_ENABLED

#include "../beef_script.h"
#include "../compiler/beef_compiler.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "editor/settings/editor_settings.h"

// Recursively collect [GodotScript] class names under p_dir (the workspace src/).
static void _collect_script_classes(const String &p_dir, Vector<String> &r_classes) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	da->list_dir_begin();
	for (String f = da->get_next(); !f.is_empty(); f = da->get_next()) {
		if (f.begins_with(".")) {
			continue;
		}
		String full = p_dir.path_join(f);
		if (da->current_is_dir()) {
			_collect_script_classes(full, r_classes);
		} else if (f.ends_with(".bf")) {
			Ref<FileAccess> sf = FileAccess::open(full, FileAccess::READ);
			if (sf.is_valid()) {
				String name, base;
				if (BeefScript::parse_global_class(sf->get_as_text(), name, base) && !name.is_empty()) {
					r_classes.push_back(name);
				}
			}
		}
	}
	da->list_dir_end();
}

void BeefExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	if (p_features.has("web")) {
		_export_web(p_debug);
		return;
	}
	// Windows-only for now (the DLL build config and runtime are Win64). Other desktop platforms
	// need their own Beef build + .so/.dylib bundling before this should fire.
	if (!p_features.has("windows")) {
		return;
	}

	BeefLanguage *lang = BeefLanguage::get_singleton();
	String workspace_dir, project_name, ignore_config;
	bool ignore_external = false;
	if (!lang || !lang->_resolve_build_settings(workspace_dir, project_name, ignore_config, ignore_external)) {
		ERR_PRINT("Beef export: workspace directory is not configured; skipping Beef scripts bundling.");
		return;
	}

	// A release export ships the Release config (statically-linked Beef runtime, no leak scanner);
	// a debug export ships the Debug config (which keeps the debug allocator's leak safety net).
	String build_config = p_debug ? String("Debug") : String("Release");

	String dll_path = BeefCompiler::get_dll_path(workspace_dir, project_name, build_config);
	if (!FileAccess::exists(dll_path)) {
		// Build the chosen config on demand so a fresh export doesn't require the user to have
		// manually built that exact config first. (The Godot-driven editor build only builds Debug.)
		BeefCompiler *compiler = BeefCompiler::get_singleton();
		if (compiler == nullptr) {
			ERR_PRINT("Beef export: compiler unavailable; cannot build the " + build_config + " scripts DLL.");
			return;
		}
		print_line("Beef export: building the " + build_config + " scripts DLL...");
		Vector<String> errors;
		if (!compiler->compile(workspace_dir, project_name, build_config, dll_path, errors)) {
			ERR_PRINT("Beef export: failed to build the " + build_config + " scripts DLL. "
					"Fix the Beef build errors and export again — the exported game would have no scripts otherwise.");
			return;
		}
	}

	// add_shared_object copies the file into the export beside the binary on desktop.
	Vector<String> tags; // empty == applies to this (Windows) export
	String build_dir = dll_path.get_base_dir();
	add_shared_object(dll_path, tags, String());

	// With static linking (BeefLibType = "Static", the default the workspace is scaffolded with) the
	// scripts DLL carries the Beef runtime inside it, so nothing else needs bundling. Only fall back to
	// shipping a runtime DLL if a dynamic build left one next to the DLL. Never ship Beef042Dbg64.dll
	// (the leak-scanning debug runtime) in a release export — its shutdown scan FatalExits the game.
	int bundled_runtimes = 0;
	const char *runtimes[] = { "Beef042RT64.dll", "Beef042Dbg64.dll", nullptr };
	for (int i = 0; runtimes[i] != nullptr; i++) {
		bool is_debug_runtime = (String(runtimes[i]) == "Beef042Dbg64.dll");
		if (!p_debug && is_debug_runtime) {
			continue; // never ship the leak-scanning runtime in a release game
		}
		String rt = build_dir.path_join(runtimes[i]);
		if (FileAccess::exists(rt)) {
			add_shared_object(rt, tags, String());
			bundled_runtimes++;
		}
	}

	if (bundled_runtimes == 0) {
		print_line("Beef export: bundled self-contained " + project_name + ".dll (" + build_config +
				", statically linked — no Beef runtime DLL needed).");
	} else {
		print_line("Beef export: bundled " + project_name + ".dll (" + build_config + ") + " +
				itos(bundled_runtimes) + " Beef runtime DLL(s) next to the executable.");
	}
}

void BeefExportPlugin::_export_web(bool p_debug) {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	BeefLanguage *lang = BeefLanguage::get_singleton();
	String workspace_dir, project_name, ignore_config;
	bool ignore_external = false;
	if (!lang || !lang->_resolve_build_settings(workspace_dir, project_name, ignore_config, ignore_external)) {
		ERR_PRINT("Beef web export: workspace directory is not configured; skipping Beef scripts bundling.");
		return;
	}
	// Always build the web side module in Release: the Beef debug runtime (leak scanner) is a no-op on
	// the single-threaded web build, and the wasm runtime objects are compiled optimized — mixing a
	// Debug script build with them pulls in debug-only symbols the web main module doesn't provide.
	String build_config = "Release";

	BeefCompiler *compiler = BeefCompiler::get_singleton();
	if (compiler == nullptr) {
		ERR_PRINT("Beef web export: compiler unavailable; cannot build the wasm side module.");
		return;
	}

	// Count [GodotScript] classes (for the log line; SIDE_MODULE=1 auto-exports their entry points).
	Vector<String> classes;
	_collect_script_classes(workspace_dir.path_join("src"), classes);

	// Per-machine SDK paths (EditorSettings, not committed project.godot). globalize_path leaves an
	// already-absolute path untouched and resolves a res://-relative one.
	String emscripten_dir = ps->globalize_path(String(EDITOR_GET("beef/editor/web/emscripten_dir")));
	String wasm_runtime_dir = ps->globalize_path(String(EDITOR_GET("beef/editor/web/wasm_runtime_dir")));

	String side_module;
	Vector<String> errors;
	if (!compiler->build_wasm_side_module(workspace_dir, project_name, build_config,
				emscripten_dir, wasm_runtime_dir, side_module, errors)) {
		for (int i = 0; i < errors.size(); i++) {
			ERR_PRINT("Beef web export: " + errors[i]);
		}
		ERR_PRINT("Beef web export: failed to build the wasm side module — the exported game would have no scripts.");
		return;
	}

	// Ship the side module inside the package as <project>.side.wasm. The (wasm) engine dlopen()s it by
	// that name at startup (see BeefScript::reload / BeefCompiler::load_dll). add_file places it in the
	// export data so it is present in the Emscripten filesystem the loader reads.
	Ref<FileAccess> f = FileAccess::open(side_module, FileAccess::READ);
	if (f.is_null()) {
		ERR_PRINT("Beef web export: built side module '" + side_module.get_file() + "' not readable.");
		return;
	}
	Vector<uint8_t> data;
	data.resize(f->get_length());
	f->get_buffer(data.ptrw(), data.size());
	add_file(project_name + ".side.wasm", data, false);

	print_line("Beef web export: bundled " + project_name + ".side.wasm (" + build_config + ", " +
			itos(data.size()) + " bytes, " + itos(classes.size()) + " script class(es)).");
}

#endif // TOOLS_ENABLED
