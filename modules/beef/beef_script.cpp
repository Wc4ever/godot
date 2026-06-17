/**************************************************************************/
/*  beef_script.cpp                                                       */
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

#include "beef_script.h"

#include "compiler/beef_compiler.h"
#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_method_pointer.h"
#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "ide/beef_ide_helper.h"

#ifdef WINDOWS_ENABLED
// Forward-declared to avoid pulling <windows.h> into this large TU (it conflicts with engine code).
extern "C" __declspec(dllimport) int __stdcall IsDebuggerPresent(void);
#endif

// ─── Godot → Beef function table ─────────────────────────────────────────────
// These C functions are passed to the Beef DLL via BeefGodot_Init so Beef code
// can call back into Godot without linking against it directly.

static void _beef_godot_print(const char *p_msg) {
	print_line(String::utf8(p_msg));
}

// Returns the MethodBind* for a given class + method. Cached by the caller.
static void *_beef_get_method_bind(const char *p_class, const char *p_method) {
	return (void *)ClassDB::get_method(StringName(p_class), StringName(p_method));
}

// Calls a MethodBind via ptrcall. args[i] points to the raw data of argument i.
// r_ret points to storage for the return value (null for void methods).
static void _beef_method_bind_ptrcall(void *p_bind, void *p_obj, const void **p_args, void *r_ret) {
	reinterpret_cast<MethodBind *>(p_bind)->ptrcall(reinterpret_cast<Object *>(p_obj), p_args, r_ret);
}

// ─── String marshaling ──────────────────────────────────────────────────────
// A Godot String is a single pointer; the Beef side reserves storage and these
// functions placement-construct / read / destruct an engine String in it so it
// can be passed to ptrcall (which expects a pointer to a live String).

// Placement-construct a String at p_dest from a UTF-8 C string.
static void _beef_string_new_utf8(void *p_dest, const char *p_utf8) {
	memnew_placement(p_dest, String(String::utf8(p_utf8)));
}

// UTF-8 byte length (excluding null terminator) of the String at p_str.
static int64_t _beef_string_utf8_len(const void *p_str) {
	return reinterpret_cast<const String *>(p_str)->utf8().length();
}

// Copy up to p_len UTF-8 bytes of the String at p_str into p_buf (no null written).
static void _beef_string_to_utf8(const void *p_str, char *p_buf, int64_t p_len) {
	CharString cs = reinterpret_cast<const String *>(p_str)->utf8();
	int64_t n = MIN(p_len, (int64_t)cs.length());
	if (n > 0) {
		memcpy(p_buf, cs.get_data(), (size_t)n);
	}
}

// Destruct the String at p_str (releases its heap data).
static void _beef_string_destroy(void *p_str) {
	reinterpret_cast<String *>(p_str)->~String();
}

// ─── StringName marshaling ──────────────────────────────────────────────────
// Same shape as String (PtrToArgDirect, one pointer); read/written as UTF-8.

static void _beef_stringname_new_utf8(void *p_dest, const char *p_utf8) {
	memnew_placement(p_dest, StringName(String::utf8(p_utf8)));
}

static int64_t _beef_stringname_utf8_len(const void *p_sn) {
	return String(*reinterpret_cast<const StringName *>(p_sn)).utf8().length();
}

static void _beef_stringname_to_utf8(const void *p_sn, char *p_buf, int64_t p_len) {
	CharString cs = String(*reinterpret_cast<const StringName *>(p_sn)).utf8();
	int64_t n = MIN(p_len, (int64_t)cs.length());
	if (n > 0) {
		memcpy(p_buf, cs.get_data(), (size_t)n);
	}
}

static void _beef_stringname_destroy(void *p_sn) {
	reinterpret_cast<StringName *>(p_sn)->~StringName();
}

// NodePath: one-pointer engine type, marshaled as text like String/StringName (NodePath has an
// explicit operator String()).
static void _beef_nodepath_new_utf8(void *p_dest, const char *p_utf8) {
	memnew_placement(p_dest, NodePath(String::utf8(p_utf8)));
}

static int64_t _beef_nodepath_utf8_len(const void *p_np) {
	return ((String) * reinterpret_cast<const NodePath *>(p_np)).utf8().length();
}

static void _beef_nodepath_to_utf8(const void *p_np, char *p_buf, int64_t p_len) {
	CharString cs = ((String) * reinterpret_cast<const NodePath *>(p_np)).utf8();
	int64_t n = MIN(p_len, (int64_t)cs.length());
	if (n > 0) {
		memcpy(p_buf, cs.get_data(), (size_t)n);
	}
}

static void _beef_nodepath_destroy(void *p_np) {
	reinterpret_cast<NodePath *>(p_np)->~NodePath();
}

// ─── Object helpers ──────────────────────────────────────────────────────────

// Writes the runtime class name (UTF-8) of p_obj into p_buf; returns the full name length.
// Used to create a Beef wrapper of the object's actual type when marshaling an Object* return.
static int64_t _beef_object_class_name(const void *p_obj, char *p_buf, int64_t p_buflen) {
	if (!p_obj) {
		return 0;
	}
	String cls = reinterpret_cast<const Object *>(p_obj)->get_class();
	CharString cs = cls.utf8();
	int64_t n = MIN(p_buflen, (int64_t)cs.length());
	if (n > 0) {
		memcpy(p_buf, cs.get_data(), (size_t)n);
	}
	return cs.length();
}

// Increment the refcount if p_obj is RefCounted (no-op otherwise). Used so a wrapper-cache
// entry holds one engine reference, keeping a returned Resource alive while Beef wraps it.
static void _beef_object_reference(void *p_obj) {
	RefCounted *rc = Object::cast_to<RefCounted>(reinterpret_cast<Object *>(p_obj));
	if (rc) {
		rc->reference();
	}
}

// Decrement the refcount if p_obj is RefCounted; delete it if that drops the count to zero.
static void _beef_object_unreference(void *p_obj) {
	RefCounted *rc = Object::cast_to<RefCounted>(reinterpret_cast<Object *>(p_obj));
	if (rc && rc->unreference()) {
		memdelete(rc);
	}
}

// ─── Instance binding: notify Beef when a wrapped (non-RefCounted) object is freed ───────────
// Registering a binding with our token on an Object makes Godot call _beef_binding_free from
// ~Object(), letting the Beef wrapper cache evict the stale entry before the pointer is reused.
// (RefCounted objects are kept alive by Beef's own reference, so they don't need this.)
//
// The free callback must NOT re-enter the Beef DLL: it runs inside ~Object, often on the stack
// of a Beef-initiated ptrcall (e.g. wrapper.Free()), and re-entering Beef there deadlocks. So we
// only queue the freed pointer here; the Beef side drains the queue on its next wrap call.

static Vector<void *> s_pending_evictions;
static Mutex s_pending_evictions_mutex;

static void *_beef_binding_create(void *p_token, void *p_instance) {
	return p_instance; // non-null marker; the Beef cache owns the actual wrapper
}

static void _beef_binding_free(void *p_token, void *p_instance, void *p_binding) {
	MutexLock lock(s_pending_evictions_mutex);
	s_pending_evictions.push_back(p_instance);
}

static GDExtensionBool _beef_binding_reference(void *p_token, void *p_binding, GDExtensionBool p_reference) {
	return true; // not used — only non-RefCounted objects are bound
}

// Pop one freed-object pointer to evict from the Beef wrapper cache, or null if none pending.
// FIFO (oldest first): a freed Object's address can be reused by a new Object before its eviction
// is drained, so draining oldest-first minimizes the window in which a stale entry outlives a reuse.
static void *_beef_next_eviction() {
	MutexLock lock(s_pending_evictions_mutex);
	if (s_pending_evictions.is_empty()) {
		return nullptr;
	}
	void *p = s_pending_evictions[0];
	s_pending_evictions.remove_at(0);
	return p;
}

static const GDExtensionInstanceBindingCallbacks s_beef_binding_callbacks = {
	_beef_binding_create,
	_beef_binding_free,
	_beef_binding_reference,
};

static char s_beef_binding_token_storage;
static void *const s_beef_binding_token = &s_beef_binding_token_storage;

// Register our binding on p_obj so _beef_binding_free fires when it is destroyed.
static void _beef_object_track(void *p_obj) {
	if (p_obj) {
		reinterpret_cast<Object *>(p_obj)->get_instance_binding(s_beef_binding_token, &s_beef_binding_callbacks);
	}
}

// Free a plain Object/Node (the engine's Object.free()). RefCounted objects are managed by their
// reference count, never freed this way; this no-ops for them.
static void _beef_object_free(void *p_obj) {
	Object *o = reinterpret_cast<Object *>(p_obj);
	if (o && !Object::cast_to<RefCounted>(o)) {
		memdelete(o);
	}
}

// Instantiate a fresh Godot object of the named class (e.g. spawn a Node). Returns the new
// Object* — the caller owns it (add it to the scene tree, or Free() it). Returns null for
// abstract / non-instantiable classes. RefCounted classes are intentionally not created through
// here yet: their lifetime needs the reference-pinning path, so the generator only emits the
// New() factory for non-RefCounted classes.
static void *_beef_instantiate(const char *p_class) {
	StringName cn(p_class);
	if (!ClassDB::can_instantiate(cn)) {
		return nullptr;
	}
	Object *obj = ClassDB::instantiate(cn);
	// For RefCounted, take the first reference so the Beef owning handle holds exactly one ref
	// (the wrapper's destructor releases it). Mirrors how the C# binding ties a managed instance.
	if (RefCounted *rc = Object::cast_to<RefCounted>(obj)) {
		rc->init_ref();
	}
	return (void *)obj;
}

// Return a Godot singleton instance (Input, OS, Engine, ...) by name, or null. Backs the generated
// per-class static Singleton accessor.
static void *_beef_get_singleton(const char *p_name) {
	return (void *)Engine::get_singleton()->get_singleton_object(StringName(p_name));
}

// Variant-based method dispatch, for variadic methods (rpc, rpc_id, call, call_deferred,
// emit_signal, ...) that cannot go through fixed-arity ptrcall. p_args is an array of Variant*;
// the method's return value is written into the Variant at r_ret (NIL for void methods).
static void _beef_object_call(void *p_obj, const char *p_method, const void **p_args, int32_t p_argc, void *r_ret) {
	Object *o = reinterpret_cast<Object *>(p_obj);
	if (!o) {
		return;
	}
	const Variant **args = reinterpret_cast<const Variant **>(p_args);
	Callable::CallError err;
	Variant ret = o->callp(StringName(p_method), args, p_argc, err);
	if (r_ret) {
		*reinterpret_cast<Variant *>(r_ret) = ret;
	}
}

// Global utility function dispatch (print, str, lerp, clamp, abs, sin, ...). Mirrors
// _beef_object_call but routes through Variant::call_utility_function. p_args is an array of
// Variant*; the result is written into the Variant at r_ret (NIL for no-return functions).
static void _beef_call_utility(const char *p_name, const void **p_args, int32_t p_argc, void *r_ret) {
	const Variant **args = reinterpret_cast<const Variant **>(p_args);
	Callable::CallError err;
	Variant ret;
	Variant::call_utility_function(StringName(p_name), &ret, args, p_argc, err);
	if (r_ret) {
		*reinterpret_cast<Variant *>(r_ret) = ret;
	}
}

// ─── Variant marshaling ─────────────────────────────────────────────────────
// The Beef Variant is an opaque buffer the size of the engine Variant, so it crosses ptrcall
// by value. These functions convert between a Variant and concrete values; the Variant owns its
// resources (string/array/object), so the Beef side destroys Variants it holds.

static int64_t _beef_variant_get_type(const void *p_v) {
	return (int64_t)reinterpret_cast<const Variant *>(p_v)->get_type();
}
static void _beef_variant_destroy(void *p_v) {
	reinterpret_cast<Variant *>(p_v)->~Variant();
}
static void _beef_variant_from_bool(void *p_dest, bool p_b) {
	memnew_placement(p_dest, Variant(p_b));
}
static bool _beef_variant_as_bool(const void *p_v) {
	return reinterpret_cast<const Variant *>(p_v)->operator bool();
}
static void _beef_variant_from_int(void *p_dest, int64_t p_i) {
	memnew_placement(p_dest, Variant(p_i));
}
static int64_t _beef_variant_as_int(const void *p_v) {
	return reinterpret_cast<const Variant *>(p_v)->operator int64_t();
}
static void _beef_variant_from_float(void *p_dest, double p_d) {
	memnew_placement(p_dest, Variant(p_d));
}
static double _beef_variant_as_float(const void *p_v) {
	return reinterpret_cast<const Variant *>(p_v)->operator double();
}
static void _beef_variant_from_string(void *p_dest, const char *p_utf8) {
	memnew_placement(p_dest, Variant(String::utf8(p_utf8)));
}
static int64_t _beef_variant_string_utf8_len(const void *p_v) {
	return reinterpret_cast<const Variant *>(p_v)->operator String().utf8().length();
}
static void _beef_variant_string_to_utf8(const void *p_v, char *p_buf, int64_t p_len) {
	CharString cs = reinterpret_cast<const Variant *>(p_v)->operator String().utf8();
	int64_t n = MIN(p_len, (int64_t)cs.length());
	if (n > 0) {
		memcpy(p_buf, cs.get_data(), (size_t)n);
	}
}
static void _beef_variant_from_object(void *p_dest, void *p_obj) {
	memnew_placement(p_dest, Variant(reinterpret_cast<Object *>(p_obj)));
}
static void *_beef_variant_as_object(const void *p_v) {
	return reinterpret_cast<const Variant *>(p_v)->operator Object *();
}
// Callable / Signal cross the boundary as opaque buffers sized to the engine type. As* placement-
// constructs a copy into the Beef-side buffer (the Beef side owns it and must destroy it); From*
// boxes a copy into a Variant.
static void _beef_variant_as_callable(const void *p_v, void *p_out) {
	memnew_placement(p_out, Callable(reinterpret_cast<const Variant *>(p_v)->operator Callable()));
}
static void _beef_variant_from_callable(void *p_dest, const void *p_c) {
	memnew_placement(p_dest, Variant(*reinterpret_cast<const Callable *>(p_c)));
}
static void _beef_variant_as_signal(const void *p_v, void *p_out) {
	memnew_placement(p_out, Signal(reinterpret_cast<const Variant *>(p_v)->operator Signal()));
}
static void _beef_variant_from_signal(void *p_dest, const void *p_s) {
	memnew_placement(p_dest, Variant(*reinterpret_cast<const Signal *>(p_s)));
}
static void _beef_signal_destroy(void *p_s) {
	reinterpret_cast<Signal *>(p_s)->~Signal();
}

