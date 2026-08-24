idf_component_register(
    SRCS
        "src/shell.c"
        "src/shell_ext.c"
        "src/ring.c"
        "shell_app.c"
    INCLUDE_DIRS
        "src"
        "."
    REQUIRES
        driver
        log
    LDFRAGMENTS
        "linker.lf"
)

# letter-shell 2.0.8 is C89-style; ESP-IDF -Werror would reject it otherwise
component_compile_options(
    -Wno-error=unused-function
    -Wno-error=unused-variable
    -Wno-error=char-subscripts
    -Wno-cast-function-type
)
