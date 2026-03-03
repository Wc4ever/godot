#include "bindings_generator.h"

#ifdef TOOLS_ENABLED

#include "../compiler/beef_compiler.h"
#include "../ide/beef_ide_helper.h"

#include "core/config/engine.h"
#include "core/core_constants.h"
#include "core/io/dir_access.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/local_vector.h"
#include "core/variant/callable.h"
#include "core/variant/variant.h"
#include "main/main.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────

String BeefBindingsGenerator::_to_pascal_case(const String &p_name) {
	String result;
	bool next_upper = true;
	for (int i = 0; i < p_name.length(); i++) {
		char32_t c = p_name[i];
		if (c == '_') {
			next_upper = true;
		} else if (next_upper) {
			char32_t uc = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
			result += String::chr(uc);
			next_upper = false;
		} else {
			result += String::chr(c);
		}
	}
	return result;
}

String BeefBindingsGenerator::_escape_beef_identifier(const String &p_name) {
	// Complete list of Beef language keywords that cannot be used as plain identifiers.
	// Prefix with '@' to use them as identifiers (Beef's escape mechanism).
	// Sourced directly from BfParser.cpp SrcPtrHasToken calls — the authoritative list.
	static const char *keywords[] = {
		"abstract", "alignof", "alloctype", "append", "as", "asm",
		"base", "box", "break",
		"case", "catch", "checked", "class", "comptype", "concrete", "const", "continue",
		"decltype", "default", "defer", "delegate", "delete", "do",
		"else", "enum", "explicit", "extension", "extern",
		"fallthrough", "false", "finally", "fixed", "for", "foreach", "function",
		"goto",
		"if", "implicit", "in", "inline", "interface", "internal", "is", "isconst",
		"let",
		"mixin", "mut",
		"nameof", "namespace", "new", "null", "nullable",
		"offsetof", "operator", "out", "override",
		"params", "private", "protected", "public",
		"readonly", "ref", "repeat", "rettype", "return",
		"scope", "sealed", "sizeof", "static", "strideof", "struct", "switch",
		"this", "throw", "true", "try", "typealias", "typeof",
		"unchecked", "unsigned", "using",
		"var", "virtual", "void", "volatile",
		"when", "where", "while",
		"yield",
		nullptr
	};
	for (int i = 0; keywords[i]; i++) {
		if (p_name == keywords[i]) {
			return "@" + p_name;
		}
	}
	return p_name;
}

void BeefBindingsGenerator::_init_type_map() {
	// Primitive Godot types → Beef built-in types
	type_map["void"] = "void";
	type_map["Nil"] = "void";
	type_map["bool"] = "bool";
	type_map["int"] = "int64";
	type_map["float"] = "double";
	type_map["String"] = "System.String";
	// StringName is marshalled to/from a Beef string (like String); NodePath stays opaque until
	// it gets its own marshalling.
	type_map["StringName"] = "System.String";
	type_map["NodePath"] = "System.String"; // marshalled as text, like StringName
	type_map["Variant"] = "Variant";

	// Math types
	type_map["Vector2"] = "Vector2";
	type_map["Vector2i"] = "Vector2I";
	type_map["Vector3"] = "Vector3";
	type_map["Vector3i"] = "Vector3I";
	type_map["Vector4"] = "Vector4";
	type_map["Vector4i"] = "Vector4I";
	type_map["Rect2"] = "Rect2";
	type_map["Rect2i"] = "Rect2I";
	type_map["Transform2D"] = "Transform2D";
	type_map["Transform3D"] = "Transform3D";
	type_map["Basis"] = "Basis";
	type_map["Quaternion"] = "Quaternion";
	type_map["Plane"] = "Plane";
	type_map["AABB"] = "Aabb";
	type_map["Color"] = "Color";
	type_map["Projection"] = "Projection";

	// Reference types
	type_map["RID"] = "Rid";
	type_map["Array"] = "GodotArray";
	type_map["Dictionary"] = "GodotDictionary";
	type_map["Callable"] = "Callable";
	type_map["Signal"] = "Signal";

	// Packed arrays
	type_map["PackedByteArray"] = "uint8[]";
	type_map["PackedInt32Array"] = "int32[]";
	type_map["PackedInt64Array"] = "int64[]";
	type_map["PackedFloat32Array"] = "float[]";
	type_map["PackedFloat64Array"] = "double[]";
	type_map["PackedStringArray"] = "System.String[]";
	type_map["PackedVector2Array"] = "Vector2[]";
	type_map["PackedVector3Array"] = "Vector3[]";
	type_map["PackedVector4Array"] = "Vector4[]";
	type_map["PackedColorArray"] = "Color[]";
}

String BeefBindingsGenerator::_get_beef_type(const String &p_godot_type) const {
	// Strip leading/trailing whitespace and enum prefixes
	String t = p_godot_type.strip_edges();

	// Handle enum types like "Node.ProcessMode"
	if (t.contains(".")) {
		return t; // Already qualified
	}

	const String *mapped = type_map.getptr(t);
	if (mapped) {
		return *mapped;
	}

	// Assume it's a Godot class — use as-is (class names are already PascalCase)
	return t;
}

// ─── ptrcall helpers ──────────────────────────────────────────────────────────

// Returns true if a Beef type name can be passed/returned via raw ptrcall pointer.
// These are all fixed-size structs / primitives stored inline in Variant.
static bool _beef_type_is_ptrcall_simple(const String &p_beef_type) {
	return p_beef_type == "bool" ||
			p_beef_type == "int8" || p_beef_type == "int16" ||
			p_beef_type == "int32" || p_beef_type == "int64" ||
			p_beef_type == "uint8" || p_beef_type == "uint16" ||
			p_beef_type == "uint32" || p_beef_type == "uint64" ||
			p_beef_type == "char16" || p_beef_type == "char32" ||
			p_beef_type == "float" || p_beef_type == "double" ||
			// Variant/Callable/Signal are opaque buffers sized to the engine type, so they cross
			// ptrcall by value just like a fixed-size struct (the user owns/destroys their contents).
			p_beef_type == "Variant" ||
			p_beef_type == "Callable" || p_beef_type == "Signal" ||
			p_beef_type == "Vector2" || p_beef_type == "Vector2I" ||
			p_beef_type == "Vector3" || p_beef_type == "Vector3I" ||
			p_beef_type == "Vector4" || p_beef_type == "Vector4I" ||
			p_beef_type == "Rect2" || p_beef_type == "Rect2I" ||
			p_beef_type == "Color" || p_beef_type == "Plane" ||
			p_beef_type == "Aabb" || p_beef_type == "Basis" ||
			p_beef_type == "Quaternion" || p_beef_type == "Transform2D" ||
			p_beef_type == "Transform3D" || p_beef_type == "Projection" ||
			p_beef_type == "Rid";
}

String BeefBindingsGenerator::_get_int_beef_type(GodotTypeInfo::Metadata p_meta) {
	switch (p_meta) {
		case GodotTypeInfo::METADATA_INT_IS_INT8:
			return "int8";
		case GodotTypeInfo::METADATA_INT_IS_INT16:
			return "int16";
		case GodotTypeInfo::METADATA_INT_IS_INT32:
			return "int32";
		case GodotTypeInfo::METADATA_INT_IS_INT64:
			return "int64";
		case GodotTypeInfo::METADATA_INT_IS_UINT8:
			return "uint8";
		case GodotTypeInfo::METADATA_INT_IS_UINT16:
			return "uint16";
		case GodotTypeInfo::METADATA_INT_IS_UINT32:
			return "uint32";
		case GodotTypeInfo::METADATA_INT_IS_UINT64:
			return "uint64";
		case GodotTypeInfo::METADATA_INT_IS_CHAR16:
			return "char16";
		case GodotTypeInfo::METADATA_INT_IS_CHAR32:
			return "char32";
		default:
			// No metadata: assume Variant-native 64-bit width.
			return "int64";
	}
}

String BeefBindingsGenerator::_get_float_beef_type(GodotTypeInfo::Metadata p_meta) {
	switch (p_meta) {
		case GodotTypeInfo::METADATA_REAL_IS_FLOAT:
			return "float";
		case GodotTypeInfo::METADATA_REAL_IS_DOUBLE:
			return "double";
		default:
			// No metadata: assume Variant-native 64-bit width.
			return "double";
	}
}

// Beef default-value literal for argument p_idx, or "" if it has no default or the default is not
// a constant Beef expression (e.g. a Vector2 / Array default). Godot defaults are trailing; Beef
// (like C#) requires defaults to be a contiguous trailing run, so the caller only emits them when
// the whole run is expressible.
static String _beef_default_arg(MethodBind *p_mb, int p_argc, int p_idx, const String &p_beef_type) {
	if (!p_mb) {
		return String();
	}
	int defc = p_mb->get_default_argument_count();
	if (p_idx < p_argc - defc) {
		return String(); // this argument has no default
	}
	Variant def = p_mb->get_default_argument(p_idx);
	// A Variant-typed parameter can only express a NIL default (as `default`); a concrete
	// value (int/float/string/bool) cannot be cast to Variant in a constant default expression.
	if (p_beef_type == "Variant" && def.get_type() != Variant::NIL) {
		return String();
	}
	switch (def.get_type()) {
		case Variant::BOOL:
			return ((bool)def) ? "true" : "false";
		case Variant::INT: {
			int64_t v = (int64_t)def;
			// Int-primitive params take the bare number; an enum param takes a cast from the value.
			if (p_beef_type == "int8" || p_beef_type == "int16" || p_beef_type == "int32" ||
					p_beef_type == "int64" || p_beef_type == "int" || p_beef_type == "uint8" ||
					p_beef_type == "uint16" || p_beef_type == "uint32" || p_beef_type == "uint64" ||
					p_beef_type == "char16" || p_beef_type == "char32") {
				return itos(v);
			}
			return "(" + p_beef_type + ")" + itos(v);
		}
		case Variant::FLOAT: {
			String s = rtos((double)def);
			return (p_beef_type == "float") ? (s + "f") : s;
		}
		case Variant::STRING:
			return "\"" + String(def).c_escape() + "\"";
		case Variant::NIL:
			// Null default: a Variant param gets the nil Variant, an object param gets null.
			return (p_beef_type == "Variant") ? "default" : "null";
		default:
			return String(); // Vector / Array / Dictionary / etc.: not a constant Beef literal
	}
}

String BeefBindingsGenerator::_resolve_type(MethodBind *p_mb, const MethodInfo &p_mi, int p_idx) const {
	const PropertyInfo &pi = (p_idx < 0) ? p_mi.return_val : p_mi.arguments[p_idx];

	if (pi.type == Variant::NIL && pi.class_name == StringName()) {
		// NIL_IS_VARIANT marks a slot that accepts/returns any Variant; a plain return slot with no
		// type is truly void. (Variant crosses ptrcall by value as an opaque buffer — see
		// _beef_type_is_ptrcall_simple — so a Variant return/arg marshals as MK_SIMPLE, not a stub.)
		if (pi.usage & PROPERTY_USAGE_NIL_IS_VARIANT) {
			return "Variant";
		}
		return p_idx < 0 ? "void" : "Variant";
	}
	// Enum-typed integers carry the enum name in class_name (e.g. "Node.ProcessMode").
	if (pi.type == Variant::INT && pi.class_name != StringName()) {
		return _get_beef_type(String(pi.class_name));
	}
	// MethodInfo from get_method_list() does not carry width metadata — read it from the
	// MethodBind so int32/float methods are not silently widened to int64/double.
	// MethodBind::get_argument_meta only exists in DEBUG_ENABLED builds (as in mono); fall back
	// to the (usually empty) MethodInfo metadata otherwise.
	GodotTypeInfo::Metadata meta = (GodotTypeInfo::Metadata)p_mi.get_argument_meta(p_idx);
#ifdef DEBUG_ENABLED
	if (p_mb) {
		meta = p_mb->get_argument_meta(p_idx);
	}
#endif
	if (pi.type == Variant::INT) {
		return _get_int_beef_type(meta);
	}
	if (pi.type == Variant::FLOAT) {
		return _get_float_beef_type(meta);
	}
	// Array/Dictionary may carry element-type hints in class_name (typed arrays) — resolve from
	// the Variant type, not class_name, so they map cleanly to GodotArray/GodotDictionary.
	if (pi.type == Variant::ARRAY) {
		return "GodotArray";
	}
	if (pi.type == Variant::DICTIONARY) {
		return "GodotDictionary";
	}
	return _get_beef_type(pi.class_name != StringName()
			? String(pi.class_name)
			: Variant::get_type_name(pi.type));
}

String BeefBindingsGenerator::_arg_name(const PropertyInfo &p_arg, int p_idx) {
	if (p_arg.name.is_empty()) {
		return "arg" + itos(p_idx);
	}
	return _escape_beef_identifier(p_arg.name);
}

BeefBindingsGenerator::MarshalKind BeefBindingsGenerator::_marshal_kind(MethodBind *p_mb, const MethodInfo &p_mi, int p_idx) const {
	String t = _resolve_type(p_mb, p_mi, p_idx);
	if (t == "void") {
		return MK_VOID;
	}
	if (_beef_type_is_ptrcall_simple(t)) {
		return MK_SIMPLE;
	}
	const PropertyInfo &pi = (p_idx < 0) ? p_mi.return_val : p_mi.arguments[p_idx];
	// Enum-typed values resolve to a named Beef enum (not in the primitive list) but are int-backed
	// value types; the generated enums are `: int64`, matching Godot's int64 enum ptrcall encoding.
	if (pi.type == Variant::INT) {
		return MK_SIMPLE;
	}
	if (pi.type == Variant::STRING) {
		return MK_STRING;
	}
	if (pi.type == Variant::STRING_NAME) {
		return MK_STRINGNAME;
	}
	if (pi.type == Variant::NODE_PATH) {
		return MK_NODEPATH;
	}
	if (pi.type == Variant::OBJECT) {
		return MK_OBJECT;
	}
	if (pi.type == Variant::ARRAY) {
		return MK_ARRAY;
	}
	if (pi.type == Variant::DICTIONARY) {
		return MK_DICTIONARY;
	}
	// POD packed arrays marshal by contiguous memcpy; PackedStringArray (non-POD) takes its own
	// per-element path (MK_PACKED_STRING).
	switch (pi.type) {
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
			return MK_PACKED;
		case Variant::PACKED_STRING_ARRAY:
			return MK_PACKED_STRING;
		default:
			break;
	}
	return MK_UNSUPPORTED;
}

bool BeefBindingsGenerator::_strlike_marshal(MarshalKind p_kind, String &r_storage, String &r_prefix) {
	if (p_kind == MK_STRING) {
		r_storage = "GodotStr";
		r_prefix = "Str";
		return true;
	}
	if (p_kind == MK_STRINGNAME) {
		r_storage = "GodotSN";
		r_prefix = "SN";
		return true;
	}
	if (p_kind == MK_NODEPATH) {
		r_storage = "GodotNP";
		r_prefix = "NP";
		return true;
	}
	return false;
}

bool BeefBindingsGenerator::_can_marshal(MethodBind *p_mb, const MethodInfo &mi) const {
	// No real MethodBind (e.g. methods registered via add_virtual_method like "free") — there is
	// nothing to ptrcall, so calling one would dereference a null bind. Such methods become stubs.
	if (!p_mb) {
		return false;
	}
	if (mi.flags & (METHOD_FLAG_VIRTUAL | METHOD_FLAG_VARARG)) {
		return false;
	}
	// Return: void / simple / string / stringname / object / array / dictionary.
	MarshalKind rk = _marshal_kind(p_mb, mi, -1);
	if (rk == MK_UNSUPPORTED) {
		return false;
	}
	// Arguments: simple / string / stringname / object / array / dictionary (not void).
	for (int i = 0; i < (int)mi.arguments.size(); i++) {
		MarshalKind k = _marshal_kind(p_mb, mi, i);
		if (k == MK_VOID || k == MK_UNSUPPORTED) {
			return false;
		}
	}
	return true;
}

String BeefBindingsGenerator::_ret_arg_expr(MarshalKind p_rk) {
	switch (p_rk) {
		case MK_STRING:
		case MK_STRINGNAME:
		case MK_NODEPATH:
			return "&_rs";
		case MK_OBJECT:
			return "&_ro";
		case MK_ARRAY:
		case MK_DICTIONARY:
		case MK_PACKED:
		case MK_PACKED_STRING:
		case MK_SIMPLE:
			return "&_ret";
		default:
			return "null"; // void / unsupported
	}
}

void BeefBindingsGenerator::_emit_return_storage(StringBuilder &p_out, const String &ind, MarshalKind p_rk, const String &p_type, int p_variant_type) {
	String stor, pre;
	if (_strlike_marshal(p_rk, stor, pre)) {
		p_out.append(ind + stor + " _rs = ?;\n" + ind + "Native." + pre + "Empty(&_rs);\n");
	} else if (p_rk == MK_OBJECT) {
		p_out.append(ind + "void* _ro = null;\n");
	} else if (p_rk == MK_ARRAY) {
		p_out.append(ind + "GodotArray _ret = ?;\n" + ind + "Native.ArrNew(&_ret);\n");
	} else if (p_rk == MK_DICTIONARY) {
		p_out.append(ind + "GodotDictionary _ret = ?;\n" + ind + "Native.DictNew(&_ret);\n");
	} else if (p_rk == MK_PACKED) {
		p_out.append(ind + "GodotPacked _ret = ?;\n" + ind + "Native.PackedEmpty(&_ret, " + itos(p_variant_type) + ");\n");
	} else if (p_rk == MK_PACKED_STRING) {
		p_out.append(ind + "GodotPacked _ret = ?;\n" + ind + "Native.PSAEmpty(&_ret);\n");
	} else if (p_rk == MK_SIMPLE) {
		p_out.append(ind + p_type + " _ret = default;\n");
	}
}

void BeefBindingsGenerator::_emit_return_read(StringBuilder &p_out, const String &ind, MarshalKind p_rk, const String &p_type, int p_variant_type) {
	String stor, pre;
	if (_strlike_marshal(p_rk, stor, pre)) {
		p_out.append(ind + "var _r = Native." + pre + "Out(&_rs);\n" + ind + "Native." + pre + "Del(&_rs);\n" + ind + "return _r;\n");
	} else if (p_rk == MK_OBJECT) {
		// RefCounted returns arrive +1 from the ptrcall; the ref-aware wrapper keeps exactly one ref.
		bool is_ref = ClassDB::is_parent_class(StringName(p_type), StringName("RefCounted"));
		String wrap = is_ref ? "WrapRefCounted" : "WrapObject";
		p_out.append(ind + "return (" + p_type + ")Native." + wrap + "(_ro, \"" + p_type + "\");\n");
	} else if (p_rk == MK_PACKED) {
		String elem = p_type.trim_suffix("[]");
		String tag = itos(p_variant_type);
		p_out.append(ind + "var _r = Native.PackedOut<" + elem + ">(&_ret, " + tag + ");\n" + ind + "Native.PackedDel(&_ret, " + tag + ");\n" + ind + "return _r;\n");
	} else if (p_rk == MK_PACKED_STRING) {
		p_out.append(ind + "var _r = Native.PSAOut(&_ret);\n" + ind + "Native.PSADel(&_ret);\n" + ind + "return _r;\n");
	} else if (p_rk == MK_SIMPLE || p_rk == MK_ARRAY || p_rk == MK_DICTIONARY) {
		// Array/Dictionary are returned by value (the handle shares the engine container's pointer);
		// the caller owns it and Disposes when done.
		p_out.append(ind + "return _ret;\n");
	}
}

void BeefBindingsGenerator::_emit_arg_prep(StringBuilder &p_out, const String &ind, MarshalKind p_ak, const String &src, const String &suf, const String &p_type, int p_variant_type, String &r_ptr_expr, String &r_cleanup) {
	String stor, pre;
	if (_strlike_marshal(p_ak, stor, pre)) {
		// String / StringName / NodePath: build an engine value, pass it, destroy it after.
		p_out.append(ind + stor + " _s" + suf + " = ?;\n");
		p_out.append(ind + "Native." + pre + "In(" + src + ", &_s" + suf + ");\n");
		r_ptr_expr = "&_s" + suf;
		r_cleanup = ind + "Native." + pre + "Del(&_s" + suf + ");\n";
	} else if (p_ak == MK_OBJECT) {
		p_out.append(ind + "void* _o" + suf + " = (" + src + " != null) ? " + src + "._godotOwner : null;\n");
		r_ptr_expr = "&_o" + suf;
	} else if (p_ak == MK_PACKED) {
		String elem = p_type.trim_suffix("[]");
		String tag = itos(p_variant_type);
		p_out.append(ind + "GodotPacked _p" + suf + " = ?;\n");
		p_out.append(ind + "Native.PackedIn<" + elem + ">(" + src + ", " + tag + ", &_p" + suf + ");\n");
		r_ptr_expr = "&_p" + suf;
		r_cleanup = ind + "Native.PackedDel(&_p" + suf + ", " + tag + ");\n";
	} else if (p_ak == MK_PACKED_STRING) {
		p_out.append(ind + "GodotPacked _p" + suf + " = ?;\n");
		p_out.append(ind + "Native.PSAIn(" + src + ", &_p" + suf + ");\n");
		r_ptr_expr = "&_p" + suf;
		r_cleanup = ind + "Native.PSADel(&_p" + suf + ");\n";
	} else { // MK_SIMPLE — copy the immutable param to a mutable, addressable local.
		p_out.append(ind + "var _v" + suf + " = " + src + ";\n");
		r_ptr_expr = "&_v" + suf;
	}
}

