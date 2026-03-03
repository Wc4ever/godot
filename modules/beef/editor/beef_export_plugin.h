#ifndef BEEF_EXPORT_PLUGIN_H
#define BEEF_EXPORT_PLUGIN_H

#ifdef TOOLS_ENABLED

#include "editor/export/editor_export_plugin.h"

// Bundles the compiled Beef scripts DLL and the Beef runtime DLLs it depends on into an exported
// game, placed next to the executable (where Windows' default DLL search path finds them). Without
// this, an exported game has no script DLL — the runtime path points at res://beef/build/ which
// isn't packed. Windows-only for now, matching the module's current platform support.
class BeefExportPlugin : public EditorExportPlugin {
	GDCLASS(BeefExportPlugin, EditorExportPlugin);

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;

private:
	// Web export: build the Beef scripts as a wasm SIDE_MODULE and bundle it so the (wasm) engine
	// dlopen()s it at runtime.
	void _export_web(bool p_debug);

public:
	virtual String get_name() const override { return "Beef"; }
};

#endif // TOOLS_ENABLED

#endif // BEEF_EXPORT_PLUGIN_H
