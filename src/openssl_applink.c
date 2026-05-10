/*
 * openssl_applink.c
 *
 * Compiled into agent.exe exactly once when OpenSSL is enabled.
 *
 * On Windows, OpenSSL uses a jump-table ("uplink") to call back into the
 * host application's C runtime (fopen, fread, etc.).  This file registers
 * that table.  Without it, the first OpenSSL I/O call crashes with:
 *
 *   OpenSSL: FATAL
 *   OPENSSL_Uplink(...): no OPENSSL_Applink
 *
 * The guard means this compiles to an empty translation unit when OpenSSL
 * is not present, so it can live in SOURCES unconditionally.
 */
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/applink.c>
#endif
