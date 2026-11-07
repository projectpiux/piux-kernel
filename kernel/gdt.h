#ifndef GDT_H
#define GDT_H

void gdt_init(void);
void gdt_set_esp0(uint32_t esp0);

#endif