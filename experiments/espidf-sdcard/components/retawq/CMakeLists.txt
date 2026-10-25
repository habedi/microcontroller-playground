idf_component_register(
    SRCS "init.c" "main.c" "parser.c" "resource.c" "stuff.c" "cursesbi.c"
    INCLUDE_DIRS "." "include"
    REQUIRES vfs lwip esp_netif mbedtls
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-pointer-sign
    -Wno-format
    -Wno-format-overflow
    -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast
    -Wno-discarded-qualifiers
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-implicit-function-declaration
    -Wno-trigraphs
    -Wno-address
)
