/**************************************************************************/
/*  interop_types.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/math/math_defs.h"

// Opaque, fixed-size value types that cross the C ABI between the engine and Beef.
// Mirrors the layout contract used by modules/mono (interop_types.h): each type is a raw
// byte buffer sized to the corresponding C++ Godot type, reinterpret-cast to the real type
// on the engine side via memnew_placement. The Beef side declares matching [CRepr] structs
// (see the generated GodotPrimitives.bf). Sizes are build-correct because real_t may be
// float or double depending on the precision build.
//
// These names intentionally match the historical GDNative names; they are only included by
// the Beef module's own translation units.

#ifdef __cplusplus
extern "C" {
#endif

#define GODOT_VARIANT_SIZE (sizeof(real_t) * 4 + sizeof(int64_t))

typedef struct {
	uint8_t _dont_touch_that[GODOT_VARIANT_SIZE];
} godot_variant;

#define GODOT_ARRAY_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[GODOT_ARRAY_SIZE];
} godot_array;

#define GODOT_DICTIONARY_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[GODOT_DICTIONARY_SIZE];
} godot_dictionary;

#define GODOT_STRING_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[GODOT_STRING_SIZE];
} godot_string;

#define GODOT_STRING_NAME_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[GODOT_STRING_NAME_SIZE];
} godot_string_name;

#define GODOT_NODE_PATH_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[GODOT_NODE_PATH_SIZE];
} godot_node_path;

// PackedArrays (PackedByteArray, PackedInt32Array, ...) share a 2-pointer cow layout.
#define GODOT_PACKED_ARRAY_SIZE (2 * sizeof(void *))

typedef struct {
	uint8_t _dont_touch_that[GODOT_PACKED_ARRAY_SIZE];
} godot_packed_array;

#define GODOT_RID_SIZE sizeof(uint64_t)

typedef struct {
	uint8_t _dont_touch_that[GODOT_RID_SIZE];
} godot_rid;

// Alignment/size hardcoded in `core/variant/callable.h`.
#define GODOT_CALLABLE_SIZE (16)

typedef struct {
	uint8_t _dont_touch_that[GODOT_CALLABLE_SIZE];
} godot_callable;

#define GODOT_SIGNAL_SIZE (16)

typedef struct {
	uint8_t _dont_touch_that[GODOT_SIGNAL_SIZE];
} godot_signal;

#ifdef __cplusplus
}
#endif
