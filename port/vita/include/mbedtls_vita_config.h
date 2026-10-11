/*
MBEDTLS_VITA_CONFIG.H

The Vita's changes to Mbed TLS's default configuration
(port/third_party/mbedtls/include/mbedtls/mbedtls_config.h, as the Linux
build uses it), given as MBEDTLS_USER_CONFIG_FILE by tools/vita_build.py,
for the update check's one HTTPS request (port/linux/src/posix_https.c):

- randomness from the system's generator (sceKernelGetRandomNumber, through
  posix_random_bytes): PSA's from mbedtls_psa_external_get_random
  (posix_https.c), no platform entropy source (there is no /dev/urandom);
- no BSD sockets or timers of Mbed TLS's own (net_sockets.c, timing.c;
  mbedtls_ms_time is posix_https.c's):
  posix_https.c sends and receives through vita_net.c.

Certificates are checked as on Linux, against the authorities built into
posix_https.c (port/linux/src/update_roots.h), with the Vita's clock for
their dates.
*/

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
/* (newlib's clock has no milliseconds Mbed TLS knows of: posix_https.c's
mbedtls_ms_time, from time(), for TLS 1.3's ticket ages) */
#define MBEDTLS_PLATFORM_MS_TIME_ALT
