/**************************************************************************/
/*  beef_editor_plugin.h                                                  */
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

#include "editor/plugins/editor_plugin.h"

#include "core/input/shortcut.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class Button;
class InputEvent;
class LineEdit;
class MenuButton;
class PopupMenu;
class RichTextLabel;
class TabContainer;
class Tree;
class TreeItem;

// Editor integration for Beef, implemented in C++ (the C# module does the equivalent in its managed
// GodotTools plugin). It mirrors that UI: a bottom panel with an Output tab (build log) and a
// Problems tab (an errors/warnings tree with filters, search and go-to-source), a build menu and a
// "show in file manager" button in the tab bar, a Build button in the run bar, and a
// build-before-play hook so a broken build aborts the run.
class BeefEditorPlugin : public EditorPlugin {
	GDCLASS(BeefEditorPlugin, EditorPlugin);

	struct Diagnostic {
		bool is_error = false;
		String file; // absolute path as reported by BeefBuild
		int line = 0;
		int column = 0;
		String message;
	};

	// Bottom panel.
	TabContainer *tabs = nullptr;
	MenuButton *build_menu = nullptr;
	Button *logs_button = nullptr;

	// Output tab.
	RichTextLabel *output_log = nullptr;
	Button *output_clear_button = nullptr;
	Button *output_copy_button = nullptr;

	// Problems tab.
	Tree *problems_tree = nullptr;
	LineEdit *search_box = nullptr;
	Button *problems_clear_button = nullptr;
	Button *problems_copy_button = nullptr;
	Button *error_filter_button = nullptr;
	Button *warning_filter_button = nullptr;

	// Debug tab (drives the IDEHelper-backed native debug session via BeefIDEHelper).
	Tree *callstack_tree = nullptr;
	Tree *variables_tree = nullptr; // Locals + Watches (Name | Value)
	Tree *breakpoints_tree = nullptr; // .bf breakpoints (Location | Condition | Hits)
	LineEdit *watch_input = nullptr; // add a watch expression
	PopupMenu *tree_copy_menu = nullptr; // right-click Copy / Copy All for the debug trees
	Tree *_copy_menu_tree = nullptr; // tree the copy menu currently targets
	int _watch_menu_index = -1; // watch index the right-click menu's "Remove Watch" targets, or -1
	RichTextLabel *debug_reason = nullptr;
	Button *debug_launch_button = nullptr;
	Button *debug_continue_button = nullptr;
	Button *debug_step_over_button = nullptr;
	Button *debug_step_into_button = nullptr;
	Button *debug_step_out_button = nullptr;
	Button *debug_stop_button = nullptr;
	bool _debug_active = false; // a session is running (poll each frame)
	int _debug_last_state = 0; // last RunState seen, to react on transitions
	bool _debug_skip_loader_pause = false; // auto-continue the initial OS loader breakpoint once
	int _debug_pid = 0; // debuggee OS process id, for a last-resort kill on stop
	// Hot-reload baseline: established once per process when a hot-swap debug session first launches, so a
	// .bf save during a session can be hot-compiled into a delta and patched live via Debugger_HotLoad.
	bool _hot_baseline_ready = false;
	String _hot_workspace;
	String _hot_project;
	String _hot_build_dir;
	int _selected_frame = 0; // call-stack frame whose variables are shown
	String _exec_script_path; // res:// of the script with the current execution-line marker
	Vector<String> _watches; // user watch expressions (persist across stops)
	HashMap<String, String> _bp_conditions; // "abs_path:line" -> Beef condition expression
	HashMap<String, void *> _bp_handles; // "abs_path:line" -> Breakpoint* for the live session (hit counts)

	// Shared.
	Button *bottom_panel_button = nullptr; // the toggle button in the bottom-panel bar (holds the status icon)
	Button *run_bar_build_button = nullptr;
	Ref<Shortcut> _build_shortcut; // Alt+B → build (applied to the run-bar button)

	Vector<Diagnostic> _diagnostics;
	int _error_count = 0;
	int _warning_count = 0;

	void _build_ui();
	void _update_theme();