// Fixed-size value types (Vector*/Rect*/Color/Plane/Aabb/Basis/Quaternion/Transform*/Projection/Rid)
// share one pair of conversion functions keyed on the Variant::Type tag. The Beef structs are CRepr
// and laid out to match the engine, so p_src/p_out point straight at the engine type's bytes.
static void _beef_variant_from_typed(void *p_dest, int64_t p_type, const void *p_src) {
	switch ((Variant::Type)p_type) {
		case Variant::VECTOR2:
			memnew_placement(p_dest, Variant(*(const Vector2 *)p_src));
			break;
		case Variant::VECTOR2I:
			memnew_placement(p_dest, Variant(*(const Vector2i *)p_src));
			break;
		case Variant::RECT2:
			memnew_placement(p_dest, Variant(*(const Rect2 *)p_src));
			break;
		case Variant::RECT2I:
			memnew_placement(p_dest, Variant(*(const Rect2i *)p_src));
			break;
		case Variant::VECTOR3:
			memnew_placement(p_dest, Variant(*(const Vector3 *)p_src));
			break;
		case Variant::VECTOR3I:
			memnew_placement(p_dest, Variant(*(const Vector3i *)p_src));
			break;
		case Variant::TRANSFORM2D:
			memnew_placement(p_dest, Variant(*(const Transform2D *)p_src));
			break;
		case Variant::VECTOR4:
			memnew_placement(p_dest, Variant(*(const Vector4 *)p_src));
			break;
		case Variant::VECTOR4I:
			memnew_placement(p_dest, Variant(*(const Vector4i *)p_src));
			break;
		case Variant::PLANE:
			memnew_placement(p_dest, Variant(*(const Plane *)p_src));
			break;
		case Variant::QUATERNION:
			memnew_placement(p_dest, Variant(*(const Quaternion *)p_src));
			break;
		case Variant::AABB:
			memnew_placement(p_dest, Variant(*(const AABB *)p_src));
			break;
		case Variant::BASIS:
			memnew_placement(p_dest, Variant(*(const Basis *)p_src));
			break;
		case Variant::TRANSFORM3D:
			memnew_placement(p_dest, Variant(*(const Transform3D *)p_src));
			break;
		case Variant::PROJECTION:
			memnew_placement(p_dest, Variant(*(const Projection *)p_src));
			break;
		case Variant::COLOR:
			memnew_placement(p_dest, Variant(*(const Color *)p_src));
			break;
		case Variant::RID:
			memnew_placement(p_dest, Variant(*(const RID *)p_src));
			break;
		default:
			memnew_placement(p_dest, Variant());
			break;
	}
}
static void _beef_variant_as_typed(const void *p_v, int64_t p_type, void *p_out) {
	const Variant *v = reinterpret_cast<const Variant *>(p_v);
	switch ((Variant::Type)p_type) {
		case Variant::VECTOR2:
			*(Vector2 *)p_out = *v;
			break;
		case Variant::VECTOR2I:
			*(Vector2i *)p_out = *v;
			break;
		case Variant::RECT2:
			*(Rect2 *)p_out = *v;
			break;
		case Variant::RECT2I:
			*(Rect2i *)p_out = *v;
			break;
		case Variant::VECTOR3:
			*(Vector3 *)p_out = *v;
			break;
		case Variant::VECTOR3I:
			*(Vector3i *)p_out = *v;
			break;
		case Variant::TRANSFORM2D:
			*(Transform2D *)p_out = *v;
			break;
		case Variant::VECTOR4:
			*(Vector4 *)p_out = *v;
			break;
		case Variant::VECTOR4I:
			*(Vector4i *)p_out = *v;
			break;
		case Variant::PLANE:
			*(Plane *)p_out = *v;
			break;
		case Variant::QUATERNION:
			*(Quaternion *)p_out = *v;
			break;
		case Variant::AABB:
			*(AABB *)p_out = *v;
			break;
		case Variant::BASIS:
			*(Basis *)p_out = *v;
			break;
		case Variant::TRANSFORM3D:
			*(Transform3D *)p_out = *v;
			break;
		case Variant::PROJECTION:
			*(Projection *)p_out = *v;
			break;
		case Variant::COLOR:
			*(Color *)p_out = *v;
			break;
		case Variant::RID:
			*(RID *)p_out = *v;
			break;
		default:
			break;
	}
}

// Array/Dictionary box/unbox: these own engine resources, so the As side placement-constructs a
// fresh container into the (uninitialized) Beef storage rather than assigning into garbage.
static void _beef_variant_from_array(void *p_dest, const void *p_arr) {
	memnew_placement(p_dest, Variant(*(const Array *)p_arr));
}
static void _beef_variant_as_array(const void *p_v, void *p_out) {
	memnew_placement(p_out, Array(reinterpret_cast<const Variant *>(p_v)->operator Array()));
}
static void _beef_variant_from_dictionary(void *p_dest, const void *p_dict) {
	memnew_placement(p_dest, Variant(*(const Dictionary *)p_dict));
}
static void _beef_variant_as_dictionary(const void *p_v, void *p_out) {
	memnew_placement(p_out, Dictionary(reinterpret_cast<const Variant *>(p_v)->operator Dictionary()));
}

// StringName / NodePath box from UTF-8 text; they preserve the Variant type (STRING_NAME / NODE_PATH)
// so engine methods that distinguish those from a plain String get the right one. Reading back uses
// the existing string reader (Variant::operator String stringifies both to their text).
static void _beef_variant_from_stringname(void *p_dest, const char *p_utf8) {
	memnew_placement(p_dest, Variant(StringName(String::utf8(p_utf8))));
}
static void _beef_variant_from_nodepath(void *p_dest, const char *p_utf8) {
	memnew_placement(p_dest, Variant(NodePath(String::utf8(p_utf8))));
}

// Packed arrays: contiguous POD element buffers. One generic trio (keyed on Variant::Type) boxes a
// Beef heap array into a Variant, reports the element count, and copies elements back out. The Beef
// element structs are CRepr layout-matched, so the buffers memcpy directly. PackedStringArray is
// excluded (its elements need per-string marshaling).
template <typename TArr, typename TElem>
static void _packed_box(void *p_dest, const void *p_data, int64_t p_count) {
	TArr arr;
	arr.resize(p_count);
	if (p_count > 0) {
		memcpy(arr.ptrw(), p_data, (size_t)p_count * sizeof(TElem));
	}
	memnew_placement(p_dest, Variant(arr));
}
template <typename TArr, typename TElem>
static void _packed_unbox(const Variant *p_v, void *p_out, int64_t p_count) {
	TArr arr = p_v->operator TArr();
	int64_t n = MIN(p_count, (int64_t)arr.size());
	if (n > 0) {
		memcpy(p_out, arr.ptr(), (size_t)n * sizeof(TElem));
	}
}

static void _beef_variant_from_packed(void *p_dest, int64_t p_type, const void *p_data, int64_t p_count) {
	switch ((Variant::Type)p_type) {
		case Variant::PACKED_BYTE_ARRAY:
			_packed_box<PackedByteArray, uint8_t>(p_dest, p_data, p_count);
			break;
		case Variant::PACKED_INT32_ARRAY:
			_packed_box<PackedInt32Array, int32_t>(p_dest, p_data, p_count);
			break;
		case Variant::PACKED_INT64_ARRAY:
			_packed_box<PackedInt64Array, int64_t>(p_dest, p_data, p_count);
			break;
		case Variant::PACKED_FLOAT32_ARRAY:
			_packed_box<PackedFloat32Array, float>(p_dest, p_data, p_count);
			break;
		case Variant::PACKED_FLOAT64_ARRAY:
			_packed_box<PackedFloat64Array, double>(p_dest, p_data, p_count);
			break;
		case Variant::PACKED_VECTOR2_ARRAY:
			_packed_box<PackedVector2Array, Vector2>(p_dest, p_data, p_count);
			break;
		case Variant::PACKED_VECTOR3_ARRAY:
			_packed_box<PackedVector3Array, Vector3>(p_dest, p_data, p_count);
			break;
		case Variant::PACKED_COLOR_ARRAY:
			_packed_box<PackedColorArray, Color>(p_dest, p_data, p_count);
			break;
		case Variant::PACKED_VECTOR4_ARRAY:
			_packed_box<PackedVector4Array, Vector4>(p_dest, p_data, p_count);
			break;
		default:
			memnew_placement(p_dest, Variant());
			break;
	}
}
static int64_t _beef_variant_packed_size(const void *p_v, int64_t p_type) {
	const Variant *v = reinterpret_cast<const Variant *>(p_v);
	switch ((Variant::Type)p_type) {
		case Variant::PACKED_BYTE_ARRAY:
			return v->operator PackedByteArray().size();
		case Variant::PACKED_INT32_ARRAY:
			return v->operator PackedInt32Array().size();
		case Variant::PACKED_INT64_ARRAY:
			return v->operator PackedInt64Array().size();
		case Variant::PACKED_FLOAT32_ARRAY:
			return v->operator PackedFloat32Array().size();
		case Variant::PACKED_FLOAT64_ARRAY:
			return v->operator PackedFloat64Array().size();
		case Variant::PACKED_VECTOR2_ARRAY:
			return v->operator PackedVector2Array().size();
		case Variant::PACKED_VECTOR3_ARRAY:
			return v->operator PackedVector3Array().size();
		case Variant::PACKED_COLOR_ARRAY:
			return v->operator PackedColorArray().size();
		case Variant::PACKED_VECTOR4_ARRAY:
			return v->operator PackedVector4Array().size();
		default:
			return 0;
	}
}
static void _beef_variant_packed_copy(const void *p_v, int64_t p_type, void *p_out, int64_t p_count) {
	const Variant *v = reinterpret_cast<const Variant *>(p_v);
	switch ((Variant::Type)p_type) {
		case Variant::PACKED_BYTE_ARRAY:
			_packed_unbox<PackedByteArray, uint8_t>(v, p_out, p_count);
			break;
		case Variant::PACKED_INT32_ARRAY:
			_packed_unbox<PackedInt32Array, int32_t>(v, p_out, p_count);
			break;
		case Variant::PACKED_INT64_ARRAY:
			_packed_unbox<PackedInt64Array, int64_t>(v, p_out, p_count);
			break;
		case Variant::PACKED_FLOAT32_ARRAY:
			_packed_unbox<PackedFloat32Array, float>(v, p_out, p_count);
			break;
		case Variant::PACKED_FLOAT64_ARRAY:
			_packed_unbox<PackedFloat64Array, double>(v, p_out, p_count);
			break;
		case Variant::PACKED_VECTOR2_ARRAY:
			_packed_unbox<PackedVector2Array, Vector2>(v, p_out, p_count);
			break;
		case Variant::PACKED_VECTOR3_ARRAY:
			_packed_unbox<PackedVector3Array, Vector3>(v, p_out, p_count);
			break;
		case Variant::PACKED_COLOR_ARRAY:
			_packed_unbox<PackedColorArray, Color>(v, p_out, p_count);
			break;
		case Variant::PACKED_VECTOR4_ARRAY:
			_packed_unbox<PackedVector4Array, Vector4>(v, p_out, p_count);
			break;
		default:
			break;
	}
}

// ─── Packed arrays via ptrcall (raw Packed*Array, not Variant) ───────────────
// A Packed*Array is one pointer (CowData), like String. The Beef side reserves that storage and
// these placement-construct / read / destruct an engine Packed*Array in it, memcpying the
// contiguous POD element buffer (Beef element structs are CRepr layout-matched). PackedStringArray
// is excluded (non-POD elements).
template <typename TArr, typename TElem>
static void _packed_raw_new(void *p_dest, const void *p_data, int64_t p_count) {
	TArr *arr = memnew_placement(p_dest, TArr);
	arr->resize(p_count);
	if (p_count > 0 && p_data) {
		memcpy(arr->ptrw(), p_data, (size_t)p_count * sizeof(TElem));
	}
}
template <typename TArr, typename TElem>
static void _packed_raw_copy(const void *p_p, void *p_out, int64_t p_count) {
	const TArr *arr = reinterpret_cast<const TArr *>(p_p);
	int64_t n = MIN(p_count, (int64_t)arr->size());
	if (n > 0) {
		memcpy(p_out, arr->ptr(), (size_t)n * sizeof(TElem));
	}
}

#define BEEF_PACKED_DISPATCH(MACRO)                          \
	MACRO(PACKED_BYTE_ARRAY, PackedByteArray, uint8_t)       \
	MACRO(PACKED_INT32_ARRAY, PackedInt32Array, int32_t)     \
	MACRO(PACKED_INT64_ARRAY, PackedInt64Array, int64_t)     \
	MACRO(PACKED_FLOAT32_ARRAY, PackedFloat32Array, float)   \
	MACRO(PACKED_FLOAT64_ARRAY, PackedFloat64Array, double)  \
	MACRO(PACKED_VECTOR2_ARRAY, PackedVector2Array, Vector2) \
	MACRO(PACKED_VECTOR3_ARRAY, PackedVector3Array, Vector3) \
	MACRO(PACKED_VECTOR4_ARRAY, PackedVector4Array, Vector4) \
	MACRO(PACKED_COLOR_ARRAY, PackedColorArray, Color)

