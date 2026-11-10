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

#include "ns_turn_ratelimit_allowlist.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ns_turn_atomic.h"
#include "ns_turn_utils.h" // for turn_realloc, TURN_LOG_FUNC

#define ALLOWLIST_KEY_LEN 16

/* IPv6 address bytes; IPv4 entries are stored IPv4-mapped (::ffff:a.b.c.d). */
typedef struct {
  uint8_t b[ALLOWLIST_KEY_LEN];
} allowlist_key;

static pthread_rwlock_t allowlist_lock = PTHREAD_RWLOCK_INITIALIZER;
static allowlist_key *allowlist_keys = NULL; /* sorted, unique; guarded by allowlist_lock */
static size_t allowlist_count = 0;           /* guarded by allowlist_lock */

/* Lets the common (empty) case skip the lock. */
static turn_atomic_u32 allowlist_nonempty;
static turn_atomic_u32 allowlist_bypasses;

static const uint8_t v4_mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

static void key_from_v4(allowlist_key *k, const struct in_addr *a) {
  memcpy(k->b, v4_mapped_prefix, sizeof(v4_mapped_prefix));
  memcpy(k->b + sizeof(v4_mapped_prefix), a, 4);
}

static bool key_from_addr(allowlist_key *k, const ioa_addr *addr) {
  if (addr->ss.sa_family == AF_INET) {
    key_from_v4(k, &addr->s4.sin_addr);
    return true;
  }
  if (addr->ss.sa_family == AF_INET6) {
    memcpy(k->b, &addr->s6.sin6_addr, ALLOWLIST_KEY_LEN);
    return true;
  }
  return false;
}

static bool key_from_string(allowlist_key *k, const char *s) {
  struct in_addr a4;
  if (inet_pton(AF_INET, s, &a4) == 1) {
    key_from_v4(k, &a4);
    return true;
  }
  return inet_pton(AF_INET6, s, k->b) == 1;
}

static int key_cmp(const void *a, const void *b) { return memcmp(a, b, ALLOWLIST_KEY_LEN); }

/* Strips surrounding whitespace in place; returns NULL for blank and '#' lines. */
static char *trim_line(char *line) {
  while (*line == ' ' || *line == '\t') {
    line++;
  }
  size_t n = strlen(line);
  while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
    line[--n] = '\0';
  }
  if (n == 0 || line[0] == '#') {
    return NULL;
  }
  return line;
}

static void swap_in(allowlist_key *keys, size_t count) {
  pthread_rwlock_wrlock(&allowlist_lock);
  allowlist_key *old = allowlist_keys;
  allowlist_keys = keys;
  allowlist_count = count;
  turn_atomic_store_u32(&allowlist_nonempty, count > 0 ? 1u : 0u);
  pthread_rwlock_unlock(&allowlist_lock);
  free(old);
}

bool turn_ratelimit_allowlist_load(const char *path) {
  if (!path || !path[0]) {
    return false;
  }
  FILE *f = fopen(path, "r");
  if (!f) {
    TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "Cannot open rate-limit allowlist %s; keeping %zu loaded entries\n", path,
                  turn_ratelimit_allowlist_size());
    return false;
  }

  allowlist_key *keys = NULL;
  size_t count = 0;
  size_t cap = 0;
  char buf[1024] = {0};
  unsigned long lineno = 0;
  while (fgets(buf, sizeof(buf), f)) {
    lineno++;
    const char *s = trim_line(buf);
    if (!s) {
      continue;
    }
    allowlist_key k = {0};
    if (!key_from_string(&k, s)) {
      TURN_LOG_FUNC(TURN_LOG_LEVEL_ERROR, "Rate-limit allowlist %s:%lu: not an IP address: %s\n", path, lineno, s);
      continue;
    }
    if (count == cap) {
      cap = cap ? cap * 2 : 16;
      keys = (allowlist_key *)turn_realloc(keys, cap * sizeof(allowlist_key));
    }
    keys[count++] = k;
  }
  fclose(f);

  if (count > 0) {
    qsort(keys, count, sizeof(allowlist_key), key_cmp);
    size_t unique = 1;
    for (size_t i = 1; i < count; i++) {
      if (key_cmp(&keys[i], &keys[unique - 1]) != 0) {
        keys[unique++] = keys[i];
      }
    }
    count = unique;
  }

  swap_in(keys, count);
  TURN_LOG_FUNC(TURN_LOG_LEVEL_INFO, "Loaded %zu address(es) into rate-limit allowlist from %s\n", count, path);
  return true;
}

bool turn_ratelimit_allowlist_contains(const ioa_addr *addr) {
  if (!addr || !turn_atomic_load_u32(&allowlist_nonempty)) {
    return false;
  }
  allowlist_key k = {0};
  if (!key_from_addr(&k, addr)) {
    return false;
  }
  pthread_rwlock_rdlock(&allowlist_lock);
  const bool found = allowlist_keys && bsearch(&k, allowlist_keys, allowlist_count, sizeof(allowlist_key), key_cmp);
  pthread_rwlock_unlock(&allowlist_lock);
  if (found) {
    turn_atomic_fetch_add_u32(&allowlist_bypasses, 1u);
  }
  return found;
}

size_t turn_ratelimit_allowlist_size(void) {
  pthread_rwlock_rdlock(&allowlist_lock);
  const size_t n = allowlist_count;
  pthread_rwlock_unlock(&allowlist_lock);
  return n;
}

uint32_t turn_ratelimit_allowlist_get_bypasses(void) { return turn_atomic_load_u32(&allowlist_bypasses); }

void turn_ratelimit_allowlist_clear(void) {
  swap_in(NULL, 0);
  turn_atomic_store_u32(&allowlist_bypasses, 0u);
}
