#include "register_types.h"

#include "beef_script.h"
#include "compiler/beef_compiler.h"
#include "ide/beef_ide_helper.h"

#include "core/config/engine.h"

#ifdef TOOLS_ENABLED
#include "editor/beef_editor_plugin.h"
#include "editor/plugins/editor_plugin.h"
#endif

BeefLanguage *script_language_beef = nullptr;
Ref<ResourceFormatLoaderBeefScript> resource_loader_beef;
Ref<ResourceFormatSaverBeefScript> resource_saver_beef;
BeefCompiler *beef_compiler = nullptr;

void initialize_beef_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_CORE) {
		beef_compiler = memnew(BeefCompiler);
		// Auto-detect BeefBuild.exe now. If the user has a custom path in EditorSettings,
		// _editor_init_callback() (called after EditorNode init) will re-run find_beef_tools().
		beef_compiler->find_beef_tools();
		return;
	}

#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<BeefEditorPlugin>();
		return;
	}
#endif

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(BeefScript);

	script_language_beef = memnew(BeefLanguage);
	ScriptServer::register_language(script_language_beef);

	resource_loader_beef.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_beef);

	resource_saver_beef.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_beef);
}

void uninitialize_beef_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_CORE) {
		BeefIDEHelper::free_singleton();
		if (beef_compiler) {
			beef_compiler->cleanup();
			memdelete(beef_compiler);
			beef_compiler = nullptr;
		}
		return;
	}

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	ScriptServer::unregister_language(script_language_beef);

	if (script_language_beef) {
		memdelete(script_language_beef);
	}

	ResourceLoader::remove_resource_format_loader(resource_loader_beef);
	resource_loader_beef.unref();

	ResourceSaver::remove_resource_format_saver(resource_saver_beef);
	resource_saver_beef.unref();
}