static void _beef_packed_new(void *p_dest, int64_t p_type, const void *p_data, int64_t p_count) {
	switch ((Variant::Type)p_type) {
#define _BPN(VT, TArr, TElem)                                  \
	case Variant::VT:                                          \
		_packed_raw_new<TArr, TElem>(p_dest, p_data, p_count); \
		break;
		BEEF_PACKED_DISPATCH(_BPN)
#undef _BPN
		default:
			break;
	}
}
static int64_t _beef_packed_size(const void *p_p, int64_t p_type) {
	switch ((Variant::Type)p_type) {
#define _BPS(VT, TArr, TElem) \
	case Variant::VT:         \
		return (int64_t)reinterpret_cast<const TArr *>(p_p)->size();
		BEEF_PACKED_DISPATCH(_BPS)
#undef _BPS
		default:
			return 0;
	}
}
static void _beef_packed_copy(const void *p_p, int64_t p_type, void *p_out, int64_t p_count) {
	switch ((Variant::Type)p_type) {
#define _BPC(VT, TArr, TElem)                               \
	case Variant::VT:                                       \
		_packed_raw_copy<TArr, TElem>(p_p, p_out, p_count); \
		break;
		BEEF_PACKED_DISPATCH(_BPC)
#undef _BPC
		default:
			break;
	}
}
static void _beef_packed_destroy(void *p_p, int64_t p_type) {
	switch ((Variant::Type)p_type) {
#define _BPD(VT, TArr, TElem)                   \
	case Variant::VT:                           \
		reinterpret_cast<TArr *>(p_p)->~TArr(); \
		break;
		BEEF_PACKED_DISPATCH(_BPD)
#undef _BPD
		default:
			break;
	}
}

// PackedStringArray: non-POD (elements are engine Strings), so it is built/read per element.
static void _beef_psa_new(void *p_dest) {
	memnew_placement(p_dest, PackedStringArray);
}
static void _beef_psa_append(void *p_p, const char *p_utf8) {
	reinterpret_cast<PackedStringArray *>(p_p)->push_back(String::utf8(p_utf8));
}
static int64_t _beef_psa_size(const void *p_p) {
	return (int64_t)reinterpret_cast<const PackedStringArray *>(p_p)->size();
}
static int64_t _beef_psa_elem_len(const void *p_p, int64_t p_idx) {
	const PackedStringArray *a = reinterpret_cast<const PackedStringArray *>(p_p);
	if (p_idx < 0 || p_idx >= a->size()) {
		return 0;
	}
	return (int64_t)(*a)[p_idx].utf8().length();
}
static void _beef_psa_elem_utf8(const void *p_p, int64_t p_idx, char *p_buf, int64_t p_len) {
	const PackedStringArray *a = reinterpret_cast<const PackedStringArray *>(p_p);
	if (p_idx < 0 || p_idx >= a->size()) {
		return;
	}
	CharString cs = (*a)[p_idx].utf8();
	int64_t n = MIN(p_len, (int64_t)cs.length());
	if (n > 0) {
		memcpy(p_buf, cs.get_data(), (size_t)n);
	}
}
static void _beef_psa_destroy(void *p_p) {
	reinterpret_cast<PackedStringArray *>(p_p)->~PackedStringArray();
}
// PackedStringArray <-> Variant (mirrors _beef_variant_from/as_array; per-element strings
// are handled by the GodotPacked the Beef side builds via PSAIn / reads via PSAOut).
static void _beef_variant_from_psa(void *p_dest, const void *p_psa) {
	memnew_placement(p_dest, Variant(*(const PackedStringArray *)p_psa));
}
static void _beef_variant_as_psa(const void *p_v, void *p_out) {
	memnew_placement(p_out, PackedStringArray(reinterpret_cast<const Variant *>(p_v)->operator PackedStringArray()));
}

// ─── Array marshaling ───────────────────────────────────────────────────────
// Array is one pointer (PtrToArgDirect); the Beef side reserves sizeof(Array) and these
// placement-construct / read / mutate / destruct an engine Array in it. Elements cross as
// Variants (the Beef side owns the out Variant and Disposes it).

static void _beef_array_new(void *p_dest) {
	memnew_placement(p_dest, Array);
}
static void _beef_array_destroy(void *p_arr) {
	reinterpret_cast<Array *>(p_arr)->~Array();
}
static int64_t _beef_array_size(const void *p_arr) {
	return (int64_t)reinterpret_cast<const Array *>(p_arr)->size();
}
static void _beef_array_get(const void *p_arr, int64_t p_idx, void *p_out) {
	const Array *a = reinterpret_cast<const Array *>(p_arr);
	memnew_placement(p_out, Variant(a->operator[]((int)p_idx)));
}
static void _beef_array_push_back(void *p_arr, const void *p_var) {
	reinterpret_cast<Array *>(p_arr)->push_back(*reinterpret_cast<const Variant *>(p_var));
}

// ─── Dictionary marshaling ──────────────────────────────────────────────────
// Same shape as Array (one pointer, PtrToArgDirect); keys/values cross as Variants.

static void _beef_dictionary_new(void *p_dest) {
	memnew_placement(p_dest, Dictionary);
}
static void _beef_dictionary_destroy(void *p_dict) {
	reinterpret_cast<Dictionary *>(p_dict)->~Dictionary();
}
static int64_t _beef_dictionary_size(const void *p_dict) {
	return (int64_t)reinterpret_cast<const Dictionary *>(p_dict)->size();
}
static void _beef_dictionary_get(const void *p_dict, const void *p_key, void *p_out) {
	const Dictionary *d = reinterpret_cast<const Dictionary *>(p_dict);
	memnew_placement(p_out, Variant(d->get(*reinterpret_cast<const Variant *>(p_key), Variant())));
}
static void _beef_dictionary_set(void *p_dict, const void *p_key, const void *p_val) {
	Dictionary *d = reinterpret_cast<Dictionary *>(p_dict);
	d->operator[](*reinterpret_cast<const Variant *>(p_key)) = *reinterpret_cast<const Variant *>(p_val);
}
static bool _beef_dictionary_has(const void *p_dict, const void *p_key) {
	return reinterpret_cast<const Dictionary *>(p_dict)->has(*reinterpret_cast<const Variant *>(p_key));
}

// Emit a signal on p_obj. args[i] points to a live engine Variant (the non-vararg core path).
static void _beef_emit_signal(void *p_obj, const char *p_name, const void **p_args, int32_t p_argc) {
	reinterpret_cast<Object *>(p_obj)->emit_signalp(StringName(String::utf8(p_name)),
			reinterpret_cast<const Variant **>(p_args), p_argc);
}

// Construct a Callable bound to p_obj's method p_method (e.g. a signal handler) at p_dest, and
// destroy one. Callable is a fixed-size struct, so it then crosses ptrcall by value to connect().
static void _beef_callable_new(void *p_dest, void *p_obj, const char *p_method) {
	memnew_placement(p_dest, Callable(reinterpret_cast<Object *>(p_obj), StringName(String::utf8(p_method))));
}
static void _beef_callable_destroy(void *p_callable) {
	reinterpret_cast<Callable *>(p_callable)->~Callable();
}

static const void *s_beef_godot_funcs[] = {
	(void *)_beef_godot_print, // 0: print(char8* msg)
	(void *)_beef_get_method_bind, // 1: get_method_bind(char8* class, char8* method) -> void*
	(void *)_beef_method_bind_ptrcall, // 2: method_bind_ptrcall(void* bind, void* obj, void** args, void* ret)
	(void *)_beef_string_new_utf8, // 3: string_new_utf8(void* dest, char8* utf8)
	(void *)_beef_string_utf8_len, // 4: string_utf8_len(void* str) -> int64
	(void *)_beef_string_to_utf8, // 5: string_to_utf8(void* str, char8* buf, int64 len)
	(void *)_beef_string_destroy, // 6: string_destroy(void* str)
	(void *)_beef_stringname_new_utf8, // 7: string_name_new_utf8(void* dest, char8* utf8)
	(void *)_beef_stringname_utf8_len, // 8: string_name_utf8_len(void* sn) -> int64
	(void *)_beef_stringname_to_utf8, // 9: string_name_to_utf8(void* sn, char8* buf, int64 len)
	(void *)_beef_stringname_destroy, // 10: string_name_destroy(void* sn)
	(void *)_beef_object_class_name, // 11: object_class_name(void* obj, char8* buf, int64 len) -> int64
	(void *)_beef_object_reference, // 12: object_reference(void* obj)   — RefCounted incref
	(void *)_beef_object_unreference, // 13: object_unreference(void* obj) — RefCounted decref (+delete at 0)
	(void *)_beef_object_track, // 14: object_track(void* obj)       — register death-notify binding
	(void *)_beef_next_eviction, // 15: next_eviction() -> void*      — pop a freed ptr to evict
	(void *)_beef_object_free, // 16: object_free(void* obj)        — Object.free() (plain objects)
	(void *)_beef_variant_get_type, // 17: variant_get_type(void* v) -> int64
	(void *)_beef_variant_destroy, // 18: variant_destroy(void* v)
	(void *)_beef_variant_from_bool, // 19: variant_from_bool(void* dest, bool b)
	(void *)_beef_variant_as_bool, // 20: variant_as_bool(void* v) -> bool
	(void *)_beef_variant_from_int, // 21: variant_from_int(void* dest, int64 i)
	(void *)_beef_variant_as_int, // 22: variant_as_int(void* v) -> int64
	(void *)_beef_variant_from_float, // 23: variant_from_float(void* dest, double d)
	(void *)_beef_variant_as_float, // 24: variant_as_float(void* v) -> double
	(void *)_beef_variant_from_string, // 25: variant_from_string(void* dest, char8* utf8)
	(void *)_beef_variant_string_utf8_len, // 26: variant_string_utf8_len(void* v) -> int64
	(void *)_beef_variant_string_to_utf8, // 27: variant_string_to_utf8(void* v, char8* buf, int64 len)
	(void *)_beef_variant_from_object, // 28: variant_from_object(void* dest, void* obj)
	(void *)_beef_variant_as_object, // 29: variant_as_object(void* v) -> void*
	(void *)_beef_array_new, // 30: array_new(void* dest)
	(void *)_beef_array_destroy, // 31: array_destroy(void* arr)
	(void *)_beef_array_size, // 32: array_size(void* arr) -> int64
	(void *)_beef_array_get, // 33: array_get(void* arr, int64 idx, void* out)
	(void *)_beef_array_push_back, // 34: array_push_back(void* arr, void* var)
	(void *)_beef_dictionary_new, // 35: dictionary_new(void* dest)
	(void *)_beef_dictionary_destroy, // 36: dictionary_destroy(void* dict)
	(void *)_beef_dictionary_size, // 37: dictionary_size(void* dict) -> int64
	(void *)_beef_dictionary_get, // 38: dictionary_get(void* dict, void* key, void* out)
	(void *)_beef_dictionary_set, // 39: dictionary_set(void* dict, void* key, void* val)
	(void *)_beef_dictionary_has, // 40: dictionary_has(void* dict, void* key) -> bool
	(void *)_beef_variant_from_typed, // 41: variant_from_typed(void* dest, int64 type, void* src)
	(void *)_beef_variant_as_typed, // 42: variant_as_typed(void* v, int64 type, void* out)
	(void *)_beef_variant_from_array, // 43: variant_from_array(void* dest, void* arr)
	(void *)_beef_variant_as_array, // 44: variant_as_array(void* v, void* out)
	(void *)_beef_variant_from_dictionary, // 45: variant_from_dictionary(void* dest, void* dict)
	(void *)_beef_variant_as_dictionary, // 46: variant_as_dictionary(void* v, void* out)
	(void *)_beef_variant_from_stringname, // 47: variant_from_stringname(void* dest, char8* utf8)
	(void *)_beef_variant_from_nodepath, // 48: variant_from_nodepath(void* dest, char8* utf8)
	(void *)_beef_variant_from_packed, // 49: variant_from_packed(void* dest, int64 type, void* data, int64 count)
	(void *)_beef_variant_packed_size, // 50: variant_packed_size(void* v, int64 type) -> int64
	(void *)_beef_variant_packed_copy, // 51: variant_packed_copy(void* v, int64 type, void* out, int64 count)
	(void *)_beef_emit_signal, // 52: emit_signal(void* obj, char8* name, void** args, int32 argc)
	(void *)_beef_callable_new, // 53: callable_new(void* dest, void* obj, char8* method)
	(void *)_beef_callable_destroy, // 54: callable_destroy(void* callable)
	(void *)_beef_instantiate, // 55: instantiate(char8* class) -> void* (new Object, caller owns)
	(void *)_beef_object_call, // 56: object_call(void* obj, char8* method, void** args, int32 argc, void* ret) — Variant dispatch
	(void *)_beef_nodepath_new_utf8, // 57: nodepath_new_utf8(void* dest, char8* utf8)
	(void *)_beef_nodepath_utf8_len, // 58: nodepath_utf8_len(void* np) -> int64
	(void *)_beef_nodepath_to_utf8, // 59: nodepath_to_utf8(void* np, char8* buf, int64 len)
	(void *)_beef_nodepath_destroy, // 60: nodepath_destroy(void* np)
	(void *)_beef_packed_new, // 61: packed_new(void* dest, int64 type, void* data, int64 count) — raw Packed*Array
	(void *)_beef_packed_size, // 62: packed_size(void* p, int64 type) -> int64
	(void *)_beef_packed_copy, // 63: packed_copy(void* p, int64 type, void* out, int64 count)
	(void *)_beef_packed_destroy, // 64: packed_destroy(void* p, int64 type)
	(void *)_beef_psa_new, // 65: psa_new(void* dest) — empty PackedStringArray
	(void *)_beef_psa_append, // 66: psa_append(void* p, char8* utf8)
	(void *)_beef_psa_size, // 67: psa_size(void* p) -> int64
	(void *)_beef_psa_elem_len, // 68: psa_elem_len(void* p, int64 idx) -> int64
	(void *)_beef_psa_elem_utf8, // 69: psa_elem_utf8(void* p, int64 idx, char8* buf, int64 len)
	(void *)_beef_psa_destroy, // 70: psa_destroy(void* p)
	(void *)_beef_get_singleton, // 71: get_singleton(char8* name) -> void* (Object, not owned)
	(void *)_beef_call_utility, // 72: call_utility_function(char8* name, Variant** args, int32 argc, Variant* ret)
	(void *)_beef_variant_as_callable, // 73: variant_as_callable(Variant* v, Callable* out)
	(void *)_beef_variant_from_callable, // 74: variant_from_callable(Variant* dest, Callable* c)
	(void *)_beef_variant_as_signal, // 75: variant_as_signal(Variant* v, Signal* out)
	(void *)_beef_variant_from_signal, // 76: variant_from_signal(Variant* dest, Signal* s)
	(void *)_beef_signal_destroy, // 77: signal_destroy(Signal* s)
	(void *)_beef_variant_from_psa, // 78: variant_from_psa(void* dest, void* psa)
	(void *)_beef_variant_as_psa, // 79: variant_as_psa(void* v, void* out)
};
static const int32_t s_beef_godot_funcs_count = sizeof(s_beef_godot_funcs) / sizeof(s_beef_godot_funcs[0]);

