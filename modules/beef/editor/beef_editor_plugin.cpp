/**************************************************************************/
/*  beef_editor_plugin.cpp                                                */
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

#include "beef_editor_plugin.h"

#include "../beef_script.h"
#include "../compiler/beef_compiler.h"
#include "../ide/beef_ide_helper.h"
#include "beef_syntax_highlighter.h"

#include "editor/debugger/editor_debugger_node.h"
#include "editor/script/script_editor_plugin.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_method_pointer.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/run/editor_run_bar.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/separator.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/tree.h"
#include "scene/scene_string_names.h"
#include "servers/display/display_server.h"

enum BuildMenuOption {
	BUILD_MENU_BUILD,
	BUILD_MENU_REBUILD,
	BUILD_MENU_CLEAN,
};

// Map an absolute OS path reported by BeefBuild / the debugger back to a res:// script path, or "" if
// it doesn't fall under the project. BeefBuild reports backslashes + a lower-case drive letter, and
// ProjectSettings::localize_path() only strips the resource path on an exact prefix — so normalize the
// separators and match the project root case-insensitively before falling back to localize_path().
static String _beef_abs_to_res_path(const String &p_abs) {
	String f = p_abs.replace_char('\\', '/').simplify_path();
	String base = ProjectSettings::get_singleton()->get_resource_path();
	String res_path;
	if (!base.is_empty() && f.to_lower().begins_with(base.to_lower())) {
		res_path = "res://" + f.substr(base.length()).trim_prefix("/");
	} else {
		res_path = ProjectSettings::get_singleton()->localize_path(f);
	}
	return res_path.begins_with("res://") ? res_path : String();
}

// ─── UI construction ──────────────────────────────────────────────────────────

void BeefEditorPlugin::_build_ui() {
	tabs = memnew(TabContainer);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	// Actions docked into the tab bar, right-aligned (build menu + show-in-file-manager), matching
	// the C# panel.
	HBoxContainer *tab_actions = memnew(HBoxContainer);
	tab_actions->set_alignment(BoxContainer::ALIGNMENT_END);
	tabs->get_tab_bar()->add_child(tab_actions);
	tab_actions->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

	build_menu = memnew(MenuButton);
	build_menu->set_flat(true);
	build_menu->set_tooltip_text(TTR("Build"));
	{
		PopupMenu *pm = build_menu->get_popup();
		pm->add_item(TTR("Build Project"), BUILD_MENU_BUILD);
		pm->add_item(TTR("Rebuild Project"), BUILD_MENU_REBUILD);
		pm->add_item(TTR("Clean Project"), BUILD_MENU_CLEAN);
		pm->connect(SNAME("id_pressed"), callable_mp(this, &BeefEditorPlugin::_on_build_menu_id));
	}
	tab_actions->add_child(build_menu);

	logs_button = memnew(Button);
	logs_button->set_flat(true);
	logs_button->set_tooltip_text(TTR("Show the Beef workspace in the file manager."));
	logs_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_on_open_logs));
	tab_actions->add_child(logs_button);

	// ── Output tab ──
	HBoxContainer *output_tab = memnew(HBoxContainer);
	output_tab->set_name(TTR("Output"));
	tabs->add_child(output_tab);

	VBoxContainer *output_left = memnew(VBoxContainer);
	output_left->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
	output_left->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	output_left->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	output_tab->add_child(output_left);

	output_log = memnew(RichTextLabel);
	output_log->set_use_bbcode(true);
	output_log->set_scroll_follow(true);
	output_log->set_selection_enabled(true);
	output_log->set_context_menu_enabled(true);
	output_log->set_focus_mode(Control::FOCUS_CLICK);
	output_log->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	output_log->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	output_left->add_child(output_log);

	VBoxContainer *output_right = memnew(VBoxContainer);
	output_tab->add_child(output_right);

	output_clear_button = memnew(Button);
	output_clear_button->set_theme_type_variation("FlatButton");
	output_clear_button->set_focus_mode(Control::FOCUS_NONE);
	output_clear_button->set_tooltip_text(TTR("Clear Output"));
	output_clear_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_clear_output));
	output_right->add_child(output_clear_button);

	output_copy_button = memnew(Button);
	output_copy_button->set_theme_type_variation("FlatButton");
	output_copy_button->set_focus_mode(Control::FOCUS_NONE);
	output_copy_button->set_tooltip_text(TTR("Copy Selection"));
	output_copy_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_copy_output));
	output_right->add_child(output_copy_button);

	// ── Problems tab ──
	HBoxContainer *problems_tab = memnew(HBoxContainer);
	problems_tab->set_name(TTR("Problems"));
	tabs->add_child(problems_tab);

	VBoxContainer *problems_left = memnew(VBoxContainer);
	problems_left->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
	problems_left->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	problems_left->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	problems_tab->add_child(problems_left);

	problems_tree = memnew(Tree);
	problems_tree->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	problems_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	problems_tree->set_hide_root(true);
	problems_tree->set_columns(1);
	problems_tree->connect("item_activated", callable_mp(this, &BeefEditorPlugin::_on_problem_activated));
	problems_left->add_child(problems_tree);

	search_box = memnew(LineEdit);
	search_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	search_box->set_placeholder(TTR("Filter Problems"));
	search_box->set_clear_button_enabled(true);
	search_box->connect(SceneStringName(text_changed), callable_mp(this, &BeefEditorPlugin::_on_search_changed));
	problems_left->add_child(search_box);

	VBoxContainer *problems_right = memnew(VBoxContainer);
	problems_tab->add_child(problems_right);

	problems_clear_button = memnew(Button);
	problems_clear_button->set_theme_type_variation("FlatButton");
	problems_clear_button->set_focus_mode(Control::FOCUS_NONE);
	problems_clear_button->set_tooltip_text(TTR("Clear"));
	problems_clear_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_clear_problems));
	problems_right->add_child(problems_clear_button);

	problems_copy_button = memnew(Button);
	problems_copy_button->set_theme_type_variation("FlatButton");
	problems_copy_button->set_focus_mode(Control::FOCUS_NONE);
	problems_copy_button->set_tooltip_text(TTR("Copy Selection"));
	problems_copy_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_copy_problems));
	problems_right->add_child(problems_copy_button);

	problems_right->add_child(memnew(HSeparator));

	error_filter_button = memnew(Button);
	error_filter_button->set_toggle_mode(true);
	error_filter_button->set_pressed(true);
	error_filter_button->set_focus_mode(Control::FOCUS_NONE);
	error_filter_button->set_tooltip_text(TTR("Toggle visibility of errors."));
	error_filter_button->connect(SceneStringName(toggled), callable_mp(this, &BeefEditorPlugin::_on_filter_toggled));
	problems_right->add_child(error_filter_button);

	warning_filter_button = memnew(Button);
	warning_filter_button->set_toggle_mode(true);
	warning_filter_button->set_pressed(true);
	warning_filter_button->set_focus_mode(Control::FOCUS_NONE);
	warning_filter_button->set_tooltip_text(TTR("Toggle visibility of warnings."));
	warning_filter_button->connect(SceneStringName(toggled), callable_mp(this, &BeefEditorPlugin::_on_filter_toggled));
	problems_right->add_child(warning_filter_button);

	_build_debug_tab();
}

// ─── Debug tab ────────────────────────────────────────────────────────────────

