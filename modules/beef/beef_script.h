/**************************************************************************/
/*  beef_script.h                                                         */
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

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/script_language.h"
#include "core/templates/hash_set.h"

class BeefScript;
class BeefInstance;
class BeefLanguage;

// ─── BeefScript ──────────────────────────────────────────────────────────────

class BeefScript : public Script {
	GDCLASS(BeefScript, Script);

	friend class BeefInstance;
	friend class BeefLanguage;

	String source_code;

	// Per-class function pointers resolved from the shared compiled DLL.
	// Convention: BeefGodot_Create_ClassName(void* owner) etc.
	typedef void *(*PFN_CreateInstance)(void *owner_object);
	typedef void (*PFN_DestroyInstance)(void *beef_obj);
	typedef void (*PFN_Notification)(void *beef_obj, int32_t what);
	// Dispatches a Godot-side method call (args/ret are engine Variant*). Returns true if the
	// Beef class handled the method; false leaves it for the engine to resolve elsewhere.
	typedef bool (*PFN_CallMethod)(void *beef_obj, const char *method, void **args, int argc, void *ret);
	// True if the Beef class defines/overrides the named method. Drives GDVIRTUAL override detection
	// (e.g. enabling _process) — it is class-level, so it takes no instance.
	typedef bool (*PFN_HasMethod)(const char *method);
	// Read/write an exported field by name; args/ret are an engine Variant*. Returns true if the
	// class has that exported field.
	typedef bool (*PFN_SetProp)(void *beef_obj, const char *name, const void *variant);
	typedef bool (*PFN_GetProp)(void *beef_obj, const char *name, void *variant);
	// Exported-property enumeration (class-level): count, and per-index name + Variant::Type tag.
	// PropName returns a pointer to a stable (interned) null-terminated string owned by the DLL.
	typedef int32_t (*PFN_PropCount)();
	typedef const char *(*PFN_PropName)(int32_t idx);
	typedef int32_t (*PFN_PropType)(int32_t idx);
	// Default value of property idx (from a fresh instance), boxed into an engine Variant*.
	typedef void (*PFN_PropDefault)(int32_t idx, void *variant_out);
	// Inspector hint (PropertyHint) + hint string for property idx.
	typedef int32_t (*PFN_PropHint)(int32_t idx);
	typedef const char *(*PFN_PropHintString)(int32_t idx);

	PFN_CreateInstance fn_create = nullptr;
	PFN_DestroyInstance fn_destroy = nullptr;
	PFN_Notification fn_notify = nullptr;
	PFN_CallMethod fn_call = nullptr;
	PFN_HasMethod fn_has_method = nullptr;
	PFN_SetProp fn_set = nullptr;
	PFN_GetProp fn_get = nullptr;
	PFN_PropCount fn_prop_count = nullptr;
	PFN_PropName fn_prop_name = nullptr;
	PFN_PropType fn_prop_type = nullptr;
	PFN_PropDefault fn_prop_default = nullptr;
	PFN_PropHint fn_prop_hint = nullptr;
	PFN_PropHintString fn_prop_hint_string = nullptr;

	// Signal enumeration (class-level): count, per-index name (stable interned pointer) + arg count.
	typedef int32_t (*PFN_SignalCount)();
	typedef const char *(*PFN_SignalName)(int32_t idx);
	typedef int32_t (*PFN_SignalArgc)(int32_t idx);
	typedef int32_t (*PFN_SignalArgType)(int32_t sig_idx, int32_t arg_idx); // Variant::Type (0=untyped)
	PFN_SignalCount fn_signal_count = nullptr;
	PFN_SignalName fn_signal_name = nullptr;
	PFN_SignalArgc fn_signal_argc = nullptr;
	PFN_SignalArgType fn_signal_arg_type = nullptr;

	typedef int32_t (*PFN_MethodCount)();
	typedef const char *(*PFN_MethodName)(int32_t idx);
	typedef int32_t (*PFN_MethodArgc)(int32_t idx);
	typedef int32_t (*PFN_MethodArgType)(int32_t method_idx, int32_t arg_idx); // Variant::Type (0=untyped)
	PFN_MethodCount fn_method_count = nullptr;
	PFN_MethodName fn_method_name = nullptr;
	PFN_MethodArgc fn_method_argc = nullptr;
	PFN_MethodArgType fn_method_arg_type = nullptr;

