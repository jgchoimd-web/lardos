#pragma once

#include <stdint.h>

/*
 * LIPC — LardOS inter-process communication (kernel message ports).
 *
 * Ports: 0 .. LIPC_NUM_PORTS-1. Each port is a FIFO of fixed-capacity messages.
 * Message: opaque byte blob, max LIPC_MAX_PAYLOAD bytes per message.
 * (No application-level framing; callers may prefix their own headers.)
 *
 * User access: SYS_LIPC_SEND / SYS_LIPC_RECV / SYS_LIPC_PENDING (syscall.h).
 * BOSL (kernel VM): lipc_send / lipc_recv ops (see lang/README.md).
 */

#define LIPC_NUM_PORTS    16
#define LIPC_QUEUE_DEPTH  8
#define LIPC_MAX_PAYLOAD  240

void lipc_init(void);

/* Queue one message. Returns len on success, -1 on invalid args or full queue. */
int lipc_send(uint32_t port_id, const void* data, uint32_t len);

/*
 * Dequeue one message into buf (max cap bytes).
 * Returns: message length (>0), 0 if no message, -1 on bad port/cap/truncation.
 */
int lipc_recv(uint32_t port_id, void* buf, uint32_t cap);

/* Number of messages waiting on port (0 if invalid port). */
uint32_t lipc_pending(uint32_t port_id);
