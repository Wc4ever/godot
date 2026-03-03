#ifndef BEEF_BINDINGS_GENERATOR_H
#define BEEF_BINDINGS_GENERATOR_H

#ifdef TOOLS_ENABLED

#include "core/object/class_db.h"
#include "core/string/string_builder.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/type_info.h"

class BeefBindingsGenerator {
	bool initialized = false;

	// Maps Godot type names to Beef type names.
	HashMap<String, String> type_map;

	// MethodBind* cache fields (_mb_<name>) needed by the class currently being generated.
	// Populated at the exact point each _mb_ reference is emitted (see _append_methods /
	// _append_properties) so the declared fields always match the references. Reset per class.
	HashSet<String> _class_binds;

	void _init_type_map();

	// Converts snake_case to PascalCase.
	static String _to_pascal_case(const String &p_name);

	// Prefixes Beef reserved keywords with '@' so they can be used as identifiers.
	static String _escape_beef_identifier(const String &p_name);

	// Argument name for codegen: escaped name, or "arg<idx>" when the engine left it blank.
	static String _arg_name(const PropertyInfo &p_arg, int p_idx);

	// Maps GodotTypeInfo::Metadata for an INT/FLOAT argument to the exact-width Beef type.
	static String _get_int_beef_type(GodotTypeInfo::Metadata p_meta);
	static String _get_float_beef_type(GodotTypeInfo::Metadata p_meta);

	// Resolves the Beef type for a method argument (p_idx >= 0) or return value (p_idx == -1),
	// honoring integer/float width metadata. Enum-typed ints resolve to the enum's Beef name.
	// p_mb (may be null) supplies the exact width metadata, which MethodInfo alone lacks.
	String _resolve_type(MethodBind *p_mb, const MethodInfo &p_mi, int p_idx) const;

	// Converts a Godot type name to its Beef equivalent.
	String _get_beef_type(const String &p_godot_type) const;

	// Generates GodotPrimitives.bf with stub type definitions for all Godot built-in types.
	Error _generate_primitives_file(const String &p_output_dir);

	// Generates GodotEnums.bf with all global Godot enums (Error, MouseButton, Key, etc.)
	Error _generate_global_enums_file(const String &p_output_dir);

	// Generates Native.bf (the Godot API call surface) into the GodotBindings StaticLib so
	// generated binding classes can call it. Must NOT go in the user DynamicLib.
	Error _generate_godotnative_file(const String &p_output_dir);

	// Generates GodotObjectFactory.bf: maps a runtime class name to `new <Class>()` so Object*
	// returns can be wrapped as their actual type. Returns null for unknown names.
	Error _generate_object_factory_file(const String &p_output_dir);

	// Generates GodotUtility.bf: a static `GD` class exposing the engine's global utility
	// functions (print, str, lerp, clamp, abs, sin, ...) via Variant-based dispatch.
	Error _generate_utility_file(const String &p_output_dir);

	// Generates GodotRuntime.bf: the [Export] BeefGodot_Init entry point (user DynamicLib only).
	Error _generate_runtime_file(const String &p_output_dir);

	// Generates GodotScript.bf: the [GodotRegister] comptime attribute that emits the per-class
	// C ABI exports from a script class's overridden virtuals (user DynamicLib only).
	Error _generate_godotscript_file(const String &p_output_dir);

	// Generates a single class file into p_output.
	Error _generate_bf_class(const StringName &p_class_name, StringBuilder &p_output);

	// Appends enum definitions for a class.
	void _append_enums(const StringName &p_class_name, StringBuilder &p_output);

	// Appends constant definitions for a class.
	void _append_constants(const StringName &p_class_name, StringBuilder &p_output);

	// Emits the static void* _mb_X cache fields collected in _class_binds during method/property
	// generation. Must run after _append_methods / _append_properties have populated the set.
	void _emit_method_bind_fields(StringBuilder &p_output);

	// Appends method declarations for a class.
	void _append_methods(const StringName &p_class_name, StringBuilder &p_output);

	// Appends property declarations for a class.
	void _append_properties(const StringName &p_class_name, StringBuilder &p_output);