	// RPC enumeration (class-level, from [GodotRpc] methods): count + per-index name and config.
	typedef int32_t (*PFN_RpcCount)();
	typedef const char *(*PFN_RpcName)(int32_t idx);
	typedef int32_t (*PFN_RpcMode)(int32_t idx); // MultiplayerAPI::RPCMode
	typedef int32_t (*PFN_RpcTransfer)(int32_t idx); // MultiplayerPeer::TransferMode
	typedef bool (*PFN_RpcCallLocal)(int32_t idx);
	typedef int32_t (*PFN_RpcChannel)(int32_t idx);
	PFN_RpcCount fn_rpc_count = nullptr;
	PFN_RpcName fn_rpc_name = nullptr;
	PFN_RpcMode fn_rpc_mode = nullptr;
	PFN_RpcTransfer fn_rpc_transfer = nullptr;
	PFN_RpcCallLocal fn_rpc_call_local = nullptr;
	PFN_RpcChannel fn_rpc_channel = nullptr;

	bool is_compiled = false;
	StringName class_name_cache;

#ifdef TOOLS_ENABLED
	// Placeholder instances used in the editor for non-tool scripts (so exported properties show in
	// the inspector without the script's code running). Mirrors modules/mono.
	HashSet<PlaceHolderScriptInstance *> placeholders;
	void _update_placeholder(PlaceHolderScriptInstance *p_placeholder) const;
#endif

protected:
	static void _bind_methods();
	virtual void _placeholder_erased(PlaceHolderScriptInstance *p_placeholder) override;

	// Backs GDScript's `MyScript.new()` — instantiates the native base type and attaches a Beef
	// instance to it (mirrors CSharpScript::_new). Bound as a vararg "new" method in _bind_methods.
	Variant _new(const Variant **p_args, int p_argcount, Callable::CallError &r_error);

public:
	// Parse a Beef script source for its registered global class name + base Godot type.
	// A [GodotRegister(typeof(Name))] marker opts the class in as a named/global type and gives its
	// name; `class Name : Base` gives the base. Returns true if the marker (and a name) were found.
	static bool parse_global_class(const String &p_source, String &r_name, String &r_base);

#ifdef TOOLS_ENABLED
	virtual StringName get_doc_class_name() const override; // base virtual is editor-only
#endif
	String script_path; // public so resource loader can set it
	virtual bool can_instantiate() const override;
	virtual Ref<Script> get_base_script() const override;
	virtual StringName get_global_name() const override;
	virtual bool inherits_script(const Ref<Script> &p_script) const override;
	virtual StringName get_instance_base_type() const override;
	virtual ScriptInstance *instance_create(Object *p_this) override;
	virtual PlaceHolderScriptInstance *placeholder_instance_create(Object *p_this) override;
	virtual bool instance_has(const Object *p_this) const override;
	virtual bool has_source_code() const override;
	virtual String get_source_code() const override;
	virtual void set_source_code(const String &p_code) override;
	virtual Error reload(bool p_keep_state = false) override;

#ifdef TOOLS_ENABLED
	virtual Vector<DocData::ClassDoc> get_documentation() const override;
	virtual String get_class_icon_path() const override;
#endif

	virtual bool has_method(const StringName &p_method) const override;
	virtual MethodInfo get_method_info(const StringName &p_method) const override;
	virtual bool is_tool() const override;
	virtual bool is_valid() const override;
	virtual bool is_abstract() const override;
	virtual ScriptLanguage *get_language() const override;
	virtual bool has_script_signal(const StringName &p_signal) const override;
	virtual void get_script_signal_list(List<MethodInfo> *r_signals) const override;
	virtual bool get_property_default_value(const StringName &p_property, Variant &r_value) const override;
	virtual void get_script_method_list(List<MethodInfo> *p_list) const override;
	virtual void get_script_property_list(List<PropertyInfo> *p_list) const override;
	virtual const Variant get_rpc_config() const override;
	virtual int get_member_line(const StringName &p_member) const override;
};

// ─── BeefInstance ────────────────────────────────────────────────────────────

