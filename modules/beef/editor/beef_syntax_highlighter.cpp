#include "beef_syntax_highlighter.h"

#include "../ide/beef_ide_helper.h"

#include "editor/settings/editor_settings.h"
#include "scene/gui/text_edit.h"

// UTF-8 byte length of a single code point. BeefIDEHelper::classify() indexes its kind array by
// UTF-8 byte offset, but TextEdit works in String (char) offsets — so we convert char columns to
// byte offsets when looking up kinds, otherwise highlighting drifts after the first multibyte char.
static int _utf8_char_len(char32_t c) {
	if (c < 0x80) {
		return 1;
	} else if (c < 0x800) {
		return 2;
	} else if (c < 0x10000) {
		return 3;
	}
	return 4;
}

void BeefSyntaxHighlighter::_update_cache() {
	// Pull highlighting colors from the editor's text-editor theme.
	_keyword_color = EDITOR_GET("text_editor/theme/highlighting/keyword_color");
	_comment_color = EDITOR_GET("text_editor/theme/highlighting/comment_color");
	_string_color = EDITOR_GET("text_editor/theme/highlighting/string_color");
	_number_color = EDITOR_GET("text_editor/theme/highlighting/number_color");
	_type_color = EDITOR_GET("text_editor/theme/highlighting/base_type_color");
	_function_color = EDITOR_GET("text_editor/theme/highlighting/function_color");
	_member_color = EDITOR_GET("text_editor/theme/highlighting/member_variable_color");
	_default_color = EDITOR_GET("text_editor/theme/highlighting/text_color");
	_classified = false;
}

void BeefSyntaxHighlighter::_clear_highlighting_cache() {
	_classified = false;
	_kinds.clear();
	_line_starts.clear();
}

void BeefSyntaxHighlighter::_classify_now() {
	_classified = true;
	_kinds.clear();
	_line_starts.clear();

	TextEdit *te = get_text_edit();
	if (!te) {
		return;
	}
	String text = te->get_text();

	BeefIDEHelper::get_singleton()->classify(text, "edit.bf", _kinds);

	// Byte offset of each line start: _kinds is indexed by UTF-8 byte, so line starts must be too.
	_line_starts.push_back(0);
	int byte_off = 0;
	for (int i = 0; i < text.length(); i++) {
		const char32_t c = text[i];
		byte_off += _utf8_char_len(c);
		if (c == '\n') {
			_line_starts.push_back(byte_off);
		}
	}
}

Color BeefSyntaxHighlighter::_color_for(uint8_t p_kind, char32_t p_first_char) const {
	switch (p_kind) {
		case BeefIDEHelper::TK_KEYWORD:
			return _keyword_color;
		case BeefIDEHelper::TK_COMMENT:
			return _comment_color;
		case BeefIDEHelper::TK_LITERAL:
			return (p_first_char == '"' || p_first_char == '\'') ? _string_color : _number_color;
		case BeefIDEHelper::TK_TYPE:
		case BeefIDEHelper::TK_PRIMITIVE_TYPE:
		case BeefIDEHelper::TK_STRUCT:
		case BeefIDEHelper::TK_GENERIC_PARAM:
		case BeefIDEHelper::TK_REF_TYPE:
		case BeefIDEHelper::TK_INTERFACE:
		case BeefIDEHelper::TK_NAMESPACE:
			return _type_color;
		case BeefIDEHelper::TK_METHOD:
			return _function_color;
		case BeefIDEHelper::TK_MEMBER:
			return _member_color;
		default:
			return _default_color;
	}
}

Dictionary BeefSyntaxHighlighter::_get_line_syntax_highlighting_impl(int p_line) {
	Dictionary result;
	if (!_classified) {
		_classify_now();
	}

	TextEdit *te = get_text_edit();
	if (!te || p_line < 0 || p_line >= _line_starts.size()) {
		return result;
	}

	String line_text = te->get_line(p_line);
	const int line_byte_start = _line_starts[p_line];
	const int n = line_text.length();

	int prev_kind = -1;
	int byte_col = 0; // byte offset within the line, advanced per code point
	for (int col = 0; col < n; col++) {
		const int idx = line_byte_start + byte_col;
		const uint8_t kind = (idx >= 0 && idx < _kinds.size()) ? _kinds[idx] : 0;
		if ((int)kind != prev_kind) {
			Dictionary entry;
			entry["color"] = _color_for(kind, line_text[col]);
			result[col] = entry;
			prev_kind = kind;
		}
		byte_col += _utf8_char_len(line_text[col]);
	}
	return result;
}

PackedStringArray BeefSyntaxHighlighter::_get_supported_languages() const {
	PackedStringArray langs;
	langs.push_back("Beef"); // matches BeefLanguage::get_name()
	return langs;
}

String BeefSyntaxHighlighter::_get_name() const {
	return "Beef";
}

Ref<EditorSyntaxHighlighter> BeefSyntaxHighlighter::_create() const {
	Ref<BeefSyntaxHighlighter> hl;
	hl.instantiate();
	return hl;
}
