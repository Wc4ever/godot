#ifndef BEEF_SYNTAX_HIGHLIGHTER_H
#define BEEF_SYNTAX_HIGHLIGHTER_H

#include "editor/script/script_editor_plugin.h"

// Editor syntax highlighting for .bf scripts, driven by IDEHelper's classifier (the same one
// BeefIDE uses). The whole document is classified once per change into per-character token kinds,
// then served line-by-line to the script editor.
class BeefSyntaxHighlighter : public EditorSyntaxHighlighter {
	GDCLASS(BeefSyntaxHighlighter, EditorSyntaxHighlighter)

	bool _classified = false;
	Vector<uint8_t> _kinds; // per-character token kind (BeefIDEHelper::TokenKind) for the whole doc
	Vector<int> _line_starts; // character offset of each line start

	// Colors pulled from the text editor theme.
	Color _keyword_color;
	Color _comment_color;
	Color _string_color;
	Color _number_color;
	Color _type_color;
	Color _function_color;
	Color _member_color;
	Color _default_color;

	void _classify_now();
	Color _color_for(uint8_t p_kind, char32_t p_first_char) const;

protected:
	static void _bind_methods() {}

public:
	virtual Dictionary _get_line_syntax_highlighting_impl(int p_line) override;
	virtual void _clear_highlighting_cache() override;
	virtual void _update_cache() override;

	virtual PackedStringArray _get_supported_languages() const override;
	virtual String _get_name() const override;
	virtual Ref<EditorSyntaxHighlighter> _create() const override;
};

#endif // BEEF_SYNTAX_HIGHLIGHTER_H
