/*
MBEDTLS_LINUX_CONFIG.H

The Linux build's changes to Mbed TLS's default configuration
(port/third_party/mbedtls/include/mbedtls/mbedtls_config.h), given as
MBEDTLS_USER_CONFIG_FILE by tools/linux_build.py to Mbed TLS and to the
posix_*.c that use it (posix_https.c):

- its threading (pthread mutexes): the update check's request and internet
  play's brokers over TLS (posix_tls_*, whose handshakes run on a thread of
  their own) can run at once, and PSA's keys and globals, which TLS 1.3
  uses, are shared by every thread.

(The Vita's: port/vita/include/mbedtls_vita_config.h, which has the same.)
*/

#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