void BeefEditorPlugin::_build_debug_tab() {
	// Layout mirrors Godot's Stack Trace tab: a control toolbar on top, then three panes below — the
	// call stack on the left, and a nested split with variables/watches and breakpoints on the right.
	VBoxContainer *debug_tab = memnew(VBoxContainer);
	debug_tab->set_name(TTR("Debug"));
	debug_tab->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
	tabs->add_child(debug_tab);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	debug_tab->add_child(toolbar);

	debug_launch_button = memnew(Button);
	debug_launch_button->set_theme_type_variation("FlatButton");
	debug_launch_button->set_focus_mode(Control::FOCUS_NONE);
	debug_launch_button->set_text(TTR("Debug"));
	debug_launch_button->set_tooltip_text(TTR("Build and launch the project under the Beef debugger (stops at breakpoints set in .bf scripts)."));
	debug_launch_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_on_debug_launch));
	toolbar->add_child(debug_launch_button);

	debug_stop_button = memnew(Button);
	debug_stop_button->set_theme_type_variation("FlatButton");
	debug_stop_button->set_focus_mode(Control::FOCUS_NONE);
	debug_stop_button->set_tooltip_text(TTR("Stop the debug session."));
	debug_stop_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_on_debug_stop));
	toolbar->add_child(debug_stop_button);

	toolbar->add_child(memnew(VSeparator));

	debug_continue_button = memnew(Button);
	debug_continue_button->set_theme_type_variation("FlatButton");
	debug_continue_button->set_focus_mode(Control::FOCUS_NONE);
	debug_continue_button->set_tooltip_text(TTR("Continue"));
	debug_continue_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_on_debug_continue));
	toolbar->add_child(debug_continue_button);

	debug_step_over_button = memnew(Button);
	debug_step_over_button->set_theme_type_variation("FlatButton");
	debug_step_over_button->set_focus_mode(Control::FOCUS_NONE);
	debug_step_over_button->set_tooltip_text(TTR("Step Over"));
	debug_step_over_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_on_debug_step_over));
	toolbar->add_child(debug_step_over_button);

	debug_step_into_button = memnew(Button);
	debug_step_into_button->set_theme_type_variation("FlatButton");
	debug_step_into_button->set_focus_mode(Control::FOCUS_NONE);
	debug_step_into_button->set_tooltip_text(TTR("Step Into"));
	debug_step_into_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_on_debug_step_into));
	toolbar->add_child(debug_step_into_button);

	debug_step_out_button = memnew(Button);
	debug_step_out_button->set_theme_type_variation("FlatButton");
	debug_step_out_button->set_focus_mode(Control::FOCUS_NONE);
	debug_step_out_button->set_tooltip_text(TTR("Step Out"));
	debug_step_out_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_on_debug_step_out));
	toolbar->add_child(debug_step_out_button);

	toolbar->add_child(memnew(VSeparator));

	debug_reason = memnew(RichTextLabel);
	debug_reason->set_use_bbcode(true);
	debug_reason->set_fit_content(true);
	debug_reason->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	debug_reason->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	debug_reason->set_scroll_active(false);
	toolbar->add_child(debug_reason);

	// Two panes: call stack on the left, variables/watches on the right (breakpoints pane follows later).
	HSplitContainer *panes = memnew(HSplitContainer);
	panes->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	panes->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	debug_tab->add_child(panes);

	callstack_tree = memnew(Tree);
	callstack_tree->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	callstack_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	callstack_tree->set_hide_root(true);
	callstack_tree->set_columns(2); // # | function - location - module
	callstack_tree->set_column_expand(0, false); // fixed-width frame index
	callstack_tree->set_column_custom_minimum_width(0, 32 * EDSCALE);
	callstack_tree->set_column_expand(1, true);
	// SELECT_MULTI replaces item_selected with cell_selected; navigate off that (uses get_selected()).
	callstack_tree->connect("cell_selected", callable_mp(this, &BeefEditorPlugin::_on_callstack_frame_selected));
	_setup_tree_copy(callstack_tree);
	panes->add_child(callstack_tree);

	// Inner split: variables (left) + breakpoints (right), nested so the tab shows three panes total.
	HSplitContainer *right_panes = memnew(HSplitContainer);
	right_panes->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_panes->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	panes->add_child(right_panes);

	VBoxContainer *vars_box = memnew(VBoxContainer);
	vars_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_panes->add_child(vars_box);

	variables_tree = memnew(Tree);
	variables_tree->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	variables_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	variables_tree->set_hide_root(true);
	variables_tree->set_columns(2);
	variables_tree->set_column_title(0, TTR("Name"));
	variables_tree->set_column_title(1, TTR("Value"));
	variables_tree->set_column_titles_visible(true);
	variables_tree->set_column_expand(0, true); // Name and Value share the width
	variables_tree->set_column_expand_ratio(0, 1);
	variables_tree->set_column_custom_minimum_width(0, 100 * EDSCALE);
	variables_tree->set_column_expand(1, true);
	variables_tree->set_column_expand_ratio(1, 2);
	variables_tree->connect("item_activated", callable_mp(this, &BeefEditorPlugin::_on_variables_item_activated));
	variables_tree->connect("item_collapsed", callable_mp(this, &BeefEditorPlugin::_on_variable_collapsed));
	_setup_tree_copy(variables_tree);
	vars_box->add_child(variables_tree);

	watch_input = memnew(LineEdit);
	watch_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	watch_input->set_placeholder(TTR("Add watch expression (Enter)..."));
	watch_input->set_clear_button_enabled(true);
	watch_input->connect("text_submitted", callable_mp(this, &BeefEditorPlugin::_on_add_watch));
	vars_box->add_child(watch_input);

	breakpoints_tree = memnew(Tree);
	breakpoints_tree->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	breakpoints_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	breakpoints_tree->set_hide_root(true);
	breakpoints_tree->set_columns(3);
	breakpoints_tree->set_column_title(0, TTR("Breakpoint"));
	breakpoints_tree->set_column_title(1, TTR("Condition"));
	breakpoints_tree->set_column_title(2, TTR("Hits"));
	breakpoints_tree->set_column_titles_visible(true);
	// Breakpoint + Condition share the width; the hit count stays fixed/narrow.
	breakpoints_tree->set_column_expand(0, true);
	breakpoints_tree->set_column_expand_ratio(0, 2);
	breakpoints_tree->set_column_expand(1, true);
	breakpoints_tree->set_column_expand_ratio(1, 2);
	breakpoints_tree->set_column_expand(2, false);
	breakpoints_tree->set_column_custom_minimum_width(2, 48 * EDSCALE);
	breakpoints_tree->connect("item_activated", callable_mp(this, &BeefEditorPlugin::_on_breakpoint_activated));
	breakpoints_tree->connect("item_edited", callable_mp(this, &BeefEditorPlugin::_on_breakpoint_edited));
	_setup_tree_copy(breakpoints_tree);
	right_panes->add_child(breakpoints_tree);

	// Shared right-click Copy / Copy All menu for the debug trees.
	tree_copy_menu = memnew(PopupMenu);
	tree_copy_menu->add_item(TTR("Copy"), 0);
	tree_copy_menu->add_item(TTR("Copy All"), 1);
	tree_copy_menu->connect("id_pressed", callable_mp(this, &BeefEditorPlugin::_on_tree_copy_menu));
	debug_tab->add_child(tree_copy_menu);

	// Keep the breakpoints pane in sync with the .bf script-editor gutter.
	if (EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton()) {
		edn->connect("breakpoint_toggled", callable_mp(this, &BeefEditorPlugin::_on_editor_breakpoint_toggled));
	}

	_set_debug_controls_enabled(false);
}

void BeefEditorPlugin::_update_theme() {
	if (!output_log) {
		return;
	}

	// Monospace output, like the editor's Output panel and the C# build log.
	Ref<Font> mono = tabs->get_theme_font(SNAME("output_source_mono"), SNAME("EditorFonts"));
	if (mono.is_valid()) {
		output_log->add_theme_font_override(SNAME("normal_font"), mono);
		output_log->add_theme_font_override(SNAME("mono_font"), mono);
	}
	int font_size = tabs->get_theme_font_size(SNAME("output_source_size"), SNAME("EditorFonts"));
	if (font_size > 0) {
		output_log->add_theme_font_size_override(SNAME("normal_font_size"), font_size);
		output_log->add_theme_font_size_override(SNAME("mono_font_size"), font_size);
	}

	build_menu->set_button_icon(tabs->get_theme_icon(SNAME("BuildBeef"), SNAME("EditorIcons")));
	logs_button->set_button_icon(tabs->get_theme_icon(SNAME("Filesystem"), SNAME("EditorIcons")));
	output_clear_button->set_button_icon(tabs->get_theme_icon(SNAME("Clear"), SNAME("EditorIcons")));
	output_copy_button->set_button_icon(tabs->get_theme_icon(SNAME("ActionCopy"), SNAME("EditorIcons")));
	problems_clear_button->set_button_icon(tabs->get_theme_icon(SNAME("Clear"), SNAME("EditorIcons")));
	problems_copy_button->set_button_icon(tabs->get_theme_icon(SNAME("ActionCopy"), SNAME("EditorIcons")));
	error_filter_button->set_button_icon(tabs->get_theme_icon(SNAME("StatusError"), SNAME("EditorIcons")));
	warning_filter_button->set_button_icon(tabs->get_theme_icon(SNAME("StatusWarning"), SNAME("EditorIcons")));
	search_box->set_right_icon(tabs->get_theme_icon(SNAME("Search"), SNAME("EditorIcons")));

	if (debug_launch_button) {
		debug_launch_button->set_button_icon(tabs->get_theme_icon(SNAME("Play"), SNAME("EditorIcons")));
		debug_stop_button->set_button_icon(tabs->get_theme_icon(SNAME("Stop"), SNAME("EditorIcons")));
		debug_continue_button->set_button_icon(tabs->get_theme_icon(SNAME("DebugContinue"), SNAME("EditorIcons")));
		debug_step_over_button->set_button_icon(tabs->get_theme_icon(SNAME("DebugNext"), SNAME("EditorIcons")));
		debug_step_into_button->set_button_icon(tabs->get_theme_icon(SNAME("DebugStep"), SNAME("EditorIcons")));
		debug_step_out_button->set_button_icon(tabs->get_theme_icon(SNAME("DebugStepOut"), SNAME("EditorIcons")));
	}

	if (run_bar_build_button) {
		run_bar_build_button->set_button_icon(tabs->get_theme_icon(SNAME("BuildBeef"), SNAME("EditorIcons")));
	}
}

// ─── Build ──────────────────────────────────────────────────────────────────

bool BeefEditorPlugin::_do_build(bool p_rebuild, bool p_reveal_on_error) {
	BeefLanguage *lang = BeefLanguage::get_singleton();
	if (!lang) {
		if (output_log) {
			output_log->add_text(TTR("Beef language not available.") + "\n");
		}
		return false;
	}

	String output;
	Vector<String> errors;
	bool ok;
	{
		// Show the editor's modal build progress while compiling (the build is blocking), mirroring
		// what the C# module does for its build.
		const String label = p_rebuild ? TTR("Rebuilding Beef project...") : TTR("Building Beef project...");
		EditorProgress progress("beef_build", label, 1);
		progress.step(label, 0);
		ok = lang->build_project(p_rebuild, output, errors);
	}

	if (output_log) {
		output_log->clear();
		output_log->add_text(_clean_build_output(output));
	}

	_parse_diagnostics(output);
	_populate_problems();
	_set_status_icon();

	if (!ok && p_reveal_on_error) {
		make_bottom_panel_item_visible(tabs);
		// Errors are more useful than the raw log when a build fails.
		if (_error_count > 0) {
			tabs->set_current_tab(1); // Problems
		}
	}
	return ok;
}

