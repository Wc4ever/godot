/**************************************************************************/
/*  test_beef_bindings.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "../beef_script.h"
#include "../editor/beef_editor_plugin.h"
#include "../editor/bindings_generator.h"
#include "../ide/beef_ide_helper.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"

#include "tests/test_macros.h"

namespace TestBeefBindings {

static String _temp_dir() {
	return OS::get_singleton()->get_cache_path().path_join("godot_beef_bindings_test");
}

// True if the file contains only ASCII bytes. The .bf files must be pure ASCII: StringBuilder
// copies appended const char* byte-by-byte into a char32_t buffer, so any byte >= 0x80 (e.g. a
// box-drawing character in a comment) becomes an invalid codepoint and corrupts/aborts output.
static bool _file_is_ascii(const String &p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return false;
	}
	const uint64_t len = f->get_length();
	for (uint64_t i = 0; i < len; i++) {
		if (f->get_8() >= 0x80) {
			return false;
		}
	}
	return true;
}

static bool _is_ident_char(char32_t c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

TEST_CASE("[Beef][Bindings] generate_bf_api produces valid, ASCII, self-consistent output") {
	BeefBindingsGenerator gen;
	REQUIRE(gen.is_initialized());

	const String dir = _temp_dir();
	DirAccess::make_dir_recursive_absolute(dir);
	REQUIRE(gen.generate_bf_api(dir) == OK);

	// Support files land alongside the bindings (StaticLib), not the user project.
	CHECK(FileAccess::exists(dir.path_join("Native.bf")));
	CHECK(FileAccess::exists(dir.path_join("GodotObjectFactory.bf")));

	// GodotPrimitives.bf: [CRepr] structs + `using System;` so the intrinsic attribute applies,
	// and pure ASCII (no mojibake).
	const String prims_path = dir.path_join("GodotPrimitives.bf");
	REQUIRE(FileAccess::exists(prims_path));
	const String prims = FileAccess::get_file_as_string(prims_path);
	CHECK(prims.contains("using System;"));
	CHECK(prims.contains("[CRepr] public struct Vector2"));
	CHECK(_file_is_ascii(prims_path));

	// Variant exposes conversions for the fixed-size value types (Vector3 tag 9, Color tag 20).
	CHECK(prims.contains("public static Godot.Variant FromVector3(Vector3 v)"));
	CHECK(prims.contains("public Vector3 AsVector3()"));
	CHECK(prims.contains("Native.sVarAsTyped(&s, 20, &o)")); // Color -> TYPE_COLOR == 20
	// Array/Dictionary box/unbox through Variant.
	CHECK(prims.contains("public GodotArray AsArray()"));
	CHECK(prims.contains("public static Godot.Variant FromDictionary(GodotDictionary d)"));
	// StringName/NodePath box from text (type-preserving).
	CHECK(prims.contains("public static Godot.Variant FromStringName(System.String v)"));
	CHECK(prims.contains("public static Godot.Variant FromNodePath(System.String v)"));
	// Packed arrays box/unbox through Variant (POD element buffers).
	CHECK(prims.contains("public static Godot.Variant FromPackedInt32(int32[] a)"));
	CHECK(prims.contains("public Vector3[] AsPackedVector3()"));
	CHECK(prims.contains("public uint8[] AsPackedByte()"));
	// PackedStringArray boxes per-element (not memcpy'd) but is still exposed on Variant.
	CHECK(prims.contains("public static Godot.Variant FromPackedStringArray(System.String[] a)"));
	CHECK(prims.contains("public System.String[] AsPackedStringArray()"));

	// Object.bf is always generated (Object is the root and always registered).
	const String obj_path = dir.path_join("Object.bf");
	REQUIRE(FileAccess::exists(obj_path));
	const String obj = FileAccess::get_file_as_string(obj_path);
	CHECK(_file_is_ascii(obj_path));

	// `free` has no real MethodBind (add_virtual_method), so it must NOT be a null-bind ptrcall;
	// it is special-cased to a direct free.
	CHECK(obj.contains("Native.FreeObject(_godotOwner)"));
	CHECK_FALSE(obj.contains("MethodBindPtrcall(_mb_free"));

	// Every referenced _mb_<name> cache field must be declared in the same file (regression:
	// property accessors used to reference undeclared _mb_get_X / _mb_set_X).
	HashSet<String> declared;
	HashSet<String> referenced;
	const Vector<String> lines = obj.split("\n");
	for (const String &line : lines) {
		const String t = line.strip_edges();
		if (t.begins_with("static void* _mb_")) {
			declared.insert(t.substr(13).trim_suffix(";")); // strip "static void* " and ";"
			continue;
		}
		int from = 0;
		while (true) {
			const int idx = line.find("_mb_", from);
			if (idx < 0) {
				break;
			}
			int end = idx + 4;
			while (end < line.length() && _is_ident_char(line[end])) {
				end++;
			}
			referenced.insert(line.substr(idx, end - idx));
			from = end;
		}
	}
	for (const String &r : referenced) {
		CHECK_MESSAGE(declared.has(r), vformat("Object.bf references undeclared MethodBind field '%s'", r));
	}
}

TEST_CASE("[Beef][Bindings] Array/Dictionary marshalling is wired end to end") {
	BeefBindingsGenerator gen;
	const String dir = _temp_dir();
	DirAccess::make_dir_recursive_absolute(dir);
	REQUIRE(gen.generate_bf_api(dir) == OK);

	// Primitives expose container handles with the value-type API (New/Dispose/Count/indexer).
	const String prims = FileAccess::get_file_as_string(dir.path_join("GodotPrimitives.bf"));
	CHECK(prims.contains("public struct GodotArray"));
	CHECK(prims.contains("public struct GodotDictionary"));
	CHECK(prims.contains("public static GodotArray New()"));
	CHECK(prims.contains("public static GodotDictionary New()"));

	// Native declares the engine container interop pointers...
	const String nat = FileAccess::get_file_as_string(dir.path_join("Native.bf"));
	CHECK(nat.contains("sArrPushBack"));
	CHECK(nat.contains("sDictGet"));
	CHECK(nat.contains("public static void ArrNew(void* dest)"));

	// ...and the runtime glue binds them into the function table.
	const String rt_dir = _temp_dir().path_join("rt");
	DirAccess::make_dir_recursive_absolute(rt_dir);
	REQUIRE(gen.generate_runtime_glue(rt_dir) == OK);
	const String rt = FileAccess::get_file_as_string(rt_dir.path_join("GodotRuntime.bf"));
	CHECK(rt.contains("Native.sArrNew"));
	CHECK(rt.contains("Native.sDictHas"));

	// A method returning Array (Node.get_children) must marshal, not stub: it builds the return
	// container before the ptrcall and returns it by value.
	const String node_path = dir.path_join("Node.bf");
	if (FileAccess::exists(node_path)) {
		const String node = FileAccess::get_file_as_string(node_path);
		CHECK(node.contains("public GodotArray GetChildren"));
		CHECK(node.contains("Native.ArrNew(&_ret)"));
	}
}

TEST_CASE("[Beef][Bindings] generate_script_base emits the GodotRegister comptime attribute") {
	BeefBindingsGenerator gen;
	const String dir = _temp_dir();
	DirAccess::make_dir_recursive_absolute(dir);
	REQUIRE(gen.generate_script_base(dir) == OK);

	const String path = dir.path_join("GodotScript.bf");
	REQUIRE(FileAccess::exists(path));
	const String src = FileAccess::get_file_as_string(path);

	// Comptime attribute that reflects a target type and emits the per-class exports.
	CHECK(src.contains("struct GodotRegisterAttribute : Attribute, IComptimeTypeApply"));
	CHECK(src.contains("Compiler.EmitTypeBody(type, s)"));
	// The five C ABI exports the engine resolves by name must all be emitted.
	CHECK(src.contains("BeefGodot_Create_"));
	CHECK(src.contains("BeefGodot_Destroy_"));
	CHECK(src.contains("BeefGodot_Notification_"));
	CHECK(src.contains("BeefGodot_HasMethod_"));
	CHECK(src.contains("BeefGodot_Call_"));
	// Intrinsic attributes must be unqualified (with `using System;`) to take effect.
	CHECK(src.contains("using System;"));
	// Dispatch handles Variant parameters and boxes non-void return values back into ret.
	CHECK(src.contains("*(Godot.Variant*)ret ="));
	CHECK(src.contains("Godot.Variant.FromBool("));
	// Exported fields: a [GodotExport] attribute plus get/set + property-list exports.
	CHECK(src.contains("struct GodotExportAttribute : Attribute"));
	CHECK(src.contains("HasCustomAttribute<GodotExportAttribute>()"));
	CHECK(src.contains("BeefGodot_Set_"));
	CHECK(src.contains("BeefGodot_Get_"));
	CHECK(src.contains("BeefGodot_PropCount_"));
	CHECK(src.contains("BeefGodot_PropName_"));
	CHECK(src.contains("BeefGodot_PropType_"));
	// Property default values come from a freshly-constructed instance.
	CHECK(src.contains("BeefGodot_PropDefault_"));
	CHECK(src.contains("let def = scope "));
	// Inspector hints from [GodotExport(hint, "string")].
	CHECK(src.contains("BeefGodot_PropHint_"));
	CHECK(src.contains("BeefGodot_PropHintString_"));
	// Signals: a [GodotSignal] attribute plus the signal-list exports.
	CHECK(src.contains("struct GodotSignalAttribute : Attribute"));
	CHECK(src.contains("HasCustomAttribute<GodotSignalAttribute>()"));
	CHECK(src.contains("BeefGodot_SignalCount_"));
	CHECK(src.contains("BeefGodot_SignalName_"));
	CHECK(src.contains("BeefGodot_SignalArgc_"));
	CHECK(src.contains("BeefGodot_SignalArgType_")); // typed signal args (sigIdx, argIdx) -> Variant::Type
	CHECK(_file_is_ascii(path));
}

TEST_CASE("[Beef][Bindings] Native exposes an EmitSignal helper") {
	BeefBindingsGenerator gen;
	const String dir = _temp_dir();
	DirAccess::make_dir_recursive_absolute(dir);
	REQUIRE(gen.generate_bf_api(dir) == OK);
	const String nat = FileAccess::get_file_as_string(dir.path_join("Native.bf"));
	CHECK(nat.contains("sEmitSignal"));
	CHECK(nat.contains("public static void EmitSignal(void* owner"));
	// Callable construction (used to connect a signal to a script method).
	CHECK(nat.contains("public static Godot.Callable MakeCallable(void* owner"));

	// Connect returns the Error enum; enum-typed returns must marshal, not stub.
	const String obj = FileAccess::get_file_as_string(dir.path_join("Object.bf"));
	CHECK(obj.contains("public Error Connect("));
	CHECK(obj.contains("MethodBindPtrcall(_mb_connect"));
	CHECK_FALSE(obj.contains("public Error Connect(System.String signal, Callable callable, uint32 flags)\n\t{\n\t\t// TODO"));
}

TEST_CASE("[Beef][Bindings] parse_global_class extracts class name + base from [GodotScript]") {
	// A [GodotScript] class: name + base come from the class declaration after the marker.
	{
		String src =
				"using Godot;\n"
				"[GodotScript]\n"
				"class MyNode : Node3D {\n"
				"\tpublic override void _Ready() {}\n"
				"}\n";
		String name, base;
		REQUIRE(BeefScript::parse_global_class(src, name, base));
		CHECK(name == "MyNode");
		CHECK(base == "Node3D");
	}
	// Combined attribute list still matches the marker as a whole token.
	{
		String src = "[GodotScript, GodotTool]\nclass Player : CharacterBody2D {}\n";
		String name, base;
		REQUIRE(BeefScript::parse_global_class(src, name, base));
		CHECK(name == "Player");
		CHECK(base == "CharacterBody2D");
	}
	// No [GodotScript] marker -> not a global class. GodotScriptAttribute must not match the token.
	{
		String src = "class Plain : Node {}\n";
		String name, base;
		CHECK_FALSE(BeefScript::parse_global_class(src, name, base));
	}
	{
		String src = "struct GodotScriptAttribute : Attribute {}\n";
		String name, base;
		CHECK_FALSE(BeefScript::parse_global_class(src, name, base));
	}
}

TEST_CASE("[Beef][Bindings] generate_script_base emits the [GodotTool] and [GodotScript] attributes") {
	// These attributes must be declared so [GodotTool] / [GodotScript] compile in user scripts.
	BeefBindingsGenerator gen;
	const String dir = _temp_dir();
	DirAccess::make_dir_recursive_absolute(dir);
	REQUIRE(gen.generate_script_base(dir) == OK);
	const String src = FileAccess::get_file_as_string(dir.path_join("GodotScript.bf"));
	CHECK(src.contains("struct GodotToolAttribute : Attribute"));
	CHECK(src.contains("struct GodotScriptAttribute : Attribute"));
	CHECK(src.contains("struct GodotRpcAttribute : Attribute"));
	CHECK(src.contains("enum GodotRpcMode"));
}

TEST_CASE("[Beef][Bindings] generate_registrations emits one registrar per [GodotScript] class") {
	BeefBindingsGenerator gen;
	const String dir = _temp_dir().path_join("reg_src");
	DirAccess::make_dir_recursive_absolute(dir);
	{
		Ref<FileAccess> f = FileAccess::open(dir.path_join("Foe.bf"), FileAccess::WRITE);
		f->store_string("using Godot;\nnamespace GameScripts;\n[GodotScript]\nclass Foe : Node3D {}\n");
	}
	{
		// No [GodotScript] marker -> must be ignored.
		Ref<FileAccess> f = FileAccess::open(dir.path_join("Helper.bf"), FileAccess::WRITE);
		f->store_string("class Helper {}\n");
	}
	{
		// A [GodotScript] class in a SUBFOLDER must be found (recursive scan).
		DirAccess::make_dir_recursive_absolute(dir.path_join("enemies"));
		Ref<FileAccess> f = FileAccess::open(dir.path_join("enemies").path_join("Goblin.bf"), FileAccess::WRITE);
		f->store_string("using Godot;\n[GodotScript]\nclass Goblin : Node2D {}\n");
	}
	// Scan `dir` recursively, write the registrations next to it.
	REQUIRE(gen.generate_registrations(dir, dir) == OK);

	const String out = FileAccess::get_file_as_string(dir.path_join("GodotRegistrations.bf"));
	CHECK(out.contains("GodotRegister(typeof(GameScripts.Foe))"));
	CHECK(out.contains("GodotRegister(typeof(Goblin))")); // from the subfolder
	CHECK(out.contains("AlwaysInclude(IncludeAllMethods=true)"));
	CHECK_FALSE(out.contains("Helper")); // unmarked class not registered
}

TEST_CASE("[Beef][Bindings] generate_runtime_glue exports BeefGodot_Init") {
	BeefBindingsGenerator gen;
	const String dir = _temp_dir();
	DirAccess::make_dir_recursive_absolute(dir);
	REQUIRE(gen.generate_runtime_glue(dir) == OK);

	const String path = dir.path_join("GodotRuntime.bf");
	REQUIRE(FileAccess::exists(path));
	const String rt = FileAccess::get_file_as_string(path);
	// Intrinsic [CLink, Export] must be unqualified (with `using System;`) to actually export.
	CHECK(rt.contains("using System;"));
	CHECK(rt.contains("[CLink, Export]"));
	CHECK(rt.contains("BeefGodot_Init"));
	CHECK(_file_is_ascii(path));
}

TEST_CASE("[Beef][Build] BeefBuild diagnostic lines parse into file/line/col/message") {
	bool is_err = false;
	String file, msg;
	int line = 0, col = 0;

	// Error: "ERROR: <message> at line <L>:<C> in <FILE>"
	REQUIRE(BeefEditorPlugin::test_parse_line(
			"ERROR: Expected expression at line 96:35 in d:\\proj\\src\\MyNode.bf", is_err, file, line, col, msg));
	CHECK(is_err);
	CHECK(line == 96);
	CHECK(col == 35);
	CHECK(file == "d:\\proj\\src\\MyNode.bf");
	CHECK(msg == "Expected expression");

	// Warning with a BFxxxx code and the capital-"Line" location form.
	REQUIRE(BeefEditorPlugin::test_parse_line(
			"WARNING(2): BF0114: Method hides inherited member from 'Godot.PhysicsServer2D'. Use either 'override' or 'new'. Line 445:2 in d:\\proj\\GodotBindings\\src\\PhysicsServer2DExtension.bf",
			is_err, file, line, col, msg));
	CHECK_FALSE(is_err);
	CHECK(line == 445);
	CHECK(col == 2);
	CHECK(file.ends_with("PhysicsServer2DExtension.bf"));
	CHECK(msg == "Method hides inherited member from 'Godot.PhysicsServer2D'. Use either 'override' or 'new'.");

	// Warning with "at line" form.
	REQUIRE(BeefEditorPlugin::test_parse_line(
			"WARNING(5): BF0168: The variable 'y' is declared but never used at line 96:31 in d:\\proj\\src\\MyNode.bf",
			is_err, file, line, col, msg));
	CHECK_FALSE(is_err);
	CHECK(line == 96);
	CHECK(col == 31);
	CHECK(msg == "The variable 'y' is declared but never used");

	// Non-diagnostic lines are rejected (summary, soft-fail, blank, plain text).
	CHECK_FALSE(BeefEditorPlugin::test_parse_line("Errors: 1. Warnings: 4.", is_err, file, line, col, msg));
	CHECK_FALSE(BeefEditorPlugin::test_parse_line("ERROR-SOFT: Compile failed. Total build time: 1.38s", is_err, file, line, col, msg));
	CHECK_FALSE(BeefEditorPlugin::test_parse_line("", is_err, file, line, col, msg));
	CHECK_FALSE(BeefEditorPlugin::test_parse_line("Compiling...", is_err, file, line, col, msg));
}

TEST_CASE("[Beef][Build] build log cleaner strips the progress bar but keeps real lines") {
	// Carriage-return redraws collapse to the final state; the bare progress bar is dropped.
	String raw = "Compiling...\n\r[  ]****\r[        ]********************\nBeef compilation time: 1.43s\n";
	String clean = BeefEditorPlugin::_clean_build_output(raw);
	CHECK(clean.contains("Compiling..."));
	CHECK(clean.contains("Beef compilation time: 1.43s"));
	CHECK_FALSE(clean.contains("*"));
	CHECK_FALSE(clean.contains("]"));

	// Real BeefBuild format: "[   ]" then backspaces (0x08) overwritten with '*'s, on one line.
	String bs = "[   ]\b\b\b\b\b*****\nBeef compilation time: 1.43s\n";
	String bs_clean = BeefEditorPlugin::_clean_build_output(bs);
	CHECK_FALSE(bs_clean.contains("*"));
	CHECK_FALSE(bs_clean.contains("["));
	CHECK(bs_clean.contains("Beef compilation time: 1.43s"));

	// A diagnostic line that merely ends with a code location is not mistaken for the bar.
	String diag = "ERROR: Expected expression at line 96:35 in d:\\proj\\src\\MyNode.bf\n";
	CHECK(BeefEditorPlugin::_clean_build_output(diag).contains("Expected expression"));
}

TEST_CASE("[Beef][IDE] IDEHelper parser reports syntax errors with locations") {
	BeefIDEHelper *ide = BeefIDEHelper::get_singleton();
	if (!ide->is_available()) {
		WARN("IDEHelper64.dll not available; skipping parser test.");
		return;
	}

	// Well-formed source: no errors.
	{
		Vector<BeefIDEHelper::ParseError> errors;
		REQUIRE(ide->get_parse_errors("namespace Test;\nclass Foo\n{\n\tpublic int X = 1;\n}\n", "Foo.bf", errors));
		int error_count = 0;
		for (const BeefIDEHelper::ParseError &e : errors) {
			if (!e.is_warning) {
				error_count++;
			}
		}
		CHECK(error_count == 0);
	}

	// Broken source: a syntax error on line 3 must be reported.
	{
		Vector<BeefIDEHelper::ParseError> errors;
		REQUIRE(ide->get_parse_errors("class Foo\n{\n\tpublic int X = ;\n}\n", "Foo.bf", errors));
		bool found_error = false;
		for (const BeefIDEHelper::ParseError &e : errors) {
			if (!e.is_warning) {
				found_error = true;
				CHECK(e.line >= 1); // 1-based line reported
			}
		}
		CHECK(found_error);
	}
}

TEST_CASE("[Beef][IDE] IDEHelper classifier tags keywords, comments and strings") {
	BeefIDEHelper *ide = BeefIDEHelper::get_singleton();
	if (!ide->is_available()) {
		WARN("IDEHelper64.dll not available; skipping classifier test.");
		return;
	}

	//             0         1         2
	//             0123456789012345678901234567
	String source = "class Foo { } // hi\n";
	Vector<uint8_t> kinds;
	REQUIRE(ide->classify(source, "Foo.bf", kinds));
	REQUIRE(kinds.size() == source.utf8().length());

	// "class" (cols 0-4) is a keyword.
	CHECK(kinds[0] == BeefIDEHelper::TK_KEYWORD);
	CHECK(kinds[4] == BeefIDEHelper::TK_KEYWORD);

	// The "// hi" trailing comment (starts at col 14) is a comment.
	CHECK(kinds[14] == BeefIDEHelper::TK_COMMENT);

	// "Foo" (col 6) is not a keyword.
	CHECK(kinds[6] != BeefIDEHelper::TK_KEYWORD);
}

TEST_CASE("[Beef][IDE] IDEHelper find_function locates method declaration lines") {
	BeefIDEHelper *ide = BeefIDEHelper::get_singleton();
	if (!ide->is_available()) {
		WARN("IDEHelper64.dll not available; skipping find_function test.");
		return;
	}

	// Lines (0-based):     0       1   2                3   4                                 5  6
	String source = "class Foo\n{\n\tpublic void Bar()\n\t{\n\t\tBaz();\n\t}\n\tpublic int Baz() { return 0; }\n}\n";
	CHECK(ide->find_function("Bar", source) == 2);
	CHECK(ide->find_function("Baz", source) == 6); // the declaration, not the call on line 4
	CHECK(ide->find_function("Missing", source) == -1);
}

TEST_CASE("[Beef][Lang] reindent_code indents by bracket depth, ignoring strings/comments") {
	// Messy indentation; braces inside a string and a comment must NOT affect depth.
	String code =
			"class Foo\n"
			"{\n"
			"public void Bar()\n"
			"{\n"
			"let s = \"a{b}c\"; // brace } in string and comment\n"
			"if (x)\n"
			"{\n"
			"return;\n"
			"}\n"
			"}\n"
			"}\n";
	BeefLanguage::reindent_code(code, 0, code.split("\n").size() - 1, "\t");
	Vector<String> lines = code.split("\n");

	CHECK(lines[0] == "class Foo");
	CHECK(lines[1] == "{");
	CHECK(lines[2] == "\tpublic void Bar()");
	CHECK(lines[3] == "\t{");
	CHECK(lines[4] == "\t\tlet s = \"a{b}c\"; // brace } in string and comment");
	CHECK(lines[5] == "\t\tif (x)");
	CHECK(lines[6] == "\t\t{");
	CHECK(lines[7] == "\t\t\treturn;");
	CHECK(lines[8] == "\t\t}");
	CHECK(lines[9] == "\t}");
	CHECK(lines[10] == "}");
}

TEST_CASE("[Beef][Lang] gather_completions offers keywords and exposed Godot types by prefix") {
	Vector<String> keywords = { "return", "new", "class", "namespace" };

	// Prefix "Nod" → exposed Godot types (Node3D, Node2D, ...), no keyword matches.
	{
		List<ScriptLanguage::CodeCompletionOption> opts;
		BeefLanguage::gather_completions("Nod", false, keywords, &opts);
		bool has_node3d = false;
		for (const ScriptLanguage::CodeCompletionOption &o : opts) {
			if (o.display == "Node3D") {
				has_node3d = true;
				CHECK(o.kind == ScriptLanguage::CODE_COMPLETION_KIND_CLASS);
			}
		}
		CHECK(has_node3d);
	}

	// Prefix "ret" → the keyword "return".
	{
		List<ScriptLanguage::CodeCompletionOption> opts;
		BeefLanguage::gather_completions("ret", false, keywords, &opts);
		bool has_return = false;
		for (const ScriptLanguage::CodeCompletionOption &o : opts) {
			if (o.display == "return") {
				has_return = true;
			}
		}
		CHECK(has_return);
	}

	// After a '.', member completion is deferred to the (not-yet-built) resolver → no options.
	{
		List<ScriptLanguage::CodeCompletionOption> opts;
		BeefLanguage::gather_completions("x", true, keywords, &opts);
		CHECK(opts.is_empty());
	}
}

} // namespace TestBeefBindings

#endif // TOOLS_ENABLED
