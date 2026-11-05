#include <stdint.h>
#include "module.h"
#include "../bin/commands.h"
#include "shell.h"

void vga_puts(const char *text);
void vga_putc(char character);

static int word_equals(const char *left, const char *right) {
    int index = 0;
    while (left[index] && left[index] == right[index]) index++;
    return left[index] == right[index];
}

static void print_error(int result) {
    if (result < 0) {
        vga_puts("mod: ");
        vga_puts(module_last_detail[0] ? module_last_detail : "load failed");
        vga_puts("\n");
    }
}

static void list_modules(void) {
    char line[96];
    int shown = 0;
    for (int index = 0; index < module_count(); index++) {
        const module_image_t *image = module_at(index);
        if (!image) continue;
        module_describe(image, line, sizeof(line));
        vga_puts("  ");
        vga_puts(line);
        if (image->info && image->info->description && image->info->description[0]) {
            vga_puts("  - ");
            vga_puts(image->info->description);
        }
        vga_puts("\n");
        shown++;
    }
    if (!shown) vga_puts("  no modules loaded\n");
}

static void list_symbols(void) {
    vga_puts("Core symbol export is compiled into kernel/core_symbols.c.\n");
    vga_puts("Modules must resolve against it (see 'mod info <name>').\n");
}

void mod_command(const char *parameter, void (*puts)(const char *), void (*putc)(char)) {
    (void)putc;
    const char *action = parameter;
    const char *argument;
    int result;

    while (*action == ' ') action++;
    argument = action;
    while (*argument && *argument != ' ') argument++;
    while (*argument == ' ') argument++;

    if (!action[0] || word_equals(action, "list")) {
        list_modules();
        return;
    }
    if (word_equals(action, "help")) {
        puts("mod list             list loaded modules\n");
        puts("mod info <name>      name, version, release, dependencies\n");
        puts("mod load <name>      load /lib/modules/<name>.mod (+ deps)\n");
        puts("mod load-path <path> load a module from an explicit path\n");
        puts("mod unload <name>    run exit hook and drop exported symbols\n");
        puts("mod reload <name>    unload then load again\n");
        return;
    }
    if (word_equals(action, "symbols") || word_equals(action, "sym")) {
        list_symbols();
        return;
    }
    if (word_equals(action, "load") && argument[0]) {
        int first_free = -1;
        for (int index = 0; index < module_count(); index++) {
            if (!module_at(index)) { first_free = index; break; }
        }
        result = module_load_by_name(argument);
        if (result == 0) {
            puts("mod: loaded");
            if (first_free >= 0 && module_at(first_free)) {
                const module_image_t *loaded = module_at(first_free);
                puts(" "); puts(loaded->name);
                if (loaded->info) { puts(" (version "); puts(loaded->info->version); }
                if (loaded->info) puts(")");
            }
            puts("\n");
        } else if (result > 0) puts("mod: already loaded\n");
        else print_error(result);
        return;
    }
    if (word_equals(action, "load-path") && argument[0]) {
        result = module_load(argument);
        if (result == 0) puts("mod: loaded\n");
        else if (result > 0) puts("mod: already loaded\n");
        else print_error(result);
        return;
    }
    if (word_equals(action, "unload") && argument[0]) {
        if (module_unload(argument) == 0) puts("mod: unloaded\n");
        else puts("mod: no such module\n");
        return;
    }
    if (word_equals(action, "reload") && argument[0]) {
        result = module_reload(argument);
        if (result < 0) print_error(result);
        else puts("mod: reloaded\n");
        return;
    }
    if (word_equals(action, "info") && argument[0]) {
        const module_image_t *image = module_find(argument);
        if (!image) {
            puts("mod: no such module\n");
            return;
        }
        puts("name:    "); puts(image->name); puts("\n");
        puts("path:    "); puts(image->path); puts("\n");
        if (image->info) {
            puts("version: "); puts(image->info->version ? image->info->version : "?"); puts("\n");
            puts("release: "); puts(image->info->release ? image->info->release : "?"); puts("\n");
            puts("abi:     ");
            putc((char)('0' + image->info->abi));
            puts("\n");
            puts("deps:    ");
            if (!image->info->dep_count) puts("none");
            for (uint32_t index = 0; index < image->info->dep_count; index++) {
                if (index) puts(", ");
                puts(image->info->deps[index]);
            }
            puts("\n");
            puts("info:    "); puts(image->info->description ? image->info->description : ""); puts("\n");
        }
        return;
    }
    puts("usage: mod [list | info <name> | load <name> | unload <name> | reload <name>]\n");
}
