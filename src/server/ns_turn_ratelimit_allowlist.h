/*
 * Copyright (C) 2024 Wire Swiss GmbH
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __TURN_RATELIMIT_ALLOWLIST__
#define __TURN_RATELIMIT_ALLOWLIST__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ns_turn_ioaddr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Source addresses exempt from --unauthorized-ratelimit.
 *
 * The file holds one literal IPv4 or IPv6 address per line; blank lines and
 * lines starting with '#' are skipped, as are malformed lines (logged). Ports
 * are never part of the match, and an IPv4-mapped IPv6 source matches its
 * IPv4 entry.
 */

/* Replace the allowlist with the contents of `path`. The new set is built
 * completely before it is swapped in; if the file cannot be opened, the
 * current set is kept and false is returned. */
bool turn_ratelimit_allowlist_load(const char *path);

/* True if `addr` (port ignored) is on the allowlist. Each hit is counted
 * as a rate-limit bypass. */
bool turn_ratelimit_allowlist_contains(const ioa_addr *addr);

/* Number of distinct addresses currently loaded. */
size_t turn_ratelimit_allowlist_size(void);

/* Monotonic count of allowlist hits since startup (wraps at 2^32). */
uint32_t turn_ratelimit_allowlist_get_bypasses(void);

/* Drop all entries and reset the bypass counter. */
void turn_ratelimit_allowlist_clear(void);

#ifdef __cplusplus
}
#endif

#endif //__TURN_RATELIMIT_ALLOWLIST__