class BeefInstance : public ScriptInstance {
	friend class BeefScript;
	friend class BeefLanguage;

	Object *owner = nullptr;
	Ref<BeefScript> beef_script;
	void *beef_obj = nullptr; // native Beef object allocated by BeefGodot_Create

	// Calls fn_destroy and nulls beef_obj. Safe to call multiple times.
	void _destroy_beef_obj();

	// Index of an exported property by name, or -1. Used for revert (default value) queries.
	int _find_prop_index(const StringName &p_name) const;

public:
	BeefInstance();
	virtual ~BeefInstance();

	virtual bool set(const StringName &p_name, const Variant &p_value) override;
	virtual bool get(const StringName &p_name, Variant &r_ret) const override;
	virtual void get_property_list(List<PropertyInfo> *p_properties) const override;
	virtual Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid = nullptr) const override;
	virtual void validate_property(PropertyInfo &p_property) const override;
	virtual bool property_can_revert(const StringName &p_name) const override;
	virtual bool property_get_revert(const StringName &p_name, Variant &r_ret) const override;
	virtual void get_method_list(List<MethodInfo> *p_list) const override;
	virtual bool has_method(const StringName &p_method) const override;
	virtual Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) override;
	virtual void notification(int p_notification, bool p_reversed = false) override;
	virtual Ref<Script> get_script() const override;
	virtual ScriptLanguage *get_language() override;
	virtual Object *get_owner() override { return owner; }
	virtual String to_string(bool *r_valid) override;

	// NOTE: refcount_incremented()/refcount_decremented() are intentionally NOT overridden.
	// CSharpInstance overrides them to swap a managed GC handle between weak and strong so the
	// tracing GC doesn't collect a C# object the engine still references. Beef has no tracing GC:
	// a script instance's Beef object is owned 1:1 by this BeefInstance (created in instance_create,
	// destroyed in the destructor when the engine Object frees), and RefCounted refs are managed
	// explicitly (see _beef_object_reference/_unreference). Nothing on the Beef side independently
	// pins the object, so the base no-op behavior (die at refcount 0) is correct.
};

// ─── BeefLanguage ────────────────────────────────────────────────────────────

class BeefLanguage : public ScriptLanguage {
	static BeefLanguage *singleton;
	friend class BeefInstance;

	// All live BeefInstance objects. Used to pre-destroy Beef objects before DLL unload,
	// preventing Beef's debug runtime from falsely reporting them as memory leaks.
	HashSet<BeefInstance *> _live_instances;

	// Calls fn_destroy on all live instances and nulls their beef_obj.
	// Must be called before unload_dll().
	void _pre_unload_destroy_instances();

public:
	static BeefLanguage *get_singleton() { return singleton; }

	// Resolve the workspace dir / project name / build config / external-build flag from project
	// settings (shared by the editor build hooks, reload(), and runtime code completion). Returns
	// false if settings are unusable. Not editor-gated: complete_code() uses it in non-tools builds.
	bool _resolve_build_settings(String &r_workspace, String &r_project, String &r_config, bool &r_external) const;

#ifdef TOOLS_ENABLED
	// Build the Beef workspace from the editor (Build button / build-before-Play). Unloads the DLL
	// first (Windows locks a loaded DLL), optionally cleans, compiles, then reloads the DLL and
	// re-resolves all loaded scripts. Returns true on success (or when external_build defers to
	// another tool). r_output receives the raw BeefBuild log; r_errors the parsed problem lines.
	bool build_project(bool p_rebuild, String &r_output, Vector<String> &r_errors);

	// Unload the DLL and delete the build output directory (no recompile).
	void clean_project();
#endif

	BeefLanguage();
	~BeefLanguage();