	// Build orchestration.
	bool _do_build(bool p_rebuild, bool p_reveal_on_error);
	void _on_build_pressed();
	void _on_rebuild_pressed();
	void _on_clean_pressed();
	void _on_build_menu_id(int p_id);
	void _on_open_logs();

	// Diagnostics.
	void _parse_diagnostics(const String &p_output);
	static bool _parse_diagnostic_line(const String &p_line, Diagnostic &r_diag);
	void _populate_problems();
	bool _should_show(const Diagnostic &p_diag) const;
	void _on_problem_activated();
	void _on_filter_toggled(bool p_pressed);
	void _on_search_changed(const String &p_text);

	// Output/problems actions.
	void _clear_output();
	void _copy_output();
	void _clear_problems();
	void _copy_problems();

	// Debug session.
	void _build_debug_tab();
	void _on_debug_launch();
	void _on_debug_continue();
	void _on_debug_step_over();
	void _on_debug_step_into();
	void _on_debug_step_out();
	void _on_debug_stop();
	void _on_resource_saved(Ref<Resource> p_res); // hot-reload a .bf saved during an active session
	void _dbg_log(const String &p_msg); // diagnostic: append to the Output tab + beef_debug.log
	void _compiler_log(const String &p_msg); // beef_log() sink: BeefCompiler/BeefIDEHelper logs -> Output tab
	void _debug_poll(); // per-frame: pump the session, react to stops/termination
	void _debug_on_stopped(); // refresh call stack + reason on a stop
	void _refresh_call_stack();
	void _refresh_variables(); // locals + watches for the selected frame
	TreeItem *_add_variable_item(TreeItem *p_parent, const String &p_name, const String &p_expr, const String &p_value, int p_watch_index);
	void _on_variable_collapsed(TreeItem *p_item); // lazily expand an object's members (defers the work)
	void _populate_variable_children(TreeItem *p_item); // deferred: build child rows (tree-safe, off the signal)
	void _on_callstack_frame_selected();
	void _navigate_to_frame(TreeItem *p_item); // jump to source + variables + execution marker
	void _set_execution_line(const String &p_res_path, int p_line); // editor execution-line marker
	void _clear_execution_line();
	void _on_add_watch(const String &p_text);
	void _on_variables_item_activated(); // remove a watch on double-click
	void _refresh_breakpoints(); // rebuild the breakpoints pane from the editor store + hit counts
	void _on_editor_breakpoint_toggled(const String &p_path, int p_line, bool p_enabled); // live add/remove during a session
	void _on_breakpoint_activated(); // jump to the breakpoint's source line
	void _on_breakpoint_edited(); // condition cell edited
	void _save_bp_conditions(); // persist breakpoint conditions to project metadata
	void _load_bp_conditions(); // restore breakpoint conditions from project metadata
	void _save_watches(); // persist watch expressions to project metadata
	void _load_watches(); // restore watch expressions from project metadata
	// Multi-row selection copy for the debug trees.
	void _setup_tree_copy(Tree *p_tree);
	void _on_tree_rmb(const Vector2 &p_pos, int p_button, Tree *p_tree);
	void _on_tree_gui_input(const Ref<InputEvent> &p_event, Tree *p_tree);
	void _on_tree_copy_menu(int p_id);
	void _copy_tree_rows(Tree *p_tree, bool p_all);
	void _set_debug_controls_enabled(bool p_stopped);
	void _end_debug_session();

	void _set_status_icon();

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	virtual String get_plugin_name() const override { return "Beef"; }

	// Called by the editor before launching the game. Returning false aborts the run.
	virtual bool build() override;

	// Testing hook: parse one BeefBuild log line. Returns true (and fills the out-params) when the
	// line is a diagnostic, false otherwise.
	static bool test_parse_line(const String &p_line, bool &r_is_error, String &r_file, int &r_line, int &r_col, String &r_message);

	// Strip BeefBuild's carriage-return progress bar (and collapse \r redraws) for display.
	static String _clean_build_output(const String &p_output);

	BeefEditorPlugin();
	~BeefEditorPlugin();
};