// ─── BeefBindingsGenerator ────────────────────────────────────────────────────

BeefBindingsGenerator::BeefBindingsGenerator() {
	_init_type_map();
	initialized = true;
}

void BeefBindingsGenerator::_append_enums(const StringName &p_class_name, StringBuilder &p_output) {
	List<StringName> enum_names;
	ClassDB::get_enum_list(p_class_name, &enum_names, true);

	for (const StringName &enum_name : enum_names) {
		p_output.append("\t[System.AllowDuplicates]\n\tpublic enum ");
		p_output.append(String(enum_name));
		p_output.append(" : int64\n\t{\n");

		List<StringName> value_names;
		ClassDB::get_enum_constants(p_class_name, enum_name, &value_names, true);

		for (const StringName &val_name : value_names) {
			int64_t val = ClassDB::get_integer_constant(p_class_name, val_name);
			p_output.append("\t\t");
			p_output.append(String(val_name));
			p_output.append(" = ");
			p_output.append(itos(val));
			p_output.append(",\n");
		}

		p_output.append("\t}\n\n");
	}
}

void BeefBindingsGenerator::_append_constants(const StringName &p_class_name, StringBuilder &p_output) {
	List<String> constant_names;
	ClassDB::get_integer_constant_list(p_class_name, &constant_names, true);

	// Skip constants that belong to enums
	List<StringName> enum_names;
	ClassDB::get_enum_list(p_class_name, &enum_names, true);
	HashSet<StringName> enum_constants_set;
	for (const StringName &en : enum_names) {
		List<StringName> vals;
		ClassDB::get_enum_constants(p_class_name, en, &vals, true);
		for (const StringName &v : vals) {
			enum_constants_set.insert(v);
		}
	}

	for (const String &const_name : constant_names) {
		if (enum_constants_set.has(const_name)) {
			continue;
		}
		int64_t val = ClassDB::get_integer_constant(p_class_name, const_name);
		p_output.append("\tpublic const int64 ");
		p_output.append(const_name);
		p_output.append(" = ");
		p_output.append(itos(val));
		p_output.append(";\n");
	}
}

void BeefBindingsGenerator::_emit_method_bind_fields(StringBuilder &p_output) {
	if (_class_binds.is_empty()) {
		return;
	}
	// Sort for deterministic output across runs.
	Vector<String> names;
	for (const String &n : _class_binds) {
		names.push_back(n);
	}
	names.sort();

	// NOTE: only ASCII may be appended to a StringBuilder — as_string() copies const char*
	// byte-by-byte into a char32_t buffer, so any byte >= 0x80 becomes an invalid codepoint.
	p_output.append("\n\t// Cached MethodBind pointers (filled lazily on first call)\n");
	for (const String &n : names) {
		p_output.append("\tstatic void* _mb_");
		p_output.append(n);
		p_output.append(";\n");
	}
}

void BeefBindingsGenerator::_append_methods(const StringName &p_class_name, StringBuilder &p_output) {
	List<MethodInfo> methods;
	ClassDB::get_method_list(p_class_name, &methods, true, true);

	// Names of virtual methods declared by any ancestor. A class may re-register a virtual its base
	// also declares (e.g. PhysicsServer2DExtension and PhysicsServer2D both expose `_init`); the
	// derived binding must `override` it, not redeclare `virtual` (which hides it — Beef BF0114).
	HashSet<String> inherited_virtuals;
	{
		StringName parent = ClassDB::get_parent_class(p_class_name);
		if (parent != StringName()) {
			List<MethodInfo> parent_methods;
			ClassDB::get_method_list(parent, &parent_methods, false, true);
			for (const MethodInfo &pmi : parent_methods) {
				if (pmi.flags & METHOD_FLAG_VIRTUAL) {
					inherited_virtuals.insert(String(pmi.name));
				}
			}
		}
	}

	for (const MethodInfo &mi : methods) {
		// Skip internal/private methods
		if (String(mi.name).begins_with("_") && !(mi.flags & METHOD_FLAG_VIRTUAL)) {
			continue;
		}

		// The MethodBind supplies the int/float width metadata that MethodInfo lacks.
		MethodBind *mb = ClassDB::get_method(p_class_name, mi.name);

		// Return type
		String return_type = _resolve_type(mb, mi, -1);

		// Method visibility and modifiers
		bool is_virtual = (mi.flags & METHOD_FLAG_VIRTUAL) != 0;
		bool is_static = (mi.flags & METHOD_FLAG_STATIC) != 0;
		bool is_vararg = (mi.flags & METHOD_FLAG_VARARG) != 0;

		// Virtual methods with leading '_' keep it to avoid collision with same-named
		// non-virtual methods (e.g. _set → _Set, set → Set — no duplicate).
		String mi_name = String(mi.name);
		String method_name;
		if (mi_name.begins_with("_") && is_virtual) {
			method_name = "_" + _to_pascal_case(mi_name.substr(1));
		} else {
			method_name = _to_pascal_case(mi_name);
		}

		p_output.append("\tpublic ");
		if (is_static) {
			p_output.append("static ");
		} else if (is_virtual) {
			// Override an ancestor's virtual of the same name instead of hiding it.
			p_output.append(inherited_virtuals.has(mi_name) ? "override " : "virtual ");
		}
		p_output.append(return_type);
		p_output.append(" ");
		p_output.append(method_name);
		p_output.append("(");

		// Parameters
		int arg_total = (int)mi.arguments.size();
		// Emit Godot's trailing default arguments — but only if EVERY defaulted param has a constant
		// Beef literal (Beef requires defaults to be a contiguous trailing run); else emit none.
		int def_total = (mb && !is_vararg && !is_virtual) ? mb->get_default_argument_count() : 0;
		bool emit_defaults = def_total > 0;
		if (emit_defaults) {
			for (int i = arg_total - def_total; i < arg_total; i++) {
				if (_beef_default_arg(mb, arg_total, i, _resolve_type(mb, mi, i)).is_empty()) {
					emit_defaults = false;
					break;
				}
			}
		}
		bool first = true;
		for (int i = 0; i < arg_total; i++) {
			if (!first) {
				p_output.append(", ");
			}
			first = false;

			p_output.append(_resolve_type(mb, mi, i));
			p_output.append(" ");
			p_output.append(_arg_name(mi.arguments[i], i));
			if (emit_defaults && i >= arg_total - def_total) {
				p_output.append(" = ");
				p_output.append(_beef_default_arg(mb, arg_total, i, _resolve_type(mb, mi, i)));
			}
		}

		if (is_vararg) {
			if (!first) {
				p_output.append(", ");
			}
			p_output.append("params Variant[] @args");
		}

		p_output.append(")");

		if (mi_name == "free" && !is_static && mi.arguments.is_empty() && return_type == "void") {
			// 'free' has no real MethodBind (add_virtual_method); route it to a direct memdelete.
			p_output.append("\n\t{\n\t\tif (_godotOwner != null) Native.FreeObject(_godotOwner);\n\t}\n");
		} else if (is_virtual) {
			// Virtual: user overrides these — provide an empty default implementation.
			if (return_type == "void") {
				p_output.append(" { }\n");
			} else {
				p_output.append("\n\t{\n\t\treturn default;\n\t}\n");
			}
		} else if (is_vararg && !is_static && (return_type == "void" || return_type == "Variant" || return_type == "Error")) {
			// Variadic dispatch via Object::callp (rpc, call, emit_signal, ...). Leading fixed params
			// are boxed into Variants, then the params Variant[] are appended; everything is passed as
			// an array of Variant* to the engine. Only String/StringName/Object/int leading params are
			// supported (the only kinds the API actually uses); anything else falls back to the stub.
			int lead = (int)mi.arguments.size();
			Vector<String> conv;
			bool supported = true;
			for (int i = 0; i < lead && supported; i++) {
				String an = _arg_name(mi.arguments[i], i);
				MarshalKind ak = _marshal_kind(mb, mi, i);
				String t = _resolve_type(mb, mi, i);
				if (ak == MK_STRING || ak == MK_STRINGNAME) {
					conv.push_back("Godot.Variant.FromString(" + an + ")");
				} else if (ak == MK_OBJECT) {
					conv.push_back("Godot.Variant.FromObject(" + an + ")");
				} else if (ak == MK_SIMPLE && (t == "int64" || t == "int32" || t == "int" || t == "int16" || t == "int8" || t == "uint64" || t == "uint32" || t == "uint16" || t == "uint8")) {
					conv.push_back("Godot.Variant.FromInt((int64)" + an + ")");
				} else {
					supported = false;
				}
			}
			if (!supported) {
				p_output.append("\n\t{\n\t\t// TODO: call native (vararg with unsupported leading arg)\n");
				if (return_type != "void") {
					p_output.append("\t\treturn default;\n");
				}
				p_output.append("\t}\n");
			} else {
				p_output.append("\n\t{\n");
				p_output.append("\t\tint32 __n = " + itos(lead) + " + (int32)@args.Count;\n");
				p_output.append("\t\tvoid*[] __ptrs = scope void*[(__n > 0) ? __n : 1];\n");
				for (int i = 0; i < lead; i++) {
					String si = itos(i);
					p_output.append("\t\tvar __l" + si + " = " + conv[i] + ";\n");
					p_output.append("\t\t__ptrs[" + si + "] = &__l" + si + ";\n");
				}
				p_output.append("\t\tfor (int __i = 0; __i < @args.Count; __i++) __ptrs[" + itos(lead) + " + __i] = &@args[__i];\n");
				p_output.append("\t\tGodot.Variant __ret = default;\n");
				p_output.append("\t\tNative.ObjectCall(_godotOwner, \"" + mi_name + "\", (__n > 0) ? &__ptrs[0] : null, __n, &__ret);\n");
				for (int i = 0; i < lead; i++) {
					p_output.append("\t\t__l" + itos(i) + ".Dispose();\n");
				}
				// The variadic call consumes its args: dispose each so a String/Object arg's Variant
				// resource is freed (a float/int arg holds nothing inline, so this is a no-op there).
				p_output.append("\t\tfor (int __i = 0; __i < @args.Count; __i++) @args[__i].Dispose();\n");
				if (return_type == "void") {
					p_output.append("\t\t__ret.Dispose();\n");
				} else if (return_type == "Variant") {
					p_output.append("\t\treturn __ret;\n");
				} else { // Error (or another int-backed enum)
					p_output.append("\t\tlet __r = (" + return_type + ")__ret.AsInt();\n");
					p_output.append("\t\t__ret.Dispose();\n");
					p_output.append("\t\treturn __r;\n");
				}
				p_output.append("\t}\n");
			}
		} else if (_can_marshal(mb, mi)) {
			_class_binds.insert(mi_name);
			p_output.append("\n\t{\n");

			// Lazy-fetch the MethodBind pointer.
			p_output.append("\t\tif (_mb_");
			p_output.append(mi_name);
			p_output.append(" == null)\n\t\t\t_mb_");
			p_output.append(mi_name);
			p_output.append(" = Native.GetMethodBind(\"");
			p_output.append(String(p_class_name));
			p_output.append("\", \"");
			p_output.append(mi_name);
			p_output.append("\");\n");

			String obj_expr = is_static ? "null" : "_godotOwner";
			int arg_count = mi.arguments.size();

			// Per-argument prep: each produces a void* (ptr_exprs[i]) into the data ptrcall wants.
			// String args build an engine value and need post-call cleanup.
			Vector<String> ptr_exprs;
			Vector<String> cleanups;
			for (int i = 0; i < arg_count; i++) {
				String an = _arg_name(mi.arguments[i], i);
				MarshalKind ak = _marshal_kind(mb, mi, i);
				String ptr_expr, cleanup;
				_emit_arg_prep(p_output, "\t\t", ak, an, itos(i), _resolve_type(mb, mi, i), (int)mi.arguments[i].type, ptr_expr, cleanup);
				ptr_exprs.push_back(ptr_expr);
				if (!cleanup.is_empty()) {
					cleanups.push_back(cleanup);
				}
			}

			// Return storage.
			MarshalKind rk = _marshal_kind(mb, mi, -1);
			_emit_return_storage(p_output, "\t\t", rk, return_type, (int)mi.return_val.type);
			String ret_arg = _ret_arg_expr(rk);

			// Arguments array.
			String args_arg = "null";
			if (arg_count > 0) {
				p_output.append("\t\tvoid*[" + itos(arg_count) + "] _args;\n");
				for (int i = 0; i < arg_count; i++) {
					p_output.append("\t\t_args[" + itos(i) + "] = " + ptr_exprs[i] + ";\n");
				}
				args_arg = "&_args[0]";
			}

			// The call.
			p_output.append("\t\tNative.MethodBindPtrcall(_mb_" + mi_name + ", " + obj_expr + ", " + args_arg + ", " + ret_arg + ");\n");

			// Cleanup engine values built for arguments.
			for (const String &c : cleanups) {
				p_output.append(c);
			}

			_emit_return_read(p_output, "\t\t", rk, return_type, (int)mi.return_val.type);
			p_output.append("\t}\n");
		} else {
			// Fallback stub for methods with no real MethodBind to ptrcall (e.g. methods registered
			// only via MethodInfo). All ordinary args/returns — including Variant/RID/StringName/
			// NodePath/Object/Array/Dictionary/packed — are handled by the _can_marshal branch above.
			p_output.append("\n\t{\n\t\t// no MethodBind to ptrcall\n");
			if (return_type != "void") {
				p_output.append("\t\treturn default;\n");
			}
			p_output.append("\t}\n");
		}
		p_output.append("\n");
	}
}

void BeefBindingsGenerator::_append_properties(const StringName &p_class_name, StringBuilder &p_output) {
	List<PropertyInfo> props;
	ClassDB::get_property_list(p_class_name, &props, true);

	for (const PropertyInfo &pi : props) {
		// Skip editor grouping / internal
		if (pi.usage & (PROPERTY_USAGE_INTERNAL | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_SUBGROUP)) {
			continue;
		}
		// Mirror C#: skip NIL-typed array group markers (type==NIL && PROPERTY_USAGE_ARRAY)
		if (pi.type == Variant::NIL && (pi.usage & PROPERTY_USAGE_ARRAY)) {
			continue;
		}
		// Skip sub-properties like "slot_0/enabled" — inspector-only, no direct accessor
		if (pi.name.contains("/")) {
			continue;
		}

		// ── Resolve type from getter/setter (mirrors C# Mono approach) ──────────
		// pi.class_name is unreliable: array-count properties store their hint format
		// ("Settings,settings/") there, and multi-type props store comma-separated class
		// names ("CameraAttributesPractical,CameraAttributesPhysical"). Using the actual
		// getter return type (or setter arg type) gives a clean, single type name.
		StringName getter_name = ClassDB::get_property_getter(p_class_name, pi.name);
		StringName setter_name = ClassDB::get_property_setter(p_class_name, pi.name);

		if (getter_name == StringName() && setter_name == StringName()) {
			continue; // No accessor — not a scriptable property
		}

		// The MethodBind carries int/float width metadata that MethodInfo lacks.
		MethodBind *getter_mb = getter_name != StringName() ? ClassDB::get_method(p_class_name, getter_name) : nullptr;
		MethodBind *setter_mb = setter_name != StringName() ? ClassDB::get_method(p_class_name, setter_name) : nullptr;

		MethodInfo getter_mi;
		bool have_getter_mi = getter_name != StringName() && ClassDB::get_method_info(p_class_name, getter_name, &getter_mi);
		MethodInfo setter_mi;
		bool have_setter_mi = setter_name != StringName() && ClassDB::get_method_info(p_class_name, setter_name, &setter_mi);

		// Indexed accessors (getter takes an index; setter takes index + value) can't be expressed as
		// a plain Beef property. Skip them entirely rather than emit a dead stub — the underlying
		// getter/setter methods are generated separately and remain callable.
		bool getter_indexed = getter_name != StringName() && have_getter_mi && getter_mi.arguments.size() > 0;
		bool setter_indexed = setter_name != StringName() && have_setter_mi && setter_mi.arguments.size() != 1;
		if (getter_indexed || setter_indexed) {
			continue;
		}

		// Prefer the getter return type; fall back to the setter's last (value) argument.
		// (Indexed setters carry the index first and the value last.)
		String prop_type;
		if (have_getter_mi && (getter_mi.return_val.type != Variant::NIL || getter_mi.return_val.class_name != StringName())) {
			prop_type = _resolve_type(getter_mb, getter_mi, -1);
		} else if (have_setter_mi && setter_mi.arguments.size() > 0) {
			prop_type = _resolve_type(setter_mb, setter_mi, setter_mi.arguments.size() - 1);
		}

		if (prop_type.is_empty() || prop_type == "void") {
			continue;
		}

		// Safety net: reject anything that isn't a plain Beef identifier
		if (prop_type.contains(",") || prop_type.contains("/") || prop_type.contains(" ")) {
			continue;
		}

		String prop_name = _to_pascal_case(pi.name);
		if (prop_name.is_empty()) {
			continue;
		}

		// ── Determine if we can implement getter/setter ──────────────────────
		// Getter must take no arguments; setter must take exactly the value argument.
		// Indexed accessors need special dispatch and fall back to stubs for now.
		// _can_marshal (not _can_ptrcall) so Object/String/Array property accessors work too.
		bool can_get = have_getter_mi && getter_mi.arguments.size() == 0 && _can_marshal(getter_mb, getter_mi);
		bool can_set = have_setter_mi && setter_mi.arguments.size() == 1 && _can_marshal(setter_mb, setter_mi);

		p_output.append("\tpublic ");
		p_output.append(prop_type);
		p_output.append(" ");
		p_output.append(prop_name);
		p_output.append("\n\t{\n");

		// Getter
		if (getter_name != StringName()) {
			if (can_get) {
				String gn = String(getter_name);
				_class_binds.insert(gn);
				MarshalKind rk = _marshal_kind(getter_mb, getter_mi, -1);
				p_output.append("\t\tget\n\t\t{\n");
				p_output.append("\t\t\tif (_mb_" + gn + " == null)\n\t\t\t\t_mb_" + gn + " = Native.GetMethodBind(\"" + String(p_class_name) + "\", \"" + gn + "\");\n");
				_emit_return_storage(p_output, "\t\t\t", rk, prop_type, (int)getter_mi.return_val.type);
				p_output.append("\t\t\tNative.MethodBindPtrcall(_mb_" + gn + ", _godotOwner, null, " + _ret_arg_expr(rk) + ");\n");
				_emit_return_read(p_output, "\t\t\t", rk, prop_type, (int)getter_mi.return_val.type);
				p_output.append("\t\t}\n");
			} else {
				p_output.append("\t\tget { return default; } // TODO: call native getter\n");
			}
		}

		// Setter
		if (setter_name != StringName()) {
			if (can_set) {
				String sn = String(setter_name);
				_class_binds.insert(sn);
				int last = setter_mi.arguments.size() - 1;
				MarshalKind ak = _marshal_kind(setter_mb, setter_mi, last);
				p_output.append("\t\tset\n\t\t{\n");
				p_output.append("\t\t\tif (_mb_" + sn + " == null)\n\t\t\t\t_mb_" + sn + " = Native.GetMethodBind(\"" + String(p_class_name) + "\", \"" + sn + "\");\n");
				String ptr_expr, cleanup;
				_emit_arg_prep(p_output, "\t\t\t", ak, "value", "0", prop_type, (int)setter_mi.arguments[last].type, ptr_expr, cleanup);
				p_output.append("\t\t\tvoid* _a0 = " + ptr_expr + ";\n");
				p_output.append("\t\t\tNative.MethodBindPtrcall(_mb_" + sn + ", _godotOwner, &_a0, null);\n");
				p_output.append(cleanup);
				p_output.append("\t\t}\n");
			} else {
				p_output.append("\t\tset { } // TODO: call native setter\n");
			}
		}

		p_output.append("\t}\n\n");
	}
}

