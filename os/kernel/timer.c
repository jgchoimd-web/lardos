#include "timer.h"
#include "io.h"
#include "irq.h"

static volatile uint32_t g_ticks = 0;

static void timer_handler(struct regs* r)
{
    (void)r;
    g_ticks++;
}

uint32_t timer_ticks(void)
{
    return g_ticks;
}

void timer_init(uint32_t frequency)
{
    uint32_t divisor = 1193180 / frequency;

    irq_register_handler(0, timer_handler);

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