void BeefEditorPlugin::_on_build_pressed() {
	_do_build(false, true);
}

void BeefEditorPlugin::_on_rebuild_pressed() {
	_do_build(true, true);
}

void BeefEditorPlugin::_on_clean_pressed() {
	if (BeefLanguage *lang = BeefLanguage::get_singleton()) {
		lang->clean_project();
		if (output_log) {
			output_log->clear();
		}
		_diagnostics.clear();
		_error_count = 0;
		_warning_count = 0;
		_populate_problems();
		_set_status_icon();
	}
}

void BeefEditorPlugin::_on_build_menu_id(int p_id) {
	switch (p_id) {
		case BUILD_MENU_BUILD:
			_on_build_pressed();
			break;
		case BUILD_MENU_REBUILD:
			_on_rebuild_pressed();
			break;
		case BUILD_MENU_CLEAN:
			_on_clean_pressed();
			break;
	}
}

void BeefEditorPlugin::_on_open_logs() {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	String workspace = ps->globalize_path(String(ps->get_setting("beef/project/workspace_dir")));
	if (!workspace.is_empty()) {
		OS::get_singleton()->shell_show_in_file_manager(workspace, true);
	}
}

bool BeefEditorPlugin::build() {
	// Build before launching the game; a failed build aborts the run (returning false).
	return _do_build(false, true);
}

// ─── Diagnostics ──────────────────────────────────────────────────────────────

bool BeefEditorPlugin::_parse_diagnostic_line(const String &p_line, Diagnostic &r_diag) {
	String t = p_line.strip_edges();
	if (t.is_empty() || t.begins_with("ERROR-SOFT")) {
		return false; // ERROR-SOFT is BeefBuild's "compile failed" summary, not a diagnostic.
	}

	String body;
	if (t.begins_with("ERROR:")) {
		r_diag.is_error = true;
		body = t.substr(6).strip_edges();
	} else if (t.begins_with("WARNING")) {
		r_diag.is_error = false;
		int colon = t.find(":"); // after the "WARNING(n)" prefix
		body = (colon >= 0) ? t.substr(colon + 1).strip_edges() : t;
		// Strip an optional "BFxxxx:" diagnostic code.
		if (body.begins_with("BF")) {
			int code_colon = body.find(":");
			if (code_colon > 0 && body.substr(2, code_colon - 2).is_valid_int()) {
				body = body.substr(code_colon + 1).strip_edges();
			}
		}
	} else {
		return false;
	}

	// Location suffix: "... [at] line <L>:<C> in <FILE>" (BeefBuild uses both "line" and "Line").
	r_diag.message = body;
	int in_idx = body.rfind(" in ");
	if (in_idx >= 0) {
		r_diag.file = body.substr(in_idx + 4).strip_edges();
		String before = body.substr(0, in_idx);
		int line_idx = before.to_lower().rfind("line ");
		if (line_idx >= 0) {
			String loc = before.substr(line_idx + 5).strip_edges(); // "96:35"
			Vector<String> lc = loc.split(":");
			if (lc.size() >= 2) {
				r_diag.line = lc[0].to_int();
				r_diag.column = lc[1].to_int();
			}
			String msg = before.substr(0, line_idx).strip_edges();
			if (msg.ends_with("at")) {
				msg = msg.substr(0, msg.length() - 2).strip_edges();
			}
			r_diag.message = msg;
		}
	}
	return true;
}

String BeefEditorPlugin::_clean_build_output(const String &p_output) {
	String result;
	PackedStringArray lines = p_output.replace("\r\n", "\n").split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i];
		// BeefBuild redraws its progress bar in place with carriage returns; keep only the final state.
		int cr = line.rfind("\r");
		if (cr >= 0) {
			line = line.substr(cr + 1);
		}
		String stripped = line.strip_edges();
		if (stripped.is_empty()) {
			continue;
		}
		// Drop the progress bar itself. BeefBuild draws it as "[   ]" then backspaces (0x08) over the
		// frame and overwrites it with '*'s, all on one captured line — so the line is made up only of
		// the frame/fill chars plus backspaces. Treat such a line as the bar and skip it.
		bool only_bar = true;
		for (int c = 0; c < stripped.length(); c++) {
			char32_t ch = stripped[c];
			if (ch != '[' && ch != ']' && ch != '*' && ch != ' ' && ch != '.' && ch != '\b') {
				only_bar = false;
				break;
			}
		}
		if (only_bar && (stripped.find("*") >= 0 || stripped.find("[") >= 0)) {
			continue;
		}
		result += line + "\n";
	}
	return result;
}

bool BeefEditorPlugin::test_parse_line(const String &p_line, bool &r_is_error, String &r_file, int &r_line, int &r_col, String &r_message) {
	Diagnostic d;
	if (!_parse_diagnostic_line(p_line, d)) {
		return false;
	}
	r_is_error = d.is_error;
	r_file = d.file;
	r_line = d.line;
	r_col = d.column;
	r_message = d.message;
	return true;
}

void BeefEditorPlugin::_parse_diagnostics(const String &p_output) {
	_diagnostics.clear();
	_error_count = 0;
	_warning_count = 0;

	PackedStringArray lines = p_output.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		Diagnostic d;
		if (_parse_diagnostic_line(lines[i], d)) {
			_diagnostics.push_back(d);
			if (d.is_error) {
				_error_count++;
			} else {
				_warning_count++;
			}
		}
	}
}

bool BeefEditorPlugin::_should_show(const Diagnostic &p_diag) const {
	if (p_diag.is_error && error_filter_button && !error_filter_button->is_pressed()) {
		return false;
	}
	if (!p_diag.is_error && warning_filter_button && !warning_filter_button->is_pressed()) {
		return false;
	}
	String needle = search_box ? search_box->get_text() : String();
	if (!needle.is_empty()) {
		if (p_diag.message.findn(needle) < 0 && p_diag.file.findn(needle) < 0) {
			return false;
		}
	}
	return true;
}

void BeefEditorPlugin::_populate_problems() {
	if (!problems_tree) {
		return;
	}
	problems_tree->clear();

	// Filter button labels carry their totals.
	if (error_filter_button) {
		error_filter_button->set_text(itos(_error_count));
	}
	if (warning_filter_button) {
		warning_filter_button->set_text(itos(_warning_count));
	}

	// Tab title carries the visible problem count.
	if (tabs) {
		int visible = 0;
		for (const Diagnostic &d : _diagnostics) {
			if (_should_show(d)) {
				visible++;
			}
		}
		int problems_idx = 1;
		String title = TTR("Problems");
		if (visible > 0) {
			title += vformat(" (%d)", visible);
		}
		tabs->set_tab_title(problems_idx, title);
	}

	Ref<Texture2D> err_icon = tabs->get_theme_icon(SNAME("StatusError"), SNAME("EditorIcons"));
	Ref<Texture2D> warn_icon = tabs->get_theme_icon(SNAME("StatusWarning"), SNAME("EditorIcons"));
	Color err_color = tabs->get_theme_color(SNAME("error_color"), SNAME("Editor"));
	Color warn_color = tabs->get_theme_color(SNAME("warning_color"), SNAME("Editor"));

	TreeItem *root = problems_tree->create_item();
	HashMap<String, TreeItem *> file_groups;

	for (int i = 0; i < _diagnostics.size(); i++) {
		const Diagnostic &d = _diagnostics[i];
		if (!_should_show(d)) {
			continue;
		}

		// Group by file (basename for display, full path in tooltip).
		String key = d.file.is_empty() ? TTR("General") : d.file;
		TreeItem *group = nullptr;
		if (HashMap<String, TreeItem *>::Iterator it = file_groups.find(key); it) {
			group = it->value;
		} else {
			group = problems_tree->create_item(root);
			group->set_text(0, d.file.is_empty() ? key : d.file.get_file());
			group->set_tooltip_text(0, d.file);
			group->set_selectable(0, false);
			file_groups.insert(key, group);
		}

		TreeItem *item = problems_tree->create_item(group);
		String text = d.message;
		if (d.line > 0) {
			text += vformat(" (%d,%d)", d.line, d.column);
		}
		item->set_text(0, text);
		item->set_icon(0, d.is_error ? err_icon : warn_icon);
		item->set_custom_color(0, d.is_error ? err_color : warn_color);
		item->set_tooltip_text(0, d.message);
		item->set_metadata(0, i);
	}
}

void BeefEditorPlugin::_on_problem_activated() {
	if (!problems_tree) {
		return;
	}
	TreeItem *sel = problems_tree->get_selected();
	if (!sel) {
		return;
	}
	Variant meta = sel->get_metadata(0);
	if (meta.get_type() != Variant::INT) {
		return; // a file-group row
	}
	int idx = meta;
	if (idx < 0 || idx >= _diagnostics.size()) {
		return;
	}
	const Diagnostic &d = _diagnostics[idx];
	if (d.file.is_empty()) {
		return;
	}
	// Beef also reports compiler-synthetic locations that are not real files — e.g. a warning inside
	// comptime-emitted code is located at "$Emit$<project>:<Type>". Only jump for actual .bf sources.
	if (d.file.contains("$") || !d.file.to_lower().ends_with(".bf")) {
		return;
	}

	String res_path = _beef_abs_to_res_path(d.file);
	if (res_path.is_empty() || !ResourceLoader::exists(res_path)) {
		return;
	}
	Ref<Script> script = ResourceLoader::load(res_path);
	if (script.is_valid()) {
		// edit_script() expects 1-based line/column (it subtracts 1 internally), and BeefBuild
		// diagnostics are already 1-based — pass them through unchanged.
		EditorInterface::get_singleton()->edit_script(script, d.line, d.column);
	}
}

