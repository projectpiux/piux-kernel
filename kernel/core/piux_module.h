#ifndef PIUX_MODULE_H
#define PIUX_MODULE_H

#define PIUX_MODULE_ABI        1
#define PIUX_MODULE_SIGNATURE  0x58554950u   
#define PIUX_MODULE_MAX_DEPS   8
#define PIUX_MODULE_NAME_SIZE  32
#define PIUX_MODULE_PATH_SIZE  64

#define PIUX_MODULE_SECTION    ".piux.module"

#ifndef PIUX_MODULE_VERSION
#define PIUX_MODULE_VERSION "0.1.0"
#endif
#ifndef PIUX_MODULE_RELEASE
#define PIUX_MODULE_RELEASE "unknown"
#endif

typedef struct piux_module {
    uint32_t signature;
    uint32_t abi;
    const char *name;
    const char *version;
    const char *release;              
    const char *const *deps;          
    uint32_t dep_count;
    int (*init)(void);                
    void (*exit)(void);               
    const char *description;
} piux_module_t;

typedef enum {
    MODULE_STATE_EMPTY = 0,
    MODULE_STATE_LOADED,
    MODULE_STATE_RUNNING,
    MODULE_STATE_FAILED
} module_state_t;

typedef struct module_image {
    char name[PIUX_MODULE_NAME_SIZE];
    char path[PIUX_MODULE_PATH_SIZE];
    module_state_t state;
    const piux_module_t *info;
    uint32_t base;
    void (*entry)(void);   
    uint32_t size;
} module_image_t;

#define PIUX_MODULE(...)                                                     \
    const piux_module_t piux_module_descriptor __attribute__((              \
        section(PIUX_MODULE_SECTION), used, aligned(4))) = {                \
        .signature = PIUX_MODULE_SIGNATURE,                                 \
        .abi = PIUX_MODULE_ABI,                                             \
        .release = PIUX_MODULE_RELEASE,                                     \
        __VA_ARGS__                                                         \
    }

#define PIUX_EXPORT(symbol) ((const void *)(uintptr_t)(symbol))

#endif
