/*
 * Copyright (C) 2022 Wire Swiss GmbH
 */

#ifndef TURN_ZREST_H
#define TURN_ZREST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool sft;
  uint32_t deadline;
  uint32_t keyindex;
} turn_zrest_username;

/*
 * Parse a zrest username of the form
 *   [sft-]d=<deadline>.v=1.k=<keyindex>.t=s.r=<[-_a-zA-Z0-9]*>
 * Returns false if the username does not match or a number exceeds 32 bits.
 */
bool turn_zrest_parse_username(const char *usname, turn_zrest_username *out);

#ifdef __cplusplus
}
#endif

#endif /* TURN_ZREST_H */