void BeefEditorPlugin::_on_filter_toggled(bool p_pressed) {
	_populate_problems();
}

void BeefEditorPlugin::_on_search_changed(const String &p_text) {
	_populate_problems();
}

// ─── Output / problems actions ─────────────────────────────────────────────────

void BeefEditorPlugin::_clear_output() {
	if (output_log) {
		output_log->clear();
	}
}

void BeefEditorPlugin::_copy_output() {
	if (!output_log) {
		return;
	}
	String text = output_log->get_selected_text();
	if (text.is_empty()) {
		text = output_log->get_parsed_text();
	}
	if (!text.is_empty()) {
		DisplayServer::get_singleton()->clipboard_set(text);
	}
}

void BeefEditorPlugin::_clear_problems() {
	_diagnostics.clear();
	_error_count = 0;
	_warning_count = 0;
	_populate_problems();
	_set_status_icon();
}

void BeefEditorPlugin::_copy_problems() {
	if (!problems_tree) {
		return;
	}
	String text;
	for (const Diagnostic &d : _diagnostics) {
		if (!_should_show(d)) {
			continue;
		}
		text += (d.is_error ? "error: " : "warning: ") + d.message;
		if (!d.file.is_empty()) {
			text += vformat(" %s(%d,%d)", d.file, d.line, d.column);
		}
		text += "\n";
	}
	if (!text.is_empty()) {
		DisplayServer::get_singleton()->clipboard_set(text);
	}
}

// ─── Debug session ──────────────────────────────────────────────────────────

void BeefEditorPlugin::_compiler_log(const String &p_msg) {
	// Sink for beef_log() — the "BeefCompiler: …" / "BeefIDEHelper: …" diagnostics. Goes to the Beef
	// panel's Output tab (not the general editor Output dock). beef_log passes messages without a trailing
	// newline (print_line added it), so add one here.
	if (output_log) {
		output_log->add_text(p_msg + "\n");
	}
}

void BeefEditorPlugin::_dbg_log(const String &p_msg) {
	if (output_log) {
		output_log->add_text(p_msg);
	}
	// Also mirror to <project>/beef_debug.log so the session/diagnostic trace is capturable even when the
	// editor is launched detached (e.g. under cdb via test_project.bat) where the GUI Output panel isn't
	// easily copied.
	String log_path = ProjectSettings::get_singleton()->globalize_path("res://beef_debug.log");
	Ref<FileAccess> f = FileAccess::open(log_path, FileAccess::READ_WRITE);
	if (f.is_null()) {
		f = FileAccess::open(log_path, FileAccess::WRITE);
	}
	if (f.is_valid()) {
		f->seek_end();
		f->store_string(p_msg);
	}
}

void BeefEditorPlugin::_set_debug_controls_enabled(bool p_stopped) {
	// p_stopped: the debuggee is paused at a breakpoint/stop (stepping + continue make sense).
	if (debug_continue_button) {
		debug_continue_button->set_disabled(!p_stopped);
	}
	if (debug_step_over_button) {
		debug_step_over_button->set_disabled(!p_stopped);
	}
	if (debug_step_into_button) {
		debug_step_into_button->set_disabled(!p_stopped);
	}
	if (debug_step_out_button) {
		debug_step_out_button->set_disabled(!p_stopped);
	}
	if (debug_stop_button) {
		debug_stop_button->set_disabled(!_debug_active);
	}
	if (debug_launch_button) {
		debug_launch_button->set_disabled(_debug_active);
	}
}

void BeefEditorPlugin::_on_debug_launch() {
	if (_debug_active) {
		return;
	}
	// Start a fresh diagnostic log for this session (mirrored by _dbg_log).
	{
		Ref<FileAccess> f = FileAccess::open(ProjectSettings::get_singleton()->globalize_path("res://beef_debug.log"), FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string("=== Beef debug session ===\n");
		}
	}
	if (!BeefIDEHelper::get_singleton()->is_available()) {
		if (debug_reason) {
			debug_reason->set_text(TTR("IDEHelper is not available; cannot debug."));
		}
		return;
	}

	// Hook .bf saves for live hot reload (connected lazily on first launch — EditorNode is fully up by
	// now, unlike at plugin-construction time).
	if (EditorNode *en = EditorNode::get_singleton()) {
		Callable cb = callable_mp(this, &BeefEditorPlugin::_on_resource_saved);
		if (!en->is_connected("resource_saved", cb)) {
			en->connect("resource_saved", cb);
		}
	}

	// Build the scripts DLL the launched game will load. For hot reload, the loaded module MUST be the
	// exact compilation the deltas come from (generated symbols like __bfStrData<N> are compilation-local
	// and Debugger_HotLoad resolves delta refs by name against the loaded module's PDB). So build it
	// IN-PROCESS via IDEHelper — that one compiler instance is also the hot baseline. Build into a
	// dedicated CLEAN dir (NOT BeefBuild's Debug_Win64, whose stale .pdb/__.lib give the debugger
	// mismatched debug info -> "failed to resolve symbols"), then COPY (not move) the DLL to get_dll_path
	// so its embedded PDB path still points into the clean dir. If the in-process link fails (no MSVC/SDK),
	// fall back to BeefBuild and disable hot reload.
	String cfg;
	bool ext = false;
	if (BeefLanguage *lang = BeefLanguage::get_singleton()) {
		lang->_resolve_build_settings(_hot_workspace, _hot_project, cfg, ext);
	}
	bool hot_built = false;
	String hot_target_dll; // the scripts DLL the hot-swap debugger resolves deltas against (empty => none)
	if (!_hot_workspace.is_empty() && !ext) {
		_hot_build_dir = _hot_workspace.path_join("build").path_join("HotBaseline").path_join(_hot_project);
		String baseline_dll = _hot_build_dir.path_join(_hot_project + ".dll");
		String load_dll = BeefCompiler::get_dll_path(_hot_workspace, _hot_project, cfg);
		// The baseline MUST be a clean full build: reusing stale objects from a previous session's
		// HotBaseline yields incremental-build inconsistencies (referenced-but-undefined generic
		// instantiations like Pointer<void>::ToString / Span<char>::__BfCtor) that fail the link.
		if (Ref<DirAccess> clean = DirAccess::open(_hot_build_dir); clean.is_valid()) {
			clean->erase_contents_recursive();
		}
		Vector<String> berr;
		if (BeefIDEHelper::get_singleton()->build_dll(_hot_workspace, _hot_project, baseline_dll, /*hot_swap=*/true, berr)) {
			// Put the freshly built baseline DLL where the game loads it (copy, not move — the embedded
			// PDB path must keep pointing into the clean build dir for the debugger to resolve symbols).
			Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
			if (da.is_valid() && da->copy(baseline_dll, load_dll) == OK) {
				// The in-process DLL links against the bundle's Beef042RT64.dll; stage that exact (version-
				// matched) RT next to the loaded DLL so the game doesn't pick up a stale one already there.
				if (BeefCompiler *bc = BeefCompiler::get_singleton()) {
					String rt = bc->get_beef_build_path().get_base_dir().path_join("Beef042RT64.dll");
					if (FileAccess::exists(rt)) {
						da->copy(rt, load_dll.get_base_dir().path_join("Beef042RT64.dll"));
					}
				}
				_hot_baseline_ready = true;
				hot_built = true;
				hot_target_dll = load_dll; // the module the hot-swap debugger must treat as the target binary
				_dbg_log("[beef-dbg] hot reload armed — save a .bf to patch the live game\n");
			} else {
				_dbg_log("[beef-dbg] in-process build OK but could not stage the loaded DLL (hot reload disabled)\n");
			}
		} else {
			_dbg_log("[beef-dbg] in-process build failed; falling back to BeefBuild (hot reload disabled this session):\n");
			for (const String &e : berr) {
				_dbg_log("[beef-dbg]   " + e + "\n");
			}
		}
	}
	if (!hot_built) {
		// Fallback: BeefBuild produces the loaded DLL, but its compilation differs from any in-process
		// baseline, so hot reload is not available this session (a failed build aborts the launch).
		if (!_do_build(false, true)) {
			return;
		}
		_hot_baseline_ready = false;
	}

	// Run the project as a game (not the editor) by passing the main scene to this same binary.
	String exe = OS::get_singleton()->get_executable_path();
	String proj_root = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
	String main_scene = ProjectSettings::get_singleton()->get_setting("application/run/main_scene");
	String args = "--path \"" + proj_root + "\"";
	if (!main_scene.is_empty()) {
		args += " " + main_scene;
	}
	// Connect the game to the editor's remote debugger (independent of the IDEHelper native hot-swap
	// debugger) so its print/Native.Print output and runtime errors are forwarded to the editor's
	// Output/Debugger dock — same as a normal "Run Project". Start the server first so a port is assigned.
	if (EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton()) {
		String uri = edn->get_server_uri();
		edn->start(uri.is_empty() ? "tcp://" : uri);
		uri = edn->get_server_uri();
		if (!uri.is_empty()) {
			args += " --remote-debug " + uri;
			args += " --editor-pid " + itos(OS::get_singleton()->get_process_id());
		}
	}

	BeefIDEHelper *dbg = BeefIDEHelper::get_singleton();
	// Launch hot-swappable: the runtime reserves a hot-swap heap so editing a .bf method body during the
	// session can be patched into the live game via Debugger_HotLoad (in-editor hot reload).
	if (!dbg->debug_start(exe, args, proj_root, /*hot_swap=*/true, hot_target_dll)) {
		if (debug_reason) {
			debug_reason->set_text(TTR("Failed to start the debug session."));
		}
		return;
	}

	// Apply the breakpoints set in the .bf script editors (shared editor breakpoint store). Paths are
	// res:// in the store; the debugger wants absolute .bf paths.
	_bp_handles.clear();
	int armed = 0;
	if (EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton()) {
		HashMap<String, Vector<int>> bps = edn->get_breakpoints_by_file();
		for (const KeyValue<String, Vector<int>> &E : bps) {
			if (!E.key.to_lower().ends_with(".bf")) {
				continue;
			}
			String abs = ProjectSettings::get_singleton()->globalize_path(E.key);
			for (int line : E.value) {
				String key = abs + ":" + itos(line);
				String cond;
				if (HashMap<String, String>::Iterator c = _bp_conditions.find(key); c) {
					cond = c->value;
				}
				void *handle = dbg->debug_add_breakpoint(abs, line, cond);
				if (handle) {
					_bp_handles[key] = handle;
					armed++;
				}
			}
		}
	}
	_dbg_log(vformat("[beef-dbg] armed %d breakpoint(s)\n", armed));
	if (armed == 0) {
		_dbg_log("[beef-dbg] (no .bf breakpoints set - the game will run without stopping)\n");
	}

	dbg->debug_begin_run();
	_debug_active = true;
	_debug_pid = 0;
	_debug_last_state = BeefIDEHelper::RS_RUNNING;
	_debug_skip_loader_pause = true; // the first pause is the OS loader breakpoint, not a user stop
	set_process(true);
	if (callstack_tree) {
		callstack_tree->clear();
	}
	if (debug_reason) {
		debug_reason->set_text(TTR("Running..."));
	}
	_set_debug_controls_enabled(false);
	_refresh_breakpoints();

	make_bottom_panel_item_visible(tabs);
	if (tabs) {
		tabs->set_current_tab(2); // Debug
	}
}