#ifdef TOOLS_ENABLED
#include "editor/beef_export_plugin.h"
#include "editor/bindings_generator.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/settings/editor_settings.h"
#endif

// ─── BeefLanguage singleton ───────────────────────────────────────────────────

BeefLanguage *BeefLanguage::singleton = nullptr;

BeefLanguage::BeefLanguage() {
	singleton = this;
}

BeefLanguage::~BeefLanguage() {
	// Belt-and-suspenders: destroy remaining instances before the singleton is gone.
	// Normally finish() covers this, but ~BeefLanguage() fires at SCENE uninit,
	// giving one more chance before CORE uninit calls FreeLibrary.
	_pre_unload_destroy_instances();
	singleton = nullptr;
}

// ─── BeefLanguage ─────────────────────────────────────────────────────────────

String BeefLanguage::get_name() const {
	return "Beef";
}

void BeefLanguage::init() {
	// Project settings (saved in project.godot, per-project)
	GLOBAL_DEF("beef/project/workspace_dir", "res://beef");
	GLOBAL_DEF("beef/project/project_name", "");
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "beef/build/build_config", PROPERTY_HINT_ENUM, "Debug,Release"), "Debug");
	// Extra arguments appended to the BeefBuild command line (project-wide — everyone building the
	// project should compile with the same flags). Space-separated.
	GLOBAL_DEF("beef/build/extra_args", "");
	// Machine- and workflow-specific settings (BeefBuild path, the external-build workflow, the web SDK
	// paths, regenerate-bindings-on-start) are per-MACHINE EditorSettings, registered in
	// _editor_init_callback — they must not be committed into the shared project.godot.

	// Register a pre-unload callback so live Beef objects are destroyed immediately before
	// every FreeLibrary call — including mid-session DLL reloads, not just at shutdown.
	// This prevents Beef's debug runtime from reporting them as memory leaks at DLL_PROCESS_DETACH.
	if (BeefCompiler *c = BeefCompiler::get_singleton()) {
		c->set_pre_unload_callback([]() {
			if (BeefLanguage *lang = BeefLanguage::get_singleton()) {
				lang->_pre_unload_destroy_instances();
			}
		});
	}

#ifdef TOOLS_ENABLED
	// EditorSettings is not yet available here — defer to after EditorNode is created.
	EditorNode::add_init_callback(&BeefLanguage::_editor_init_callback);
#endif
}

#ifdef TOOLS_ENABLED
void BeefLanguage::_editor_init_callback() {
	// This runs after EditorNode (and EditorSettings) are fully initialized.
	EDITOR_DEF("beef/editor/beef_build_path", "");
	EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
			Variant::STRING, "beef/editor/beef_build_path", PROPERTY_HINT_GLOBAL_FILE, "*.exe"));

	// Build ownership (per machine — one dev may drive builds from BeefIDE, another from the engine).
	// false (default) = Godot-driven: the engine runs BeefBuild and loads the result. true = externally
	// built: the engine does NOT compile; it loads the DLL another tool (e.g. BeefIDE) already built.
	EDITOR_DEF("beef/editor/external_build", false);

	// Skip the ~1027-file Godot API binding regeneration on editor start once they are stable (faster
	// startup). Bindings are still (re)generated when missing or when this is on. Re-enable / use the
	// Build flow after a Godot version bump so the bindings track the engine.
	EDITOR_DEF("beef/editor/regenerate_bindings_on_start", true);

	// Auto hot-patch a saved .bf into the live game during a debug session. Off = saving a .bf does not
	// patch the running game (restart to pick up changes). Per machine (a dev preference).
	EDITOR_DEF("beef/editor/hot_reload", true);

	// Web (WebAssembly) export SDK paths (per machine — they point at YOUR Emscripten SDK + Beef wasm
	// runtime objects). Leave empty to auto-detect emcc from BeefBuild's Emscripten path / PATH.
	EDITOR_DEF("beef/editor/web/emscripten_dir", "");
	EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
			Variant::STRING, "beef/editor/web/emscripten_dir", PROPERTY_HINT_GLOBAL_DIR));
	EDITOR_DEF("beef/editor/web/wasm_runtime_dir", "");
	EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
			Variant::STRING, "beef/editor/web/wasm_runtime_dir", PROPERTY_HINT_GLOBAL_DIR));

	// Register the export plugin (EditorExport exists by the time this init callback runs) so
	// exported games bundle the Beef scripts DLL + runtime next to the executable.
	if (EditorExport::get_singleton()) {
		Ref<BeefExportPlugin> export_plugin;
		export_plugin.instantiate();
		EditorExport::get_singleton()->add_export_plugin(export_plugin);
	}

	// Re-run BeefBuild discovery with the user-configured override (if any).
	BeefCompiler *compiler = BeefCompiler::get_singleton();
	if (compiler) {
		String override_path = EDITOR_GET("beef/editor/beef_build_path");
		if (!override_path.is_empty() || !compiler->is_found()) {
			compiler->find_beef_tools(override_path);
		}

		// Auto-create beef project structure in the Godot project if it doesn't exist yet.
		String workspace_dir = ProjectSettings::get_singleton()->globalize_path(
				String(ProjectSettings::get_singleton()->get_setting("beef/project/workspace_dir")));

		String project_name = ProjectSettings::get_singleton()->get_setting("beef/project/project_name");
		if (project_name.is_empty()) {
			project_name = ProjectSettings::get_singleton()->get_setting("application/config/name");
		}
		if (project_name.is_empty()) {
			project_name = "GameScripts";
		}

		compiler->ensure_project_files(workspace_dir, project_name);

		// Regenerate the Godot API bindings (.bf) into GodotBindings/src/ on editor start so stale
		// bindings from a previous build never cause BeefBuild to fail with thousands of errors (which
		// would deadlock the output pipe and crash the editor). Honor the per-machine opt-out, but ALWAYS
		// (re)generate when the bindings are missing (a marker file is absent), so a skip can't leave the
		// project unbuildable.
		String bindings_src = BeefCompiler::get_bindings_src_dir(workspace_dir);
		bool regen_bindings = true;
		if (EditorSettings::get_singleton()) {
			regen_bindings = (bool)EDITOR_GET("beef/editor/regenerate_bindings_on_start");
		}
		BeefBindingsGenerator gen;
		if (regen_bindings || !FileAccess::exists(bindings_src.path_join("GodotPrimitives.bf"))) {
			// Clear old bindings first so we never leave a mix of old and new files.
			if (DirAccess::exists(bindings_src)) {
				Ref<DirAccess> da = DirAccess::open(bindings_src);
				if (da.is_valid()) {
					// Collect names first, then delete (safe iteration)
					Vector<String> to_delete;
					da->list_dir_begin();
					String fname = da->get_next();
					while (!fname.is_empty()) {
						if (!da->current_is_dir() && fname.ends_with(".bf")) {
							to_delete.push_back(fname);
						}
						fname = da->get_next();
					}
					da->list_dir_end();
					for (const String &f : to_delete) {
						da->remove(f);
					}
				}
			}

			print_line("BeefScript: generating Godot API bindings");
			gen.generate_bf_api(bindings_src);

			// Copy the hand-written math value-type extensions (Vector2.Length(), constructors, operators,
			// ...) alongside the generated bindings; they extend the [CRepr] layout stubs.
			gen.copy_math_glue(bindings_src);
		}

		// Generated support files live in src/BeefGen/ — under the DynamicLib src/ (BeefBuild compiles
		// src/ recursively, so [CLink, Export] still reaches the DLL) but out of the user's way. A
		// .gdignore keeps Godot from scanning them as script resources.
		String user_src = workspace_dir.path_join("src");
		String gen_dir = user_src.path_join("BeefGen");
		DirAccess::make_dir_recursive_absolute(gen_dir);
		if (!FileAccess::exists(gen_dir.path_join(".gdignore"))) {
			FileAccess::open(gen_dir.path_join(".gdignore"), FileAccess::WRITE);
		}

		// Remove any stale copies left in src/ from older layouts (they'd double-define the types),
		// along with their .uid companions.
		for (const String &stale : { "GodotRuntime.bf", "GodotScript.bf", "GodotRegistrations.bf" }) {
			for (const String &f : { user_src.path_join(stale), user_src.path_join(stale + ".uid") }) {
				if (FileAccess::exists(f)) {
					DirAccess::remove_absolute(f);
				}
			}
		}

		gen.generate_runtime_glue(gen_dir);
		gen.generate_script_base(gen_dir);
		// Auto-generate the registrars for every [GodotScript] class (scanning all of src/), so users
		// never write one.
		gen.generate_registrations(user_src, gen_dir);

		// Allow a fresh compile now that bindings are up to date.
		compiler->unblock_compile();

		// Pre-warm the semantic-completion resolve system at idle (after the editor window is up), so the
		// first code completion is instant instead of paying the one-time ~1.4s build then. Deferred +
		// main-thread for now; see the TODO in BeefIDEHelper::prewarm about moving it off-thread.
		callable_mp_static(&BeefLanguage::_prewarm_resolve).bind(workspace_dir, project_name).call_deferred();
	}
}

void BeefLanguage::_prewarm_resolve(const String &p_workspace, const String &p_project) {
	if (p_workspace.is_empty() || !DirAccess::dir_exists_absolute(p_workspace)) {
		return; // no Beef workspace on disk yet — nothing to resolve against
	}
	BeefIDEHelper::get_singleton()->prewarm(p_workspace, p_project);
}

bool BeefLanguage::build_project(bool p_rebuild, String &r_output, Vector<String> &r_errors) {
	BeefCompiler *compiler = BeefCompiler::get_singleton();
	if (!compiler) {
		r_errors.push_back("Beef compiler unavailable.");
		return false;
	}

	String workspace, project, config;
	bool external = false;
	if (!_resolve_build_settings(workspace, project, config, external)) {
		r_errors.push_back("Beef workspace directory is not configured.");
		return false;
	}

	// Externally-built workflow: the engine must not run BeefBuild (another tool owns the build).
	// Treat it as success so pressing Play is not blocked; just report what will be loaded.
	if (external) {
		r_output = "External build mode (beef/editor/external_build = true): the engine does not compile.\n"
				   "Build with your external tool (e.g. BeefIDE). The engine loads:\n  " +
				BeefCompiler::get_dll_path(workspace, project, config);
		return true;
	}

	if (!compiler->is_found()) {
		String msg = "BeefBuild.exe not found. Set beef/editor/beef_build_path or install Beef IDE.";
		r_errors.push_back(msg);
		r_output = msg;
		return false;
	}

	// NOTE: the editor loads the DLL from a shadow copy (see reload()), so the real output file is
	// not locked and BeefBuild can relink it WITHOUT us unloading first. We deliberately never
	// FreeLibrary the Beef debug runtime mid-session — that crashes (its background scan thread).
	// The editor therefore keeps its already-loaded (now older) library until restart; the running
	// game loads the freshly built DLL in its own process on Play.
	if (p_rebuild) {
		compiler->clean_build(workspace, config);
	}

	// A manual build is an explicit retry — clear any prior "blocked" state from a failed compile.
	compiler->unblock_compile();

	// Refresh the auto-generated [GodotScript] registrars so classes added since editor start build
	// without requiring a restart.
	{
		String user_src = workspace.path_join("src");
		BeefBindingsGenerator gen;
		gen.generate_registrations(user_src, user_src.path_join("BeefGen"));
	}

	String dll_path;
	bool ok = compiler->compile(workspace, project, config, dll_path, r_errors, &r_output);
	return ok;
}

void BeefLanguage::clean_project() {
	BeefCompiler *compiler = BeefCompiler::get_singleton();
	if (!compiler) {
		return;
	}
	String workspace, project, config;
	bool external = false;
	if (!_resolve_build_settings(workspace, project, config, external)) {
		return;
	}
	// The real output is not locked (the editor holds a shadow copy), so just delete the build
	// output. No unload/FreeLibrary — that is unsafe with the Beef debug runtime.
	compiler->clean_build(workspace, config);
}
#endif

// Not editor-gated: complete_code() (compiled into template/runtime builds too) resolves the
// workspace/project/config from here. Only reads ProjectSettings, so it carries no tools dependency.
bool BeefLanguage::_resolve_build_settings(String &r_workspace, String &r_project, String &r_config, bool &r_external) const {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	r_workspace = ps->globalize_path(String(ps->get_setting("beef/project/workspace_dir")));

	r_project = ps->get_setting("beef/project/project_name");
	if (r_project.is_empty()) {
		r_project = ps->get_setting("application/config/name");
	}
	if (r_project.is_empty()) {
		r_project = "GameScripts";
	}

	r_config = ps->get_setting("beef/build/build_config");
	if (r_config.is_empty()) {
		r_config = "Debug";
	}

	// external_build is a per-machine EditorSetting and only meaningful in the editor. At game/template
	// runtime (no EditorSettings) it is always false — the exported game just loads its bundled DLL.
	r_external = false;
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton()) {
		r_external = (bool)EDITOR_GET("beef/editor/external_build");
	}
#endif
	return !r_workspace.is_empty();
}

String BeefLanguage::get_type() const {
	return "BeefScript";
}

String BeefLanguage::get_extension() const {
	return "bf";
}

String BeefLanguage::get_preferred_script_directory() const {
	// Beef only compiles sources under the workspace's user-project src/ folder, so new scripts must
	// default there (not the scene directory). Mirrors the workspace_dir project setting.
	String workspace = ProjectSettings::get_singleton()->get_setting("beef/project/workspace_dir");
	if (workspace.is_empty()) {
		workspace = "res://beef";
	}
	return workspace.path_join("src");
}

String BeefLanguage::validate_path(const String &p_path) const {
	// Lock new scripts to the Beef source folder (and its subfolders): BeefBuild only compiles src/,
	// so a script created elsewhere would never build. This blocks the create dialog's Create button.
	String src_dir = get_preferred_script_directory();
	if (src_dir.is_empty()) {
		return String();
	}
	String dir = ProjectSettings::get_singleton()->localize_path(src_dir).trim_suffix("/") + "/";
	String path = ProjectSettings::get_singleton()->localize_path(p_path);
	if (!path.begins_with(dir)) {
		return vformat(RTR("Beef scripts must be created under \"%s\"."), dir);
	}
	return String();
}

