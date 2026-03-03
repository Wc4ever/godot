def can_build(env, platform):
    # The runtime is implemented for Windows (DLL load + IDEHelper) and Web (wasm side module via
    # dlopen). On other platforms the module would compile to a scripting language that can't load
    # any compiled Beef library yet, so don't advertise buildability there (cross-platform desktop
    # .so/.dylib loading is a pending TODO).
    return platform in ("windows", "web")


def configure(env):
    pass


def get_doc_classes():
    return [
        "BeefScript",
    ]


def get_doc_path():
    return "doc_classes"


def is_enabled():
    # Disabled by default. Use module_beef_enabled=yes to enable.
    return False