Error BeefBindingsGenerator::_generate_bf_class(const StringName &p_class_name, StringBuilder &p_output) {
	StringName parent_class = ClassDB::get_parent_class(p_class_name);

	p_output.append("namespace Godot;\n\n");
	p_output.append("public class ");
	p_output.append(String(p_class_name));

	if (parent_class != StringName()) {
		p_output.append(" : ");
		p_output.append(String(parent_class));
	}

	p_output.append("\n{\n");

	// Object is the root of the Godot class hierarchy and owns _godotOwner; all subclasses inherit
	// it. Two constructors on every wrapper: a parameterless one (back-compat) and one taking the
	// owner pointer. Because Beef runs the ROOT constructor before any derived field initializers
	// or constructors, threading the owner up to Object's ctor makes _godotOwner valid throughout
	// derived construction (so a derived ctor/field-initializer can call engine methods on itself).
	if (parent_class == StringName()) {
		// Root (Object).
		p_output.append("\t/// Opaque pointer to the Godot-side object.\n");
		p_output.append("\tpublic void* _godotOwner;\n\n");
		p_output.append("\tpublic this() { }\n");
		p_output.append("\tpublic this(void* owner) { _godotOwner = owner; }\n\n");
		// Equality by the underlying engine object, so distinct wrappers/handles of the same object
		// compare equal. UnsafeCastToPtr is a null-safe reinterpret (not the == operator), so the
		// null checks don't recurse back into operator==.
		p_output.append("\tpublic static bool operator==(Godot.Object a, Godot.Object b)\n\t{\n");
		p_output.append("\t\tvoid* pa = (System.Internal.UnsafeCastToPtr(a) != null) ? a._godotOwner : null;\n");
		p_output.append("\t\tvoid* pb = (System.Internal.UnsafeCastToPtr(b) != null) ? b._godotOwner : null;\n");
		p_output.append("\t\treturn pa == pb;\n\t}\n");
		p_output.append("\tpublic static bool operator!=(Godot.Object a, Godot.Object b) { return !(a == b); }\n\n");
	} else {
		p_output.append("\tpublic this() { }\n");
		p_output.append("\tpublic this(void* owner) : base(owner) { }\n\n");
	}

	// Object creation + RefCounted ownership.
	//  - Node/plain Object: New() returns the cached wrapper; caller manages lifetime (tree / Free()).
	//  - RefCounted: New() returns an OWNING handle holding one engine reference, released by the
	//    destructor (delete / scope / defer delete). Clone() takes another owning reference. The
	//    RefCounted base gets a ~this that unrefs; subclasses inherit it via the destructor chain.
	{
		StringName rc("RefCounted");
		bool is_ref = (p_class_name == rc) || ClassDB::is_parent_class(p_class_name, rc);
		bool instantiable = ClassDB::can_instantiate(p_class_name);
		String cn = String(p_class_name);
		// Every Godot subclass re-declares static New() / instance Clone() with a more-derived
		// `Self` return type, which *hides* the inherited version. Mark these with `new` so Beef
		// doesn't emit BF0114 for each of the ~1027 classes (1000+ warnings hits BeefBuild's cap
		// and aborts the whole build). The root (no Godot base) and the first Clone() declarer
		// (RefCounted) hide nothing, so they omit the keyword.
		bool has_godot_base = !String(ClassDB::get_parent_class(p_class_name)).is_empty();
		String new_kw = has_godot_base ? "new " : "";
		if (instantiable && !is_ref) {
			p_output.append("\t/// Create a new engine " + cn + ". You own it: add it to the scene tree, or call Free().\n");
			p_output.append("\tpublic " + new_kw + "static Self New() { return (Self)Native.WrapObject(Native.Instantiate(\"" + cn + "\"), \"" + cn + "\"); }\n\n");
		} else if (instantiable && is_ref) {
			p_output.append("\t/// Create a new engine " + cn + ". Owning handle holding one reference:\n");
			p_output.append("\t/// delete it (or use scope / defer delete) when done; Clone() for another owner.\n");
			p_output.append("\tpublic " + new_kw + "static Self New() { void* __p = Native.Instantiate(\"" + cn + "\"); return (__p != null) ? new Self(__p) : null; }\n\n");
		}
		if (is_ref) {
			String clone_kw = (p_class_name == rc) ? "" : "new ";
			p_output.append("\t/// Take another owning reference to the same object. Delete each handle when done.\n");
			p_output.append("\tpublic " + clone_kw + "Self Clone() { Native.ObjRef(_godotOwner); return new Self(_godotOwner); }\n\n");
		}
		if (p_class_name == rc) {
			p_output.append("\t/// Release this handle's engine reference (frees the object at zero refs).\n");
			p_output.append("\tpublic ~this() { Native.ObjUnref(_godotOwner); }\n\n");
		}
		// Singleton accessor for classes registered as an engine singleton (Input, OS, Engine, ...).
		if (Engine::get_singleton()->has_singleton(p_class_name)) {
			p_output.append("\t/// The engine singleton instance.\n");
			p_output.append("\tpublic static Self Singleton { get { return (Self)Native.WrapObject(Native.GetSingleton(\"" + cn + "\"), \"" + cn + "\"); } }\n\n");
		}
	}

	// Reset the per-class MethodBind cache set. _append_properties / _append_methods add to it
	// at the exact point each _mb_ reference is emitted, so the field declarations emitted by
	// _emit_method_bind_fields always match the references (Beef allows members in any order).
	_class_binds.clear();

	_append_enums(p_class_name, p_output);
	_append_constants(p_class_name, p_output);
	_append_properties(p_class_name, p_output);
	_append_methods(p_class_name, p_output);
	_emit_method_bind_fields(p_output);

	p_output.append("}\n");

	return OK;
}

Error BeefBindingsGenerator::_generate_primitives_file(const String &p_output_dir) {
	String file_path = p_output_dir.path_join("GodotPrimitives.bf");
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Cannot write: " + file_path);

	// real_t is float in standard builds and double in double-precision builds. The Beef structs
	// must match the engine's C++ layout exactly, so pick the component type from the build.
	// [CRepr] forces C-compatible layout (Beef reorders fields by alignment otherwise).
	const String r = (sizeof(real_t) == 8) ? "double" : "float";

	// Opaque value types must match the engine's sizeof exactly so they occupy the right
	// space on the stack/in structs. These sizes are build-correct because the generator runs
	// in the same engine build the bindings target.
	const String variant_sz = itos((int)sizeof(Variant));
	const String callable_sz = itos((int)sizeof(Callable));
	const String signal_sz = itos((int)sizeof(Signal));

	// Variant <-> fixed-size value-type conversions, generated from a table so the type tag (the
	// engine Variant::Type) stays in lockstep with the C++ switch in _beef_variant_from/as_typed.
	// All these Beef structs are CRepr layout-matched, so the engine reads/writes their bytes directly.
	struct TypedConv {
		const char *beef;
		int tag;
	};
	const TypedConv typed_convs[] = {
		{ "Vector2", Variant::VECTOR2 }, { "Vector2I", Variant::VECTOR2I },
		{ "Rect2", Variant::RECT2 }, { "Rect2I", Variant::RECT2I },
		{ "Vector3", Variant::VECTOR3 }, { "Vector3I", Variant::VECTOR3I },
		{ "Transform2D", Variant::TRANSFORM2D }, { "Vector4", Variant::VECTOR4 },
		{ "Vector4I", Variant::VECTOR4I }, { "Plane", Variant::PLANE },
		{ "Quaternion", Variant::QUATERNION }, { "Aabb", Variant::AABB },
		{ "Basis", Variant::BASIS }, { "Transform3D", Variant::TRANSFORM3D },
		{ "Projection", Variant::PROJECTION }, { "Color", Variant::COLOR },
		{ "Rid", Variant::RID },
	};
	String variant_typed;
	for (const TypedConv &e : typed_convs) {
		const String bt = e.beef;
		const String tag = itos(e.tag);
		variant_typed += "\tpublic static Godot.Variant From" + bt + "(" + bt + " v) { Godot.Variant r = default; var t = v; Native.sVarFromTyped(&r, " + tag + ", &t); return r; }\n";
		variant_typed += "\tpublic " + bt + " As" + bt + "() { var s = this; " + bt + " o = ?; Native.sVarAsTyped(&s, " + tag + ", &o); return o; }\n";
	}

	// Packed-array conversions, table-driven the same way. Each From<T> boxes a Beef heap array
	// into a Variant; each As<T> allocates and returns a fresh Beef array the CALLER must delete.
	struct PackedConv {
		const char *name;
		const char *elem;
		int tag;
	};
	const PackedConv packed_convs[] = {
		{ "PackedByte", "uint8", Variant::PACKED_BYTE_ARRAY },
		{ "PackedInt32", "int32", Variant::PACKED_INT32_ARRAY },
		{ "PackedInt64", "int64", Variant::PACKED_INT64_ARRAY },
		{ "PackedFloat32", "float", Variant::PACKED_FLOAT32_ARRAY },
		{ "PackedFloat64", "double", Variant::PACKED_FLOAT64_ARRAY },
		{ "PackedVector2", "Vector2", Variant::PACKED_VECTOR2_ARRAY },
		{ "PackedVector3", "Vector3", Variant::PACKED_VECTOR3_ARRAY },
		{ "PackedColor", "Color", Variant::PACKED_COLOR_ARRAY },
		{ "PackedVector4", "Vector4", Variant::PACKED_VECTOR4_ARRAY },
	};
	String variant_packed;
	for (const PackedConv &e : packed_convs) {
		const String nm = e.name;
		const String el = e.elem;
		const String tag = itos(e.tag);
		variant_packed += "\tpublic static Godot.Variant From" + nm + "(" + el + "[] a) { Godot.Variant r = default; Native.sVarFromPacked(&r, " + tag + ", (a.Count > 0) ? a.Ptr : null, (int64)a.Count); return r; }\n";
		variant_packed += "\tpublic " + el + "[] As" + nm + "() { var s = this; int n = (int)Native.sVarPackedSize(&s, " + tag + "); " + el + "[] o = new " + el + "[n]; if (n > 0) Native.sVarPackedCopy(&s, " + tag + ", o.Ptr, (int64)n); return o; }\n";
	}

	f->store_string(
			"// Auto-generated by Godot-Beef. Type definitions for Godot built-in types.\n"
			"// Math structs are [CRepr] and laid out to match the engine's C++ memory layout.\n"
			"// Opaque types (Variant/String/Array/...) are pointer-sized handles, filled in by\n"
			"// later interop phases.\n"
			// using System so the compiler-intrinsic [CRepr] resolves AND applies (the qualified
			// form [System.CRepr] resolves the type but does NOT trigger the layout behavior).
			"using System;\n"
			"namespace Godot;\n"
			"\n"
			"// --- Opaque handle types (real layout added in later phases) ---\n"
			"[CRepr] public struct StringName { void* _ptr; }\n"
			"[CRepr] public struct NodePath { void* _ptr; }\n"
			"[CRepr] public struct Variant\n"
			"{\n"
			"\tuint8[" + variant_sz + "] _opaque;\n"
			"\t// Conversions to/from concrete values. A Variant holding a string/array/object owns\n"
			"\t// engine resources; call Dispose() on Variants you keep (returned ones, or From* ones).\n"
			"\tpublic Godot.Variant.Type GetVariantType() { var s = this; return (Godot.Variant.Type)Native.sVarType(&s); }\n"
			"\tpublic void Dispose() mut { Native.sVarDestroy(&this); }\n"
			"\tpublic static Godot.Variant FromBool(bool v) { Godot.Variant r = default; Native.sVarFromBool(&r, v); return r; }\n"
			"\tpublic static Godot.Variant FromInt(int64 v) { Godot.Variant r = default; Native.sVarFromInt(&r, v); return r; }\n"
			"\tpublic static Godot.Variant FromFloat(double v) { Godot.Variant r = default; Native.sVarFromFloat(&r, v); return r; }\n"
			"\tpublic static Godot.Variant FromString(System.String v) { Godot.Variant r = default; Native.sVarFromString(&r, v.CStr()); return r; }\n"
			"\tpublic static Godot.Variant FromObject(Godot.Object o) { Godot.Variant r = default; Native.sVarFromObject(&r, (o != null) ? o._godotOwner : null); return r; }\n"
			"\t// Implicit conversions so scalars can be passed where a Variant is expected (e.g. the GD\n"
			"\t// utility functions and variadic Call/Rpc). A converted temp holds a Variant resource only\n"
			"\t// for String; variadic callees consume (Dispose) their args, so these don't leak.\n"
			"\tpublic static implicit operator Godot.Variant(bool v) { return FromBool(v); }\n"
			"\tpublic static implicit operator Godot.Variant(int v) { return FromInt(v); }\n"
			"\tpublic static implicit operator Godot.Variant(int64 v) { return FromInt(v); }\n"
			"\tpublic static implicit operator Godot.Variant(float v) { return FromFloat(v); }\n"
			"\tpublic static implicit operator Godot.Variant(double v) { return FromFloat(v); }\n"
			"\tpublic static implicit operator Godot.Variant(System.String v) { return FromString(v); }\n"
			"\tpublic bool AsBool() { var s = this; return Native.sVarAsBool(&s); }\n"
			"\tpublic int64 AsInt() { var s = this; return Native.sVarAsInt(&s); }\n"
			"\tpublic double AsFloat() { var s = this; return Native.sVarAsFloat(&s); }\n"
			"\tpublic System.String AsString()\n"
			"\t{\n"
			"\t\tvar s = this;\n"
			"\t\tlet result = new System.String();\n"
			"\t\tint len = (int)Native.sVarStrLen(&s);\n"
			"\t\tif (len > 0)\n"
			"\t\t{\n"
			"\t\t\tchar8* buf = result.PrepareBuffer(len);\n"
			"\t\t\tNative.sVarStrToUtf8(&s, buf, len);\n"
			"\t\t}\n"
			"\t\treturn result;\n"
			"\t}\n"
			"\tpublic Godot.Object AsObject() { var s = this; return Native.WrapObject(Native.sVarAsObject(&s), \"Object\"); }\n" +
			variant_typed +
			// Array/Dictionary box/unbox. As* placement-constructs a fresh engine container into the
			// returned handle; the caller owns it and Disposes when done.
			"\tpublic static Godot.Variant FromArray(GodotArray a) { Godot.Variant r = default; var t = a; Native.sVarFromArray(&r, &t); return r; }\n"
			"\tpublic GodotArray AsArray() { var s = this; GodotArray o = ?; Native.sVarAsArray(&s, &o); return o; }\n"
			"\tpublic static Godot.Variant FromDictionary(GodotDictionary d) { Godot.Variant r = default; var t = d; Native.sVarFromDict(&r, &t); return r; }\n"
			"\tpublic GodotDictionary AsDictionary() { var s = this; GodotDictionary o = ?; Native.sVarAsDict(&s, &o); return o; }\n"
			// Callable/Signal cross as opaque buffers; As* returns a copy the caller Disposes.
			"\tpublic static Godot.Variant FromCallable(Callable c) { Godot.Variant r = default; var t = c; Native.sVarFromCallable(&r, &t); return r; }\n"
			"\tpublic Callable AsCallable() { var s = this; Callable o = ?; Native.sVarAsCallable(&s, &o); return o; }\n"
			"\tpublic static Godot.Variant FromSignal(Signal sig) { Godot.Variant r = default; var t = sig; Native.sVarFromSignal(&r, &t); return r; }\n"
			"\tpublic Signal AsSignal() { var s = this; Signal o = ?; Native.sVarAsSignal(&s, &o); return o; }\n"
			// StringName/NodePath box from text and preserve the Variant type; reading uses AsString
			// (the engine stringifies both), exposed here as named helpers for symmetry.
			"\tpublic static Godot.Variant FromStringName(System.String v) { Godot.Variant r = default; Native.sVarFromStringName(&r, v.CStr()); return r; }\n"
			"\tpublic System.String AsStringName() { return AsString(); }\n"
			"\tpublic static Godot.Variant FromNodePath(System.String v) { Godot.Variant r = default; Native.sVarFromNodePath(&r, v.CStr()); return r; }\n"
			"\tpublic System.String AsNodePath() { return AsString(); }\n"
			// PackedStringArray boxes per-element (not memcpy'd like the POD packed arrays): build a
			// temp engine PSA from the Beef String[], box it, free the temp. As* returns a fresh
			// String[] the caller owns (delete each element, then the array).
			"\tpublic static Godot.Variant FromPackedStringArray(System.String[] a) { GodotPacked _p = ?; Native.PSAIn(a, &_p); Godot.Variant r = default; Native.sVarFromPSA(&r, &_p); Native.PSADel(&_p); return r; }\n"
			"\tpublic System.String[] AsPackedStringArray() { var s = this; GodotPacked _p = ?; Native.sVarAsPSA(&s, &_p); var o = Native.PSAOut(&_p); Native.PSADel(&_p); return o; }\n" +
			variant_packed +
			"}\n"
			// Array/Dictionary are one engine pointer (copy-on-write, refcounted by the engine).
			// The struct value IS the engine container; elements cross as Variants. Containers you
			// build or receive own engine resources -- call Dispose() when done.
			"[CRepr] public struct GodotArray\n"
			"{\n"
			"\tvoid* _ptr;\n"
			"\tpublic static GodotArray New() { GodotArray r = ?; Native.sArrNew(&r); return r; }\n"
			"\tpublic void Dispose() mut { Native.sArrDestroy(&this); }\n"
			"\tpublic int Count { get { var s = this; return (int)Native.sArrSize(&s); } }\n"
			"\tpublic Godot.Variant this[int index]\n"
			"\t{\n"
			"\t\tget { var s = this; Godot.Variant v = ?; Native.sArrGet(&s, (int64)index, &v); return v; }\n"
			"\t}\n"
			"\tpublic void Add(Godot.Variant value) mut { var v = value; Native.sArrPushBack(&this, &v); }\n"
			"}\n"
			"[CRepr] public struct GodotDictionary\n"
			"{\n"
			"\tvoid* _ptr;\n"
			"\tpublic static GodotDictionary New() { GodotDictionary r = ?; Native.sDictNew(&r); return r; }\n"
			"\tpublic void Dispose() mut { Native.sDictDestroy(&this); }\n"
			"\tpublic int Count { get { var s = this; return (int)Native.sDictSize(&s); } }\n"
			"\tpublic bool Has(Godot.Variant key) { var s = this; var k = key; return Native.sDictHas(&s, &k); }\n"
			"\tpublic Godot.Variant this[Godot.Variant key]\n"
			"\t{\n"
			"\t\tget { var s = this; var k = key; Godot.Variant v = ?; Native.sDictGet(&s, &k, &v); return v; }\n"
			"\t\tset mut { var k = key; var val = value; Native.sDictSet(&this, &k, &val); }\n"
			"\t}\n"
			"}\n"
			"[CRepr] public struct Callable\n"
			"{\n"
			"\tuint8[" + callable_sz + "] _opaque;\n"
			"\tpublic void Dispose() mut { if (Native.sCallableDestroy != null) Native.sCallableDestroy(&this); }\n"
			"}\n"
			"[CRepr] public struct Signal\n"
			"{\n"
			"\tuint8[" + signal_sz + "] _opaque;\n"
			"\tpublic void Dispose() mut { if (Native.sSignalDestroy != null) Native.sSignalDestroy(&this); }\n"
			"}\n"
			"[CRepr] public struct Rid { uint64 _id; }\n"
			// One-pointer storage for an engine String/StringName/NodePath during marshalling.
			"[CRepr] public struct GodotStr { void* _p; }\n"
			"[CRepr] public struct GodotSN { void* _p; }\n"
			"[CRepr] public struct GodotNP { void* _p; }\n"
			// One-pointer storage for an engine Packed*Array (CowData) during marshalling.
			"[CRepr] public struct GodotPacked { void* _p; }\n"
			"\n"
			"// --- Math types ---\n"
			"[CRepr] public struct Vector2 { public " + r + " X, Y;\n"
			"\tpublic enum Axis : int32 { AXIS_X = 0, AXIS_Y = 1 }\n"
			"}\n"
			"[CRepr] public struct Vector2I { public int32 X, Y; }\n"
			"[CRepr] public struct Vector3 { public " + r + " X, Y, Z;\n"
			"\tpublic enum Axis : int32 { AXIS_X = 0, AXIS_Y = 1, AXIS_Z = 2 }\n"
			"}\n"
			"[CRepr] public struct Vector3I { public int32 X, Y, Z; }\n"
			"[CRepr] public struct Vector4 { public " + r + " X, Y, Z, W;\n"
			"\tpublic enum Axis : int32 { AXIS_X = 0, AXIS_Y = 1, AXIS_Z = 2, AXIS_W = 3 }\n"
			"}\n"
			"[CRepr] public struct Vector4I { public int32 X, Y, Z, W; }\n"
			"[CRepr] public struct Rect2 { public Vector2 Position, Size; }\n"
			"[CRepr] public struct Rect2I { public Vector2I Position, Size; }\n"
			"[CRepr] public struct Color { public float R, G, B, A; }\n"
			"[CRepr] public struct Plane { public Vector3 Normal; public " + r + " D; }\n"
			"[CRepr] public struct Aabb { public Vector3 Position, Size; }\n"
			// Godot stores a Basis row-major (Vector3 rows[3]); X/Y/Z columns are exposed via the
			// hand-written extension. Field names match C#'s Row0/Row1/Row2.
			"[CRepr] public struct Basis { public Vector3 Row0, Row1, Row2; }\n"
			"[CRepr] public struct Quaternion { public " + r + " X, Y, Z, W; }\n"
			"[CRepr] public struct Transform2D { public Vector2 X, Y, Origin; }\n"
			"[CRepr] public struct Transform3D { public Basis Basis; public Vector3 Origin; }\n"
			"[CRepr] public struct Projection { public Vector4 X, Y, Z, W; }\n");

	return OK;
}