	virtual String get_name() const override;
	virtual void init() override;
#ifdef TOOLS_ENABLED
	static void _editor_init_callback();
	static void _prewarm_resolve(const String &p_workspace, const String &p_project); // deferred: build the completion resolve system at editor-idle
#endif
	virtual String get_type() const override;
	virtual String get_extension() const override;
	virtual String get_preferred_script_directory() const override;
	virtual String validate_path(const String &p_path) const override;
	virtual void finish() override;
	virtual Vector<String> get_reserved_words() const override;
	virtual bool is_control_flow_keyword(const String &p_string) const override;
	virtual Vector<String> get_comment_delimiters() const override;
	virtual Vector<String> get_doc_comment_delimiters() const override;
	virtual Vector<String> get_string_delimiters() const override;
	virtual bool validate(const String &p_script, const String &p_path = "", List<String> *r_functions = nullptr, List<ScriptError> *r_errors = nullptr, List<Warning> *r_warnings = nullptr, HashSet<int> *r_safe_lines = nullptr) const override;
	virtual String get_global_class_name(const String &p_path, String *r_base_type = nullptr, String *r_icon_path = nullptr, bool *r_is_abstract = nullptr, bool *r_is_tool = nullptr) const override;
	virtual bool handles_global_class_type(const String &p_type) const override;
	virtual Script *create_script() const override;
	virtual Ref<Script> make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const override;
	virtual Vector<ScriptTemplate> get_built_in_templates(const StringName &p_object) override;
	virtual bool is_using_templates() override { return true; }
	virtual bool supports_builtin_mode() const override;
	virtual int find_function(const String &p_function, const String &p_code) const override;
	virtual String make_function(const String &p_class, const String &p_name, const PackedStringArray &p_args) const override;
	virtual void auto_indent_code(String &p_code, int p_from_line, int p_to_line) const override;
	// Reindent lines [from, to] of p_code by bracket depth using p_unit per level. String/char/comment
	// contents are ignored. Public + static so it can be unit-tested without a language instance.
	static void reindent_code(String &p_code, int p_from_line, int p_to_line, const String &p_unit);

	virtual Error complete_code(const String &p_code, const String &p_path, Object *p_owner, List<CodeCompletionOption> *r_options, bool &r_force, String &r_call_hint) override;
	// Hover / Ctrl-click: resolve the symbol under the cursor to a Godot class member so the editor shows
	// the engine documentation (DocData) — the same content as the online class reference.
	virtual Error lookup_code(const String &p_code, const String &p_symbol, const String &p_path, Object *p_owner, LookupResult &r_result) override;
	// Add keyword + exposed-class completions matching p_prefix (nothing after a `.`, which needs type
	// resolution). Public + static for unit testing without a language instance.
	static void gather_completions(const String &p_prefix, bool p_after_dot, const Vector<String> &p_keywords, List<CodeCompletionOption> *r_options);
	virtual void add_global_constant(const StringName &p_variable, const Variant &p_value) override;

	// Debugger
	virtual String debug_get_error() const override;
	virtual int debug_get_stack_level_count() const override;
	virtual int debug_get_stack_level_line(int p_level) const override;
	virtual String debug_get_stack_level_function(int p_level) const override;
	virtual String debug_get_stack_level_source(int p_level) const override;
	virtual void debug_get_stack_level_locals(int p_level, List<String> *p_locals, List<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
	virtual void debug_get_stack_level_members(int p_level, List<String> *p_members, List<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
	virtual void debug_get_globals(List<String> *p_globals, List<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override;
	virtual String debug_parse_stack_level_expression(int p_level, const String &p_expression, int p_max_subitems = -1, int p_max_depth = -1) override;

	// Reload
	virtual void reload_all_scripts() override;
	virtual void reload_scripts(const Array &p_scripts, bool p_soft_reload) override;
	virtual void reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;

	// Loader
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual void get_public_functions(List<MethodInfo> *p_functions) const override;
	virtual void get_public_constants(List<Pair<String, Variant>> *p_constants) const override;
	virtual void get_public_annotations(List<MethodInfo> *p_annotations) const override;

	// Profiling
	virtual void profiling_start() override;
	virtual void profiling_stop() override;
	virtual void profiling_set_save_native_calls(bool p_enable) override;
	virtual int profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) override;
	virtual int profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) override;
};

// ─── Resource loader / saver ─────────────────────────────────────────────────

class ResourceFormatLoaderBeefScript : public ResourceFormatLoader {
public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;
};

class ResourceFormatSaverBeefScript : public ResourceFormatSaver {
public:
	virtual Error save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags = 0) override;
	virtual void get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const override;
	virtual bool recognize(const Ref<Resource> &p_resource) const override;
};
