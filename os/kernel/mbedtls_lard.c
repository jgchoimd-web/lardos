#include "mem.h"
#include "rtc.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

extern const char lard_ca_bundle_pem[];
extern const size_t lard_ca_bundle_pem_len;

static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr_drbg;
static mbedtls_x509_crt g_ca;
static mbedtls_ssl_config g_conf;
static int g_tls_ready;

static void* lard_calloc(size_t n, size_t sz)
{
    if (n == 0 || sz == 0) return NULL;
    if (sz > (size_t)0xFFFFFFFFu / n) return NULL;
    uint32_t total = (uint32_t)(n * sz);
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

int mbedtls_hardware_poll(void* data, unsigned char* output, size_t len, size_t* olen)
{
    (void)data;
    if (!output || !olen) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    uint32_t i = 0;
    while (i < len) {
        unsigned lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        uint64_t t = ((uint64_t)hi << 32) | lo;
        output[i++] = (unsigned char)(t & 0xFFu);
        if (i < len) output[i++] = (unsigned char)((t >> 8) & 0xFFu);
        if (i < len) output[i++] = (unsigned char)((t >> 16) & 0xFFu);
        if (i < len) output[i++] = (unsigned char)((t >> 24) & 0xFFu);
    }
    *olen = len;
    return 0;
}

int lard_tls_ready(void)
{
    return g_tls_ready;
}

int lard_mbedtls_global_init(void)
{
    if (g_tls_ready) return 0;

    mbedtls_platform_set_calloc_free(lard_calloc, kfree);
    mbedtls_platform_set_time(rtc_mbedtls_time);

    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr_drbg);

    const unsigned char pers[] = "lardos tls v1";
    int r = mbedtls_ctr_drbg_seed(&g_ctr_drbg, mbedtls_entropy_func, &g_entropy, pers, sizeof(pers) - 1u);
    if (r != 0) goto fail_ctr;

    mbedtls_x509_crt_init(&g_ca);
    r = mbedtls_x509_crt_parse(&g_ca, (const unsigned char*)lard_ca_bundle_pem, lard_ca_bundle_pem_len + 1u);
    if (r < 0) goto fail_ca;

    mbedtls_ssl_config_init(&g_conf);
    r = mbedtls_ssl_config_defaults(&g_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0) goto fail_conf;

    mbedtls_ssl_conf_authmode(&g_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&g_conf, &g_ca, NULL);
    mbedtls_ssl_conf_rng(&g_conf, mbedtls_ctr_drbg_random, &g_ctr_drbg);
    mbedtls_ssl_conf_read_timeout(&g_conf, 0);

    mbedtls_ssl_conf_min_version(&g_conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&g_conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);

    g_tls_ready = 1;
    return 0;

fail_conf:
    mbedtls_ssl_config_free(&g_conf);
fail_ca:
    mbedtls_x509_crt_free(&g_ca);
fail_ctr:
    mbedtls_ctr_drbg_free(&g_ctr_drbg);
    mbedtls_entropy_free(&g_entropy);
    return -1;
}

mbedtls_ssl_config* lard_tls_ssl_config(void)
{
    return g_tls_ready ? &g_conf : NULL;
}