void BeefEditorPlugin::_on_resource_saved(Ref<Resource> p_res) {
	// A .bf saved during an active hot-swap session -> hot-compile a delta and patch the live game.
	if (!_debug_active || !_hot_baseline_ready || p_res.is_null()) {
		return;
	}
	// Honor the per-machine auto-hot-patch toggle (beef/editor/hot_reload).
	if (EditorSettings::get_singleton() && !(bool)EDITOR_GET("beef/editor/hot_reload")) {
		return;
	}
	String path = p_res->get_path();
	if (!path.to_lower().ends_with(".bf")) {
		return;
	}
	// Hot-reload only the saved file (minimal delta). Match the build parser map's normalized key form
	// (globalized, lowercase, forward slashes).
	Vector<String> changed;
	changed.push_back(ProjectSettings::get_singleton()->globalize_path(path).replace_char('\\', '/').to_lower());
	Vector<String> errs;
	if (BeefIDEHelper::get_singleton()->hot_reload(_hot_workspace, _hot_project, _hot_build_dir, changed, errs)) {
		_dbg_log("[beef-dbg] hot reloaded " + path.get_file() + " into the running game\n");
		// The edit compiled — clear any stale errors from a previous failed hot compile / build.
		_diagnostics.clear();
		_error_count = 0;
		_populate_problems();
		_set_status_icon();
		if (debug_reason) {
			debug_reason->set_text(TTR("Hot reloaded ") + path.get_file());
		}
	} else {
		// A mistake in the edited file is a real compilation error — report it like a normal build:
		// parse the diagnostics into the Problems tab (navigable file:line) and reveal it. Don't also dump
		// each error into the debug log — that's the Problems tab's job; just note the failure once.
		_parse_diagnostics(String("\n").join(errs));
		_populate_problems();
		_set_status_icon();
		_dbg_log(vformat("[beef-dbg] hot reload failed (%d error(s)) — see Problems\n", _error_count));
		if (tabs) {
			make_bottom_panel_item_visible(tabs);
			if (_error_count > 0) {
				tabs->set_current_tab(1); // Problems
			}
		}
		if (debug_reason) {
			debug_reason->set_text(TTR("Hot reload failed — see Problems"));
		}
	}
}

void BeefEditorPlugin::_debug_poll() {
	if (!_debug_active) {
		return;
	}
	// Capture the debuggee's OS pid once it exists, so Stop can guarantee a kill even if the debugger
	// can't resume a breakpoint-suspended thread to terminate cleanly.
	if (_debug_pid == 0) {
		_debug_pid = BeefIDEHelper::get_singleton()->debug_process_id();
	}
	Vector<String> messages;
	int state = BeefIDEHelper::get_singleton()->debug_poll(messages);
	for (int i = 0; i < messages.size(); i++) {
		const String &m = messages[i];
		// Drop the debugger's internal/OS chatter (module + thread churn, the "msg ..." stream of dxgi/OBS/
		// vulkan/first-chance-exception/thread-exit noise). Genuine breaks surface as RunState changes below;
		// real failures (e.g. "error Hot swapping failed ...") don't start with these prefixes, so they pass.
		if (m.begins_with("msg ") || m == "modulesChanged" || m.begins_with("symsrv ") ||
				m.begins_with("dbgInfoLoaded")) {
			continue;
		}
		_dbg_log("[beef-dbg] " + m + "\n");
	}

	const bool was_stopped = (_debug_last_state == BeefIDEHelper::RS_PAUSED ||
			_debug_last_state == BeefIDEHelper::RS_BREAKPOINT || _debug_last_state == BeefIDEHelper::RS_EXCEPTION);
	const bool is_stopped = (state == BeefIDEHelper::RS_PAUSED ||
			state == BeefIDEHelper::RS_BREAKPOINT || state == BeefIDEHelper::RS_EXCEPTION);

	if (state == BeefIDEHelper::RS_TERMINATED) {
		// The game exited on its own (closed window / quit).
		_end_debug_session();
		return;
	}
	if (is_stopped && !was_stopped) {
		// The first stop after launch is the OS's initial loader breakpoint (paused in ntdll with no
		// active breakpoint, before any user code runs). Continue past it silently instead of surfacing
		// it as a user stop. A real breakpoint hit first (RS_BREAKPOINT) still surfaces normally.
		if (_debug_skip_loader_pause) {
			_debug_skip_loader_pause = false;
			if (state == BeefIDEHelper::RS_PAUSED && BeefIDEHelper::get_singleton()->debug_active_breakpoint_line() == 0) {
				BeefIDEHelper::get_singleton()->debug_continue();
				_debug_last_state = BeefIDEHelper::RS_RUNNING;
				return;
			}
		}
		_debug_on_stopped();
	}
	_debug_last_state = state;
}

void BeefEditorPlugin::_debug_on_stopped() {
	BeefIDEHelper *dbg = BeefIDEHelper::get_singleton();
	_clear_execution_line(); // drop the previous marker; the top-frame select below sets the new one
	int bp_line = dbg->debug_active_breakpoint_line();
	if (debug_reason) {
		if (dbg->debug_run_state() == BeefIDEHelper::RS_EXCEPTION) {
			debug_reason->set_text(TTR("Stopped on exception"));
		} else if (bp_line > 0) {
			debug_reason->set_text(vformat(TTR("Paused at breakpoint (line %d)"), bp_line));
		} else {
			debug_reason->set_text(TTR("Paused"));
		}
	}
	_refresh_call_stack();
	_refresh_breakpoints(); // update hit counts
	_set_debug_controls_enabled(true);
	make_bottom_panel_item_visible(tabs);
	if (tabs) {
		tabs->set_current_tab(2);
	}
	// Select the top frame and navigate to it explicitly (don't depend on select() emitting a signal).
	if (callstack_tree) {
		TreeItem *root = callstack_tree->get_root();
		if (root && root->get_first_child()) {
			TreeItem *top = root->get_first_child();
			top->select(0);
			_navigate_to_frame(top);
		}
	}
}

void BeefEditorPlugin::_refresh_call_stack() {
	if (!callstack_tree) {
		return;
	}
	callstack_tree->clear();
	TreeItem *root = callstack_tree->create_item();
	Vector<BeefIDEHelper::DebugFrame> frames = BeefIDEHelper::get_singleton()->debug_call_stack();
	for (int i = 0; i < frames.size(); i++) {
		const BeefIDEHelper::DebugFrame &f = frames[i];
		TreeItem *item = callstack_tree->create_item(root);

		// The frame description is "module!function"; split on the first '!'.
		String module, func;
		int bang = f.description.find("!");
		if (bang >= 0) {
			module = f.description.substr(0, bang);
			func = f.description.substr(bang + 1);
		} else {
			func = f.description;
		}
		String loc = f.file.is_empty() ? String() : vformat("%s:%d", f.file.get_file(), f.line);

		// "function - location - module" (skip empty parts).
		String combined = func;
		if (!loc.is_empty()) {
			combined += (combined.is_empty() ? "" : " - ") + loc;
		}
		if (!module.is_empty()) {
			combined += (combined.is_empty() ? "" : " - ") + module;
		}

		item->set_text(0, itos(i));
		item->set_text(1, combined);
		item->set_tooltip_text(1, f.file.is_empty() ? f.description : vformat("%s\n%s:%d", f.description, f.file, f.line));

		Dictionary meta;
		meta["file"] = f.file;
		meta["line"] = f.line;
		meta["frame"] = i;
		item->set_metadata(0, meta);
	}
}