void BeefLanguage::finish() {
	// Destroy all live Beef objects before the DLL is unloaded.
	// Without this, Beef's debug runtime detects them as memory leaks at DLL_PROCESS_DETACH
	// and triggers a DebugBreak crash.
	_pre_unload_destroy_instances();
}

void BeefLanguage::_pre_unload_destroy_instances() {
	for (BeefInstance *inst : _live_instances) {
		inst->_destroy_beef_obj();
	}

	// Release the engine references held by the Object* wrapper cache (frees RefCounted objects
	// the cache kept alive) before the DLL is unloaded/reloaded.
	if (BeefCompiler *compiler = BeefCompiler::get_singleton()) {
		if (void *fn = compiler->get_proc("BeefGodot_ClearWrappers")) {
			((void (*)())fn)();
		}
	}

#ifdef WINDOWS_ENABLED
	// Emulate BeefIDE's shutdown leak report. The Beef runtime's automatic leak scan runs at DLL
	// detach during process exit (LdrShutdownProcess), too late for a debugger to receive its
	// DebugBreak. Here — after instances + wrappers are freed, while the process is still alive — we
	// trigger the scan on demand so the break IS delivered to an attached debugger. Gated on a
	// debugger being present: the scan DebugBreaks on remaining allocations, which would crash an
	// undebugged game.
	if (::IsDebuggerPresent()) {
		if (BeefCompiler *compiler = BeefCompiler::get_singleton()) {
			if (void *fn = compiler->get_proc("BeefGodot_ReportLeaks")) {
				print_line("BeefScript: debugger attached - running on-demand leak scan");
				((void (*)())fn)();
			}
		}
	}
#endif

	// Drop any queued evictions referring to the (about-to-be-unloaded) DLL's wrappers.
	{
		MutexLock lock(s_pending_evictions_mutex);
		s_pending_evictions.clear();
	}
}

Vector<String> BeefLanguage::get_reserved_words() const {
	return {
		"namespace",
		"using",
		"class",
		"struct",
		"interface",
		"enum",
		"public",
		"private",
		"protected",
		"internal",
		"static",
		"readonly",
		"abstract",
		"virtual",
		"override",
		"sealed",
		"extern",
		"var",
		"let",
		"if",
		"else",
		"for",
		"while",
		"do",
		"switch",
		"case",
		"default",
		"break",
		"continue",
		"return",
		"new",
		"delete",
		"null",
		"true",
		"false",
		"this",
		"base",
		"typeof",
		"sizeof",
		"in",
		"out",
		"ref",
		"params",
		"delegate",
		"function",
	};
}

bool BeefLanguage::is_control_flow_keyword(const String &p_string) const {
	return p_string == "if" || p_string == "else" || p_string == "for" ||
			p_string == "while" || p_string == "do" || p_string == "switch" ||
			p_string == "break" || p_string == "continue" || p_string == "return";
}

Vector<String> BeefLanguage::get_comment_delimiters() const {
	return { "//", "/* */" };
}

Vector<String> BeefLanguage::get_doc_comment_delimiters() const {
	return { "///" };
}

Vector<String> BeefLanguage::get_string_delimiters() const {
	return { "\" \"", "' '" };
}

bool BeefLanguage::validate(const String &p_script, const String &p_path, List<String> *r_functions, List<ScriptError> *r_errors, List<Warning> *r_warnings, HashSet<int> *r_safe_lines) const {
	// Use IDEHelper's parser for real syntax diagnostics. Semantic/whole-project errors still come
	// from BeefBuild (the Problems panel); this is the fast per-edit syntax check.
	BeefIDEHelper *ide = BeefIDEHelper::get_singleton();
	if (!ide->is_available()) {
		return true; // no parser available — don't block saving/editing
	}

	Vector<BeefIDEHelper::ParseError> errors;
	if (!ide->get_parse_errors(p_script, p_path, errors)) {
		return true;
	}

	bool valid = true;
	for (const BeefIDEHelper::ParseError &e : errors) {
		if (e.is_warning) {
			if (r_warnings) {
				Warning w;
				w.start_line = e.line;
				w.end_line = e.line;
				w.code = 0;
				w.message = e.message;
				r_warnings->push_back(w);
			}
		} else {
			valid = false;
			if (r_errors) {
				ScriptError se;
				se.path = p_path; // must match the script path, or the editor treats it as an external
				se.line = e.line; // (depended) error and drops it, so nothing is shown.
				se.column = e.column;
				se.message = e.message;
				r_errors->push_back(se);
			}
		}
	}
	return valid;
}

// Defined further below (with the other .bf source-parsing helpers); forward-declared here so the
// language hooks above the definitions can use it.
static bool _has_tool_marker(const String &p_source);
static bool _is_ident_char(char32_t c);

Script *BeefLanguage::create_script() const {
	return memnew(BeefScript);
}

String BeefLanguage::get_global_class_name(const String &p_path, String *r_base_type, String *r_icon_path, bool *r_is_abstract, bool *r_is_tool) const {
	// Generated support files are never user script classes; skip them so a `[GodotScript]` mention in
	// their comments/examples can't be misread as a class definition.
	String file = p_path.get_file();
	if (file == "GodotRuntime.bf" || file == "GodotScript.bf" || file == "GodotRegistrations.bf" || file == "GodotBridge.bf") {
		return String();
	}

	// Read the .bf source and extract the registered global class name + base type, so the editor
	// can list Beef script types in the Create-Node / Add-Script dialogs without compiling.
	String source = FileAccess::get_file_as_string(p_path);
	if (source.is_empty()) {
		return String();
	}
	String name, base;
	if (!BeefScript::parse_global_class(source, name, base)) {
		return String();
	}
	if (r_base_type) {
		*r_base_type = base;
	}
	if (r_icon_path) {
		*r_icon_path = String();
	}
	if (r_is_abstract) {
		*r_is_abstract = false;
	}
	if (r_is_tool) {
		*r_is_tool = _has_tool_marker(source);
	}
	return name;
}

bool BeefLanguage::handles_global_class_type(const String &p_type) const {
	return p_type == "BeefScript";
}

Ref<Script> BeefLanguage::make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
	Ref<BeefScript> script;
	script.instantiate();

	String base = p_base_class_name.is_empty() ? String("Godot.Object") : p_base_class_name;

	String src = p_template;
	if (src.is_empty()) {
		// Safety net when no template is supplied: still emit a working [GodotScript] class.
		src =
				"using Godot;\n"
				"using System;\n"
				"\n"
				"[GodotScript]\n"
				"class %CLASS% : %BASE%\n"
				"{\n"
				"}\n";
	}
	src = src.replace("%BASE%", base);
	src = src.replace("%CLASS%", p_class_name);

	script->set_source_code(src);
	return script;
}

Vector<ScriptLanguage::ScriptTemplate> BeefLanguage::get_built_in_templates(const StringName &p_object) {
	Vector<ScriptTemplate> templates;

	// Default: a [GodotScript] class extending the attached node's type, with the common lifecycle
	// overrides. The [GodotScript] marker is all that's needed — the engine glue is generated for you.
	{
		ScriptTemplate t;
		t.inherit = String(p_object);
		t.name = "Default";
		t.description = "Node with the common lifecycle methods";
		t.content =
				"using Godot;\n"
				"using System;\n"
				"\n"
				"[GodotScript]\n"
				"class %CLASS% : %BASE%\n"
				"{\n"
				"\t// Called when the node enters the scene tree for the first time.\n"
				"\tpublic override void _Ready()\n"
				"\t{\n"
				"\t}\n"
				"\n"
				"\t// Called every frame. 'delta' is the time since the previous frame.\n"
				"\tpublic override void _Process(double delta)\n"
				"\t{\n"
				"\t}\n"
				"}\n";
		t.id = 0;
		t.origin = TEMPLATE_BUILT_IN;
		templates.push_back(t);
	}

	// Empty: just the class and its mandatory registrar.
	{
		ScriptTemplate t;
		t.inherit = String(p_object);
		t.name = "Empty";
		t.description = "Empty script";
		t.content =
				"using Godot;\n"
				"using System;\n"
				"\n"
				"[GodotScript]\n"
				"class %CLASS% : %BASE%\n"
				"{\n"
				"}\n";
		t.id = 1;
		t.origin = TEMPLATE_BUILT_IN;
		templates.push_back(t);
	}

	return templates;
}

bool BeefLanguage::supports_builtin_mode() const {
	return false;
}

int BeefLanguage::find_function(const String &p_function, const String &p_code) const {
	return BeefIDEHelper::get_singleton()->find_function(p_function, p_code);
}

String BeefLanguage::make_function(const String &p_class, const String &p_name, const PackedStringArray &p_args) const {
	return String();
}

void BeefLanguage::auto_indent_code(String &p_code, int p_from_line, int p_to_line) const {
	// Indentation unit: respect the editor's tab/space setting where available.
	String unit = "\t";
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton() && (bool)EDITOR_GET("text_editor/behavior/indent/type")) {
		int sz = EDITOR_GET("text_editor/behavior/indent/size");
		unit = String(" ").repeat(MAX(1, sz));
	}
#endif
	reindent_code(p_code, p_from_line, p_to_line, unit);
}

void BeefLanguage::reindent_code(String &p_code, int p_from_line, int p_to_line, const String &p_unit) {
	// Reindent the requested line range by brace depth. A single forward pass lexes the whole text so
	// that braces inside strings, char literals and comments (incl. multi-line block comments) are
	// ignored, and so the depth carried into p_from_line is correct. Only leading whitespace changes,
	// so the line count is preserved (the editor applies the result line by line).
	Vector<String> lines = p_code.split("\n");

	// Depth at the start of each line.
	Vector<int> line_depth;
	line_depth.resize(lines.size());
	int depth = 0;
	bool in_line_comment = false, in_block_comment = false;
	char32_t string_delim = 0; // 0 = not in a string; otherwise '"' or '\''
	const int len = p_code.length();
	int line_idx = 0;
	if (lines.size() > 0) {
		line_depth.write[0] = 0;
	}
	for (int i = 0; i < len; i++) {
		char32_t c = p_code[i];
		char32_t n = (i + 1 < len) ? p_code[i + 1] : 0;
		if (c == '\n') {
			in_line_comment = false; // line comments end at newline
			line_idx++;
			if (line_idx < line_depth.size()) {
				line_depth.write[line_idx] = depth;
			}
			continue;
		}
		if (in_line_comment) {
			continue;
		}
		if (in_block_comment) {
			if (c == '*' && n == '/') {
				in_block_comment = false;
				i++;
			}
			continue;
		}
		if (string_delim != 0) {
			if (c == '\\') {
				i++; // skip the escaped char
			} else if (c == string_delim) {
				string_delim = 0;
			}
			continue;
		}
		// Not in a comment or string.
		if (c == '/' && n == '/') {
			in_line_comment = true;
			i++;
		} else if (c == '/' && n == '*') {
			in_block_comment = true;
			i++;
		} else if (c == '"' || c == '\'') {
			string_delim = c;
		} else if (c == '{' || c == '(' || c == '[') {
			depth++;
		} else if (c == '}' || c == ')' || c == ']') {
			depth = MAX(0, depth - 1);
		}
	}

	int from = MAX(0, p_from_line);
	int to = MIN(p_to_line, lines.size() - 1);
	for (int i = from; i <= to; i++) {
		String stripped = lines[i].strip_edges();
		if (stripped.is_empty()) {
			lines.write[i] = String();
			continue;
		}
		int this_depth = line_depth[i];
		// A line that opens with a closing bracket belongs to the outer level.
		if (stripped[0] == '}' || stripped[0] == ')' || stripped[0] == ']') {
			this_depth = MAX(0, this_depth - 1);
		}
		lines.write[i] = p_unit.repeat(this_depth) + stripped;
	}

	p_code = String("\n").join(lines);
}

void BeefLanguage::gather_completions(const String &p_prefix, bool p_after_dot, const Vector<String> &p_keywords, List<CodeCompletionOption> *r_options) {
	// Member access (`foo.bar`) needs type resolution we don't have without the full compiler — offer
	// nothing rather than wrong globals.
	if (p_after_dot) {
		return;
	}
	for (const String &k : p_keywords) {
		if (p_prefix.is_empty() || k.begins_with(p_prefix)) {
			r_options->push_back(CodeCompletionOption(k, CODE_COMPLETION_KIND_PLAIN_TEXT));
		}
	}
	// Godot type names, only once there is a prefix (avoids dumping the whole ClassDB on Ctrl+Space).
	if (!p_prefix.is_empty()) {
		LocalVector<StringName> classes;
		ClassDB::get_class_list(classes);
		for (const StringName &c : classes) {
			if (!ClassDB::is_class_exposed(c)) {
				continue;
			}
			String cs = c;
			if (cs.begins_with(p_prefix)) {
				r_options->push_back(CodeCompletionOption(cs, CODE_COMPLETION_KIND_CLASS));
			}
		}
	}
}

