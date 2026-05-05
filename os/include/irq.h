#pragma once

#include <stdint.h>
#include "isr.h"

typedef void (*irq_handler_t)(struct regs* r);

void irq_install(void);
void irq_register_handler(int irq, irq_handler_t handler);

