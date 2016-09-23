/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#ifndef AOM_DSP_BITREADER_H_
#define AOM_DSP_BITREADER_H_

#include <stddef.h>
#include <limits.h>

#include "./aom_config.h"

#if CONFIG_BITSTREAM_DEBUG
#include <assert.h>
#include <stdio.h>
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "aom/aomdx.h"
#include "aom/aom_integer.h"
#if CONFIG_ANS
#include "aom_dsp/ansreader.h"
#elif CONFIG_DAALA_EC
#include "aom_dsp/daalaboolreader.h"
#else
#include "aom_dsp/dkboolreader.h"
#endif
#include "aom_dsp/prob.h"
#include "av1/common/odintrin.h"

#if CONFIG_ACCOUNTING
#include "av1/common/accounting.h"
#define ACCT_STR_NAME acct_str
#define ACCT_STR_PARAM , const char *ACCT_STR_NAME
#define ACCT_STR_ARG(s) , s
#else
#define ACCT_STR_PARAM
#define ACCT_STR_ARG(s)
#endif

#define aom_read(r, prob, ACCT_STR_NAME) \
  aom_read_(r, prob ACCT_STR_ARG(ACCT_STR_NAME))
#define aom_read_bit(r, ACCT_STR_NAME) \
  aom_read_bit_(r ACCT_STR_ARG(ACCT_STR_NAME))
#define aom_read_tree(r, tree, probs, ACCT_STR_NAME) \
  aom_read_tree_(r, tree, probs ACCT_STR_ARG(ACCT_STR_NAME))
#define aom_read_literal(r, bits, ACCT_STR_NAME) \
  aom_read_literal_(r, bits ACCT_STR_ARG(ACCT_STR_NAME))
#define aom_read_tree_bits(r, tree, probs, ACCT_STR_NAME) \
  aom_read_tree_bits_(r, tree, probs ACCT_STR_ARG(ACCT_STR_NAME))
#define aom_read_symbol(r, cdf, nsymbs, ACCT_STR_NAME) \
  aom_read_symbol_(r, cdf, nsymbs ACCT_STR_ARG(ACCT_STR_NAME))

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ANS
typedef struct AnsDecoder aom_reader;
#elif CONFIG_DAALA_EC
typedef struct daala_reader aom_reader;
#else
typedef struct aom_dk_reader aom_reader;
#endif

static INLINE int aom_reader_init(aom_reader *r, const uint8_t *buffer,
                                  size_t size, aom_decrypt_cb decrypt_cb,
                                  void *decrypt_state) {
#if CONFIG_ANS
  (void)decrypt_cb;
  (void)decrypt_state;
  assert(size <= INT_MAX);
  return ans_read_init(r, buffer, size);
#elif CONFIG_DAALA_EC
  (void)decrypt_cb;
  (void)decrypt_state;
  return aom_daala_reader_init(r, buffer, size);
#else
  return aom_dk_reader_init(r, buffer, size, decrypt_cb, decrypt_state);
#endif
}

static INLINE const uint8_t *aom_reader_find_end(aom_reader *r) {
#if CONFIG_ANS
  (void)r;
  assert(0 && "Use the raw buffer size with ANS");
  return NULL;
#elif CONFIG_DAALA_EC
  return aom_daala_reader_find_end(r);
#else
  return aom_dk_reader_find_end(r);
#endif
}

static INLINE int aom_reader_has_error(aom_reader *r) {
#if CONFIG_ANS
  return ans_reader_has_error(r);
#elif CONFIG_DAALA_EC
  return aom_daala_reader_has_error(r);
#else
  return aom_dk_reader_has_error(r);
#endif
}

// Returns the position in the bit reader in bits.
static INLINE uint32_t aom_reader_tell(const aom_reader *r) {
#if CONFIG_ANS
  (void)r;
  assert(0 && "aom_reader_tell() is unimplemented for ANS");
  return 0;
#elif CONFIG_DAALA_EC
  return aom_daala_reader_tell(r);
#else
  return aom_dk_reader_tell(r);
#endif
}

// Returns the position in the bit reader in 1/8th bits.
static INLINE uint32_t aom_reader_tell_frac(const aom_reader *r) {
#if CONFIG_ANS
  (void)r;
  assert(0 && "aom_reader_tell_frac() is unimplemented for ANS");
  return 0;
#elif CONFIG_DAALA_EC
  return aom_daala_reader_tell_frac(r);
#else
  return aom_dk_reader_tell_frac(r);
#endif
}