Error BeefBindingsGenerator::copy_math_glue(const String &p_bindings_src) {
	// Locate the hand-written glue source: bundled next to the executable first (shipping), then the
	// in-repo path (dev: <root>/bin -> <root>/modules/beef/glue).
	String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	Vector<String> candidates;
	candidates.push_back(exe_dir.path_join("BeefGlue"));
	candidates.push_back(exe_dir.get_base_dir().path_join("modules/beef/glue"));
	String glue_dir;
	for (int i = 0; i < candidates.size(); i++) {
		if (DirAccess::dir_exists_absolute(candidates[i])) {
			glue_dir = candidates[i];
			break;
		}
	}
	if (glue_dir.is_empty()) {
		WARN_PRINT("BeefScript: math glue source not found; built-in structs will lack methods.");
		return ERR_FILE_NOT_FOUND;
	}

	Ref<DirAccess> da = DirAccess::open(glue_dir);
	if (da.is_null()) {
		return ERR_CANT_OPEN;
	}
	int copied = 0;
	da->list_dir_begin();
	for (String name = da->get_next(); !name.is_empty(); name = da->get_next()) {
		if (da->current_is_dir() || !name.to_lower().ends_with(".bf")) {
			continue;
		}
		Ref<FileAccess> src = FileAccess::open(glue_dir.path_join(name), FileAccess::READ);
		if (src.is_null()) {
			continue;
		}
		String contents = src->get_as_utf8_string();
		Ref<FileAccess> dst = FileAccess::open(p_bindings_src.path_join(name), FileAccess::WRITE);
		if (dst.is_valid()) {
			dst->store_string(contents);
			copied++;
		}
	}
	da->list_dir_end();
	return OK;
}

Error BeefBindingsGenerator::generate_runtime_glue(const String &p_src_dir) {
	ERR_FAIL_COND_V(!initialized, ERR_UNCONFIGURED);
	return _generate_runtime_file(p_src_dir);
}

Error BeefBindingsGenerator::generate_script_base(const String &p_src_dir) {
	ERR_FAIL_COND_V(!initialized, ERR_UNCONFIGURED);
	return _generate_godotscript_file(p_src_dir);
}

static bool _reg_is_ident(char32_t c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Namespace of a .bf file (from `namespace X;`), or empty for the root namespace.
static String _reg_parse_namespace(const String &p_src) {
	int n = p_src.find("namespace ");
	if (n < 0) {
		return String();
	}
	int i = n + 10;
	while (i < p_src.length() && (p_src[i] == ' ' || p_src[i] == '\t')) {
		i++;
	}
	int start = i;
	while (i < p_src.length() && (_reg_is_ident(p_src[i]) || p_src[i] == '.')) {
		i++;
	}
	return p_src.substr(start, i - start);
}

// (ns, class name) of each [GodotScript] class found while scanning the source tree.
struct BeefScriptReg {
	String ns;
	String name;
};

// Collect [GodotScript] classes in one .bf source.
static void _reg_scan_source(const String &p_src, Vector<BeefScriptReg> &r_regs) {
	String ns = _reg_parse_namespace(p_src);
	int pos = 0;
	while (true) {
		int m = p_src.find("GodotScript", pos);
		if (m < 0) {
			break;
		}
		int after = m + 11; // length of "GodotScript"
		pos = after;
		// Must be the bare attribute token, not GodotScriptAttribute / GodotScriptRegistrations.
		if (after < p_src.length() && _reg_is_ident(p_src[after])) {
			continue;
		}
		int c = p_src.find("class ", m);
		if (c < 0) {
			continue;
		}
		int i = c + 6;
		while (i < p_src.length() && (p_src[i] == ' ' || p_src[i] == '\t')) {
			i++;
		}
		int name_start = i;
		while (i < p_src.length() && _reg_is_ident(p_src[i])) {
			i++;
		}
		String name = p_src.substr(name_start, i - name_start);
		if (!name.is_empty()) {
			r_regs.push_back({ ns, name });
		}
		pos = i;
	}
}

// Recursively walk a source folder collecting [GodotScript] classes.
static void _reg_scan_dir(const String &p_dir, Vector<BeefScriptReg> &r_regs) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	da->list_dir_begin();
	for (String fn = da->get_next(); !fn.is_empty(); fn = da->get_next()) {
		if (fn == "." || fn == "..") {
			continue;
		}
		if (da->current_is_dir()) {
			_reg_scan_dir(p_dir.path_join(fn), r_regs); // includes BeefGen (skipped by file name below)
			continue;
		}
		if (fn.get_extension() != "bf") {
			continue;
		}
		// Don't scan our own generated support files.
		if (fn == "GodotRuntime.bf" || fn == "GodotScript.bf" || fn == "GodotRegistrations.bf" || fn == "GodotBridge.bf") {
			continue;
		}
		_reg_scan_source(FileAccess::get_file_as_string(p_dir.path_join(fn)), r_regs);
	}
	da->list_dir_end();
}

Error BeefBindingsGenerator::generate_registrations(const String &p_scan_dir, const String &p_output_dir) {
	Vector<BeefScriptReg> regs;
	_reg_scan_dir(p_scan_dir, regs);

	String out =
			"// Auto-generated by Godot-Beef -- registrars for [GodotScript] classes. DO NOT EDIT.\n"
			"// Regenerated on editor start and before each build.\n"
			"using Godot;\n"
			"using System;\n"
			"\n"
			"namespace GodotScriptRegistrations;\n"
			"\n";
	for (const BeefScriptReg &r : regs) {
		String type_ref = r.ns.is_empty() ? r.name : (r.ns + "." + r.name);
		String reg_name = (r.ns.is_empty() ? String() : r.ns.replace(".", "_") + "_") + r.name + "Reg";
		out += "[AlwaysInclude(IncludeAllMethods=true), GodotRegister(typeof(" + type_ref + "))]\n";
		out += "class " + reg_name + " {}\n\n";
	}

	String path = p_output_dir.path_join("GodotRegistrations.bf");
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Cannot write: " + path);
	f->store_string(out);
	return OK;
}

