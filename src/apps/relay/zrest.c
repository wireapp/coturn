/*
 * Copyright (C) 2022 Wire Swiss GmbH
 */

#include "zrest.h"

#include <string.h>

static bool skip_literal(const char **p, const char *lit) {
  const size_t n = strlen(lit);
  if (strncmp(*p, lit, n) != 0) {
    return false;
  }
  *p += n;
  return true;
}

static bool parse_u32(const char **p, uint32_t *out) {
  const char *s = *p;
  uint64_t v = 0;
  if (*s < '0' || *s > '9') {
    return false;
  }
  for (; *s >= '0' && *s <= '9'; ++s) {
    v = v * 10 + (uint64_t)(*s - '0');
    if (v > UINT32_MAX) {
      return false;
    }
  }
  *out = (uint32_t)v;
  *p = s;
  return true;
}

static bool is_random_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

bool turn_zrest_parse_username(const char *usname, turn_zrest_username *out) {
  turn_zrest_username u = {0};
  const char *p = usname;

  if (!usname || !out) {
    return false;
  }

  u.sft = skip_literal(&p, "sft-");
  if (!skip_literal(&p, "d=") || !parse_u32(&p, &u.deadline) || !skip_literal(&p, ".v=1.k=") ||
      !parse_u32(&p, &u.keyindex) || !skip_literal(&p, ".t=s.r=")) {
    return false;
  }
  while (is_random_char(*p)) {
    ++p;
  }
  if (*p != '\0') {
    return false;
  }

  *out = u;
  return true;
}