void BeefEditorPlugin::_refresh_variables() {
	if (!variables_tree) {
		return;
	}
	variables_tree->clear();
	BeefIDEHelper *dbg = BeefIDEHelper::get_singleton();
	TreeItem *root = variables_tree->create_item();

	// Locals are only meaningful while paused at a stop; the watch list is always shown so it can be
	// built up and edited before a session starts (values fill in once the debuggee is paused).
	if (_debug_active) {
		TreeItem *locals_group = variables_tree->create_item(root);
		locals_group->set_text(0, TTR("Locals"));
		locals_group->set_selectable(0, false);
		locals_group->set_selectable(1, false);
		Vector<String> locals = dbg->debug_locals(_selected_frame);
		for (int i = 0; i < locals.size(); i++) {
			// "$this" is the receiver. Display it as "this", but keep "$this" as the evaluation expression:
			// member expansion builds "<expr>.field", and the bare "this" base is not a valid debugger
			// expression (evaluating "this.field" crashed the evaluator), whereas "$this.field" is.
			String name = locals[i];
			String value = dbg->debug_evaluate(name, _selected_frame);
			String display = (name == "$this") ? String("this") : name;
			_add_variable_item(locals_group, display, name, value, -1);
		}
	}

	if (!_watches.is_empty()) {
		TreeItem *watch_group = variables_tree->create_item(root);
		watch_group->set_text(0, TTR("Watches"));
		watch_group->set_selectable(0, false);
		watch_group->set_selectable(1, false);
		for (int i = 0; i < _watches.size(); i++) {
			String value = _debug_active ? dbg->debug_evaluate(_watches[i], _selected_frame) : String(TTR("(not running)"));
			_add_variable_item(watch_group, _watches[i], _watches[i], value, i);
		}
	}
}

// Returns true if an evaluated value looks like an object/struct with members (has a "{ ... }" summary).
static bool _beef_value_expandable(const String &p_value) {
	return p_value.find("{") >= 0 && p_value.find("}") >= 0;
}

// Split the top-level members of a "...{ a=1 b=(2, 3) c={ ... } }" summary into "name=value" tokens,
// respecting nesting (braces/parens/brackets) and quotes so values with spaces stay intact.
static Vector<String> _beef_split_members(const String &p_value) {
	Vector<String> out;
	int lb = p_value.find("{");
	int rb = p_value.rfind("}");
	if (lb < 0 || rb <= lb) {
		return out;
	}
	String inner = p_value.substr(lb + 1, rb - lb - 1);
	int depth = 0;
	bool in_quote = false;
	char32_t quote = 0;
	int start = 0;
	for (int i = 0; i < inner.length(); i++) {
		char32_t c = inner[i];
		if (in_quote) {
			if (c == quote) {
				in_quote = false;
			}
			continue;
		}
		if (c == '"' || c == '\'') {
			in_quote = true;
			quote = c;
		} else if (c == '{' || c == '(' || c == '[') {
			depth++;
		} else if (c == '}' || c == ')' || c == ']') {
			depth--;
		} else if (c == ' ' && depth == 0) {
			if (i > start) {
				out.push_back(inner.substr(start, i - start));
			}
			start = i + 1;
		}
	}
	if (start < inner.length()) {
		out.push_back(inner.substr(start));
	}
	return out;
}

TreeItem *BeefEditorPlugin::_add_variable_item(TreeItem *p_parent, const String &p_name, const String &p_expr, const String &p_value, int p_watch_index) {
	TreeItem *it = variables_tree->create_item(p_parent);
	it->set_text(0, p_name);
	it->set_text(1, p_value);
	it->set_tooltip_text(1, p_value);
	Dictionary meta;
	meta["expr"] = p_expr;
	meta["loaded"] = false;
	if (p_watch_index >= 0) {
		meta["watch"] = p_watch_index;
	}
	it->set_metadata(0, meta);
	if (_beef_value_expandable(p_value)) {
		// Add a placeholder child so the fold arrow appears; real children load lazily on expand.
		variables_tree->create_item(it);
		it->set_collapsed(true);
	}
	return it;
}