Error BeefLanguage::complete_code(const String &p_code, const String &p_path, Object *p_owner, List<CodeCompletionOption> *r_options, bool &r_force, String &r_call_hint) {
	r_force = false;
	// The editor passes the full text with a 0xFFFF marker at the caret (CodeEdit::get_text_for_code_completion).
	int marker = p_code.find_char(0xFFFF);
	if (marker < 0) {
		return ERR_UNAVAILABLE;
	}

	// Prefix: identifier chars immediately before the caret.
	int start = marker;
	while (start > 0 && _is_ident_char(p_code[start - 1])) {
		start--;
	}
	String prefix = p_code.substr(start, marker - start);
	bool after_dot = (start > 0 && p_code[start - 1] == '.');

	// Semantic completion via the IDEHelper resolve compiler (real type-aware members/types/locals).
	// The source is the editor buffer minus the caret marker; the cursor is the marker's byte offset in
	// the UTF-8 encoding (Beef indexes source by byte). Falls back to the heuristic gather below if the
	// resolver is unavailable (non-Windows / DLL missing) or returns nothing.
	{
		String source = p_code.substr(0, marker) + p_code.substr(marker + 1);
		int cursor_bytes = p_code.substr(0, marker).utf8().length();
		String workspace, project, config;
		bool external = false;
		_resolve_build_settings(workspace, project, config, external);
		String file = ProjectSettings::get_singleton()->globalize_path(p_path);
		Vector<BeefIDEHelper::Completion> entries;
		String call_hint;
		if (BeefIDEHelper::get_singleton()->get_completions(workspace, project, file, source, cursor_bytes, entries, &call_hint)) {
			// Signature/argument hint shown when the caret is inside a call's parentheses.
			if (!call_hint.is_empty()) {
				r_call_hint = call_hint;
			}
			if (!entries.is_empty()) {
				for (const BeefIDEHelper::Completion &e : entries) {
					ScriptLanguage::CodeCompletionKind kind = CODE_COMPLETION_KIND_PLAIN_TEXT;
					if (e.kind == "method" || e.kind == "extmethod") {
						kind = CODE_COMPLETION_KIND_FUNCTION;
					} else if (e.kind == "field" || e.kind == "value" || e.kind == "property") {
						kind = CODE_COMPLETION_KIND_MEMBER;
					} else if (e.kind == "namespace") {
						kind = CODE_COMPLETION_KIND_PLAIN_TEXT;
					} else if (e.kind == "enum") {
						kind = CODE_COMPLETION_KIND_ENUM;
					} else if (e.kind == "class" || e.kind == "struct" || e.kind == "interface" ||
							e.kind == "object" || e.kind == "valuetype" || e.kind == "type" || e.kind == "generic") {
						kind = CODE_COMPLETION_KIND_CLASS;
					}
					r_options->push_back(CodeCompletionOption(e.name, kind));
				}
				r_force = true;
				return OK;
			}
			if (!call_hint.is_empty()) {
				return OK; // call hint only (caret between parentheses, no member list)
			}
		}
	}

	gather_completions(prefix, after_dot, get_reserved_words(), r_options);

	// Identifiers already used elsewhere in this file — cheap, useful local completions.
	if (!after_dot && !prefix.is_empty()) {
		HashSet<String> seen;
		seen.insert(prefix);
		const int n = p_code.length();
		int i = 0;
		while (i < n) {
			if (!_is_ident_char(p_code[i]) || (p_code[i] >= '0' && p_code[i] <= '9')) {
				i++;
				continue;
			}
			int j = i;
			bool has_marker = false;
			while (j < n && (_is_ident_char(p_code[j]) || p_code[j] == 0xFFFF)) {
				if (p_code[j] == 0xFFFF) {
					has_marker = true;
				}
				j++;
			}
			if (!has_marker) { // skip the word currently under the caret
				String w = p_code.substr(i, j - i);
				if (w.begins_with(prefix) && !seen.has(w)) {
					seen.insert(w);
					r_options->push_back(CodeCompletionOption(w, CODE_COMPLETION_KIND_VARIABLE, LOCATION_LOCAL));
				}
			}
			i = j;
		}
	}
	return OK;
}

// snake_case -> PascalCase, matching BeefBindingsGenerator::_to_pascal_case (the binding member naming).
static String _beef_to_pascal_case(const String &p_name) {
	String result;
	bool next_upper = true;
	for (int i = 0; i < p_name.length(); i++) {
		char32_t c = p_name[i];
		if (c == '_') {
			next_upper = true;
		} else if (next_upper) {
			result += String::chr((c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c);
			next_upper = false;
		} else {
			result += String::chr(c);
		}
	}
	return result;
}

Error BeefLanguage::lookup_code(const String &p_code, const String &p_symbol, const String &p_path, Object *p_owner, LookupResult &r_result) {
	// The editor passes the buffer with a 0xFFFF char at the hovered position (get_text_with_cursor_char).
	int marker = p_code.find_char(0xFFFF);
	if (marker < 0 || p_symbol.is_empty()) {
		return ERR_UNAVAILABLE;
	}
	String source = p_code.substr(0, marker) + p_code.substr(marker + 1);
	int cursor_bytes = p_code.substr(0, marker).utf8().length();

	String workspace, project, config;
	bool external = false;
	if (!_resolve_build_settings(workspace, project, config, external)) {
		return ERR_UNAVAILABLE;
	}
	String file = ProjectSettings::get_singleton()->globalize_path(p_path);

	// Resolve the symbol under the cursor to its owning Beef type (e.g. "Godot.Node3D") + reference kind.
	String type_full, kind;
	if (!BeefIDEHelper::get_singleton()->resolve_symbol(workspace, project, file, source, cursor_bytes, type_full, kind)) {
		return ERR_UNAVAILABLE;
	}
	// Only engine (Godot.*) types have DocData docs. Strip the namespace to the Godot class name.
	if (!type_full.begins_with("Godot.")) {
		return ERR_UNAVAILABLE;
	}
	String cls = type_full.substr(6); // "Node3D"
	if (cls.find(".") >= 0 || cls.find("`") >= 0 || !ClassDB::class_exists(cls)) {
		return ERR_UNAVAILABLE; // nested/generic/non-class — no simple DocData mapping
	}

	if (kind == "typeRef") {
		r_result.type = LOOKUP_RESULT_CLASS;
		r_result.class_name = cls;
		return OK;
	}

	// Map the hovered Beef member (PascalCase) back to the Godot snake_case name by forward-converting the
	// class's real members (the same conversion the generator used) and matching — exact, not lossy.
	if (kind == "methodRef" || kind == "ctorRef") {
		List<MethodInfo> methods;
		ClassDB::get_method_list(cls, &methods, false); // include inherited
		for (const MethodInfo &m : methods) {
			String beef = m.name.begins_with("_") ? ("_" + _beef_to_pascal_case(m.name.substr(1))) : _beef_to_pascal_case(m.name);
			if (beef == p_symbol) {
				r_result.type = LOOKUP_RESULT_CLASS_METHOD;
				r_result.class_name = cls;
				r_result.class_member = m.name;
				return OK;
			}
		}
	} else { // fieldRef / propertyRef
		List<PropertyInfo> props;
		ClassDB::get_property_list(cls, &props, false); // include inherited
		for (const PropertyInfo &pi : props) {
			if (_beef_to_pascal_case(pi.name) == p_symbol) {
				r_result.type = LOOKUP_RESULT_CLASS_PROPERTY;
				r_result.class_name = cls;
				r_result.class_member = pi.name;
				return OK;
			}
		}
	}
	return ERR_UNAVAILABLE;
}

void BeefLanguage::add_global_constant(const StringName &p_variable, const Variant &p_value) {}

// Debugger stubs

String BeefLanguage::debug_get_error() const {
	return String();
}

int BeefLanguage::debug_get_stack_level_count() const {
	return 0;
}

int BeefLanguage::debug_get_stack_level_line(int p_level) const {
	return -1;
}

String BeefLanguage::debug_get_stack_level_function(int p_level) const {
	return String();
}

String BeefLanguage::debug_get_stack_level_source(int p_level) const {
	return String();
}

void BeefLanguage::debug_get_stack_level_locals(int p_level, List<String> *p_locals, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {}

void BeefLanguage::debug_get_stack_level_members(int p_level, List<String> *p_members, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {}

void BeefLanguage::debug_get_globals(List<String> *p_globals, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {}

String BeefLanguage::debug_parse_stack_level_expression(int p_level, const String &p_expression, int p_max_subitems, int p_max_depth) {
	return String();
}

// Reload stubs

void BeefLanguage::reload_all_scripts() {}

void BeefLanguage::reload_scripts(const Array &p_scripts, bool p_soft_reload) {}

void BeefLanguage::reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {}

// Loader

void BeefLanguage::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("bf");
}

void BeefLanguage::get_public_functions(List<MethodInfo> *p_functions) const {}

void BeefLanguage::get_public_constants(List<Pair<String, Variant>> *p_constants) const {}

void BeefLanguage::get_public_annotations(List<MethodInfo> *p_annotations) const {}

// Profiling stubs

void BeefLanguage::profiling_start() {}

void BeefLanguage::profiling_stop() {}

void BeefLanguage::profiling_set_save_native_calls(bool p_enable) {}

int BeefLanguage::profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) {
	return 0;
}

int BeefLanguage::profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) {
	return 0;
}

// ─── BeefScript ───────────────────────────────────────────────────────────────

#ifdef TOOLS_ENABLED
StringName BeefScript::get_doc_class_name() const {
	return StringName();
}
#endif

bool BeefScript::can_instantiate() const {
#ifdef TOOLS_ENABLED
	// In the editor, only tool scripts (or a running game, where scripting is enabled) may
	// instantiate a live instance. Otherwise the engine creates a PlaceHolderScriptInstance instead,
	// so the script's code (e.g. _Process) does not run while editing the scene. Mirrors modules/mono.
	bool extra_cond = is_tool() || ScriptServer::is_scripting_enabled();
#else
	bool extra_cond = true;
#endif
	return is_compiled && fn_create != nullptr && extra_cond;
}

Ref<Script> BeefScript::get_base_script() const {
	return Ref<Script>();
}

static bool _is_ident_char(char32_t c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Strip a namespace qualifier: "GameScripts.MyNode" -> "MyNode".
static String _last_segment(const String &p_name) {
	int dot = p_name.rfind(".");
	return dot < 0 ? p_name : p_name.substr(dot + 1);
}

// Find the base type in `class <p_class> : <Base>` (word-boundary match on the class name).
static String _find_beef_base(const String &p_source, const String &p_class) {
	int len = p_source.length();
	int pos = 0;
	while (true) {
		int c = p_source.find("class ", pos);
		if (c < 0) {
			return String();
		}
		int i = c + 6;
		while (i < len && (p_source[i] == ' ' || p_source[i] == '\t')) {
			i++;
		}
		int name_start = i;
		while (i < len && _is_ident_char(p_source[i])) {
			i++;
		}
		if (p_source.substr(name_start, i - name_start) == p_class) {
			while (i < len && (p_source[i] == ' ' || p_source[i] == '\t')) {
				i++;
			}
			if (i < len && p_source[i] == ':') {
				i++;
				while (i < len && (p_source[i] == ' ' || p_source[i] == '\t')) {
					i++;
				}
				int base_start = i;
				while (i < len && (_is_ident_char(p_source[i]) || p_source[i] == '.')) {
					i++;
				}
				return _last_segment(p_source.substr(base_start, i - base_start));
			}
			return String(); // class declared with no base
		}
		pos = c + 6;
	}
}

// True if the source carries a [GodotTool] marker (word-boundary match, so it won't trip on Godot
// classes like GodotToolButton). A tool script also runs in the editor.
static bool _has_tool_marker(const String &p_source) {
	int len = p_source.length();
	int pos = 0;
	while (true) {
		int t = p_source.find("GodotTool", pos);
		if (t < 0) {
			return false;
		}
		int after = t + 9; // length of "GodotTool"
		if (after >= len || !_is_ident_char(p_source[after])) {
			return true;
		}
		pos = after;
	}
}

bool BeefScript::parse_global_class(const String &p_source, String &r_name, String &r_base) {
	// A script class is marked `[GodotScript]` (matched as a whole token, not GodotScriptAttribute /
	// GodotScriptRegistrations); the class declaration that follows gives the name and base.
	int pos = 0;
	while (true) {
		int m = p_source.find("GodotScript", pos);
		if (m < 0) {
			return false;
		}
		int after = m + 11; // length of "GodotScript"
		pos = after;
		if (after < p_source.length() && _is_ident_char(p_source[after])) {
			continue; // part of a longer identifier — not the bare attribute
		}
		int c = p_source.find("class ", m);
		if (c < 0) {
			continue;
		}
		int i = c + 6;
		while (i < p_source.length() && (p_source[i] == ' ' || p_source[i] == '\t')) {
			i++;
		}
		int name_start = i;
		while (i < p_source.length() && _is_ident_char(p_source[i])) {
			i++;
		}
		r_name = p_source.substr(name_start, i - name_start);
		if (r_name.is_empty()) {
			continue;
		}
		r_base = _find_beef_base(p_source, r_name);
		return true;
	}
}

StringName BeefScript::get_global_name() const {
	String cls_name, base;
	if (parse_global_class(source_code, cls_name, base)) {
		return StringName(cls_name);
	}
	return StringName();
}

bool BeefScript::inherits_script(const Ref<Script> &p_script) const {
	return false;
}

StringName BeefScript::get_instance_base_type() const {
	String cls_name, base;
	if (parse_global_class(source_code, cls_name, base) && !base.is_empty()) {
		return StringName(base);
	}
	// Fall back to the base of the class named after the file.
	String cn = script_path.get_file().get_basename();
	String b = _find_beef_base(source_code, cn);
	return b.is_empty() ? StringName() : StringName(b);
}

ScriptInstance *BeefScript::instance_create(Object *p_this) {
	if (!is_compiled || !fn_create) {
		return nullptr;
	}
	BeefInstance *instance = memnew(BeefInstance);
	instance->owner = p_this;
	instance->beef_script = Ref<BeefScript>(this);
	instance->beef_obj = fn_create(p_this);
	return instance;
}

void BeefScript::_bind_methods() {
	// Expose `new` on the script resource so GDScript can do `load("X.bf").new()` (and so
	// Beef-scripted classes can be instantiated programmatically, not only via scenes/.tres).
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "new", &BeefScript::_new, MethodInfo("new"));
}

Variant BeefScript::_new(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	(void)p_args;
	(void)p_argcount; // Beef has no constructor-arg dispatch yet; the base ctor + _Ready run instead.
	r_error.error = Callable::CallError::CALL_OK;

	if (!can_instantiate()) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	// Create the native base object (Node, RefCounted, ...) the script extends, then attach.
	StringName native_name = get_instance_base_type();
	if (native_name == StringName()) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		ERR_FAIL_V_MSG(Variant(), "BeefScript: cannot determine native base type for " + script_path);
	}

	Object *owner = ClassDB::instantiate(native_name);
	ERR_FAIL_NULL_V_MSG(owner, Variant(), "BeefScript: failed to instantiate native base '" + String(native_name) + "'.");

	// Hold a reference if the base is RefCounted, so returning it through Variant is safe.
	Ref<RefCounted> ref;
	RefCounted *r = Object::cast_to<RefCounted>(owner);
	if (r) {
		ref = Ref<RefCounted>(r);
	}

	ScriptInstance *instance = instance_create(owner);
	if (!instance) {
		if (ref.is_null()) {
			memdelete(owner);
		}
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}
	owner->set_script_instance(instance);

	if (ref.is_valid()) {
		return ref;
	}
	return owner;
}