	// How a single value crosses the ptrcall boundary.
	enum MarshalKind {
		MK_VOID, // return only
		MK_SIMPLE, // fixed-size value type passed by pointer to a copy
		MK_STRING, // engine String constructed/read via Native.Str*
		MK_STRINGNAME, // engine StringName constructed/read via Native.SN*
		MK_NODEPATH, // engine NodePath constructed/read via Native.NP* (marshalled as text)
		MK_OBJECT, // Object* passed as &wrapper._godotOwner
		MK_ARRAY, // engine Array constructed/read via Native.Arr*
		MK_DICTIONARY, // engine Dictionary constructed/read via Native.Dict*
		MK_PACKED, // POD Packed*Array (uint8[]/int32[]/Vector2[]/...) via Native.Packed*
		MK_PACKED_STRING, // PackedStringArray (System.String[]) via Native.PSA* (per-element)
		MK_UNSUPPORTED, // not handled yet -> method falls back to a stub
	};
	MarshalKind _marshal_kind(MethodBind *p_mb, const MethodInfo &p_mi, int p_idx) const;

	// For a "string-like" kind (String/StringName), returns its Beef storage type and the
	// Native helper prefix (so codegen is shared). Returns false for other kinds.
	static bool _strlike_marshal(MarshalKind p_kind, String &r_storage, String &r_prefix);

	// True if the return value and every argument have a supported marshal kind (simple/string/
	// stringname/object/array/dictionary/packed; void return is allowed, void/unsupported args are not).
	// Methods with no real MethodBind, or virtual/vararg methods, are not marshallable.
	bool _can_marshal(MethodBind *p_mb, const MethodInfo &p_mi) const;

	// Shared ptrcall codegen used by both the method emitter and the property getter/setter emitter.
	// p_indent is the leading tabs (methods nest one shallower than property accessors). p_type is the
	// Beef return/property/argument type; p_variant_type is the engine Variant::Type (packed tagging).
	// The ptrcall ret pointer for a return kind ("&_rs"/"&_ro"/"&_ret"/"null").
	static String _ret_arg_expr(MarshalKind p_rk);
	// Emit the return-value storage decl(s) (nothing for void); pair with _emit_return_read around the call.
	void _emit_return_storage(StringBuilder &p_out, const String &p_indent, MarshalKind p_rk, const String &p_type, int p_variant_type);
	// Emit the post-call read + return statement (nothing for void).
	void _emit_return_read(StringBuilder &p_out, const String &p_indent, MarshalKind p_rk, const String &p_type, int p_variant_type);
	// Emit prep for one ptrcall argument from p_src (e.g. an arg name or "value"), naming locals with the
	// p_suffix. Appends the arg's data-pointer expression to r_ptr_expr and any post-call cleanup to
	// r_cleanup. p_type/p_variant_type describe the argument (packed element/tag).
	void _emit_arg_prep(StringBuilder &p_out, const String &p_indent, MarshalKind p_ak, const String &p_src, const String &p_suffix, const String &p_type, int p_variant_type, String &r_ptr_expr, String &r_cleanup);

public:
	BeefBindingsGenerator();

	bool is_initialized() const { return initialized; }

	// Generates .bf bindings for the entire Godot API into p_output_dir.
	Error generate_bf_api(const String &p_output_dir);

	// Generates only GodotRuntime.bf into p_src_dir (must be in the DynamicLib project's src/).
	// Call this separately from generate_bf_api() so the [Export] works.
	Error generate_runtime_glue(const String &p_src_dir);

	// Generates GodotScript.bf (the [GodotRegister] comptime attribute) into p_src_dir. Like the
	// runtime glue it must live in the DynamicLib src/ so the emitted [Export]s reach the DLL.
	Error generate_script_base(const String &p_src_dir);

	// Recursively scans p_scan_dir for `[GodotScript] class X : Base` and writes GodotRegistrations.bf
	// into p_output_dir, with a generated `[AlwaysInclude, GodotRegister(typeof(X))]` registrar per
	// class, so users never write the registrar by hand. Regenerated on editor start and before each
	// build. Scripts may live in any subfolder of the source tree.
	Error generate_registrations(const String &p_scan_dir, const String &p_output_dir);

	// Copies the hand-written math value-type extensions (modules/beef/glue/*.bf) into p_bindings_src so
	// the Godot built-in structs (Vector2, etc.) gain their methods/operators/constructors. These ride
	// on the [CRepr] layout stubs in GodotPrimitives.bf, so the engine marshalling layout is unchanged.
	Error copy_math_glue(const String &p_bindings_src);

	// Handles --generate-beef-glue command-line option.
	static void handle_cmdline_args(const List<String> &p_cmdline_args);
};

#endif // TOOLS_ENABLED

#endif // BEEF_BINDINGS_GENERATOR_H
