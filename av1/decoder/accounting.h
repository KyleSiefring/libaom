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
#ifndef AOM_AV1_DECODER_ACCOUNTING_H_
#define AOM_AV1_DECODER_ACCOUNTING_H_
#include <stdlib.h>
#include "aom_dsp/entdec.h"
#include "aom/aomdx.h"
#include "av1/decoder/map/uthash.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#define AOM_ACCOUNTING_HASH_SIZE (1021)

/* Max number of entries for symbol types in the dictionary (increase as
   necessary). */
#define MAX_SYMBOL_TYPES (256)

/*The resolution of fractional-precision bit usage measurements, i.e.,
   3 => 1/8th bits.*/
#define AOM_ACCT_BITRES (3)



/* Data:
 *  name
 *  cdf id
 *  # symbols
 *  initial state
 */
#define INIT_CDF (0)

/* Data:
 *  cdf id
 *  symbol value
 */
#define SYMBOL_UPDATE (1)

/* Data:
 *  cdf id
 *  symbol value
 */
#define SYMBOL_NO_UPDATE (2)

/* Data:
 *  parent_context
 *  child_context (could be elided)
 */
#define MOVE_CONTEXT (3)

/* Data:
 *  parent_context
 *  child_context (could be elided)
 */
#define SET_CURRENT_CONTEXT (4)

/* Data:
 *  context_timestamp
 */
//#define RESET_CONTEXT_COUNTERS (5)



typedef struct {
  int16_t x;
  int16_t y;
} AccountingSymbolContext;

typedef struct {
  AccountingSymbolContext context;
  uint32_t id;
  /** Number of bits in units of 1/8 bit. */
  uint32_t bits;
  uint32_t samples;
} AccountingSymbol;

/** Dictionary for translating strings into id. */
typedef struct {
  char *strs[MAX_SYMBOL_TYPES];
  //map_void_t cdfs;
  //char cdfs[MAX_SYMBOL_TYPES][CDF_SIZE(16)];
  int num_strs;
  //int cdf_id;
} AccountingDictionary;

typedef struct {
  /** All recorded symbols decoded. */
  AccountingSymbol *syms;
  /** Number of syntax actually recorded. */
  int num_syms;
  /** Raw symbol decoding calls for non-binary values. */
  int num_multi_syms;
  /** Raw binary symbol decoding calls. */
  int num_binary_syms;
  /** Dictionary for translating strings into id. */
  AccountingDictionary dictionary;
} AccountingSymbols;

typedef struct {
  int id;
  UT_hash_handle hh;
} CDF_Record;

struct Accounting {
  AccountingSymbols syms;
  /** Size allocated for symbols (not all may be used). */
  int num_syms_allocated;
  int16_t hash_dictionary[AOM_ACCOUNTING_HASH_SIZE];
  AccountingSymbolContext context;
  uint32_t last_tell_frac;

  FILE *event_file;
  int current_frame_context;
  CDF_Record *cdf_map;
};

void aom_accounting_init(Accounting *accounting);
void aom_accounting_reset(Accounting *accounting);
void aom_accounting_clear(Accounting *accounting);
void aom_accounting_set_context(Accounting *accounting, int16_t x, int16_t y);
int aom_accounting_dictionary_lookup(Accounting *accounting, const char *str);
void aom_accounting_record(Accounting *accounting, const char *str,
                           uint32_t bits);
void aom_accounting_dump(Accounting *accounting);

Accounting *GLOBAL_ACCOUNTING;
int FRAME_CONTEXT_TIME;

void aom_accounting_log_symbol(Accounting *accounting, const char *str,
                               const char *srcname, int line,
                               const aom_cdf_prob *cdf,
                               int nsymbs, int symb, int update);
void aom_accounting_log_context_move(Accounting *accounting,
                                     int parent_timestamp,
                                     int child_timestamp);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus
#endif  // AOM_AV1_DECODER_ACCOUNTING_H_