PlaceHolderScriptInstance *BeefScript::placeholder_instance_create(Object *p_this) {
#ifdef TOOLS_ENABLED
	PlaceHolderScriptInstance *si = memnew(PlaceHolderScriptInstance(BeefLanguage::get_singleton(), Ref<Script>(this), p_this));
	placeholders.insert(si);
	_update_placeholder(si);
	return si;
#else
	return nullptr;
#endif
}

#ifdef TOOLS_ENABLED
void BeefScript::_update_placeholder(PlaceHolderScriptInstance *p_placeholder) const {
	// Feed the placeholder the script's exported properties + their default values, so the inspector
	// shows them while editing even though no live instance exists.
	List<PropertyInfo> props;
	get_script_property_list(&props);

	HashMap<StringName, Variant> values;
	for (const PropertyInfo &pi : props) {
		Variant dv;
		if (!get_property_default_value(pi.name, dv)) {
			dv = Variant();
		}
		values[pi.name] = dv;
	}
	p_placeholder->update(props, values);
}

void BeefScript::_placeholder_erased(PlaceHolderScriptInstance *p_placeholder) {
	placeholders.erase(p_placeholder);
}
#else
void BeefScript::_placeholder_erased(PlaceHolderScriptInstance *p_placeholder) {}
#endif

bool BeefScript::instance_has(const Object *p_this) const {
	return false;
}

bool BeefScript::has_source_code() const {
	return !source_code.is_empty();
}

String BeefScript::get_source_code() const {
	return source_code;
}

void BeefScript::set_source_code(const String &p_code) {
	source_code = p_code;
}

Error BeefScript::reload(bool p_keep_state) {
	BeefCompiler *compiler = BeefCompiler::get_singleton();
	if (!compiler) {
		return ERR_UNAVAILABLE;
	}
#ifndef WEB_DLINK_ENABLED
	// BeefBuild is only needed to COMPILE (editor/desktop). The web build never compiles — it dlopens a
	// prebuilt side module — so don't gate script loading on BeefBuild being present there.
	if (!compiler->is_found()) {
		return ERR_UNAVAILABLE;
	}
#endif
	if (script_path.is_empty()) {
		return ERR_INVALID_DATA;
	}

	class_name_cache = StringName(script_path.get_file().get_basename());

	// Compile the shared beef project on first load (or if DLL was unloaded).
	// Skip silently if a previous compile already failed (_compile_blocked) — the user
	// already saw the error output; no point repeating it on every script reload.
	if (!compiler->is_dll_loaded() && !compiler->is_compile_blocked()) {
		String workspace_dir, project_name, build_config;
		bool external_build = false;
		BeefLanguage *lang = BeefLanguage::get_singleton();
		if (!lang || !lang->_resolve_build_settings(workspace_dir, project_name, build_config, external_build)) {
			return ERR_UNAVAILABLE;
		}

		// Only the editor runs BeefBuild. A running game instance loads the DLL the editor already
		// built — never compiles. This matters for multiple instances (e.g. multiplayer testing):
		// if every game instance ran BeefBuild on the same workspace into the same build/ output,
		// they would race and corrupt each other's compile, leaving one instance with no DLL (its
		// scripts fail to load -> null script). external_build also loads directly (some other tool,
		// e.g. BeefIDE, owns the build and may be hot-patching the process).
		String dll_path;
		bool godot_driven_build = !external_build && Engine::get_singleton()->is_editor_hint();
		if (godot_driven_build) {
			// Editor, Godot-driven workflow: run BeefBuild and load the result.
			Vector<String> errors;
			if (!compiler->compile(workspace_dir, project_name, build_config, dll_path, errors)) {
				// compile() already printed each error line via ERR_PRINT; just add the hint
				if (!errors.is_empty()) {
					print_line("BeefScript: place your .bf scripts in: " + workspace_dir + "/src/");
				}
				return FAILED;
			}
		} else {
#ifdef WEB_DLINK_ENABLED
			// Web (Emscripten dlink): the Beef scripts ship as a wasm SIDE_MODULE bundled into the .pck
			// by BeefExportPlugin (res://<project>.side.wasm). load_dll stages it from there into the
			// Emscripten MEMFS and dlopens it — there is no exe-adjacent file in the browser.
			dll_path = "res://" + project_name + ".side.wasm";
			print_line("BeefScript: web — loading bundled side module " + dll_path);
#else
			// Running game instance, or external-build workflow: load the already-built DLL.
			// In an EXPORTED game the DLL (and its Beef runtime) is bundled next to the executable by
			// BeefExportPlugin; res://beef/build/ doesn't exist there. Prefer the exe-adjacent copy and
			// fall back to the workspace build dir (the play-from-editor case, where bin/ has no DLL).
			String exe_dll = OS::get_singleton()->get_executable_path().get_base_dir().path_join(project_name + ".dll");
			if (FileAccess::exists(exe_dll)) {
				dll_path = exe_dll;
				print_line("BeefScript: exported game — loading bundled " + dll_path);
			} else {
				dll_path = BeefCompiler::get_dll_path(workspace_dir, project_name, build_config);
				if (!FileAccess::exists(dll_path)) {
					WARN_PRINT("BeefScript: no built DLL at " + dll_path + " — open/build the project in the editor first.");
					return ERR_FILE_NOT_FOUND;
				}
				print_line(external_build ? ("BeefScript: external build — loading " + dll_path)
										  : ("BeefScript: game instance — loading prebuilt " + dll_path));
			}
#endif
		}
		// In the editor (Godot-driven mode) load a shadow copy so the real output DLL stays unlocked
		// and a subsequent build can relink it without us calling FreeLibrary (unsafe with the Beef
		// debug runtime). The running game and the external-build workflow load the DLL in place.
		bool use_shadow = Engine::get_singleton()->is_editor_hint() && !external_build;
		if (!compiler->load_dll(dll_path, /*quiet=*/false, use_shadow)) {
			return FAILED;
		}

		// Call BeefGodot_Init once after loading — passes our C function table so Beef
		// code can call back into Godot (print, method binds, etc.) without linking against it.
		typedef void (*PFN_Init)(const void *funcs, int32_t count);
		PFN_Init fn_init = (PFN_Init)compiler->get_proc("BeefGodot_Init");
		if (fn_init) {
			fn_init(s_beef_godot_funcs, s_beef_godot_funcs_count);
		} else {
			WARN_PRINT("BeefScript: BeefGodot_Init not exported. Add GodotRuntime.bf to GodotBindings.");
		}
	}

	// Resolve per-class entry points from the shared DLL.
	// Convention: BeefGodot_Create_ClassName, BeefGodot_Destroy_ClassName, etc.
	String cn = String(class_name_cache);
	fn_create = (PFN_CreateInstance)compiler->get_proc(("BeefGodot_Create_" + cn).utf8().get_data());
	fn_destroy = (PFN_DestroyInstance)compiler->get_proc(("BeefGodot_Destroy_" + cn).utf8().get_data());
	fn_notify = (PFN_Notification)compiler->get_proc(("BeefGodot_Notification_" + cn).utf8().get_data());
	fn_call = (PFN_CallMethod)compiler->get_proc(("BeefGodot_Call_" + cn).utf8().get_data());
	fn_has_method = (PFN_HasMethod)compiler->get_proc(("BeefGodot_HasMethod_" + cn).utf8().get_data());
	fn_set = (PFN_SetProp)compiler->get_proc(("BeefGodot_Set_" + cn).utf8().get_data());
	fn_get = (PFN_GetProp)compiler->get_proc(("BeefGodot_Get_" + cn).utf8().get_data());
	fn_prop_count = (PFN_PropCount)compiler->get_proc(("BeefGodot_PropCount_" + cn).utf8().get_data());
	fn_prop_name = (PFN_PropName)compiler->get_proc(("BeefGodot_PropName_" + cn).utf8().get_data());
	fn_prop_type = (PFN_PropType)compiler->get_proc(("BeefGodot_PropType_" + cn).utf8().get_data());
	fn_prop_default = (PFN_PropDefault)compiler->get_proc(("BeefGodot_PropDefault_" + cn).utf8().get_data());
	fn_prop_hint = (PFN_PropHint)compiler->get_proc(("BeefGodot_PropHint_" + cn).utf8().get_data());
	fn_prop_hint_string = (PFN_PropHintString)compiler->get_proc(("BeefGodot_PropHintString_" + cn).utf8().get_data());
	fn_signal_count = (PFN_SignalCount)compiler->get_proc(("BeefGodot_SignalCount_" + cn).utf8().get_data());
	fn_signal_name = (PFN_SignalName)compiler->get_proc(("BeefGodot_SignalName_" + cn).utf8().get_data());
	fn_signal_argc = (PFN_SignalArgc)compiler->get_proc(("BeefGodot_SignalArgc_" + cn).utf8().get_data());
	fn_signal_arg_type = (PFN_SignalArgType)compiler->get_proc(("BeefGodot_SignalArgType_" + cn).utf8().get_data());
	fn_method_count = (PFN_MethodCount)compiler->get_proc(("BeefGodot_MethodCount_" + cn).utf8().get_data());
	fn_method_name = (PFN_MethodName)compiler->get_proc(("BeefGodot_MethodName_" + cn).utf8().get_data());
	fn_method_argc = (PFN_MethodArgc)compiler->get_proc(("BeefGodot_MethodArgc_" + cn).utf8().get_data());
	fn_method_arg_type = (PFN_MethodArgType)compiler->get_proc(("BeefGodot_MethodArgType_" + cn).utf8().get_data());
	fn_rpc_count = (PFN_RpcCount)compiler->get_proc(("BeefGodot_RpcCount_" + cn).utf8().get_data());
	fn_rpc_name = (PFN_RpcName)compiler->get_proc(("BeefGodot_RpcName_" + cn).utf8().get_data());
	fn_rpc_mode = (PFN_RpcMode)compiler->get_proc(("BeefGodot_RpcMode_" + cn).utf8().get_data());
	fn_rpc_transfer = (PFN_RpcTransfer)compiler->get_proc(("BeefGodot_RpcTransfer_" + cn).utf8().get_data());
	fn_rpc_call_local = (PFN_RpcCallLocal)compiler->get_proc(("BeefGodot_RpcCallLocal_" + cn).utf8().get_data());
	fn_rpc_channel = (PFN_RpcChannel)compiler->get_proc(("BeefGodot_RpcChannel_" + cn).utf8().get_data());

	// Only warn about missing exports when this file actually declares a registered script class
	// named after the file (i.e. it has [GodotRegister(typeof(<filename>))]). Support files like
	// GodotRuntime.bf / GodotScript.bf / the GodotBridge.bf placeholder aren't scripts and have no
	// Create export by design — warning about them is just noise.
	String reg_name, reg_base;
	bool has_script_class = parse_global_class(source_code, reg_name, reg_base);
	// The engine resolves a class's C exports by the FILE name (class_name_cache, set above), but
	// the [GodotScript] registrar emits them under the actual CLASS name. If they differ, every
	// BeefGodot_*_<file> lookup silently misses -> the script loads with no instance, no methods,
	// no error. Surface that mismatch loudly instead.
	if (has_script_class && reg_name != cn) {
		ERR_PRINT(vformat("BeefScript: class '%s' in '%s' does not match the file name. A Beef "
						  "script's class name must equal its file name. Rename the class to '%s' "
						  "or rename the file to '%s.bf'.",
				reg_name, script_path.get_file(), cn, reg_name));
	}
	bool is_script_class = has_script_class && reg_name == cn;
	if (is_script_class) {
		if (!fn_create) {
			WARN_PRINT("BeefScript: no BeefGodot_Create_" + cn + " exported. Register the class with [GodotRegister(typeof(" + cn + "))].");
		}
		if (!fn_destroy) {
			WARN_PRINT("BeefScript: no BeefGodot_Destroy_" + cn + " exported. Memory leak will occur when the DLL is unloaded.");
		}
	}

	is_compiled = compiler->is_dll_loaded();
	return OK;
}

#ifdef TOOLS_ENABLED
Vector<DocData::ClassDoc> BeefScript::get_documentation() const {
	return Vector<DocData::ClassDoc>();
}

String BeefScript::get_class_icon_path() const {
	return String();
}
#endif

bool BeefScript::has_method(const StringName &p_method) const {
	return fn_has_method && fn_has_method(String(p_method).utf8().get_data());
}

MethodInfo BeefScript::get_method_info(const StringName &p_method) const {
	if (fn_method_count && fn_method_name) {
		int32_t n = fn_method_count();
		for (int32_t i = 0; i < n; i++) {
			const char *cname = fn_method_name(i);
			if (cname && p_method == StringName(String::utf8(cname))) {
				MethodInfo mi(String::utf8(cname));
				int32_t argc = fn_method_argc ? fn_method_argc(i) : 0;
				for (int32_t a = 0; a < argc; a++) {
					Variant::Type at = fn_method_arg_type ? (Variant::Type)fn_method_arg_type(i, a) : Variant::NIL;
					mi.arguments.push_back(PropertyInfo(at, "arg" + itos(a)));
				}
				return mi;
			}
		}
	}
	return MethodInfo();
}

bool BeefScript::is_tool() const {
	return _has_tool_marker(source_code);
}

bool BeefScript::is_valid() const {
	return is_compiled;
}

bool BeefScript::is_abstract() const {
	return false;
}

int BeefScript::get_member_line(const StringName &p_member) const {
	// No symbol metadata from the external compiler, so scan the source for the first line that
	// declares the member as a whole word (field/method/property). Best-effort for editor "go to
	// member"; returns 1-based line or -1. A preceding char must be a non-identifier (so "Health"
	// doesn't match inside "MaxHealth"), and the following char likewise.
	const String member = String(p_member);
	if (member.is_empty()) {
		return -1;
	}
	Vector<String> lines = source_code.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String &line = lines[i];
		int from = 0;
		while (true) {
			int at = line.find(member, from);
			if (at < 0) {
				break;
			}
			char32_t before = (at > 0) ? line[at - 1] : ' ';
			int after_idx = at + member.length();
			char32_t after = (after_idx < line.length()) ? line[after_idx] : ' ';
			bool boundary_ok = !(is_ascii_identifier_char(before)) && !(is_ascii_identifier_char(after));
			if (boundary_ok) {
				return i + 1;
			}
			from = at + 1;
		}
	}
	return -1;
}

