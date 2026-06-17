/**************************************************************************/
/*  beef_syntax_highlighter.h                                             */
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