void BeefEditorPlugin::_on_variable_collapsed(TreeItem *p_item) {
	if (!p_item || p_item->is_collapsed()) {
		return; // only act on expand
	}
	Variant meta = p_item->get_metadata(0);
	if (meta.get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary d = meta;
	if (bool(d.get("loaded", false))) {
		return;
	}
	d["loaded"] = true;
	p_item->set_metadata(0, d);

	// This fires from within Tree::propagate_mouse_event, during which the Tree is "blocked" and
	// create_item() returns nullptr (crashing on the next set_text). Defer the actual population so the
	// tree is mutated after input processing finishes.
	callable_mp(this, &BeefEditorPlugin::_populate_variable_children).call_deferred(p_item);
}

void BeefEditorPlugin::_populate_variable_children(TreeItem *p_item) {
	if (!p_item) {
		return; // item freed (e.g. tree cleared) before this deferred call ran
	}
	// Drop the placeholder child(ren).
	while (TreeItem *c = p_item->get_first_child()) {
		p_item->remove_child(c);
		memdelete(c);
	}

	Dictionary d = p_item->get_metadata(0);
	String parent_expr = d.get("expr", String());
	String value = p_item->get_text(1);
	BeefIDEHelper *dbg = BeefIDEHelper::get_singleton();
	Vector<String> members = _beef_split_members(value);
	for (int i = 0; i < members.size(); i++) {
		const String &tok = members[i];
		int eq = tok.find("=");
		String mname = (eq >= 0) ? tok.substr(0, eq).strip_edges() : tok.strip_edges();
		if (mname.is_empty()) {
			continue;
		}
		// Evaluate the member by expression so objects/pointers resolve to their own summary (and become
		// expandable in turn); fall back to the inline value from the summary if evaluation fails.
		String mexpr = parent_expr + "." + mname;
		String mval = dbg->debug_evaluate(mexpr, _selected_frame);
		if (mval.is_empty() || mval.begins_with("!")) {
			mval = (eq >= 0) ? tok.substr(eq + 1).strip_edges() : String();
		}
		_add_variable_item(p_item, mname, mexpr, mval, -1);
	}
}

void BeefEditorPlugin::_on_add_watch(const String &p_text) {
	String expr = p_text.strip_edges();
	if (expr.is_empty()) {
		return;
	}
	if (!_watches.has(expr)) {
		_watches.push_back(expr);
		_save_watches();
	}
	if (watch_input) {
		watch_input->clear();
	}
	_refresh_variables();
}

void BeefEditorPlugin::_on_variables_item_activated() {
	// Double-clicking a watch row removes it.
	if (!variables_tree) {
		return;
	}
	TreeItem *sel = variables_tree->get_selected();
	if (!sel) {
		return;
	}
	Variant meta = sel->get_metadata(0);
	if (meta.get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary d = meta;
	if (d.has("watch")) {
		int idx = d["watch"];
		if (idx >= 0 && idx < _watches.size()) {
			_watches.remove_at(idx);
			_save_watches();
			_refresh_variables();
		}
	}
}

void BeefEditorPlugin::_on_callstack_frame_selected() {
	if (callstack_tree) {
		_navigate_to_frame(callstack_tree->get_selected());
	}
}

void BeefEditorPlugin::_navigate_to_frame(TreeItem *p_item) {
	if (!p_item) {
		return;
	}
	Variant meta = p_item->get_metadata(0);
	if (meta.get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary d = meta;
	// Refresh the variables pane for the newly selected frame.
	_selected_frame = d.get("frame", 0);
	_refresh_variables();

	String file = d.get("file", String());
	int line = d.get("line", 0);
	if (file.is_empty() || line <= 0 || !file.to_lower().ends_with(".bf")) {
		return;
	}
	// Map the debugger's absolute path back to a res:// script and open it at the frame's line.
	String res_path = _beef_abs_to_res_path(file);
	if (res_path.is_empty() || !ResourceLoader::exists(res_path)) {
		return;
	}
	Ref<Script> script = ResourceLoader::load(res_path);
	if (script.is_valid()) {
		EditorInterface::get_singleton()->edit_script(script, line, 1);
		_set_execution_line(res_path, line);
	}
}

void BeefEditorPlugin::_set_execution_line(const String &p_res_path, int p_line) {
	// Mark the current execution line in the script editor (the gutter arrow), reusing the same
	// set_execution signal the GDScript debugger drives. The marker line is 0-based.
	if (!ResourceLoader::exists(p_res_path)) {
		return;
	}
	Ref<Script> script = ResourceLoader::load(p_res_path);
	if (script.is_null()) {
		return;
	}
	if (EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton()) {
		edn->emit_signal(SNAME("set_execution"), script, p_line - 1);
	}
	_exec_script_path = p_res_path;
}

void BeefEditorPlugin::_clear_execution_line() {
	if (_exec_script_path.is_empty()) {
		return;
	}
	if (ResourceLoader::exists(_exec_script_path)) {
		Ref<Script> script = ResourceLoader::load(_exec_script_path);
		if (script.is_valid()) {
			if (EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton()) {
				edn->emit_signal(SNAME("clear_execution"), script);
			}
		}
	}
	_exec_script_path = String();
}

void BeefEditorPlugin::_refresh_breakpoints() {
	if (!breakpoints_tree) {
		return;
	}
	breakpoints_tree->clear();
	TreeItem *root = breakpoints_tree->create_item();
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (!edn) {
		return;
	}
	BeefIDEHelper *dbg = BeefIDEHelper::get_singleton();
	HashMap<String, Vector<int>> bps = edn->get_breakpoints_by_file();
	for (const KeyValue<String, Vector<int>> &E : bps) {
		if (!E.key.to_lower().ends_with(".bf")) {
			continue;
		}
		String abs = ProjectSettings::get_singleton()->globalize_path(E.key);
		for (int line : E.value) {
			String key = abs + ":" + itos(line);
			TreeItem *item = breakpoints_tree->create_item(root);
			item->set_text(0, vformat("%s:%d", E.key.get_file(), line));
			item->set_tooltip_text(0, vformat("%s:%d", E.key, line));
			Dictionary meta;
			meta["file"] = abs;
			meta["res"] = E.key;
			meta["line"] = line;
			meta["key"] = key;
			item->set_metadata(0, meta);

			// Editable condition cell.
			item->set_cell_mode(1, TreeItem::CELL_MODE_STRING);
			item->set_editable(1, true);
			if (HashMap<String, String>::Iterator c = _bp_conditions.find(key); c) {
				item->set_text(1, c->value);
			}

			// Hit count (only meaningful while a session holds a handle for this breakpoint).
			int hits = 0;
			if (HashMap<String, void *>::Iterator h = _bp_handles.find(key); h && _debug_active) {
				hits = dbg->debug_breakpoint_hit_count(h->value);
			}
			item->set_text(2, itos(hits));
			item->set_selectable(2, false);
		}
	}
}

void BeefEditorPlugin::_on_editor_breakpoint_toggled(const String &p_path, int p_line, bool p_enabled) {
	// Fired by the editor whenever a .bf breakpoint changes — including when scripts (and their saved
	// breakpoints) are restored on editor start. Always refresh the pane so it reflects the persisted
	// breakpoints; only arm/remove on the live debugger when a session is actually running.
	if (!p_path.to_lower().ends_with(".bf")) {
		return;
	}
	if (_debug_active) {
		BeefIDEHelper *dbg = BeefIDEHelper::get_singleton();
		String abs = ProjectSettings::get_singleton()->globalize_path(p_path);
		String key = abs + ":" + itos(p_line);
		if (p_enabled) {
			if (!_bp_handles.has(key)) {
				String cond;
				if (HashMap<String, String>::Iterator c = _bp_conditions.find(key); c) {
					cond = c->value;
				}
				if (void *handle = dbg->debug_add_breakpoint(abs, p_line, cond)) {
					_bp_handles[key] = handle;
				}
			}
		} else if (HashMap<String, void *>::Iterator h = _bp_handles.find(key); h) {
			dbg->debug_delete_breakpoint(h->value);
			_bp_handles.erase(key);
		}
	}
	_refresh_breakpoints();
}

void BeefEditorPlugin::_on_breakpoint_activated() {
	if (!breakpoints_tree) {
		return;
	}
	TreeItem *sel = breakpoints_tree->get_selected();
	if (!sel) {
		return;
	}
	Variant meta = sel->get_metadata(0);
	if (meta.get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary d = meta;
	String res_path = d.get("res", String());
	int line = d.get("line", 0);
	if (res_path.is_empty() || line <= 0 || !ResourceLoader::exists(res_path)) {
		return;
	}
	Ref<Script> script = ResourceLoader::load(res_path);
	if (script.is_valid()) {
		EditorInterface::get_singleton()->edit_script(script, line, 1);
	}
}

void BeefEditorPlugin::_on_breakpoint_edited() {
	if (!breakpoints_tree) {
		return;
	}
	TreeItem *item = breakpoints_tree->get_edited();
	if (!item) {
		return;
	}
	Variant meta = item->get_metadata(0);
	if (meta.get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary d = meta;
	String key = d.get("key", String());
	if (key.is_empty()) {
		return;
	}
	String cond = item->get_text(1).strip_edges();
	if (cond.is_empty()) {
		_bp_conditions.erase(key);
	} else {
		_bp_conditions[key] = cond;
	}
	_save_bp_conditions();
	// If a session is running and this breakpoint is live, push the new condition immediately so it
	// takes effect on the next hit (an empty string clears it); otherwise it applies on the next launch.
	if (_debug_active) {
		if (HashMap<String, void *>::Iterator h = _bp_handles.find(key); h) {
			BeefIDEHelper::get_singleton()->debug_set_breakpoint_condition(h->value, cond);
		}
	}
}

// Breakpoint conditions are a Beef-specific concept the editor doesn't persist, so we store them
// ourselves in the per-project metadata (.godot/editor/project_metadata.cfg). Keys are stored as
// res:// paths so they survive the project being moved on disk.
void BeefEditorPlugin::_save_bp_conditions() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}
	ProjectSettings *ps = ProjectSettings::get_singleton();
	Dictionary d;
	for (const KeyValue<String, String> &E : _bp_conditions) {
		int colon = E.key.rfind(":");
		if (colon < 0) {
			continue;
		}
		String abs = E.key.substr(0, colon);
		String line = E.key.substr(colon + 1);
		d[ps->localize_path(abs) + ":" + line] = E.value;
	}
	es->set_project_metadata("beef_debugger", "breakpoint_conditions", d);
}

void BeefEditorPlugin::_load_bp_conditions() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}
	ProjectSettings *ps = ProjectSettings::get_singleton();
	Dictionary d = es->get_project_metadata("beef_debugger", "breakpoint_conditions", Dictionary());
	for (const Variant *k = d.next(nullptr); k; k = d.next(k)) {
		String res_key = *k;
		int colon = res_key.rfind(":");
		if (colon < 0) {
			continue;
		}
		String res_path = res_key.substr(0, colon);
		String line = res_key.substr(colon + 1);
		_bp_conditions[ps->globalize_path(res_path) + ":" + line] = String(d[*k]);
	}
}

// Watch expressions are plain Beef expressions (no paths), so they persist verbatim in the per-project
// metadata and survive editor restarts, mirroring how the breakpoints/conditions are stored.
void BeefEditorPlugin::_save_watches() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}
	Array a;
	for (const String &w : _watches) {
		a.push_back(w);
	}
	es->set_project_metadata("beef_debugger", "watches", a);
}

void BeefEditorPlugin::_load_watches() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}
	Array a = es->get_project_metadata("beef_debugger", "watches", Array());
	_watches.clear();
	for (int i = 0; i < a.size(); i++) {
		String w = a[i];
		if (!w.is_empty() && !_watches.has(w)) {
			_watches.push_back(w);
		}
	}
}

// ─── Debug-tree multi-row copy ──────────────────────────────────────────────

void BeefEditorPlugin::_setup_tree_copy(Tree *p_tree) {
	p_tree->set_select_mode(Tree::SELECT_MULTI); // Ctrl/Shift multi-row selection
	p_tree->set_allow_rmb_select(true);
	p_tree->connect("item_mouse_selected", callable_mp(this, &BeefEditorPlugin::_on_tree_rmb).bind(p_tree));
	p_tree->connect(SNAME("gui_input"), callable_mp(this, &BeefEditorPlugin::_on_tree_gui_input).bind(p_tree));
}

void BeefEditorPlugin::_on_tree_rmb(const Vector2 &p_pos, int p_button, Tree *p_tree) {
	if (p_button != (int)MouseButton::RIGHT || !tree_copy_menu) {
		return;
	}
	_copy_menu_tree = p_tree;
	// Rebuild the menu per popup so context actions match the right-clicked row.
	tree_copy_menu->clear();
	tree_copy_menu->add_item(TTR("Copy"), 0);
	tree_copy_menu->add_item(TTR("Copy All"), 1);
	_watch_menu_index = -1;
	if (p_tree == variables_tree) {
		if (TreeItem *it = p_tree->get_selected()) {
			Variant meta = it->get_metadata(0);
			if (meta.get_type() == Variant::DICTIONARY) {
				Dictionary d = meta;
				if (d.has("watch")) {
					_watch_menu_index = d["watch"];
					tree_copy_menu->add_separator();
					tree_copy_menu->add_item(TTR("Remove Watch"), 2);
				}
			}
		}
	}
	tree_copy_menu->set_position(DisplayServer::get_singleton()->mouse_get_position());
	tree_copy_menu->reset_size();
	tree_copy_menu->popup();
}

void BeefEditorPlugin::_on_tree_gui_input(const Ref<InputEvent> &p_event, Tree *p_tree) {
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed() && k->is_command_or_control_pressed() && k->get_keycode() == Key::C) {
		_copy_tree_rows(p_tree, false);
		p_tree->accept_event();
	}
}

void BeefEditorPlugin::_on_tree_copy_menu(int p_id) {
	if (p_id == 2) { // Remove Watch
		if (_watch_menu_index >= 0 && _watch_menu_index < _watches.size()) {
			_watches.remove_at(_watch_menu_index);
			_save_watches();
			_refresh_variables();
		}
		return;
	}
	if (_copy_menu_tree) {
		_copy_tree_rows(_copy_menu_tree, p_id == 1); // 1 == Copy All
	}
}