// Emits GodotScript.bf: the [GodotRegister(typeof(MyClass))] comptime attribute. Applied to a small
// companion type, it reflects the target script class's overridden Godot virtuals (a foreign,
// fully-defined type, which avoids the comptime data cycle that reflecting one's own type causes)
// and emits the per-class C ABI exports the engine resolves by name. The emitted dispatch unboxes
// each Variant argument to the override's parameter type and forwards the call.
//
// The content is a raw string literal so the Beef source needs no C++ escaping; it must stay pure
// ASCII (store_string copies bytes into a wider buffer, so non-ASCII corrupts the output).
Error BeefBindingsGenerator::_generate_godotscript_file(const String &p_output_dir) {
	String file_path = p_output_dir.path_join("GodotScript.bf");
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Cannot write: " + file_path);

	f->store_string(R"BF(// Auto-generated by Godot-Beef. Comptime script registration.
//
// Mark a script class with the GodotScript attribute and write it normally:
//
//   [GodotScript] class MyNode : Node3D { public override void _Process(double delta) {...} }
//
// Godot-Beef then auto-generates the registrar (in GodotRegistrations.bf) -- you never write it.
// Under the hood that registrar carries GodotRegister, which reflects the target class's overridden
// virtuals and emits the C ABI exports (Create/Destroy/Notification/HasMethod/Call) the engine
// resolves by name. GodotRegister reflects the target (a different, fully-defined type) and emits
// into the registrar's own body -- reflecting one's own type during its init would create a comptime
// data cycle. [AlwaysInclude] keeps the otherwise unreferenced registrar (and its exports) from
// being stripped from the DLL.
using System;
using System.Reflection;
namespace Godot;

// Mark a public field for export: it becomes a script property (get/set + inspector/serialization).
// Optional (hint, hintString) gives an inspector hint, e.g. [GodotExport((.)PropertyHint.Range, "0,100")]
// for a slider or [GodotExport((.)PropertyHint.Enum, "Idle,Run,Jump")] for a dropdown.
[AttributeUsage(.Field, .ReflectAttribute)]
struct GodotExportAttribute : Attribute
{
	public int32 mHint = 0;
	public String mHintString = "";
	public this() {}
	public this(int32 hint, String hintString) { mHint = hint; mHintString = hintString; }
}

// Mark an (empty-bodied) method as a signal declaration: its name and parameters define the signal.
[AttributeUsage(.Method, .ReflectAttribute)]
struct GodotSignalAttribute : Attribute
{
}

// Mark a class as a Godot script: it is registered with the engine and gets its C ABI glue generated
// automatically (no hand-written registrar needed). Just write: [GodotScript] class MyNode : Node3D {}
[AttributeUsage(.Class, .ReflectAttribute)]
struct GodotScriptAttribute : Attribute
{
}

// Mark a script class to also run in the editor (a Tool script).
[AttributeUsage(.Class, .ReflectAttribute)]
struct GodotToolAttribute : Attribute
{
}

// Network RPC mode (mirrors MultiplayerAPI.RPCMode).
enum GodotRpcMode { Disabled = 0, AnyPeer = 1, Authority = 2 }
// Network transfer mode (mirrors MultiplayerPeer.TransferMode).
enum GodotTransferMode { Unreliable = 0, UnreliableOrdered = 1, Reliable = 2 }

// Mark a method as a remote procedure call. Defaults: Authority, no local call, Reliable, channel 0.
//   [GodotRpc] / [GodotRpc(.AnyPeer)] / [GodotRpc(.AnyPeer, true, .Unreliable, 1)]
[AttributeUsage(.Method, .ReflectAttribute)]
struct GodotRpcAttribute : Attribute
{
	public GodotRpcMode mMode = .Authority;
	public bool mCallLocal = false;
	public GodotTransferMode mTransferMode = .Reliable;
	public int32 mChannel = 0;
	public this() {}
	public this(GodotRpcMode mode) { mMode = mode; }
	public this(GodotRpcMode mode, bool callLocal) { mMode = mode; mCallLocal = callLocal; }
	public this(GodotRpcMode mode, bool callLocal, GodotTransferMode transferMode) { mMode = mode; mCallLocal = callLocal; mTransferMode = transferMode; }
	public this(GodotRpcMode mode, bool callLocal, GodotTransferMode transferMode, int32 channel) { mMode = mode; mCallLocal = callLocal; mTransferMode = transferMode; mChannel = channel; }
}

)BF"
			// Split here: a single string literal would exceed the MSVC limit (C2026). Adjacent
			// literals are concatenated by the compiler into one argument.
			R"BF([AttributeUsage(.Class, .ReflectAttribute)]
struct GodotRegisterAttribute : Attribute, IComptimeTypeApply
{
	Type mTarget;

	public this(Type target)
	{
		mTarget = target;
	}

	// Map a Beef type to its Godot Variant::Type tag (0 = NIL/untyped). Shared by signal- and
	// method-argument typing so the type<->tag mapping lives in exactly one place.
	[Comptime]
	static int32 VariantTag(Type pt)
	{
		if (pt == typeof(bool)) return 1;
		if (pt == typeof(int64) || pt == typeof(int32) || pt == typeof(int)) return 2;
		if (pt == typeof(double) || pt == typeof(float)) return 3;
		if (pt == typeof(System.String)) return 4;
		if (pt == typeof(Godot.Vector2)) return 5;
		if (pt == typeof(Godot.Vector2I)) return 6;
		if (pt == typeof(Godot.Rect2)) return 7;
		if (pt == typeof(Godot.Rect2I)) return 8;
		if (pt == typeof(Godot.Vector3)) return 9;
		if (pt == typeof(Godot.Vector3I)) return 10;
		if (pt == typeof(Godot.Transform2D)) return 11;
		if (pt == typeof(Godot.Vector4)) return 12;
		if (pt == typeof(Godot.Vector4I)) return 13;
		if (pt == typeof(Godot.Plane)) return 14;
		if (pt == typeof(Godot.Quaternion)) return 15;
		if (pt == typeof(Godot.Aabb)) return 16;
		if (pt == typeof(Godot.Basis)) return 17;
		if (pt == typeof(Godot.Transform3D)) return 18;
		if (pt == typeof(Godot.Projection)) return 19;
		if (pt == typeof(Godot.Color)) return 20;
		if (pt == typeof(Godot.Rid)) return 23;
		if (pt == typeof(Godot.Callable)) return 25;
		if (pt == typeof(Godot.Signal)) return 26;
		if (pt == typeof(Godot.GodotDictionary)) return 27;
		if (pt == typeof(Godot.GodotArray)) return 28;
		if (pt.IsEnum) return 2;
		if (pt.IsSubtypeOf(typeof(Godot.Object))) return 24;
		return 0;
	}

	// Append the expression that READS a value of type `t` out of the Variant pointed to by `v`.
	// Returns the marshal kind: 0=unsupported, 1=plain (no cleanup), 2=String (caller deletes the
	// result), 3=container (Array/Dictionary/Callable/Signal; caller Disposes the result).
	[Comptime]
	static int MarshalAs(Type t, StringView v, String o)
	{
		if (t == typeof(bool)) { o.AppendF($"{v}.AsBool()"); return 1; }
		if (t == typeof(int64)) { o.AppendF($"{v}.AsInt()"); return 1; }
		if (t == typeof(int)) { o.AppendF($"(int){v}.AsInt()"); return 1; }
		if (t == typeof(int32)) { o.AppendF($"(int32){v}.AsInt()"); return 1; }
		if (t == typeof(double)) { o.AppendF($"{v}.AsFloat()"); return 1; }
		if (t == typeof(float)) { o.AppendF($"(float){v}.AsFloat()"); return 1; }
		if (t == typeof(System.String)) { o.AppendF($"{v}.AsString()"); return 2; }
		if (t == typeof(Godot.Vector2)) { o.AppendF($"{v}.AsVector2()"); return 1; }
		if (t == typeof(Godot.Vector3)) { o.AppendF($"{v}.AsVector3()"); return 1; }
		if (t == typeof(Godot.Vector4)) { o.AppendF($"{v}.AsVector4()"); return 1; }
		if (t == typeof(Godot.Vector2I)) { o.AppendF($"{v}.AsVector2I()"); return 1; }
		if (t == typeof(Godot.Vector3I)) { o.AppendF($"{v}.AsVector3I()"); return 1; }
		if (t == typeof(Godot.Vector4I)) { o.AppendF($"{v}.AsVector4I()"); return 1; }
		if (t == typeof(Godot.Rect2)) { o.AppendF($"{v}.AsRect2()"); return 1; }
		if (t == typeof(Godot.Rect2I)) { o.AppendF($"{v}.AsRect2I()"); return 1; }
		if (t == typeof(Godot.Plane)) { o.AppendF($"{v}.AsPlane()"); return 1; }
		if (t == typeof(Godot.Aabb)) { o.AppendF($"{v}.AsAabb()"); return 1; }
		if (t == typeof(Godot.Basis)) { o.AppendF($"{v}.AsBasis()"); return 1; }
		if (t == typeof(Godot.Quaternion)) { o.AppendF($"{v}.AsQuaternion()"); return 1; }
		if (t == typeof(Godot.Transform2D)) { o.AppendF($"{v}.AsTransform2D()"); return 1; }
		if (t == typeof(Godot.Transform3D)) { o.AppendF($"{v}.AsTransform3D()"); return 1; }
		if (t == typeof(Godot.Projection)) { o.AppendF($"{v}.AsProjection()"); return 1; }
		if (t == typeof(Godot.Color)) { o.AppendF($"{v}.AsColor()"); return 1; }
		if (t == typeof(Godot.Rid)) { o.AppendF($"{v}.AsRid()"); return 1; }
		if (t == typeof(Godot.Variant)) { o.AppendF($"*{v}"); return 1; }
		if (t == typeof(Godot.GodotArray)) { o.AppendF($"{v}.AsArray()"); return 3; }
		if (t == typeof(Godot.GodotDictionary)) { o.AppendF($"{v}.AsDictionary()"); return 3; }
		if (t == typeof(Godot.Callable)) { o.AppendF($"{v}.AsCallable()"); return 3; }
		if (t == typeof(Godot.Signal)) { o.AppendF($"{v}.AsSignal()"); return 3; }
		if (t.IsEnum) { o.AppendF($"({t}){v}.AsInt()"); return 1; }
		if (t.IsSubtypeOf(typeof(Godot.Object))) { o.AppendF($"({t})({v}.AsObject())"); return 1; }
		return 0;
	}

	// Append the expression that BOXES the value `x` (of type `t`) into a Variant. Returns the kind:
	// 0=unsupported, 1=plain, 2=String (the source `x` must be deleted after), 3=container (the source
	// `x` must be Disposed after), 4=Variant passthrough (the box IS the value; no From* wrapper).
	[Comptime]
	static int MarshalFrom(Type t, StringView x, String o)
	{
		if (t == typeof(Godot.Variant)) { o.AppendF($"{x}"); return 4; }
		if (t == typeof(bool)) { o.AppendF($"Godot.Variant.FromBool({x})"); return 1; }
		if (t == typeof(int64) || t == typeof(int32) || t == typeof(int)) { o.AppendF($"Godot.Variant.FromInt((int64)({x}))"); return 1; }
		if (t == typeof(double) || t == typeof(float)) { o.AppendF($"Godot.Variant.FromFloat((double)({x}))"); return 1; }
		if (t == typeof(System.String)) { o.AppendF($"Godot.Variant.FromString({x})"); return 2; }
		if (t == typeof(Godot.Vector2)) { o.AppendF($"Godot.Variant.FromVector2({x})"); return 1; }
		if (t == typeof(Godot.Vector3)) { o.AppendF($"Godot.Variant.FromVector3({x})"); return 1; }
		if (t == typeof(Godot.Vector4)) { o.AppendF($"Godot.Variant.FromVector4({x})"); return 1; }
		if (t == typeof(Godot.Vector2I)) { o.AppendF($"Godot.Variant.FromVector2I({x})"); return 1; }
		if (t == typeof(Godot.Vector3I)) { o.AppendF($"Godot.Variant.FromVector3I({x})"); return 1; }
		if (t == typeof(Godot.Vector4I)) { o.AppendF($"Godot.Variant.FromVector4I({x})"); return 1; }
		if (t == typeof(Godot.Rect2)) { o.AppendF($"Godot.Variant.FromRect2({x})"); return 1; }
		if (t == typeof(Godot.Rect2I)) { o.AppendF($"Godot.Variant.FromRect2I({x})"); return 1; }
		if (t == typeof(Godot.Plane)) { o.AppendF($"Godot.Variant.FromPlane({x})"); return 1; }
		if (t == typeof(Godot.Aabb)) { o.AppendF($"Godot.Variant.FromAabb({x})"); return 1; }
		if (t == typeof(Godot.Basis)) { o.AppendF($"Godot.Variant.FromBasis({x})"); return 1; }
		if (t == typeof(Godot.Quaternion)) { o.AppendF($"Godot.Variant.FromQuaternion({x})"); return 1; }
		if (t == typeof(Godot.Transform2D)) { o.AppendF($"Godot.Variant.FromTransform2D({x})"); return 1; }
		if (t == typeof(Godot.Transform3D)) { o.AppendF($"Godot.Variant.FromTransform3D({x})"); return 1; }
		if (t == typeof(Godot.Projection)) { o.AppendF($"Godot.Variant.FromProjection({x})"); return 1; }
		if (t == typeof(Godot.Color)) { o.AppendF($"Godot.Variant.FromColor({x})"); return 1; }
		if (t == typeof(Godot.Rid)) { o.AppendF($"Godot.Variant.FromRid({x})"); return 1; }
		if (t == typeof(Godot.GodotArray)) { o.AppendF($"Godot.Variant.FromArray({x})"); return 3; }
		if (t == typeof(Godot.GodotDictionary)) { o.AppendF($"Godot.Variant.FromDictionary({x})"); return 3; }
		if (t == typeof(Godot.Callable)) { o.AppendF($"Godot.Variant.FromCallable({x})"); return 3; }
		if (t == typeof(Godot.Signal)) { o.AppendF($"Godot.Variant.FromSignal({x})"); return 3; }
		if (t.IsEnum) { o.AppendF($"Godot.Variant.FromInt((int64)({x}))"); return 1; }
		if (t.IsSubtypeOf(typeof(Godot.Object))) { o.AppendF($"Godot.Variant.FromObject({x})"); return 1; }
		return 0;
	}

	[Comptime]
	public void ApplyToType(Type type)
	{
		let target = mTarget;

		String cn = scope .();
		target.GetName(cn);
		String fn = scope .();
		target.GetFullName(fn);

		String has = scope .();
		String call = scope .();
		String signalName = scope .();
		String signalArgc = scope .();
		String signalArgType = scope .();
		int signalCount = 0;

		String rpcName = scope .();
		String rpcMode = scope .();
		String rpcTransfer = scope .();
		String rpcCallLocal = scope .();
		String rpcChannel = scope .();
		int rpcCount = 0;

		String methodName = scope .();
		String methodArgc = scope .();
		String methodArgType = scope .();
		int methodCount = 0;

		for (var m in target.GetMethods(.Public | .Instance | .DeclaredOnly))
		{
			let nm = m.Name;
			if (m.IsStatic || m.IsConstructor)
				continue;
			// [GodotRpc] methods stay normal dispatchable methods (so they can be received/called
			// locally); the attribute just contributes their network config. Don't 'continue' here.
			if (m.HasCustomAttribute<GodotRpcAttribute>())
			{
				let rpc = m.GetCustomAttribute<GodotRpcAttribute>().Value;
				rpcName.AppendF($"\t\tif (idx == {rpcCount}) return \"{nm}\".CStr();\n");
				rpcMode.AppendF($"\t\tif (idx == {rpcCount}) return {(int)rpc.mMode};\n");
				rpcTransfer.AppendF($"\t\tif (idx == {rpcCount}) return {(int)rpc.mTransferMode};\n");
				rpcCallLocal.AppendF($"\t\tif (idx == {rpcCount}) return {rpc.mCallLocal ? "true" : "false"};\n");
				rpcChannel.AppendF($"\t\tif (idx == {rpcCount}) return {rpc.mChannel};\n");
				rpcCount++;
			}
			// [GodotSignal] methods are declarations: their name + params define the signal.
			if (m.HasCustomAttribute<GodotSignalAttribute>())
			{
				signalName.AppendF($"\t\tif (idx == {signalCount}) return \"{nm}\".CStr();\n");
				signalArgc.AppendF($"\t\tif (idx == {signalCount}) return {m.ParamCount};\n");
				for (int ai < m.ParamCount)
				{
					let tag = VariantTag(m.GetParamType(ai));
					signalArgType.AppendF($"\t\tif (sigIdx == {signalCount} && argIdx == {ai}) return {tag};\n");
				}
				signalCount++;
				continue;
			}
			// Skip names that are not plain identifiers (operators, etc.).
			if (nm.IsEmpty || !((nm[0] >= 'A' && nm[0] <= 'Z') || (nm[0] >= 'a' && nm[0] <= 'z') || nm[0] == '_'))
				continue;

			// _PascalCase methods are Godot virtual overrides -> snake_case engine name (_Process ->
			// _process). Any other public method is callable verbatim (so GDScript / signal handlers
			// can reach it).
			String gd = scope .();
			if (nm.Length >= 2 && nm[0] == '_' && nm[1] >= 'A' && nm[1] <= 'Z')
			{
				gd.Append('_');
				for (int i = 1; i < nm.Length; i++)
				{
					let c = nm[i];
					if (c >= 'A' && c <= 'Z')
					{
						if (i > 1)
							gd.Append('_');
						gd.Append((char8)(c - 'A' + 'a'));
					}
					else
						gd.Append(c);
				}
			}
			else
				gd.Set(nm);

			has.AppendF($"\t\tif (name == \"{gd}\") return true;\n");

			// Enumerable for get_script_method_list (signal-connect UI, method pickers, Callable checks).
			methodName.AppendF($"\t\tif (idx == {methodCount}) return \"{gd}\".CStr();\n");
			methodArgc.AppendF($"\t\tif (idx == {methodCount}) return {m.ParamCount};\n");
			for (int ai < m.ParamCount)
			{
				let tag = VariantTag(m.GetParamType(ai));
				methodArgType.AppendF($"\t\tif (mIdx == {methodCount} && aIdx == {ai}) return {tag};\n");
			}
			methodCount++;

			// Unbox each argument into a local. String args allocate a fresh System.String (via
			// AsString) that we own only for the call duration, so they are deleted afterward --
			// else every dispatch with a String parameter (e.g. _get/_set the inspector polls) leaks.
			String argDecls = scope .();
			String argRefs = scope .();
			String argCleanup = scope .();
			for (int i < m.ParamCount)
			{
				if (i > 0)
					argRefs.Append(", ");
				let t = m.GetParamType(i);
				let a = scope $"((Godot.Variant*)args[{i}])";
				let an = scope $"__a{i}";
				String expr = scope .();
				let kind = MarshalAs(t, a, expr); // 0=unsupported 1=plain 2=String 3=container
				// An unsupported param type would make `var x = default` un-inferable and break the
				// WHOLE build, so emit a typed zero-value local instead (the method still dispatches,
				// that one arg is just default). Matched types keep the inferred `var`.
				if (kind == 0)
					argDecls.AppendF($"\t\t\t{t} {an} = default;\n");
				else
					argDecls.AppendF($"\t\t\tvar {an} = {expr};\n");
				argRefs.Append(an);
				if (kind == 2)
					argCleanup.AppendF($"\t\t\tdelete {an};\n");
				else if (kind == 3)
					argCleanup.AppendF($"\t\t\t{an}.Dispose();\n");
			}

			let rt = m.ReturnType;
			let invoke = scope $"inst.{nm}({argRefs})";
			call.AppendF($"\t\tif (name == \"{gd}\")\n");
			call.Append("\t\t{\n");
			call.Append(argDecls);
			if (rt == typeof(void))
				call.AppendF($"\t\t\t{invoke};\n");
			else
			{
				// Box the return into ret. String/Array/Dictionary/Callable/Signal returns own a
				// resource that must be freed after it is copied into the Variant, so route them
				// through a temp; plain values box from the temp; a Variant return passes straight through.
				String boxed = scope .();
				let kind = MarshalFrom(rt, "__r", boxed); // 0=none 1=plain 2=String 3=container 4=Variant
				if (kind == 0)
					call.AppendF($"\t\t\t{invoke};\n"); // unsupported return type: call for effect, ret stays nil
				else if (kind == 4)
					call.AppendF($"\t\t\t*(Godot.Variant*)ret = {invoke};\n"); // already a Variant
				else
				{
					call.AppendF($"\t\t\tvar __r = {invoke};\n");
					call.AppendF($"\t\t\t*(Godot.Variant*)ret = {boxed};\n");
					if (kind == 2)
						call.Append("\t\t\tdelete __r;\n");
					else if (kind == 3)
						call.Append("\t\t\t__r.Dispose();\n");
				}
			}
			call.Append(argCleanup);
			call.Append("\t\t\treturn true;\n");
			call.Append("\t\t}\n");
		}

)BF"
			// Split here to keep each string literal under the MSVC C2026 size limit.
			R"BF(		// Exported fields ([GodotExport] on a public field) -> get/set + a property-list entry.
		String setBody = scope .();
		String getBody = scope .();
		String propName = scope .();
		String propType = scope .();
		String propDefault = scope .();
		String propHint = scope .();
		String propHintString = scope .();
		int propCount = 0;
		for (var field in target.GetFields(.Public | .Instance))
		{
			if (!field.HasCustomAttribute<GodotExportAttribute>())
				continue;
			let pn = field.Name;
			let ft = field.FieldType;
			let v = "((Godot.Variant*)variant)";

			// Optional inspector hint from the [GodotExport(hint, "string")] arguments.
			let exp = field.GetCustomAttribute<GodotExportAttribute>().Value;
			let hintVal = exp.mHint;
			let hintStr = (exp.mHintString != null) ? exp.mHintString : "";

			// Read-from-Variant + box-to-Variant exprs + Variant::Type tag for the field type, from the
			// shared marshalling helpers. Exported fields cover scalars/String/math/enum/Object
			// (MarshalAs kinds 1 and 2); container/Variant field types (kinds 3/4) are not exported.
			String asExpr = scope .();
			String fromExpr = scope .();
			let fkind = MarshalAs(ft, v, asExpr);
			if (fkind != 1 && fkind != 2)
				continue; // unsupported field type
			MarshalFrom(ft, scope $"inst.{pn}", fromExpr);
			let tag = VariantTag(ft);

			// The default value reads the same field from a freshly-constructed instance (def).
			String fromDef = scope .();
			fromDef.Set(fromExpr);
			fromDef.Replace("inst.", "def.");

			setBody.AppendF($"\t\tif (n == \"{pn}\") {{ inst.{pn} = {asExpr}; return true; }}\n");
			getBody.AppendF($"\t\tif (n == \"{pn}\") {{ *(Godot.Variant*)variant = {fromExpr}; return true; }}\n");
			propName.AppendF($"\t\tif (idx == {propCount}) return \"{pn}\".CStr();\n");
			propType.AppendF($"\t\tif (idx == {propCount}) return {tag};\n");
			propDefault.AppendF($"\t\tif (idx == {propCount}) {{ *(Godot.Variant*)variantOut = {fromDef}; return; }}\n");
			propHint.AppendF($"\t\tif (idx == {propCount}) return {hintVal};\n");
			propHintString.AppendF($"\t\tif (idx == {propCount}) return \"{hintStr}\".CStr();\n");
			propCount++;
		}

		String s = scope .();

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static void* BeefGodot_Create_{cn}(void* owner)\n");
		s.Append("{\n");
		s.AppendF($"\tlet obj = new {fn}();\n");
		s.Append("\tobj._godotOwner = owner;\n");
		s.Append("\treturn Godot.Native.TrackObject(obj);\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static void BeefGodot_Destroy_{cn}(void* self)\n");
		s.Append("{\n");
		s.Append("\tdelete Godot.Native.UntrackObject(self);\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static void BeefGodot_Notification_{cn}(void* self, int32 what)\n");
		s.Append("{\n");
		s.AppendF($"\tlet inst = ({fn})Godot.Native.GetObject(self);\n");
		s.Append("\tinst._Notification(what);\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static bool BeefGodot_HasMethod_{cn}(char8* method)\n");
		s.Append("{\n");
		if (!has.IsEmpty)
			s.Append("\tvar name = System.StringView(method);\n");
		s.Append(has);
		s.Append("\treturn false;\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static bool BeefGodot_Call_{cn}(void* self, char8* method, void** args, int32 argc, void* ret)\n");
		s.Append("{\n");
		if (!call.IsEmpty)
		{
			s.AppendF($"\tlet inst = ({fn})Godot.Native.GetObject(self);\n");
			s.Append("\tvar name = System.StringView(method);\n");
		}
		s.Append(call);
		s.Append("\treturn false;\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static bool BeefGodot_Set_{cn}(void* self, char8* name, void* variant)\n");
		s.Append("{\n");
		if (!setBody.IsEmpty)
		{
			s.AppendF($"\tlet inst = ({fn})Godot.Native.GetObject(self);\n");
			s.Append("\tvar n = System.StringView(name);\n");
		}
		s.Append(setBody);
		s.Append("\treturn false;\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static bool BeefGodot_Get_{cn}(void* self, char8* name, void* variant)\n");
		s.Append("{\n");
		if (!getBody.IsEmpty)
		{
			s.AppendF($"\tlet inst = ({fn})Godot.Native.GetObject(self);\n");
			s.Append("\tvar n = System.StringView(name);\n");
		}
		s.Append(getBody);
		s.Append("\treturn false;\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_PropCount_{cn}()\n");
		s.AppendF($"{{\n\treturn {propCount};\n}}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static char8* BeefGodot_PropName_{cn}(int32 idx)\n");
		s.Append("{\n");
		s.Append(propName);
		s.Append("\treturn null;\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_PropType_{cn}(int32 idx)\n");
		s.Append("{\n");
		s.Append(propType);
		s.Append("\treturn 0;\n");
		s.Append("}\n\n");

		// Default value of each exported field, read from a freshly-constructed instance.
		s.Append("[CLink, Export]\n");
		s.AppendF($"public static void BeefGodot_PropDefault_{cn}(int32 idx, void* variantOut)\n");
		s.Append("{\n");
		if (!propDefault.IsEmpty)
			s.AppendF($"\tlet def = scope {fn}();\n");
		s.Append(propDefault);
		s.Append("}\n\n");

		// Inspector hint + hint string per exported field (from [GodotExport(hint, "string")]).
		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_PropHint_{cn}(int32 idx)\n");
		s.Append("{\n");
		s.Append(propHint);
		s.Append("\treturn 0;\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static char8* BeefGodot_PropHintString_{cn}(int32 idx)\n");
		s.Append("{\n");
		s.Append(propHintString);
		s.Append("\treturn \"\";\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_SignalCount_{cn}()\n");
		s.AppendF($"{{\n\treturn {signalCount};\n}}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static char8* BeefGodot_SignalName_{cn}(int32 idx)\n");
		s.Append("{\n");
		s.Append(signalName);
		s.Append("\treturn null;\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_SignalArgc_{cn}(int32 idx)\n");
		s.Append("{\n");
		s.Append(signalArgc);
		s.Append("\treturn 0;\n");
		s.Append("}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_SignalArgType_{cn}(int32 sigIdx, int32 argIdx)\n");
		s.Append("{\n");
		s.Append(signalArgType);
		s.Append("\treturn 0;\n");
		s.Append("}\n");

		// --- Method list (for get_script_method_list: signal-connect UI, method pickers) ---
		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_MethodCount_{cn}()\n{{\n\treturn {methodCount};\n}}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static char8* BeefGodot_MethodName_{cn}(int32 idx)\n{{\n");
		s.Append(methodName);
		s.Append("\treturn null;\n}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_MethodArgc_{cn}(int32 idx)\n{{\n");
		s.Append(methodArgc);
		s.Append("\treturn 0;\n}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_MethodArgType_{cn}(int32 mIdx, int32 aIdx)\n{{\n");
		s.Append(methodArgType);
		s.Append("\treturn 0;\n}\n");

		// --- RPC config (from [GodotRpc] methods) ---
		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_RpcCount_{cn}()\n{{\n\treturn {rpcCount};\n}}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static char8* BeefGodot_RpcName_{cn}(int32 idx)\n{{\n");
		s.Append(rpcName);
		s.Append("\treturn null;\n}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_RpcMode_{cn}(int32 idx)\n{{\n");
		s.Append(rpcMode);
		s.Append("\treturn 2;\n}\n\n"); // default: Authority

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_RpcTransfer_{cn}(int32 idx)\n{{\n");
		s.Append(rpcTransfer);
		s.Append("\treturn 2;\n}\n\n"); // default: Reliable

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static bool BeefGodot_RpcCallLocal_{cn}(int32 idx)\n{{\n");
		s.Append(rpcCallLocal);
		s.Append("\treturn false;\n}\n\n");

		s.Append("[CLink, Export]\n");
		s.AppendF($"public static int32 BeefGodot_RpcChannel_{cn}(int32 idx)\n{{\n");
		s.Append(rpcChannel);
		s.Append("\treturn 0;\n}\n");

		Compiler.EmitTypeBody(type, s);
	}
}
)BF");

	return OK;
}

Error BeefBindingsGenerator::_generate_godotnative_file(const String &p_output_dir) {
	// Native lives in the GodotBindings StaticLib (NOT the user DynamicLib) so the generated
	// binding classes — which call Native.GetMethodBind/MethodBindPtrcall in their ptrcall
	// bodies — can see it. GameScripts (DynamicLib) depends on GodotBindings, so user scripts and
	// BeefGodot_Init can reach Native too. (The [Export] entry must stay in the DynamicLib.)
	String file_path = p_output_dir.path_join("Native.bf");
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Cannot write: " + file_path);

	f->store_string(
			"// Auto-generated by Godot-Beef. Godot API call surface, shared by all bindings.\n"
			"using System;\n"
			"using System.Collections;\n"
			"namespace Godot;\n"
			"\n"
			"/// Wrappers around Godot's C API, received at init time via BeefGodot_Init.\n"
			"static class Native\n"
			"{\n"
			"\t// --- Function pointers filled by BeefGodot_Init ---\n"
			"\tpublic static function void(char8* msg) sPrint;\n"
			"\tpublic static function void*(char8*, char8*) sGetMethodBind;\n"
			"\tpublic static function void(void*, void*, void**, void*) sMethodBindPtrcall;\n"
			"\tpublic static function void(void* dest, char8* utf8) sStrNew;\n"
			"\tpublic static function int64(void* str) sStrLen;\n"
			"\tpublic static function void(void* str, char8* buf, int64 len) sStrToUtf8;\n"
			"\tpublic static function void(void* str) sStrDel;\n"
			"\tpublic static function void(void* dest, char8* utf8) sSNNew;\n"
			"\tpublic static function int64(void* sn) sSNLen;\n"
			"\tpublic static function void(void* sn, char8* buf, int64 len) sSNToUtf8;\n"
			"\tpublic static function void(void* sn) sSNDel;\n"
			"\tpublic static function void(void* dest, char8* utf8) sNPNew;\n"
			"\tpublic static function int64(void* np) sNPLen;\n"
			"\tpublic static function void(void* np, char8* buf, int64 len) sNPToUtf8;\n"
			"\tpublic static function void(void* np) sNPDel;\n"
			"\tpublic static function void(void* dest, int64 type, void* data, int64 count) sPackedNew;\n"
			"\tpublic static function int64(void* p, int64 type) sPackedSize;\n"
			"\tpublic static function void(void* p, int64 type, void* outData, int64 count) sPackedCopy;\n"
			"\tpublic static function void(void* p, int64 type) sPackedDestroy;\n"
			"\tpublic static function void(void* dest) sPSANew;\n"
			"\tpublic static function void(void* p, char8* utf8) sPSAAppend;\n"
			"\tpublic static function int64(void* p) sPSASize;\n"
			"\tpublic static function int64(void* p, int64 idx) sPSAElemLen;\n"
			"\tpublic static function void(void* p, int64 idx, char8* buf, int64 len) sPSAElemUtf8;\n"
			"\tpublic static function void(void* p) sPSADestroy;\n"
			"\tpublic static function int64(void* obj, char8* buf, int64 len) sObjClass;\n"
			"\tpublic static function void(void* obj) sObjRef;\n"
			"\tpublic static function void(void* obj) sObjUnref;\n"
			"\tpublic static function void(void* obj) sObjTrack;\n"
			"\tpublic static function void*() sNextEvict;\n"
			"\tpublic static function void(void* obj) sObjFree;\n"
			"\tpublic static function int64(void* v) sVarType;\n"
			"\tpublic static function void(void* v) sVarDestroy;\n"
			"\tpublic static function void(void* dest, bool b) sVarFromBool;\n"
			"\tpublic static function bool(void* v) sVarAsBool;\n"
			"\tpublic static function void(void* dest, int64 i) sVarFromInt;\n"
			"\tpublic static function int64(void* v) sVarAsInt;\n"
			"\tpublic static function void(void* dest, double d) sVarFromFloat;\n"
			"\tpublic static function double(void* v) sVarAsFloat;\n"
			"\tpublic static function void(void* dest, char8* utf8) sVarFromString;\n"
			"\tpublic static function int64(void* v) sVarStrLen;\n"
			"\tpublic static function void(void* v, char8* buf, int64 len) sVarStrToUtf8;\n"
			"\tpublic static function void(void* dest, void* obj) sVarFromObject;\n"
			"\tpublic static function void*(void* v) sVarAsObject;\n"
			"\tpublic static function void(void* dest) sArrNew;\n"
			"\tpublic static function void(void* arr) sArrDestroy;\n"
			"\tpublic static function int64(void* arr) sArrSize;\n"
			"\tpublic static function void(void* arr, int64 idx, void* outVar) sArrGet;\n"
			"\tpublic static function void(void* arr, void* val) sArrPushBack;\n"
			"\tpublic static function void(void* dest) sDictNew;\n"
			"\tpublic static function void(void* dict) sDictDestroy;\n"
			"\tpublic static function int64(void* dict) sDictSize;\n"
			"\tpublic static function void(void* dict, void* key, void* outVar) sDictGet;\n"
			"\tpublic static function void(void* dict, void* key, void* val) sDictSet;\n"
			"\tpublic static function bool(void* dict, void* key) sDictHas;\n"
			"\tpublic static function void(void* dest, int64 type, void* src) sVarFromTyped;\n"
			"\tpublic static function void(void* v, int64 type, void* outVal) sVarAsTyped;\n"
			"\tpublic static function void(void* dest, void* arr) sVarFromArray;\n"
			"\tpublic static function void(void* v, void* outArr) sVarAsArray;\n"
			"\tpublic static function void(void* dest, void* dict) sVarFromDict;\n"
			"\tpublic static function void(void* v, void* outDict) sVarAsDict;\n"
			"\tpublic static function void(void* dest, char8* utf8) sVarFromStringName;\n"
			"\tpublic static function void(void* dest, char8* utf8) sVarFromNodePath;\n"
			"\tpublic static function void(void* dest, int64 type, void* data, int64 count) sVarFromPacked;\n"
			"\tpublic static function int64(void* v, int64 type) sVarPackedSize;\n"
			"\tpublic static function void(void* v, int64 type, void* outData, int64 count) sVarPackedCopy;\n"
			"\tpublic static function void(void* dest, void* psa) sVarFromPSA;\n"
			"\tpublic static function void(void* v, void* outPsa) sVarAsPSA;\n"
			"\tpublic static function void(void* obj, char8* name, void** args, int32 argc) sEmitSignal;\n"
			"\tpublic static function void(void* dest, void* obj, char8* method) sCallableNew;\n"
			"\tpublic static function void(void* callable) sCallableDestroy;\n"
			"\tpublic static function void*(char8* className) sInstantiate;\n"
			"\tpublic static function void(void* obj, char8* method, void** args, int32 argc, void* ret) sObjectCall;\n"
			"\tpublic static function void*(char8* name) sGetSingleton;\n"
			"\tpublic static function void(char8* name, void** args, int32 argc, void* ret) sCallUtility;\n"
			"\tpublic static function void(void* v, void* outBuf) sVarAsCallable;\n"
			"\tpublic static function void(void* dest, void* c) sVarFromCallable;\n"
			"\tpublic static function void(void* v, void* outBuf) sVarAsSignal;\n"
			"\tpublic static function void(void* dest, void* s) sVarFromSignal;\n"
			"\tpublic static function void(void* s) sSignalDestroy;\n"
			"\n"
			"\t// --- Live-object registry ---\n"
			"\t// Keeps a Beef reference for every object whose lifetime is managed by Godot.\n"
			"\t// Beef's debug runtime background scan finds allocations with no Beef-side\n"
			"\t// reference and calls DebugBreak. TrackObject/UntrackObject prevent that.\n"
			"\tstatic List<System.Object> sLiveObjects = new .() ~ delete _;\n"
			"\n"
			"\t// --- Print ---\n"
			"\t/// Print a string to Godot's output panel and console.\n"
			"\tpublic static void Print(System.String msg)\n"
			"\t{\n"
			"\t\tif (sPrint != null)\n"
			"\t\t\tsPrint(msg);\n"
			"\t}\n"
			"\n"
			"\t/// Formatted print — no `scope` and no `$` needed at the call site. Uses Beef's `{}` format\n"
			"\t/// placeholders, e.g.  Native.Print(\"pos ({}, {})\", mX, mY);\n"
			"\tpublic static void Print(System.StringView fmt, params System.Span<System.Object> args)\n"
			"\t{\n"
			"\t\tPrint(scope System.String()..AppendF(fmt, params args));\n"
			"\t}\n"
			"\n"
			"\t// --- Object lifetime tracking ---\n"
			"\t/// Call in BeefGodot_Create_* instead of returning Internal.UnsafeCastToPtr directly.\n"
			"\tpublic static void* TrackObject(System.Object obj)\n"
			"\t{\n"
			"\t\tsLiveObjects.Add(obj);\n"
			"\t\treturn Internal.UnsafeCastToPtr(obj);\n"
			"\t}\n"
			"\n"
			"\t/// Call in BeefGodot_Destroy_*. Releases the Beef reference; delete the returned object.\n"
			"\tpublic static System.Object UntrackObject(void* ptr)\n"
			"\t{\n"
			"\t\tlet obj = Internal.UnsafeCastToObject(ptr);\n"
			"\t\tsLiveObjects.Remove(obj);\n"
			"\t\treturn obj;\n"
			"\t}\n"
			"\n"
			"\t/// Get the Beef object from a void* handle without untracking it.\n"
			"\t/// Use this in Notification/Call handlers to recover 'this'.\n"
			"\tpublic static System.Object GetObject(void* ptr)\n"
			"\t{\n"
			"\t\treturn Internal.UnsafeCastToObject(ptr);\n"
			"\t}\n"
			"\n"
			"\t// --- Method dispatch ---\n"
			"\t/// Get a cached MethodBind* for a Godot class method. Store in a static field.\n"
			"\tpublic static void* GetMethodBind(char8* className, char8* methodName)\n"
			"\t{\n"
			"\t\treturn sGetMethodBind(className, methodName);\n"
			"\t}\n"
			"\n"
			"\t/// Call a Godot method via ptrcall (no Variant boxing; args are raw typed pointers).\n"
			"\t/// args[i] = pointer to argument i's data. ret = pointer to return value storage (null for void).\n"
			"\tpublic static void MethodBindPtrcall(void* methodBind, void* obj, void** args, void* ret)\n"
			"\t{\n"
			"\t\tsMethodBindPtrcall(methodBind, obj, args, ret);\n"
			"\t}\n"
			"\n"
			"\t// --- String marshalling (GodotStr is one-pointer engine-String storage) ---\n"
			"\t/// Construct an engine String into dest from a Beef string (for a ptrcall arg).\n"
			"\tpublic static void StrIn(System.String s, GodotStr* dest)\n"
			"\t{\n"
			"\t\tsStrNew(dest, s.CStr());\n"
			"\t}\n"
			"\n"
			"\t/// Construct an empty engine String into dest (to receive a ptrcall String return).\n"
			"\tpublic static void StrEmpty(GodotStr* dest)\n"
			"\t{\n"
			"\t\tsStrNew(dest, \"\");\n"
			"\t}\n"
			"\n"
			"\t/// Read an engine String into a newly-allocated Beef string. Caller owns it (delete).\n"
			"\tpublic static System.String StrOut(GodotStr* s)\n"
			"\t{\n"
			"\t\tlet result = new System.String();\n"
			"\t\tint len = (int)sStrLen(s);\n"
			"\t\tif (len > 0)\n"
			"\t\t{\n"
			"\t\t\tchar8* buf = result.PrepareBuffer(len);\n"
			"\t\t\tsStrToUtf8(s, buf, len);\n"
			"\t\t}\n"
			"\t\treturn result;\n"
			"\t}\n"
			"\n"
			"\t/// Destruct an engine String held in GodotStr storage.\n"
			"\tpublic static void StrDel(GodotStr* s)\n"
			"\t{\n"
			"\t\tsStrDel(s);\n"
			"\t}\n"
			"\n"
			"\t// --- StringName marshalling (same shape as String) ---\n"
			"\tpublic static void SNIn(System.String s, GodotSN* dest)\n"
			"\t{\n"
			"\t\tsSNNew(dest, s.CStr());\n"
			"\t}\n"
			"\n"
			"\tpublic static void SNEmpty(GodotSN* dest)\n"
			"\t{\n"
			"\t\tsSNNew(dest, \"\");\n"
			"\t}\n"
			"\n"
			"\tpublic static System.String SNOut(GodotSN* s)\n"
			"\t{\n"
			"\t\tlet result = new System.String();\n"
			"\t\tint len = (int)sSNLen(s);\n"
			"\t\tif (len > 0)\n"
			"\t\t{\n"
			"\t\t\tchar8* buf = result.PrepareBuffer(len);\n"
			"\t\t\tsSNToUtf8(s, buf, len);\n"
			"\t\t}\n"
			"\t\treturn result;\n"
			"\t}\n"
			"\n"
			"\tpublic static void SNDel(GodotSN* s)\n"
			"\t{\n"
			"\t\tsSNDel(s);\n"
			"\t}\n"
			"\n"
			"\t// --- NodePath marshalling (same shape as String/StringName) ---\n"
			"\tpublic static void NPIn(System.String s, GodotNP* dest)\n"
			"\t{\n"
			"\t\tsNPNew(dest, s.CStr());\n"
			"\t}\n"
			"\n"
			"\tpublic static void NPEmpty(GodotNP* dest)\n"
			"\t{\n"
			"\t\tsNPNew(dest, \"\");\n"
			"\t}\n"
			"\n"
			"\tpublic static System.String NPOut(GodotNP* s)\n"
			"\t{\n"
			"\t\tlet result = new System.String();\n"
			"\t\tint len = (int)sNPLen(s);\n"
			"\t\tif (len > 0)\n"
			"\t\t{\n"
			"\t\t\tchar8* buf = result.PrepareBuffer(len);\n"
			"\t\t\tsNPToUtf8(s, buf, len);\n"
			"\t\t}\n"
			"\t\treturn result;\n"
			"\t}\n"
			"\n"
			"\tpublic static void NPDel(GodotNP* s)\n"
			"\t{\n"
			"\t\tsNPDel(s);\n"
			"\t}\n"
			"\n"
			"\t// --- Packed-array marshalling (POD element buffers, memcpy'd) ---\n"
			"\t/// Construct an engine Packed*Array (of variantType) at dest from a Beef element array.\n"
			"\tpublic static void PackedIn<T>(T[] arr, int64 variantType, GodotPacked* dest)\n"
			"\t{\n"
			"\t\tsPackedNew(dest, variantType, ((arr != null) && (arr.Count > 0)) ? (void*)arr.Ptr : null, (arr != null) ? (int64)arr.Count : 0);\n"
			"\t}\n"
			"\n"
			"\t/// Construct an empty engine Packed*Array (of variantType) at dest (for a ptrcall return).\n"
			"\tpublic static void PackedEmpty(GodotPacked* dest, int64 variantType)\n"
			"\t{\n"
			"\t\tsPackedNew(dest, variantType, null, 0);\n"
			"\t}\n"
			"\n"
			"\t/// Read an engine Packed*Array into a fresh Beef element array. Caller owns it (delete).\n"
			"\tpublic static T[] PackedOut<T>(GodotPacked* src, int64 variantType)\n"
			"\t{\n"
			"\t\tint64 n = sPackedSize(src, variantType);\n"
			"\t\tT[] o = new T[(int)n];\n"
			"\t\tif (n > 0) sPackedCopy(src, variantType, (void*)o.Ptr, n);\n"
			"\t\treturn o;\n"
			"\t}\n"
			"\n"
			"\tpublic static void PackedDel(GodotPacked* p, int64 variantType)\n"
			"\t{\n"
			"\t\tsPackedDestroy(p, variantType);\n"
			"\t}\n"
			"\n"
			"\t// --- PackedStringArray marshalling (per-element strings) ---\n"
			"\t/// Build an engine PackedStringArray at dest from a Beef System.String[].\n"
			"\tpublic static void PSAIn(System.String[] arr, GodotPacked* dest)\n"
			"\t{\n"
			"\t\tsPSANew(dest);\n"
			"\t\tif (arr != null)\n"
			"\t\t\tfor (let s in arr)\n"
			"\t\t\t\tsPSAAppend(dest, (s != null) ? s.CStr() : \"\");\n"
			"\t}\n"
			"\n"
			"\tpublic static void PSAEmpty(GodotPacked* dest)\n"
			"\t{\n"
			"\t\tsPSANew(dest);\n"
			"\t}\n"
			"\n"
			"\t/// Read an engine PackedStringArray into a fresh Beef System.String[]. The caller owns the\n"
			"\t/// array AND each string (delete every element, then the array).\n"
			"\tpublic static System.String[] PSAOut(GodotPacked* src)\n"
			"\t{\n"
			"\t\tint64 n = sPSASize(src);\n"
			"\t\tSystem.String[] o = new System.String[(int)n];\n"
			"\t\tfor (int64 i = 0; i < n; i++)\n"
			"\t\t{\n"
			"\t\t\tlet s = new System.String();\n"
			"\t\t\tint len = (int)sPSAElemLen(src, i);\n"
			"\t\t\tif (len > 0)\n"
			"\t\t\t{\n"
			"\t\t\t\tchar8* buf = s.PrepareBuffer(len);\n"
			"\t\t\t\tsPSAElemUtf8(src, i, buf, len);\n"
			"\t\t\t}\n"
			"\t\t\to[(int)i] = s;\n"
			"\t\t}\n"
			"\t\treturn o;\n"
			"\t}\n"
			"\n"
			"\tpublic static void PSADel(GodotPacked* p)\n"
			"\t{\n"
			"\t\tsPSADestroy(p);\n"
			"\t}\n"
			"\n"
			"\t// --- Object* return wrapping ---\n"
			"\t// One Beef wrapper per engine Object*. The cache both avoids per-call allocation and\n"
			"\t// keeps a Beef reference so the debug runtime's scan does not flag the wrapper.\n"
			"\t// (Wrappers currently live for the process; engine-object-death eviction comes later.)\n"
			"\tstatic Dictionary<int, Godot.Object> sWrappers = new .() ~ delete _;\n"
			"\t// Evicted wrappers: kept referenced (no GC to reclaim them) and marked dead. We cannot\n"
			"\t// delete them — the user, or the in-progress call that freed the object, may still hold\n"
			"\t// the wrapper. This is a bounded leak (one per freed, previously-wrapped object).\n"
			"\tstatic List<Godot.Object> sDeadWrappers = new .() ~ delete _;\n"
			"\n"
			"\t// Shared lookup/create for the two Wrap* entry points: returns the cached wrapper, or builds\n"
			"\t// one of the object's actual runtime type (fallbackType if unknown) and caches it. isNew is\n"
			"\t// true only when a wrapper was just created, so callers add tracking/refs exactly once.\n"
			"\tstatic Godot.Object WrapCommon(void* ptr, System.StringView fallbackType, out bool isNew)\n"
			"\t{\n"
			"\t\tisNew = false;\n"
			"\t\tif (sWrappers.TryGetValue((int)ptr, let existing))\n"
			"\t\t\treturn existing;\n"
			"\t\tchar8[256] buf = ?;\n"
			"\t\tint len = (int)sObjClass(ptr, &buf[0], 256);\n"
			"\t\tif (len > 256) len = 256;\n"
			"\t\tGodot.Object w = GodotObjectFactory.Create(System.StringView(&buf[0], len), ptr);\n"
			"\t\tif (w == null)\n"
			"\t\t\tw = GodotObjectFactory.Create(fallbackType, ptr);\n"
			"\t\tif (w == null)\n"
			"\t\t\treturn null;\n"
			"\t\tsWrappers[(int)ptr] = w;\n"
			"\t\tisNew = true;\n"
			"\t\treturn w;\n"
			"\t}\n"
			"\n"
			"\t/// Wrap an engine Object* as a Beef wrapper of its actual runtime type (cached).\n"
			"\t/// fallbackType is the method's static return type, used if the runtime class is unknown.\n"
			"\tpublic static Godot.Object WrapObject(void* ptr, System.StringView fallbackType)\n"
			"\t{\n"
			"\t\tif (ptr == null)\n"
			"\t\t\treturn null;\n"
			"\t\tDrainEvictions(); // remove wrappers for any objects freed since the last wrap\n"
			"\t\tbool isNew = false;\n"
			"\t\tGodot.Object w = WrapCommon(ptr, fallbackType, out isNew);\n"
			"\t\t// On a fresh wrap, ask the engine to notify us when this object is freed so we can evict\n"
			"\t\t// the stale wrapper. (RefCounted objects use WrapRefCounted and are pinned by our own ref.)\n"
			"\t\tif (isNew && w != null && sObjTrack != null) sObjTrack(ptr);\n"
			"\t\treturn w;\n"
			"\t}\n"
			"\n"
			"\t/// Drain the engine's queue of freed tracked objects and evict their cached wrappers so a\n"
			"\t/// reused pointer never returns a stale wrapper. Pull-based: the engine free callback only\n"
			"\t/// queues a pointer (never re-enters Beef, which would deadlock inside ~Object); we drain\n"
			"\t/// here before each wrap. Evicted wrappers are marked dead and kept (cannot be deleted —\n"
			"\t/// the user may still hold them).\n"
			"\tpublic static void DrainEvictions()\n"
			"\t{\n"
			"\t\tif (sNextEvict == null) return;\n"
			"\t\twhile (true)\n"
			"\t\t{\n"
			"\t\t\tlet p = sNextEvict();\n"
			"\t\t\tif (p == null) break;\n"
			"\t\t\tif (sWrappers.GetAndRemove((int)p) case .Ok(let kv))\n"
			"\t\t\t{\n"
			"\t\t\t\tkv.value._godotOwner = null;\n"
			"\t\t\t\tsDeadWrappers.Add(kv.value);\n"
			"\t\t\t}\n"
			"\t\t}\n"
			"\t}\n"
			"\n"
			"\t/// Like WrapObject, but for a RefCounted return: the ptrcall handed us a +1 (our\n"
			"\t/// temporary storage never runs a Ref destructor), so balance it, and keep exactly one\n"
			"\t/// engine reference for the cache entry so the object stays alive while wrapped.\n"
			"\tpublic static Godot.Object WrapRefCounted(void* ptr, System.StringView fallbackType)\n"
			"\t{\n"
			"\t\tif (ptr == null)\n"
			"\t\t\treturn null;\n"
			"\t\tDrainEvictions();\n"
			"\t\tdefer sObjUnref(ptr); // release the reference the ptrcall return handed us\n"
			"\t\tbool isNew = false;\n"
			"\t\tGodot.Object w = WrapCommon(ptr, fallbackType, out isNew);\n"
			"\t\tif (isNew && w != null) sObjRef(ptr); // the cache entry holds one reference\n"
			"\t\treturn w;\n"
			"\t}\n"
			"\n"
			"\t/// Release every engine reference the wrapper cache holds and empty it. Called before\n"
			"\t/// DLL unload (shutdown or reload) so RefCounted objects the cache kept alive are freed.\n"
			"\t/// Also deletes the wrapper objects themselves: they are normally retained for the\n"
			"\t/// process lifetime (no GC, and the user may still hold one), so at teardown they would\n"
			"\t/// otherwise be reported as raw memory leaks. Wrappers have no destructor, so deleting\n"
			"\t/// only frees their Beef memory and never touches the underlying engine object.\n"
			"\tpublic static void ClearWrappers()\n"
			"\t{\n"
			"\t\t// RefCounted wrappers release their engine reference in ~this (balancing the one the\n"
			"\t\t// cache took); Node/plain wrappers have no ~this. So just delete every wrapper.\n"
			"\t\tfor (let w in sWrappers.Values)\n"
			"\t\t\tdelete w;\n"
			"\t\tsWrappers.Clear();\n"
			"\t\tfor (let w in sDeadWrappers)\n"
			"\t\t\tdelete w;\n"
			"\t\tsDeadWrappers.Clear();\n"
			"\t}\n"
			"\n"
			"\t/// Free a plain (non-RefCounted) engine object. Backs the generated Object.Free().\n"
			"\tpublic static void FreeObject(void* obj)\n"
			"\t{\n"
			"\t\tif (sObjFree != null) sObjFree(obj);\n"
			"\t}\n"
			"\n"
			"\t/// Increment / decrement a RefCounted's engine reference. Back the owning-handle\n"
			"\t/// Clone() and the RefCounted wrapper destructor.\n"
			"\tpublic static void ObjRef(void* obj) { if (sObjRef != null && obj != null) sObjRef(obj); }\n"
			"\tpublic static void ObjUnref(void* obj) { if (sObjUnref != null && obj != null) sObjUnref(obj); }\n"
			"\n"
			"\t/// Instantiate a fresh engine object of the given class; returns its Object* (or null).\n"
			"\t/// Backs the generated static New() factories.\n"
			"\tpublic static void* Instantiate(System.String className)\n"
			"\t{\n"
			"\t\treturn (sInstantiate != null) ? sInstantiate(className.CStr()) : null;\n"
			"\t}\n"
			"\n"
			"\t/// Get a Godot singleton object (Input, OS, Engine, ...) by name. Backs ClassName.Singleton.\n"
			"\tpublic static void* GetSingleton(System.String name)\n"
			"\t{\n"
			"\t\treturn (sGetSingleton != null) ? sGetSingleton(name.CStr()) : null;\n"
			"\t}\n"
			"\n"
			"\t/// Variant-based call (for variadic methods: rpc, call, emit_signal, ...). args is an\n"
			"\t/// array of Variant*; the return Variant is written into ret. Backs generated Rpc/Call/etc.\n"
			"\tpublic static void ObjectCall(void* obj, System.String method, void** args, int32 argc, void* ret)\n"
			"\t{\n"
			"\t\tif (sObjectCall != null) sObjectCall(obj, method.CStr(), args, argc, ret);\n"
			"\t}\n"
			"\n"
			"\t/// Call a global utility function (print, str, lerp, clamp, ...) by name. args is an\n"
			"\t/// array of Variant*; the return Variant is written into ret. Backs the generated GD class.\n"
			"\tpublic static void CallUtility(System.String name, void** args, int32 argc, void* ret)\n"
			"\t{\n"
			"\t\tif (sCallUtility != null) sCallUtility(name.CStr(), args, argc, ret);\n"
			"\t}\n"
			"\n"
			"\t/// Construct an empty engine Array/Dictionary into return storage before a ptrcall\n"
			"\t/// (ptrcall assigns into pre-constructed storage). Backs generated container returns.\n"
			"\tpublic static void ArrNew(void* dest) { if (sArrNew != null) sArrNew(dest); }\n"
			"\tpublic static void DictNew(void* dest) { if (sDictNew != null) sDictNew(dest); }\n"
			"\n"
			"\t/// Emit a signal on a Godot object. Pass the script's _godotOwner and the signal args.\n"
			"\tpublic static void EmitSignal(void* owner, System.String name, params System.Span<Godot.Variant> args)\n"
			"\t{\n"
			"\t\tif (sEmitSignal == null) return;\n"
			"\t\tvoid*[16] ptrs = ?;\n"
			"\t\tint argc = (args.Length < 16) ? args.Length : 16;\n"
			"\t\tfor (int i = 0; i < argc; i++)\n"
			"\t\t\tptrs[i] = &args[i];\n"
			"\t\tsEmitSignal(owner, name.CStr(), (argc > 0) ? &ptrs[0] : null, (int32)argc);\n"
			"\t}\n"
			"\n"
			"\t/// Build a Callable bound to owner's method (e.g. for Connect). Dispose it after use.\n"
			"\tpublic static Godot.Callable MakeCallable(void* owner, System.String method)\n"
			"\t{\n"
			"\t\tGodot.Callable c = ?;\n"
			"\t\tif (sCallableNew != null) sCallableNew(&c, owner, method.CStr());\n"
			"\t\treturn c;\n"
			"\t}\n"
			"}\n");

	return OK;
}

Error BeefBindingsGenerator::_generate_object_factory_file(const String &p_output_dir) {
	String file_path = p_output_dir.path_join("GodotObjectFactory.bf");
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Cannot write: " + file_path);

	StringBuilder sb;
	sb.append("// Auto-generated by Godot-Beef. Maps a runtime class name to its Beef wrapper type.\n");
	sb.append("// Used by Native.WrapObject to wrap an Object* return as its actual type.\n");
	sb.append("namespace Godot;\n\n");
	sb.append("static class GodotObjectFactory\n{\n");
	sb.append("\tpublic static Object Create(System.StringView className, void* owner)\n\t{\n");
	sb.append("\t\tswitch (className)\n\t\t{\n");

	LocalVector<StringName> class_names_vec;
	ClassDB::get_class_list(class_names_vec);
	List<StringName> class_names;
	for (const StringName &n : class_names_vec) {
		class_names.push_back(n);
	}
	class_names.sort_custom<StringName::AlphCompare>();

	for (const StringName &class_name : class_names) {
		if (!ClassDB::is_class_exposed(class_name)) {
			continue;
		}
		String cn = String(class_name);
		sb.append("\t\tcase \"");
		sb.append(cn);
		sb.append("\": return new ");
		sb.append(cn);
		sb.append("(owner);\n");
	}

	sb.append("\t\t}\n");
	sb.append("\t\treturn null; // unknown / unexposed class\n");
	sb.append("\t}\n}\n");

	f->store_string(sb.as_string());
	return OK;
}

Error BeefBindingsGenerator::_generate_utility_file(const String &p_output_dir) {
	String file_path = p_output_dir.path_join("GodotUtility.bf");
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Cannot write: " + file_path);

	StringBuilder sb;
	sb.append("// Auto-generated by Godot-Beef. The engine's global utility functions (print, str,\n");
	sb.append("// lerp, clamp, abs, sin, ...) exposed as a static GD class, dispatched via Variant.\n");
	sb.append("// Scalar args/returns (bool/int/float/String) are typed; everything else is Variant.\n");
	sb.append("// A returned System.String or Variant is owned by the caller (delete / Dispose it).\n");
	sb.append("namespace Godot;\n\n");
	sb.append("public static class GD\n{\n");

	// Map a Variant::Type to the Beef type we box/unbox directly; anything else stays a Variant.
	auto beef_t = [](Variant::Type t) -> String {
		switch (t) {
			case Variant::BOOL:
				return "bool";
			case Variant::INT:
				return "int64";
			case Variant::FLOAT:
				return "double";
			case Variant::STRING:
				return "System.String";
			default:
				return "Variant";
		}
	};

	List<StringName> fn_list;
	Variant::get_utility_function_list(&fn_list);
	fn_list.sort_custom<StringName::AlphCompare>();

	for (const StringName &fn : fn_list) {
		String snake = String(fn);
		String pascal = _to_pascal_case(snake);
		bool vararg = Variant::is_utility_function_vararg(fn);
		bool has_ret = Variant::has_utility_function_return_value(fn);
		String ret_type = has_ret ? beef_t(Variant::get_utility_function_return_type(fn)) : "void";
		int argc = vararg ? 0 : Variant::get_utility_function_argument_count(fn);

		// Signature.
		sb.append("\tpublic static " + ret_type + " " + pascal + "(");
		Vector<String> box_expr; // Variant-producing expression per fixed arg
		Vector<bool> boxed; // true if the temp is freshly allocated (must Dispose)
		if (vararg) {
			sb.append("params System.Span<Godot.Variant> @args");
		} else {
			for (int i = 0; i < argc; i++) {
				String bt = beef_t(Variant::get_utility_function_argument_type(fn, i));
				String an = Variant::get_utility_function_argument_name(fn, i);
				an = an.is_empty() ? ("arg" + itos(i)) : _escape_beef_identifier(an);
				if (i > 0) {
					sb.append(", ");
				}
				sb.append(bt + " " + an);
				if (bt == "bool") {
					box_expr.push_back("Godot.Variant.FromBool(" + an + ")");
					boxed.push_back(true);
				} else if (bt == "int64") {
					box_expr.push_back("Godot.Variant.FromInt(" + an + ")");
					boxed.push_back(true);
				} else if (bt == "double") {
					box_expr.push_back("Godot.Variant.FromFloat(" + an + ")");
					boxed.push_back(true);
				} else if (bt == "System.String") {
					box_expr.push_back("Godot.Variant.FromString(" + an + ")");
					boxed.push_back(true);
				} else {
					box_expr.push_back(an); // Variant passthrough — caller owns it, don't Dispose
					boxed.push_back(false);
				}
			}
		}
		sb.append(")\n\t{\n");

		// Build the Variant* argument array.
		if (vararg) {
			sb.append("\t\tint32 __n = (int32)@args.Length;\n");
			sb.append("\t\tvoid*[] __ptrs = scope void*[(__n > 0) ? __n : 1];\n");
			sb.append("\t\tfor (int __i = 0; __i < @args.Length; __i++) __ptrs[__i] = &@args[__i];\n");
		} else {
			for (int i = 0; i < argc; i++) {
				sb.append("\t\tvar __a" + itos(i) + " = " + box_expr[i] + ";\n");
			}
			sb.append("\t\tint32 __n = " + itos(argc) + ";\n");
			if (argc > 0) {
				sb.append("\t\tvoid*[" + itos(argc) + "] __ptrs = ?;\n");
				for (int i = 0; i < argc; i++) {
					sb.append("\t\t__ptrs[" + itos(i) + "] = &__a" + itos(i) + ";\n");
				}
			}
		}

		String args_ptr = vararg ? "(__n > 0) ? &__ptrs[0] : null" : (argc > 0 ? "&__ptrs[0]" : "null");
		sb.append("\t\tGodot.Variant __ret = default;\n");
		sb.append("\t\tNative.CallUtility(\"" + snake + "\", " + args_ptr + ", __n, &__ret);\n");

		// Dispose freshly-boxed temps (a String box owns a Variant String; int/float/bool boxes
		// hold nothing inline so Dispose is a no-op). Variant-passthrough args stay caller-owned.
		// Variadic functions CONSUME their args (like Call/Rpc): dispose each so implicit-conversion
		// temps (e.g. a String literal) are freed — do not Dispose args you pass to a vararg GD call.
		if (vararg) {
			sb.append("\t\tfor (int __i = 0; __i < @args.Length; __i++) @args[__i].Dispose();\n");
		} else {
			for (int i = 0; i < argc; i++) {
				if (boxed[i]) {
					sb.append("\t\t__a" + itos(i) + ".Dispose();\n");
				}
			}
		}

		if (ret_type == "void") {
			sb.append("\t\t__ret.Dispose();\n");
		} else if (ret_type == "bool") {
			sb.append("\t\tlet __r = __ret.AsBool(); __ret.Dispose(); return __r;\n");
		} else if (ret_type == "int64") {
			sb.append("\t\tlet __r = __ret.AsInt(); __ret.Dispose(); return __r;\n");
		} else if (ret_type == "double") {
			sb.append("\t\tlet __r = __ret.AsFloat(); __ret.Dispose(); return __r;\n");
		} else if (ret_type == "System.String") {
			sb.append("\t\tlet __r = __ret.AsString(); __ret.Dispose(); return __r;\n");
		} else { // Variant — caller owns it
			sb.append("\t\treturn __ret;\n");
		}
		sb.append("\t}\n\n");
	}

	sb.append("}\n");
	f->store_string(sb.as_string());
	return OK;
}

Error BeefBindingsGenerator::_generate_runtime_file(const String &p_output_dir) {
	// BeefGodot_Init must live in the user DynamicLib (src/) so [Export] makes it a real DLL
	// export. It populates Native (in the GodotBindings StaticLib, which this project
	// depends on) with the engine's C function table.
	String file_path = p_output_dir.path_join("GodotRuntime.bf");
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Cannot write: " + file_path);

	f->store_string(
			"// Auto-generated by Godot-Beef. DLL entry point that wires up Native.\n"
			// using System so the compiler-intrinsic [CLink]/[Export] resolve AND apply (the
			// qualified form [System.Export] resolves but does NOT add the symbol to the export table).
			"using System;\n"
			"namespace Godot;\n"
			"\n"
			"/// Called by Godot once after loading the DLL.\n"
			"/// Receives Godot's exported C function table (array of void* function pointers)\n"
			"/// and stores them in Native for use by Beef scripts.\n"
			"static class BeefGodotBridge\n"
			"{\n"
			"\t[CLink, Export]\n"
			"\tpublic static void BeefGodot_Init(void* godot_funcs, int32 count)\n"
			"\t{\n"
			"\t\t// Report Beef runtime crashes to the console (stdout) instead of a modal GUI popup,\n"
			"\t\t// so they show up in Godot's Output / captured logs and never block a headless or\n"
			"\t\t// multi-instance run.\n"
			"\t\tSystem.Runtime.SetCrashReportKind(.Console);\n"
			"\t\t// godot_funcs is a void*[] array; cast to int* for pointer-sized indexed access.\n"
			"\t\tvar funcs = (int*)godot_funcs;\n"
			"\t\tif (count > 0) Native.sPrint             = (.)funcs[0];\n"
			"\t\tif (count > 1) Native.sGetMethodBind     = (.)funcs[1];\n"
			"\t\tif (count > 2) Native.sMethodBindPtrcall = (.)funcs[2];\n"
			"\t\tif (count > 3) Native.sStrNew            = (.)funcs[3];\n"
			"\t\tif (count > 4) Native.sStrLen            = (.)funcs[4];\n"
			"\t\tif (count > 5) Native.sStrToUtf8         = (.)funcs[5];\n"
			"\t\tif (count > 6) Native.sStrDel            = (.)funcs[6];\n"
			"\t\tif (count > 7) Native.sSNNew             = (.)funcs[7];\n"
			"\t\tif (count > 8) Native.sSNLen             = (.)funcs[8];\n"
			"\t\tif (count > 9) Native.sSNToUtf8          = (.)funcs[9];\n"
			"\t\tif (count > 10) Native.sSNDel            = (.)funcs[10];\n"
			"\t\tif (count > 11) Native.sObjClass         = (.)funcs[11];\n"
			"\t\tif (count > 12) Native.sObjRef           = (.)funcs[12];\n"
			"\t\tif (count > 13) Native.sObjUnref         = (.)funcs[13];\n"
			"\t\tif (count > 14) Native.sObjTrack         = (.)funcs[14];\n"
			"\t\tif (count > 15) Native.sNextEvict        = (.)funcs[15];\n"
			"\t\tif (count > 16) Native.sObjFree          = (.)funcs[16];\n"
			"\t\tif (count > 17) Native.sVarType          = (.)funcs[17];\n"
			"\t\tif (count > 18) Native.sVarDestroy       = (.)funcs[18];\n"
			"\t\tif (count > 19) Native.sVarFromBool      = (.)funcs[19];\n"
			"\t\tif (count > 20) Native.sVarAsBool        = (.)funcs[20];\n"
			"\t\tif (count > 21) Native.sVarFromInt       = (.)funcs[21];\n"
			"\t\tif (count > 22) Native.sVarAsInt         = (.)funcs[22];\n"
			"\t\tif (count > 23) Native.sVarFromFloat     = (.)funcs[23];\n"
			"\t\tif (count > 24) Native.sVarAsFloat       = (.)funcs[24];\n"
			"\t\tif (count > 25) Native.sVarFromString    = (.)funcs[25];\n"
			"\t\tif (count > 26) Native.sVarStrLen        = (.)funcs[26];\n"
			"\t\tif (count > 27) Native.sVarStrToUtf8     = (.)funcs[27];\n"
			"\t\tif (count > 28) Native.sVarFromObject    = (.)funcs[28];\n"
			"\t\tif (count > 29) Native.sVarAsObject      = (.)funcs[29];\n"
			"\t\tif (count > 30) Native.sArrNew           = (.)funcs[30];\n"
			"\t\tif (count > 31) Native.sArrDestroy       = (.)funcs[31];\n"
			"\t\tif (count > 32) Native.sArrSize          = (.)funcs[32];\n"
			"\t\tif (count > 33) Native.sArrGet           = (.)funcs[33];\n"
			"\t\tif (count > 34) Native.sArrPushBack      = (.)funcs[34];\n"
			"\t\tif (count > 35) Native.sDictNew          = (.)funcs[35];\n"
			"\t\tif (count > 36) Native.sDictDestroy      = (.)funcs[36];\n"
			"\t\tif (count > 37) Native.sDictSize         = (.)funcs[37];\n"
			"\t\tif (count > 38) Native.sDictGet          = (.)funcs[38];\n"
			"\t\tif (count > 39) Native.sDictSet          = (.)funcs[39];\n"
			"\t\tif (count > 40) Native.sDictHas          = (.)funcs[40];\n"
			"\t\tif (count > 41) Native.sVarFromTyped     = (.)funcs[41];\n"
			"\t\tif (count > 42) Native.sVarAsTyped       = (.)funcs[42];\n"
			"\t\tif (count > 43) Native.sVarFromArray     = (.)funcs[43];\n"
			"\t\tif (count > 44) Native.sVarAsArray       = (.)funcs[44];\n"
			"\t\tif (count > 45) Native.sVarFromDict      = (.)funcs[45];\n"
			"\t\tif (count > 46) Native.sVarAsDict        = (.)funcs[46];\n"
			"\t\tif (count > 47) Native.sVarFromStringName = (.)funcs[47];\n"
			"\t\tif (count > 48) Native.sVarFromNodePath  = (.)funcs[48];\n"
			"\t\tif (count > 49) Native.sVarFromPacked    = (.)funcs[49];\n"
			"\t\tif (count > 50) Native.sVarPackedSize    = (.)funcs[50];\n"
			"\t\tif (count > 51) Native.sVarPackedCopy    = (.)funcs[51];\n"
			"\t\tif (count > 52) Native.sEmitSignal       = (.)funcs[52];\n"
			"\t\tif (count > 53) Native.sCallableNew      = (.)funcs[53];\n"
			"\t\tif (count > 54) Native.sCallableDestroy  = (.)funcs[54];\n"
			"\t\tif (count > 55) Native.sInstantiate      = (.)funcs[55];\n"
			"\t\tif (count > 56) Native.sObjectCall       = (.)funcs[56];\n"
			"\t\tif (count > 71) Native.sGetSingleton     = (.)funcs[71];\n"
			"\t\tif (count > 72) Native.sCallUtility      = (.)funcs[72];\n"
			"\t\tif (count > 73) Native.sVarAsCallable    = (.)funcs[73];\n"
			"\t\tif (count > 74) Native.sVarFromCallable  = (.)funcs[74];\n"
			"\t\tif (count > 75) Native.sVarAsSignal      = (.)funcs[75];\n"
			"\t\tif (count > 76) Native.sVarFromSignal    = (.)funcs[76];\n"
			"\t\tif (count > 77) Native.sSignalDestroy    = (.)funcs[77];\n"
			"\t\tif (count > 57) Native.sNPNew            = (.)funcs[57];\n"
			"\t\tif (count > 58) Native.sNPLen            = (.)funcs[58];\n"
			"\t\tif (count > 59) Native.sNPToUtf8         = (.)funcs[59];\n"
			"\t\tif (count > 60) Native.sNPDel            = (.)funcs[60];\n"
			"\t\tif (count > 61) Native.sPackedNew        = (.)funcs[61];\n"
			"\t\tif (count > 62) Native.sPackedSize       = (.)funcs[62];\n"
			"\t\tif (count > 63) Native.sPackedCopy       = (.)funcs[63];\n"
			"\t\tif (count > 64) Native.sPackedDestroy    = (.)funcs[64];\n"
			"\t\tif (count > 65) Native.sPSANew           = (.)funcs[65];\n"
			"\t\tif (count > 66) Native.sPSAAppend        = (.)funcs[66];\n"
			"\t\tif (count > 67) Native.sPSASize          = (.)funcs[67];\n"
			"\t\tif (count > 68) Native.sPSAElemLen       = (.)funcs[68];\n"
			"\t\tif (count > 69) Native.sPSAElemUtf8      = (.)funcs[69];\n"
			"\t\tif (count > 70) Native.sPSADestroy       = (.)funcs[70];\n"
			"\t\tif (count > 78) Native.sVarFromPSA       = (.)funcs[78];\n"
			"\t\tif (count > 79) Native.sVarAsPSA         = (.)funcs[79];\n"
			"\t}\n"
			"\n"
			"\t/// Called by the engine just before the DLL is unloaded (shutdown or reload) so the\n"
			"\t/// wrapper cache releases the engine references it holds.\n"
			"\t[CLink, Export]\n"
			"\tpublic static void BeefGodot_ClearWrappers()\n"
			"\t{\n"
			"\t\tNative.ClearWrappers();\n"
			"\t}\n"
			"\n"
			"\t/// Run the Beef runtime's leak scan ON DEMAND (vs. only at DLL detach during process\n"
			"\t/// exit, which is too late for a debugger to intercept). The engine calls this during\n"
			"\t/// shutdown - after instances + wrappers are freed - only when a debugger is attached, so\n"
			"\t/// the leak report's DebugBreak is delivered to it (emulating BeefIDE). DebugDumpLeaks\n"
			"\t/// DebugBreaks if anything remains, which would crash an undebugged process - hence the\n"
			"\t/// engine-side IsDebuggerPresent gate.\n"
			"\t[CLink, Export]\n"
			"\tpublic static void BeefGodot_ReportLeaks()\n"
			"\t{\n"
			"\t\tSystem.GC.DebugDumpLeaks();\n"
			"\t}\n"
			"}\n");

	return OK;
}

Error BeefBindingsGenerator::_generate_global_enums_file(const String &p_output_dir) {
	String file_path = p_output_dir.path_join("GodotEnums.bf");
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Cannot write: " + file_path);

	StringBuilder sb;
	sb.append("// Auto-generated by Godot-Beef. Global Godot enums.\n");
	sb.append("namespace Godot;\n\n");

	// Separate top-level enums (no dot) from dotted enums like "Variant.Type"
	HashMap<String, Vector<int>> top_enums;
	HashMap<String, HashMap<String, Vector<int>>> ext_enums; // prefix → (suffix → indices)

	int count = CoreConstants::get_global_constant_count();
	for (int i = 0; i < count; i++) {
		StringName enum_sn = CoreConstants::get_global_constant_enum(i);
		if (enum_sn == StringName()) {
			continue; // Not part of any enum — skip bare constants
		}
		String enum_name = String(enum_sn);
		int dot = enum_name.find(".");
		if (dot >= 0) {
			String prefix = enum_name.substr(0, dot);
			String suffix = enum_name.substr(dot + 1);
			ext_enums[prefix][suffix].push_back(i);
		} else {
			top_enums[enum_name].push_back(i);
		}
	}

	// Helper lambda to emit a single enum block
	auto emit_enum = [&](const String &name, const Vector<int> &indices, const String &indent) {
		sb.append(indent);
		sb.append("[System.AllowDuplicates]\n");
		sb.append(indent);
		sb.append("public enum ");
		sb.append(name);
		sb.append(" : int64\n");
		sb.append(indent);
		sb.append("{\n");
		for (int idx : indices) {
			sb.append(indent);
			sb.append("\t");
			sb.append(CoreConstants::get_global_constant_name(idx));
			sb.append(" = ");
			sb.append(itos(CoreConstants::get_global_constant_value(idx)));
			sb.append(",\n");
		}
		sb.append(indent);
		sb.append("}\n\n");
	};

	// Top-level enums
	List<String> sorted_top;
	for (const KeyValue<String, Vector<int>> &kv : top_enums) {
		sorted_top.push_back(kv.key);
	}
	sorted_top.sort();
	for (const String &name : sorted_top) {
		Vector<int> *p = top_enums.getptr(name);
		if (p) {
			emit_enum(name, *p, "");
		}
	}

	// Extension blocks for dotted enums (e.g. "Variant.Type" → extension Variant { enum Type })
	for (KeyValue<String, HashMap<String, Vector<int>>> &prefix_kv : ext_enums) {
		sb.append("extension ");
		sb.append(prefix_kv.key);
		sb.append("\n{\n");
		List<String> sorted_suffixes;
		for (const KeyValue<String, Vector<int>> &kv : prefix_kv.value) {
			sorted_suffixes.push_back(kv.key);
		}
		sorted_suffixes.sort();
		for (const String &suffix : sorted_suffixes) {
			Vector<int> *indices_ptr = prefix_kv.value.getptr(suffix);
			if (indices_ptr) {
				emit_enum(suffix, *indices_ptr, "\t");
			}
		}
		sb.append("}\n\n");
	}

	f->store_string(sb.as_string());
	return OK;
}

Error BeefBindingsGenerator::generate_bf_api(const String &p_output_dir) {
	ERR_FAIL_COND_V(!initialized, ERR_UNCONFIGURED);

	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	ERR_FAIL_COND_V(da.is_null(), ERR_CANT_CREATE);

	if (!DirAccess::exists(p_output_dir)) {
		Error err = da->make_dir_recursive(p_output_dir);
		ERR_FAIL_COND_V(err != OK, ERR_CANT_CREATE);
	}

	// Write GodotPrimitives.bf first — defines stub types for all Godot built-in types
	// (StringName, Vector2, etc.) so class binding files can reference them.
	_generate_primitives_file(p_output_dir);

	// Write GodotEnums.bf — all global Godot enums (Error, MouseButton, Key, etc.)
	_generate_global_enums_file(p_output_dir);

	// Write Native.bf — the Godot API call surface. Must live here (StaticLib) so the
	// generated binding classes can call Native.GetMethodBind / MethodBindPtrcall.
	_generate_godotnative_file(p_output_dir);

	// Write GodotObjectFactory.bf — runtime class name -> new wrapper, for Object* returns.
	_generate_object_factory_file(p_output_dir);

	// Write GodotUtility.bf — the static GD class wrapping the engine's global utility functions.
	_generate_utility_file(p_output_dir);

	LocalVector<StringName> class_names_vec;
	ClassDB::get_class_list(class_names_vec);
	List<StringName> class_names;
	for (const StringName &n : class_names_vec) {
		class_names.push_back(n);
	}
	class_names.sort_custom<StringName::AlphCompare>();

	int generated = 0;

	for (const StringName &class_name : class_names) {
		// Only generate for classes that have a Godot API type
		if (!ClassDB::is_class_exposed(class_name)) {
			continue;
		}

		StringBuilder output;
		Error err = _generate_bf_class(class_name, output);
		if (err != OK) {
			ERR_PRINT("Failed to generate bindings for class: " + String(class_name));
			continue;
		}

		String out_str = output.as_string();
		String file_path = p_output_dir.path_join(String(class_name) + ".bf");
		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
		ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Cannot write: " + file_path);

		f->store_string(out_str);
		generated++;
	}

	print_line("Beef bindings: generated " + itos(generated) + " class files.");
	return OK;
}

// ─── Command-line entry point ─────────────────────────────────────────────────

static String generate_beef_glue_option = "--generate-beef-glue";

static void _handle_cmdline(const String &p_glue_dir) {
	BeefBindingsGenerator generator;
	if (!generator.is_initialized()) {
		ERR_PRINT("Failed to initialize Beef bindings generator.");
		return;
	}

	if (generator.generate_bf_api(p_glue_dir) != OK) {
		ERR_PRINT(generate_beef_glue_option + ": Failed to generate Beef API bindings.");
	}
}

static String beef_leak_check_option = "--beef-leak-check";
static String beef_debug_option = "--beef-debug";
static String beef_complete_option = "--beef-complete";
static String beef_classify_bench_option = "--beef-classify-bench";
static String beef_inproc_build_option = "--beef-inproc-build";
static String beef_hot_test_option = "--beef-hot-test";

void BeefBindingsGenerator::handle_cmdline_args(const List<String> &p_cmdline_args) {
	const List<String>::Element *elem = p_cmdline_args.front();

	while (elem) {
		if (elem->get() == generate_beef_glue_option) {
			const List<String>::Element *path_elem = elem->next();
			if (path_elem) {
				_handle_cmdline(path_elem->get());
				elem = path_elem->next();
			} else {
				ERR_PRINT(generate_beef_glue_option + ": Missing output path argument.");
				elem = elem->next();
			}
		} else if (elem->get() == beef_leak_check_option) {
			// --beef-leak-check <game.exe> "<args>" : run the game under IDEHelper's debugger and
			// print the symbolicated Beef shutdown leak report (headless "run under BeefIDE"). The
			// args are ONE quoted string so this host doesn't parse the debuggee's own flags.
			const List<String>::Element *exe_elem = elem->next();
			if (exe_elem) {
				String exe = exe_elem->get();
				const List<String>::Element *args_elem = exe_elem->next();
				String args = args_elem ? args_elem->get() : String("--autoquit");
				Vector<String> messages;
				BeefIDEHelper::get_singleton()->run_leak_check(exe, args, exe.get_base_dir(), messages);
				// One-shot tool: terminate immediately rather than continue booting the editor. Hard
				// kill because IDEHelper keeps background threads alive — any normal/quick exit that
				// joins them hangs (the same reason we never FreeLibrary it).
				BeefIDEHelper::hard_exit(0); // TerminateProcess; orderly exit hangs on IDEHelper threads
			} else {
				ERR_PRINT(beef_leak_check_option + ": Missing <game.exe> argument.");
			}
			elem = elem->next();
		} else if (elem->get() == beef_inproc_build_option) {
			// --beef-inproc-build <workspace> <project> <out_dll> [hot] : compile+link the scripts DLL
			// fully in-process via IDEHelper (no BeefBuild.exe), to verify the in-process build path.
			const List<String>::Element *ws = elem->next();
			const List<String>::Element *pr = ws ? ws->next() : nullptr;
			const List<String>::Element *od = pr ? pr->next() : nullptr;
			if (od) {
				const List<String>::Element *hot = od->next();
				bool is_hot = hot && hot->get() == "hot";
				Vector<String> errors;
				bool ok = BeefIDEHelper::get_singleton()->build_dll(ws->get(), pr->get(), od->get(), is_hot, errors);
				for (int i = 0; i < errors.size(); i++) {
					print_line("[inproc-build] " + errors[i]);
				}
				print_line(ok ? "[inproc-build] SUCCESS" : "[inproc-build] FAILED");
				BeefIDEHelper::hard_exit(ok ? 0 : 1);
			} else {
				ERR_PRINT(beef_inproc_build_option + ": usage: --beef-inproc-build <workspace> <project> <out_dll> [hot]");
			}
			elem = elem->next();
		} else if (elem->get() == beef_hot_test_option) {
			// --beef-hot-test <workspace> <project> <game.exe> "<game_args>" <bf_file> <find> <replace>
			// Verifies in-editor hot reload end-to-end: establish an IDEHelper baseline, launch the game
			// under a hot-swap debug session, edit <bf_file> (find->replace), hot_reload, report HotLoad.
			Vector<String> a;
			for (const List<String>::Element *e = elem->next(); e && a.size() < 8; e = e->next()) {
				a.push_back(e->get());
			}
			if (a.size() >= 7) {
				BeefIDEHelper *ide = BeefIDEHelper::get_singleton();
				String ws = a[0], proj = a[1], game = a[2], gargs = a[3], bf = a[4], find = a[5], repl = a[6];
				bool selfdll = a.size() >= 8 && a[7] == "selfdll"; // build the loaded DLL in-process (same version)
				String build_dir = ws.path_join("build").path_join("HotTest");
				Vector<String> errs;
				// 1. Baseline (hotIdx 0). With selfdll, the SAME IDEHelper instance also LINKS the loaded
				// DLL and copies it over the game's DLL path, so build + deltas are the exact same compiler
				// version (tests the BeefBuild-vs-IDEHelper version-mismatch theory). Otherwise compile-only
				// baseline against whatever DLL the game already has (BeefBuild's).
				String loaded_dll = ws.path_join("build").path_join("Debug_Win64").path_join(proj).path_join(proj + ".dll");
				bool base_ok;
				if (selfdll) {
					String inproc = build_dir.path_join(proj).path_join(proj + ".dll");
					base_ok = ide->build_dll(ws, proj, inproc, true, errs);
					if (base_ok) {
						Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
						if (da.is_valid()) {
							da->copy(loaded_dll, loaded_dll + ".hotbak2");
							da->copy(inproc, loaded_dll);
							// Stage the version-matched bundle RT next to the loaded DLL (else the game loads a
							// stale Beef042RT64.dll already there and the scripts DLL fails to load).
							if (BeefCompiler *bc = BeefCompiler::get_singleton()) {
								String rt = bc->get_beef_build_path().get_base_dir().path_join("Beef042RT64.dll");
								if (FileAccess::exists(rt)) {
									da->copy(rt, loaded_dll.get_base_dir().path_join("Beef042RT64.dll"));
								}
							}
						}
						print_line("[hot-test] selfdll: copied IDEHelper-built DLL over the loaded path");
					}
				} else {
					base_ok = ide->build_hot_baseline(ws, proj, build_dir.path_join(proj), errs);
				}
				print_line(vformat("[hot-test] baseline build: %s", base_ok ? "OK" : "FAILED"));
				for (const String &e : errs) {
					print_line("[hot-test] " + e);
				}
				// 2. Launch the game under a hot-swap debug session and run it.
				if (base_ok && ide->debug_start(game, gargs, ws, true, loaded_dll)) {
					ide->debug_begin_run();
					Vector<String> msgs;
					bool hot_ok = false, edited = false;
					bool dll_seen = false;
						int edit_iter = 0;
					for (int i = 0; i < 4000; i++) {
						int st = ide->debug_poll(msgs);
						for (const String &m : msgs) {
							print_line("[hot-test][dbgmsg] " + m);
							if (m.to_lower().contains("godot-beef-test")) {
								dll_seen = true;
							}
						}
						msgs.clear();
						// Edit + hot-reload only AFTER the scripts DLL has actually loaded into the game.
						if (dll_seen && !edited) {
							// 3. Edit the .bf on disk (back it up first), then hot reload.
							String orig = FileAccess::get_file_as_string(bf);
							Ref<FileAccess> bak = FileAccess::open(bf + ".hotbak", FileAccess::WRITE);
							if (bak.is_valid()) {
								bak->store_string(orig);
								bak->close();
							}
							String mod = orig.replace(find, repl);
							Ref<FileAccess> wf = FileAccess::open(bf, FileAccess::WRITE);
							if (wf.is_valid()) {
								wf->store_string(mod);
								wf->close();
							}
							edited = true;
							edit_iter = i;
							Vector<String> herrs;
							Vector<String> chg;
							chg.push_back(bf.replace_char('\\', '/').to_lower());
							hot_ok = ide->hot_reload(ws, proj, build_dir, chg, herrs);
							for (const String &e : herrs) {
								print_line("[hot-test] " + e);
							}
							// restore the source
							Ref<FileAccess> rf = FileAccess::open(bf, FileAccess::WRITE);
							if (rf.is_valid()) {
								rf->store_string(orig);
								rf->close();
							}
						}
						if (st == BeefIDEHelper::RS_TERMINATED) {
							break;
						}
						if (edited && i >= edit_iter + 60) {
							break; // flushed the reload result, stop
						}
						OS::get_singleton()->delay_usec(2000);
					}
					ide->debug_stop();
					print_line(hot_ok ? "[hot-test] HOT RELOAD OK (call returned true)" : "[hot-test] HOT RELOAD FAILED");
				}
				if (selfdll) {
					// Restore the original (BeefBuild) loaded DLL.
					Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
					if (da.is_valid() && FileAccess::exists(loaded_dll + ".hotbak2")) {
						da->copy(loaded_dll + ".hotbak2", loaded_dll);
						da->remove(loaded_dll + ".hotbak2");
					}
				}
				BeefIDEHelper::hard_exit(0);
			} else {
				ERR_PRINT(beef_hot_test_option + ": usage: --beef-hot-test <workspace> <project> <game.exe> \"<args>\" <bf_file> <find> <replace>");
			}
			elem = elem->next();
		} else if (elem->get() == beef_debug_option) {
			// --beef-debug <game.exe> "<args>" <bf_file> <line> [condition] : launch the game under
			// IDEHelper's debugger with a breakpoint at <bf_file>:<line> (1-based), optionally only firing
			// when the Beef expression [condition] is true, run, and on each stop report the active
			// breakpoint line + the Beef call stack, then continue, until the game terminates. Headless
			// verification of the breakpoint/condition/stepping/call-stack engine.
			const List<String>::Element *exe_elem = elem->next();
			const List<String>::Element *args_elem = exe_elem ? exe_elem->next() : nullptr;
			const List<String>::Element *file_elem = args_elem ? args_elem->next() : nullptr;
			const List<String>::Element *line_elem = file_elem ? file_elem->next() : nullptr;
			const List<String>::Element *cond_elem = line_elem ? line_elem->next() : nullptr;
			if (exe_elem && args_elem && file_elem && line_elem) {
				String exe = exe_elem->get();
				String args = args_elem->get();
				String bf_file = file_elem->get();
				int line = line_elem->get().to_int();
				String condition = cond_elem ? cond_elem->get() : String();
				BeefIDEHelper *dbg = BeefIDEHelper::get_singleton();
				if (dbg->debug_start(exe, args, exe.get_base_dir())) {
					void *bp = dbg->debug_add_breakpoint(bf_file, line, condition);
					dbg->debug_begin_run();
					int stops = 0;
					bool did_step_demo = false;
					while (true) {
						Vector<String> messages;
						int state = dbg->debug_wait(messages, 60000);
						// Skip routine module/thread churn; surface only debugger events of interest.
						for (int i = 0; i < messages.size(); i++) {
							const String &m = messages[i];
							if (m.begins_with("msg Loading DLL") || m.begins_with("msg Unloading DLL") ||
									m.begins_with("msg Creating thread") || m.begins_with("msg Exiting thread") ||
									m == "modulesChanged") {
								continue;
							}
							print_line("[beef-dbg] " + m);
						}
						if (state == BeefIDEHelper::RS_TERMINATED) {
							print_line("[beef-dbg] debuggee terminated.");
							break;
						}
						if (state == BeefIDEHelper::RS_BREAKPOINT || state == BeefIDEHelper::RS_PAUSED || state == BeefIDEHelper::RS_EXCEPTION) {
							stops++;
							int bp_line = dbg->debug_active_breakpoint_line();
							int hit_count = dbg->debug_breakpoint_hit_count(bp);
							print_line(vformat("[beef-dbg] STOP #%d state=%d active_breakpoint_line=%d hit_count=%d", stops, state, bp_line, hit_count));
							Vector<BeefIDEHelper::DebugFrame> frames = dbg->debug_call_stack();
							print_line(vformat("[beef-dbg] call stack (%d frames):", frames.size()));
							for (int i = 0; i < frames.size() && i < 12; i++) {
								const BeefIDEHelper::DebugFrame &f = frames[i];
								print_line(vformat("[beef-dbg]   #%d %s  (%s:%d)", i, f.description, f.file, f.line));
							}
							// On the first real breakpoint hit, demonstrate variable inspection by evaluating a
							// few expressions in the top frame.
							if (state == BeefIDEHelper::RS_BREAKPOINT && !did_step_demo) {
								const char *exprs[] = { "mX", "mDir", "IsMine", "delta", "this", nullptr };
								for (int e = 0; exprs[e] != nullptr; e++) {
									String val = dbg->debug_evaluate(exprs[e], 0);
									print_line(vformat("[beef-dbg]   eval %s = %s", exprs[e], val));
								}
							}
							// On the first real breakpoint hit, demonstrate single-stepping: step over a few
							// source lines and show the top frame advancing, then fall through to continue.
							if (state == BeefIDEHelper::RS_BREAKPOINT && !did_step_demo) {
								did_step_demo = true;
								for (int s = 0; s < 3; s++) {
									dbg->debug_step_over();
									Vector<String> step_msgs;
									dbg->debug_wait(step_msgs, 10000);
									Vector<BeefIDEHelper::DebugFrame> sf = dbg->debug_call_stack();
									if (!sf.is_empty()) {
										print_line(vformat("[beef-dbg]   step-over %d -> %s (%s:%d)", s + 1,
												sf[0].description, sf[0].file, sf[0].line));
									}
								}
							}
							if (stops > 20) {
								print_line("[beef-dbg] stop cap reached; detaching.");
								break;
							}
							dbg->debug_continue();
						} else {
							break; // unexpected state; bail
						}
					}
					dbg->debug_stop();
				}
				BeefIDEHelper::hard_exit(0); // IDEHelper background threads hang an orderly exit
			} else {
				ERR_PRINT(beef_debug_option + ": usage: --beef-debug <game.exe> \"<args>\" <bf_file> <line>");
			}
			elem = elem->next();
		} else if (elem->get() == beef_complete_option) {
			// --beef-complete <bf_file> <cursor> : run semantic autocomplete at <cursor> (byte offset)
			// in <bf_file> and print the resolved completion entries. Headless verification of the
			// resolve-compiler autocomplete path.
			const List<String>::Element *file_elem = elem->next();
			const List<String>::Element *cur_elem = file_elem ? file_elem->next() : nullptr;
			if (file_elem && cur_elem) {
				String file = file_elem->get();
				int cursor = cur_elem->get().to_int();
				OS::get_singleton()->set_environment("BEEF_AC_VERBOSE", "1"); // surface resolve diagnostics
				Ref<FileAccess> f = FileAccess::open(file, FileAccess::READ);
				if (f.is_valid()) {
					String source = f->get_as_utf8_string();
					ProjectSettings *ps = ProjectSettings::get_singleton();
					String workspace = ps->globalize_path(String(ps->get_setting("beef/project/workspace_dir")));
					String project_name = ps->get_setting("beef/project/project_name");
					if (project_name.is_empty()) {
						project_name = "GameScripts";
					}
					// Run a few times: the first call pays the one-time build cost, later calls exercise the
					// persistent/incremental path (and must return identical results).
					Vector<BeefIDEHelper::Completion> entries;
					String call_hint;
					for (int run = 0; run < 3; run++) {
						entries.clear();
						call_hint = String();
						uint64_t t0 = OS::get_singleton()->get_ticks_msec();
						bool ok = BeefIDEHelper::get_singleton()->get_completions(workspace, project_name, file, source, cursor, entries, &call_hint);
						uint64_t dt = OS::get_singleton()->get_ticks_msec() - t0;
						print_line(vformat("[beef-complete] run=%d ok=%s entries=%d time=%dms", run, ok ? "yes" : "no", entries.size(), (int)dt));
					}
					if (!call_hint.is_empty()) {
						// Show 0xFFFF current-arg markers as [[ ]] so they're visible in the console.
						print_line("[beef-complete] call_hint=" + call_hint.replace(String::chr(0xFFFF), "|"));
					}
					for (int i = 0; i < entries.size() && i < 80; i++) {
						print_line(vformat("[beef-complete]   %s\t%s", entries[i].kind, entries[i].name));
					}
				} else {
					ERR_PRINT(beef_complete_option + ": cannot read " + file);
				}
				BeefIDEHelper::hard_exit(0);
			} else {
				ERR_PRINT(beef_complete_option + ": usage: --beef-complete <bf_file> <cursor>");
			}
			elem = elem->next();
		} else if (elem->get() == "--beef-symbol") {
			// --beef-symbol <bf_file> <cursor> : resolve the symbol at <cursor> (byte offset) and print its
			// owning type + ref kind. Headless verification of the hover/lookup_code (GetSymbolInfo) path.
			const List<String>::Element *file_elem = elem->next();
			const List<String>::Element *cur_elem = file_elem ? file_elem->next() : nullptr;
			if (file_elem && cur_elem) {
				String file = file_elem->get();
				int cursor = cur_elem->get().to_int();
				Ref<FileAccess> f = FileAccess::open(file, FileAccess::READ);
				if (f.is_valid()) {
					String source = f->get_as_utf8_string();
					ProjectSettings *ps = ProjectSettings::get_singleton();
					String workspace = ps->globalize_path(String(ps->get_setting("beef/project/workspace_dir")));
					String project_name = ps->get_setting("beef/project/project_name");
					if (project_name.is_empty()) {
						project_name = "GameScripts";
					}
					String type_full, kind;
					bool ok = BeefIDEHelper::get_singleton()->resolve_symbol(workspace, project_name, file, source, cursor, type_full, kind);
					print_line(vformat("[beef-symbol] ok=%s kind=%s type=%s", ok ? "yes" : "no", kind, type_full));
				} else {
					ERR_PRINT("--beef-symbol: cannot read " + file);
				}
				BeefIDEHelper::hard_exit(0);
			} else {
				ERR_PRINT("--beef-symbol: usage: --beef-symbol <bf_file> <cursor>");
			}
			elem = elem->next();
		} else if (elem->get() == beef_classify_bench_option) {
			// --beef-classify-bench <bf_file> : time syntax-highlight classify() over many iterations,
			// both BEFORE and AFTER building the resolve system, to see if the persistent system makes
			// the per-edit re-parse heavier.
			const List<String>::Element *file_elem = elem->next();
			if (file_elem) {
				String file = file_elem->get();
				Ref<FileAccess> f = FileAccess::open(file, FileAccess::READ);
				if (f.is_valid()) {
					String source = f->get_as_utf8_string();
					const int iters = 50;
					auto bench = [&](const char *label) {
						uint64_t t0 = OS::get_singleton()->get_ticks_usec();
						for (int i = 0; i < iters; i++) {
							Vector<uint8_t> kinds;
							BeefIDEHelper::get_singleton()->classify(source, "edit.bf", kinds);
						}
						uint64_t dt = OS::get_singleton()->get_ticks_usec() - t0;
						print_line(vformat("[classify-bench] %s: %.2f ms/call (%d lines, %d chars)", label,
								(double)dt / 1000.0 / iters, source.split("\n").size(), source.length()));
					};
					bench("cold (empty system)");
					ProjectSettings *ps = ProjectSettings::get_singleton();
					String workspace = ps->globalize_path(String(ps->get_setting("beef/project/workspace_dir")));
					String project_name = ps->get_setting("beef/project/project_name");
					if (project_name.is_empty()) {
						project_name = "GameScripts";
					}
					BeefIDEHelper::get_singleton()->prewarm(workspace, project_name);
					bench("after resolve build");
				} else {
					ERR_PRINT(beef_classify_bench_option + ": cannot read " + file);
				}
				BeefIDEHelper::hard_exit(0);
			} else {
				ERR_PRINT(beef_classify_bench_option + ": usage: --beef-classify-bench <bf_file>");
			}
			elem = elem->next();
		} else {
			elem = elem->next();
		}
	}
}

#endif // TOOLS_ENABLED
