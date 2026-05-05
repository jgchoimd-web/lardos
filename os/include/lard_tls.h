#pragma once

#include <stdint.h>

#define LARD_TLS_OK 0
#define LARD_TLS_ERR_BAD_ARG       -300
#define LARD_TLS_ERR_OVERFLOW      -301
#define LARD_TLS_ERR_IO            -302
#define LARD_TLS_ERR_BAD_RECORD    -303
#define LARD_TLS_ERR_ALERT         -304
#define LARD_TLS_ERR_UNEXPECTED    -305
#define LARD_TLS_ERR_UNSUPPORTED_CIPHER -306
#define LARD_TLS_ERR_UNSUPPORTED_CERT   -307
#define LARD_TLS_ERR_CERT_VERIFY        -308
#define LARD_TLS_ERR_DECRYPT            -309
#define LARD_TLS_ERR_BAD_FINISHED       -310

#define LARD_TLS_RSA_MAX_BYTES 512
typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    uint32_t used;
} lard_tls_sha256_state_t;

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
    int cert_verified;
    int write_encrypted;
    int read_encrypted;
    uint8_t pre_master[48];
    uint8_t master_secret[48];
    uint8_t client_write_mac[32];
    uint8_t server_write_mac[32];
    uint8_t client_write_key[16];
    uint8_t server_write_key[16];
    uint8_t rsa_modulus[LARD_TLS_RSA_MAX_BYTES];
    uint16_t rsa_modulus_len;
    uint8_t rsa_exponent[8];
    uint8_t rsa_exponent_len;
    uint64_t client_seq;
    uint64_t server_seq;
    lard_tls_sha256_state_t transcript_hash;
    uint32_t pending_record_pos;
    uint32_t pending_record_len;
    uint32_t rx_plain_pos;
    uint32_t rx_plain_len;
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