#if CONFIG_ACCOUNTING
static INLINE void aom_process_accounting(const aom_reader *r ACCT_STR_PARAM) {
  if (r->accounting != NULL) {
    uint32_t tell_frac;
    tell_frac = aom_reader_tell_frac(r);
    aom_accounting_record(r->accounting, ACCT_STR_NAME,
                          tell_frac - r->accounting->last_tell_frac);
    r->accounting->last_tell_frac = tell_frac;
  }
}
#endif

static INLINE int aom_read_(aom_reader *r, int prob ACCT_STR_PARAM) {
  int ret;
#if CONFIG_ANS
  ret = uabs_read(r, prob);
#elif CONFIG_DAALA_EC
  ret = aom_daala_read(r, prob);
#else
  ret = aom_dk_read(r, prob);
#endif
#if CONFIG_ACCOUNTING
  if (ACCT_STR_NAME) aom_process_accounting(r, ACCT_STR_NAME);
#endif

#if CONFIG_BITSTREAM_DEBUG
  {
    int ref_bit, ref_prob;
    const int queue_r = bitstream_queue_get_read();
    const int frame_idx = bitstream_queue_get_frame_read();
    bitstream_queue_pop(&ref_bit, &ref_prob);
    if (prob != ref_prob) {
      fprintf(
          stderr,
          "\n *** prob error, frame_idx_r %d prob %d ref_prob %d queue_r %d\n",
          frame_idx, prob, ref_prob, queue_r);
      assert(0);
    }
    if (ret != ref_bit) {
      fprintf(stderr, "\n *** bit error, frame_idx_r %d bit %d ref_bit %d\n",
              frame_idx, ret, ref_bit);
      assert(0);
    }
  }
#endif  // CONFIG_BITSTREAM_DEBUG

  return ret;
}

static INLINE int aom_read_bit_(aom_reader *r ACCT_STR_PARAM) {
  int ret;
#if CONFIG_ANS
  ret = uabs_read_bit(r);  // Non trivial optimization at half probability
#else
  ret = aom_read(r, 128, NULL);  // aom_prob_half
#endif
#if CONFIG_ACCOUNTING
  if (ACCT_STR_NAME) aom_process_accounting(r, ACCT_STR_NAME);
#endif
  return ret;
}

static INLINE int aom_read_literal_(aom_reader *r, int bits ACCT_STR_PARAM) {
  int literal = 0, bit;

  for (bit = bits - 1; bit >= 0; bit--) literal |= aom_read_bit(r, NULL) << bit;
#if CONFIG_ACCOUNTING
  if (ACCT_STR_NAME) aom_process_accounting(r, ACCT_STR_NAME);
#endif
  return literal;
}

static INLINE int aom_read_tree_bits_(aom_reader *r, const aom_tree_index *tree,
                                      const aom_prob *probs ACCT_STR_PARAM) {
  aom_tree_index i = 0;

  while ((i = tree[i + aom_read(r, probs[i >> 1], NULL)]) > 0) continue;
#if CONFIG_ACCOUNTING
  if (ACCT_STR_NAME) aom_process_accounting(r, ACCT_STR_NAME);
#endif
  return -i;
}

static INLINE int aom_read_tree_(aom_reader *r, const aom_tree_index *tree,
                                 const aom_prob *probs ACCT_STR_PARAM) {
  int ret;
#if CONFIG_DAALA_EC
  ret = daala_read_tree_bits(r, tree, probs);
#else
  ret = aom_read_tree_bits(r, tree, probs, NULL);
#endif
#if CONFIG_ACCOUNTING
  if (ACCT_STR_NAME) aom_process_accounting(r, ACCT_STR_NAME);
#endif
  return ret;
}

static INLINE int aom_read_symbol_(aom_reader *r, aom_cdf_prob *cdf,
                                   int nsymbs ACCT_STR_PARAM) {
  int ret;
#if CONFIG_RANS
  (void)nsymbs;
  ret = rans_read(r, cdf);
#elif CONFIG_DAALA_EC
  ret = daala_read_symbol(r, cdf, nsymbs);
#else
  (void)r;
  (void)cdf;
  (void)nsymbs;
  assert(0 && "Unsupported bitreader operation");
  ret = -1;
#endif
#if ((CONFIG_RANS || CONFIG_DAALA_EC) && CONFIG_EC_ADAPT)
  update_cdf(cdf, ret, nsymbs);
#endif

#if CONFIG_ACCOUNTING
  if (ACCT_STR_NAME) aom_process_accounting(r, ACCT_STR_NAME);
#endif
  return ret;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_DSP_BITREADER_H_