ScriptLanguage *BeefScript::get_language() const {
	return BeefLanguage::get_singleton();
}

bool BeefScript::has_script_signal(const StringName &p_signal) const {
	if (!fn_signal_count || !fn_signal_name) {
		return false;
	}
	int32_t n = fn_signal_count();
	for (int32_t i = 0; i < n; i++) {
		const char *cname = fn_signal_name(i);
		if (cname && p_signal == StringName(String::utf8(cname))) {
			return true;
		}
	}
	return false;
}

void BeefScript::get_script_signal_list(List<MethodInfo> *r_signals) const {
	if (!fn_signal_count || !fn_signal_name || !fn_signal_argc) {
		return;
	}
	int32_t n = fn_signal_count();
	for (int32_t i = 0; i < n; i++) {
		const char *cname = fn_signal_name(i);
		if (!cname) {
			continue;
		}
		MethodInfo mi(String::utf8(cname));
		int32_t argc = fn_signal_argc(i);
		for (int32_t a = 0; a < argc; a++) {
			Variant::Type at = fn_signal_arg_type ? (Variant::Type)fn_signal_arg_type(i, a) : Variant::NIL;
			mi.arguments.push_back(PropertyInfo(at, "arg" + itos(a)));
		}
		r_signals->push_back(mi);
	}
}

bool BeefScript::get_property_default_value(const StringName &p_property, Variant &r_value) const {
	if (!fn_prop_count || !fn_prop_name || !fn_prop_default) {
		return false;
	}
	int32_t n = fn_prop_count();
	for (int32_t i = 0; i < n; i++) {
		const char *cname = fn_prop_name(i);
		if (cname && p_property == StringName(String::utf8(cname))) {
			fn_prop_default(i, &r_value);
			return true;
		}
	}
	return false;
}

void BeefScript::get_script_method_list(List<MethodInfo> *p_list) const {
	if (!fn_method_count || !fn_method_name) {
		return;
	}
	int32_t n = fn_method_count();
	for (int32_t i = 0; i < n; i++) {
		const char *cname = fn_method_name(i);
		if (!cname) {
			continue;
		}
		MethodInfo mi(String::utf8(cname));
		int32_t argc = fn_method_argc ? fn_method_argc(i) : 0;
		for (int32_t a = 0; a < argc; a++) {
			Variant::Type at = fn_method_arg_type ? (Variant::Type)fn_method_arg_type(i, a) : Variant::NIL;
			mi.arguments.push_back(PropertyInfo(at, "arg" + itos(a)));
		}
		p_list->push_back(mi);
	}
}

void BeefScript::get_script_property_list(List<PropertyInfo> *p_list) const {
	if (!fn_prop_count || !fn_prop_name || !fn_prop_type) {
		return;
	}
	int32_t n = fn_prop_count();
	for (int32_t i = 0; i < n; i++) {
		const char *cname = fn_prop_name(i);
		Variant::Type type = (Variant::Type)fn_prop_type(i);
		if (cname) {
			PropertyInfo pi(type, String::utf8(cname));
			if (fn_prop_hint) {
				pi.hint = (PropertyHint)fn_prop_hint(i);
			}
			if (fn_prop_hint_string) {
				const char *hs = fn_prop_hint_string(i);
				if (hs) {
					pi.hint_string = String::utf8(hs);
				}
			}
			p_list->push_back(pi);
		}
	}
}

const Variant BeefScript::get_rpc_config() const {
	if (!fn_rpc_count) {
		return Variant();
	}
	int32_t count = fn_rpc_count();
	if (count <= 0) {
		return Variant();
	}
	// { method_name: { rpc_mode, transfer_mode, call_local, channel } } — consumed by
	// SceneRPCInterface::_parse_rpc_config. String keys are accepted (read back via operator String()).
	Dictionary config;
	for (int32_t i = 0; i < count; i++) {
		const char *cname = fn_rpc_name ? fn_rpc_name(i) : nullptr;
		if (!cname || cname[0] == '\0') {
			continue;
		}
		Dictionary method_config;
		method_config["rpc_mode"] = fn_rpc_mode ? fn_rpc_mode(i) : 2; // RPC_MODE_AUTHORITY
		method_config["transfer_mode"] = fn_rpc_transfer ? fn_rpc_transfer(i) : 2; // TRANSFER_MODE_RELIABLE
		method_config["call_local"] = fn_rpc_call_local ? fn_rpc_call_local(i) : false;
		method_config["channel"] = fn_rpc_channel ? fn_rpc_channel(i) : 0;
		config[String::utf8(cname)] = method_config;
	}
	return config;
}

// ─── BeefInstance ─────────────────────────────────────────────────────────────

BeefInstance::BeefInstance() {
	if (BeefLanguage *lang = BeefLanguage::get_singleton()) {
		lang->_live_instances.insert(this);
	}
}

BeefInstance::~BeefInstance() {
	_destroy_beef_obj();
	if (BeefLanguage *lang = BeefLanguage::get_singleton()) {
		lang->_live_instances.erase(this);
	}
}

void BeefInstance::_destroy_beef_obj() {
	if (!beef_obj) {
		return; // already destroyed or never created
	}
	if (!beef_script.is_valid()) {
		WARN_PRINT("BeefScript: _destroy_beef_obj: beef_script is no longer valid — Beef object leaked!");
		beef_obj = nullptr;
		return;
	}
	if (beef_script->fn_destroy) { // missing destroy export is already warned at load (reload())
		beef_script->fn_destroy(beef_obj);
	}
	beef_obj = nullptr;
}

bool BeefInstance::set(const StringName &p_name, const Variant &p_value) {
	if (!beef_obj || !beef_script.is_valid()) {
		return false;
	}
	// 1. Exported field.
	if (beef_script->fn_set && beef_script->fn_set(beef_obj, String(p_name).utf8().get_data(), &p_value)) {
		return true;
	}
	// 2. Dynamic property via the _set virtual (returns true if it handled the assignment).
	if (beef_script->fn_has_method && beef_script->fn_has_method("_set")) {
		Variant name_arg = String(p_name);
		const Variant *args[2] = { &name_arg, &p_value };
		Callable::CallError err;
		Variant handled = callp(SNAME("_set"), args, 2, err);
		if (err.error == Callable::CallError::CALL_OK && handled.operator bool()) {
			return true;
		}
	}
	return false;
}

bool BeefInstance::get(const StringName &p_name, Variant &r_ret) const {
	if (!beef_obj || !beef_script.is_valid()) {
		return false;
	}
	// 1. Exported field.
	if (beef_script->fn_get && beef_script->fn_get(beef_obj, String(p_name).utf8().get_data(), &r_ret)) {
		return true;
	}
	// 2. Dynamic property via the _get virtual (a nil return means "not handled").
	if (beef_script->fn_has_method && beef_script->fn_has_method("_get")) {
		Variant name_arg = String(p_name);
		const Variant *args[1] = { &name_arg };
		Callable::CallError err;
		Variant result = const_cast<BeefInstance *>(this)->callp(SNAME("_get"), args, 1, err);
		if (err.error == Callable::CallError::CALL_OK && result.get_type() != Variant::NIL) {
			r_ret = result;
			return true;
		}
	}
	return false;
}

void BeefInstance::get_property_list(List<PropertyInfo> *p_properties) const {
	if (!beef_script.is_valid()) {
		return;
	}
	// 1. Exported fields (same enumeration the Script-level list uses).
	beef_script->get_script_property_list(p_properties);
	// 2. Dynamic properties advertised by the _get_property_list virtual (Array of Dictionaries with
	//    name/type/hint/hint_string/usage), so dynamic _get/_set props appear in the inspector.
	if (beef_obj && beef_script->fn_has_method && beef_script->fn_has_method("_get_property_list")) {
		Callable::CallError err;
		Variant ret = const_cast<BeefInstance *>(this)->callp(SNAME("_get_property_list"), nullptr, 0, err);
		if (err.error == Callable::CallError::CALL_OK && ret.get_type() == Variant::ARRAY) {
			Array arr = ret;
			for (int i = 0; i < arr.size(); i++) {
				if (arr[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				Dictionary d = arr[i];
				PropertyInfo pi;
				pi.name = d.get("name", "");
				pi.type = (Variant::Type)(int)d.get("type", (int)Variant::NIL);
				pi.hint = (PropertyHint)(int)d.get("hint", (int)PROPERTY_HINT_NONE);
				pi.hint_string = d.get("hint_string", "");
				pi.usage = (int)d.get("usage", (int)PROPERTY_USAGE_DEFAULT);
				if (!pi.name.is_empty()) {
					p_properties->push_back(pi);
				}
			}
		}
	}
}

Variant::Type BeefInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	// Resolve exported (`[GodotExport]`) fields from the per-class property type table.
	int idx = _find_prop_index(p_name);
	if (idx >= 0 && beef_script.is_valid() && beef_script->fn_prop_type) {
		if (r_is_valid) {
			*r_is_valid = true;
		}
		return (Variant::Type)beef_script->fn_prop_type(idx);
	}
	if (r_is_valid) {
		*r_is_valid = false;
	}
	return Variant::NIL;
}

void BeefInstance::validate_property(PropertyInfo &p_property) const {}

bool BeefInstance::property_can_revert(const StringName &p_name) const {
	// An exported property can revert to its (compile-time) default value.
	return beef_script.is_valid() && beef_script->fn_prop_count && beef_script->fn_prop_name &&
			beef_script->fn_prop_default && _find_prop_index(p_name) >= 0;
}

bool BeefInstance::property_get_revert(const StringName &p_name, Variant &r_ret) const {
	if (!beef_script.is_valid() || !beef_script->fn_prop_default) {
		return false;
	}
	int idx = _find_prop_index(p_name);
	if (idx < 0) {
		return false;
	}
	beef_script->fn_prop_default(idx, &r_ret);
	return true;
}

int BeefInstance::_find_prop_index(const StringName &p_name) const {
	if (!beef_script.is_valid() || !beef_script->fn_prop_count || !beef_script->fn_prop_name) {
		return -1;
	}
	int32_t n = beef_script->fn_prop_count();
	for (int32_t i = 0; i < n; i++) {
		const char *name = beef_script->fn_prop_name(i);
		if (name && p_name == StringName(String::utf8(name))) {
			return i;
		}
	}
	return -1;
}

void BeefInstance::get_method_list(List<MethodInfo> *p_list) const {
	if (beef_script.is_valid()) {
		beef_script->get_script_method_list(p_list);
	}
}

bool BeefInstance::has_method(const StringName &p_method) const {
	return beef_script.is_valid() && beef_script->fn_has_method != nullptr &&
			beef_script->fn_has_method(String(p_method).utf8().get_data());
}

Variant BeefInstance::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (!beef_obj || !beef_script.is_valid() || !beef_script->fn_call) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}
	// fn_call dispatches by method name; args/ret are engine Variant*. p_args is const Variant**,
	// so each args[i] is a pointer to a live engine Variant the Beef side reads (and must not free).
	Variant ret;
	bool handled = beef_script->fn_call(beef_obj, String(p_method).utf8().get_data(),
			(void **)p_args, p_argcount, &ret);
	// Not handled -> report INVALID_METHOD so GDVIRTUAL_CALL falls through to other resolution.
	r_error.error = handled ? Callable::CallError::CALL_OK : Callable::CallError::CALL_ERROR_INVALID_METHOD;
	return ret;
}

void BeefInstance::notification(int p_notification, bool p_reversed) {
	if (beef_obj && beef_script.is_valid() && beef_script->fn_notify) {
		beef_script->fn_notify(beef_obj, (int32_t)p_notification);
	}
}

Ref<Script> BeefInstance::get_script() const {
	return beef_script;
}

ScriptLanguage *BeefInstance::get_language() {
	return BeefLanguage::get_singleton();
}

String BeefInstance::to_string(bool *r_valid) {
	// Route to the script's `_ToString` override (engine virtual `_to_string`) if it has one;
	// otherwise report invalid so the engine falls back to the default "<Class#id>" form.
	if (has_method(StringName("_to_string"))) {
		Callable::CallError err;
		Variant ret = callp(StringName("_to_string"), nullptr, 0, err);
		if (err.error == Callable::CallError::CALL_OK && ret.get_type() == Variant::STRING) {
			if (r_valid) {
				*r_valid = true;
			}
			return ret;
		}
	}
	if (r_valid) {
		*r_valid = false;
	}
	return String();
}

// ─── ResourceFormatLoaderBeefScript ──────────────────────────────────────────

Ref<Resource> ResourceFormatLoaderBeefScript::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	Ref<BeefScript> res;
	res.instantiate();

	Error err;
	String code = FileAccess::get_file_as_string(p_path, &err);
	if (err != OK) {
		if (r_error) {
			*r_error = err;
		}
		return Ref<Resource>();
	}

	res->set_source_code(code);
	res->set_path(p_original_path);
	res->script_path = p_original_path;
	res->reload();

	if (r_error) {
		*r_error = OK;
	}
	return res;
}

void ResourceFormatLoaderBeefScript::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("bf");
}

bool ResourceFormatLoaderBeefScript::handles_type(const String &p_type) const {
	return p_type == "Script" || p_type == "BeefScript";
}

String ResourceFormatLoaderBeefScript::get_resource_type(const String &p_path) const {
	return p_path.get_extension().to_lower() == "bf" ? "BeefScript" : "";
}

// ─── ResourceFormatSaverBeefScript ───────────────────────────────────────────

Error ResourceFormatSaverBeefScript::save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	Ref<BeefScript> res = p_resource;
	ERR_FAIL_COND_V(res.is_null(), ERR_INVALID_PARAMETER);

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_V(f.is_null(), ERR_CANT_OPEN);

	f->store_string(res->get_source_code());
	return OK;
}

void ResourceFormatSaverBeefScript::get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const {
	if (Object::cast_to<BeefScript>(*p_resource)) {
		p_extensions->push_back("bf");
	}
}

bool ResourceFormatSaverBeefScript::recognize(const Ref<Resource> &p_resource) const {
	return Object::cast_to<BeefScript>(*p_resource) != nullptr;
}