void BeefEditorPlugin::_copy_tree_rows(Tree *p_tree, bool p_all) {
	if (!p_tree) {
		return;
	}
	const int cols = p_tree->get_columns();
	String text;
	auto append_row = [&](TreeItem *it) {
		String row;
		for (int c = 0; c < cols; c++) {
			if (c > 0) {
				row += "\t";
			}
			row += it->get_text(c);
		}
		text += row + "\n";
	};

	if (p_all) {
		TreeItem *root = p_tree->get_root();
		for (TreeItem *it = root ? root->get_next_in_tree() : nullptr; it; it = it->get_next_in_tree()) {
			append_row(it);
		}
	} else {
		for (TreeItem *it = p_tree->get_next_selected(nullptr); it; it = p_tree->get_next_selected(it)) {
			append_row(it);
		}
	}

	text = text.strip_edges();
	if (!text.is_empty()) {
		DisplayServer::get_singleton()->clipboard_set(text);
	}
}

void BeefEditorPlugin::_on_debug_continue() {
	if (!_debug_active) {
		return;
	}
	BeefIDEHelper::get_singleton()->debug_continue();
	_debug_last_state = BeefIDEHelper::RS_RUNNING;
	if (debug_reason) {
		debug_reason->set_text(TTR("Running..."));
	}
	if (callstack_tree) {
		callstack_tree->clear();
	}
	if (variables_tree) {
		variables_tree->clear();
	}
	_clear_execution_line();
	_set_debug_controls_enabled(false);
}

void BeefEditorPlugin::_on_debug_step_over() {
	if (_debug_active) {
		BeefIDEHelper::get_singleton()->debug_step_over();
		_debug_last_state = BeefIDEHelper::RS_RUNNING;
	}
}

void BeefEditorPlugin::_on_debug_step_into() {
	if (_debug_active) {
		BeefIDEHelper::get_singleton()->debug_step_into();
		_debug_last_state = BeefIDEHelper::RS_RUNNING;
	}
}

void BeefEditorPlugin::_on_debug_step_out() {
	if (_debug_active) {
		BeefIDEHelper::get_singleton()->debug_step_out();
		_debug_last_state = BeefIDEHelper::RS_RUNNING;
	}
}

void BeefEditorPlugin::_on_debug_stop() {
	if (!_debug_active) {
		return;
	}
	BeefIDEHelper *dbg = BeefIDEHelper::get_singleton();
	if (_debug_pid == 0) {
		_debug_pid = dbg->debug_process_id();
	}
	int st = dbg->debug_run_state();
	// Delete our breakpoints first so a resumed process won't immediately break again.
	for (const KeyValue<String, void *> &E : _bp_handles) {
		dbg->debug_delete_breakpoint(E.value);
	}
	_bp_handles.clear();
	_clear_execution_line();

	// IDEHelper's debug loop runs on its own background thread; we just drive Update and wait for the
	// run-state to settle, mirroring the IDE's `while (GetRunState() != Terminated) Update()` stop loop.
	// The single bound is a wall-clock deadline: unlike a standalone IDE, an embedded debugger must not
	// be able to freeze the host editor if a debuggee refuses to die.
	const uint64_t deadline_ms = OS::get_singleton()->get_ticks_msec() + 5000;

	// A debuggee paused at a breakpoint has all threads suspended, and Windows cannot complete
	// TerminateProcess on a fully-suspended process. Resume it (back to Running) before terminating.
	if (st == BeefIDEHelper::RS_PAUSED || st == BeefIDEHelper::RS_BREAKPOINT ||
			st == BeefIDEHelper::RS_EXCEPTION || st == BeefIDEHelper::RS_HOT_STEP) {
		dbg->debug_continue();
		while (OS::get_singleton()->get_ticks_msec() < deadline_ms) {
			int s = dbg->debug_run_state();
			if (s == BeefIDEHelper::RS_RUNNING || s == BeefIDEHelper::RS_TERMINATED) {
				break;
			}
			Vector<String> d;
			dbg->debug_poll(d);
		}
	}

	dbg->debug_stop(); // StopDebugging -> TerminateProcess (process now running, so it completes)
	while (dbg->debug_run_state() != BeefIDEHelper::RS_TERMINATED && OS::get_singleton()->get_ticks_msec() < deadline_ms) {
		Vector<String> drain;
		dbg->debug_poll(drain);
	}

	// Last resort only if the debugger could not terminate it within the deadline (e.g. a thread it
	// cannot resume): kill the OS process directly, as Godot's own EditorRun::stop does.
	if (dbg->debug_run_state() != BeefIDEHelper::RS_TERMINATED && _debug_pid != 0) {
		OS::get_singleton()->kill((OS::ProcessID)_debug_pid);
	}
	_end_debug_session();
}

void BeefEditorPlugin::_end_debug_session() {
	// Final teardown once the debuggee has terminated (or as a forced fallback).
	BeefIDEHelper *dbg = BeefIDEHelper::get_singleton();
	for (const KeyValue<String, void *> &E : _bp_handles) {
		dbg->debug_delete_breakpoint(E.value); // game may have exited on its own without a Stop
	}
	_bp_handles.clear();
	// Tear down the debugger (and its background thread) so the next run starts with a fresh one;
	// without this a second session's background thread fights the first and can't terminate.
	dbg->debug_detach();
	// Stop the editor remote-debug server we started for output forwarding.
	if (EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton()) {
		edn->stop();
	}
	_debug_active = false;
	_debug_pid = 0;
	_debug_last_state = BeefIDEHelper::RS_TERMINATED;
	set_process(false);
	if (debug_reason) {
		debug_reason->set_text(TTR("Not running"));
	}
	if (variables_tree) {
		variables_tree->clear();
	}
	_clear_execution_line();
	_set_debug_controls_enabled(false);
	_refresh_variables(); // keep the watch list visible after the session ends (values clear)
	_refresh_breakpoints(); // drop stale hit counts
	_dbg_log("[beef-dbg] session ended\n");
}

void BeefEditorPlugin::_set_status_icon() {
	if (!bottom_panel_button) {
		return;
	}
	Ref<Texture2D> icon;
	if (_error_count > 0) {
		icon = tabs->get_theme_icon(SNAME("StatusError"), SNAME("EditorIcons"));
	} else if (_warning_count > 0) {
		icon = tabs->get_theme_icon(SNAME("StatusWarning"), SNAME("EditorIcons"));
	}
	bottom_panel_button->set_button_icon(icon);
}

// ─── Lifecycle ──────────────────────────────────────────────────────────────

void BeefEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			// Add a Build button to the run bar, to the left of the Play controls.
			if (EditorRunBar *run_bar = EditorRunBar::get_singleton()) {
				run_bar_build_button = memnew(Button);
				run_bar_build_button->set_tooltip_text(TTR("Build the Beef project."));
				run_bar_build_button->set_focus_mode(Control::FOCUS_NONE);
				run_bar_build_button->set_theme_type_variation("RunBarButton");
				if (_build_shortcut.is_valid()) {
					run_bar_build_button->set_shortcut(_build_shortcut);
					run_bar_build_button->set_shortcut_in_tooltip(true);
				}
				run_bar_build_button->connect(SceneStringName(pressed), callable_mp(this, &BeefEditorPlugin::_on_build_pressed));

				HBoxContainer *buttons = run_bar->get_buttons_container();
				buttons->add_child(run_bar_build_button);
				buttons->move_child(run_bar_build_button, 0);
			}
			_update_theme();
			_load_bp_conditions(); // restore saved conditions before painting the pane
			_load_watches(); // restore watch expressions from the previous session
			_refresh_variables(); // show the restored watch list (values fill in when a session pauses)
			_refresh_breakpoints(); // show breakpoints already set in .bf scripts
		} break;
		case NOTIFICATION_PROCESS: {
			// Only enabled while a debug session is active (set_process toggled in launch/end).
			_debug_poll();
		} break;
	}
}

BeefEditorPlugin::BeefEditorPlugin() {
	_build_ui();

	_build_shortcut = ED_SHORTCUT("beef/build_project", TTRC("Build Beef Project"), KeyModifierMask::ALT | Key::B);

	// The plugin is a Node and won't receive NOTIFICATION_THEME_CHANGED, so refresh icons/fonts when
	// the panel control's theme changes.
	tabs->connect(SNAME("theme_changed"), callable_mp(this, &BeefEditorPlugin::_update_theme));

	bottom_panel_button = add_control_to_bottom_panel(tabs, TTR("Beef"));

	// Route BeefCompiler/BeefIDEHelper diagnostics to the Beef panel's Output tab instead of the general
	// editor Output dock. (Falls back to print_line when no sink — game/headless.)
	beef_set_log_sink(callable_mp(this, &BeefEditorPlugin::_compiler_log));

	// Register IDEHelper-backed syntax highlighting for .bf scripts.
	if (ScriptEditor *se = ScriptEditor::get_singleton()) {
		Ref<BeefSyntaxHighlighter> highlighter;
		highlighter.instantiate();
		se->register_syntax_highlighter(highlighter);
	}
}

BeefEditorPlugin::~BeefEditorPlugin() {
	beef_set_log_sink(Callable()); // stop routing to this (about-to-be-freed) plugin
	if (run_bar_build_button) {
		if (run_bar_build_button->get_parent()) {
			run_bar_build_button->get_parent()->remove_child(run_bar_build_button);
		}
		memdelete(run_bar_build_button);
		run_bar_build_button = nullptr;
	}
	if (tabs) {
		remove_control_from_bottom_panel(tabs);
		memdelete(tabs);
		tabs = nullptr;
	}
}
