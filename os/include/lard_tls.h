#pragma once

#include <stdint.h>

#define LARD_TLS_OK 0
#define LARD_TLS_ERR_BAD_ARG       -300
#define LARD_TLS_ERR_OVERFLOW      -301
#define LARD_TLS_ERR_IO            -302
#define LARD_TLS_ERR_BAD_RECORD    -303
#define LARD_TLS_ERR_ALERT         -304
#define LARD_TLS_ERR_UNEXPECTED    -305
#define LARD_TLS_ERR_CRYPTO_TODO   -306

typedef int (*lard_tls_send_fn)(void* io, const uint8_t* data, uint32_t len);
typedef int (*lard_tls_recv_fn)(void* io, uint8_t* data, uint32_t cap, uint32_t* out_len);

typedef struct {
    void* io;
    lard_tls_send_fn send;
    lard_tls_recv_fn recv;
    char server_name[128];
    uint8_t client_random[32];
    uint8_t server_random[32];
    uint16_t protocol_version;
    uint16_t cipher_suite;
    int got_server_hello;
    int handshake_done;
} lard_tls_client_t;

int lard_tls_client_init(lard_tls_client_t* c,
                         const char* server_name,
                         void* io,
                         lard_tls_send_fn send,
                         lard_tls_recv_fn recv);
int lard_tls_client_handshake(lard_tls_client_t* c);
int lard_tls_write(lard_tls_client_t* c, const uint8_t* data, uint32_t len);
int lard_tls_read(lard_tls_client_t* c, uint8_t* data, uint32_t cap, uint32_t* out_len);
const char* lard_tls_status_text(int code);
