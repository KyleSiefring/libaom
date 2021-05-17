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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aom/aom_integer.h"
#include "av1/decoder/accounting.h"

static int accounting_hash(const char *str) {
  uint32_t val;
  const unsigned char *ustr;
  val = 0;
  ustr = (const unsigned char *)str;
  /* This is about the worst hash one can design, but it should be good enough
     here. */
  while (*ustr) val += *ustr++;
  return val % AOM_ACCOUNTING_HASH_SIZE;
}

/* Dictionary lookup based on an open-addressing hash table. */
int aom_accounting_dictionary_lookup(Accounting *accounting, const char *str) {
  int hash;
  size_t len;
  AccountingDictionary *dictionary;
  dictionary = &accounting->syms.dictionary;
  hash = accounting_hash(str);
  while (accounting->hash_dictionary[hash] != -1) {
    if (strcmp(dictionary->strs[accounting->hash_dictionary[hash]], str) == 0) {
      return accounting->hash_dictionary[hash];
    }
    hash++;
    if (hash == AOM_ACCOUNTING_HASH_SIZE) hash = 0;
  }
  /* No match found. */
  assert(dictionary->num_strs + 1 < MAX_SYMBOL_TYPES);
  accounting->hash_dictionary[hash] = dictionary->num_strs;
  len = strlen(str);
  dictionary->strs[dictionary->num_strs] = malloc(len + 1);
  snprintf(dictionary->strs[dictionary->num_strs], len + 1, "%s", str);
  dictionary->num_strs++;
  return dictionary->num_strs - 1;
}

void aom_accounting_init(Accounting *accounting) {
  int i;
  accounting->num_syms_allocated = 1000;
  accounting->syms.syms =
      malloc(sizeof(AccountingSymbol) * accounting->num_syms_allocated);
  accounting->syms.dictionary.num_strs = 0;
  assert(AOM_ACCOUNTING_HASH_SIZE > 2 * MAX_SYMBOL_TYPES);
  for (i = 0; i < AOM_ACCOUNTING_HASH_SIZE; i++)
    accounting->hash_dictionary[i] = -1;


  accounting->event_file = fopen("../events.bin", "w");
  accounting->cdf_map = NULL;
  accounting->current_frame_context = -1;
  GLOBAL_ACCOUNTING = accounting;
  FRAME_CONTEXT_TIME = 0;


  aom_accounting_reset(accounting);
}

void aom_accounting_reset(Accounting *accounting) {
  accounting->syms.num_syms = 0;
  accounting->syms.num_binary_syms = 0;
  accounting->syms.num_multi_syms = 0;
  accounting->context.x = -1;
  accounting->context.y = -1;
  accounting->last_tell_frac = 0;
}

void aom_accounting_clear(Accounting *accounting) {
  int i;
  AccountingDictionary *dictionary;
  free(accounting->syms.syms);
  dictionary = &accounting->syms.dictionary;
  for (i = 0; i < dictionary->num_strs; i++) {
    free(dictionary->strs[i]);
  }

  fclose(accounting->event_file);
}

void aom_accounting_set_context(Accounting *accounting, int16_t x, int16_t y) {
  accounting->context.x = x;
  accounting->context.y = y;
}

void aom_accounting_record(Accounting *accounting, const char *str,
                           uint32_t bits) {
  AccountingSymbol sym;
  // Reuse previous symbol if it has the same context and symbol id.
  if (accounting->syms.num_syms) {
    AccountingSymbol *last_sym;
    last_sym = &accounting->syms.syms[accounting->syms.num_syms - 1];
    if (memcmp(&last_sym->context, &accounting->context,
               sizeof(AccountingSymbolContext)) == 0) {
      uint32_t id;
      id = aom_accounting_dictionary_lookup(accounting, str);
      if (id == last_sym->id) {
        last_sym->bits += bits;
        last_sym->samples++;
        return;
      }
    }
  }
  sym.context = accounting->context;
  sym.samples = 1;
  sym.bits = bits;
  sym.id = aom_accounting_dictionary_lookup(accounting, str);
  assert(sym.id <= 255);
  if (accounting->syms.num_syms == accounting->num_syms_allocated) {
    accounting->num_syms_allocated *= 2;
    accounting->syms.syms =
        realloc(accounting->syms.syms,
                sizeof(AccountingSymbol) * accounting->num_syms_allocated);
    assert(accounting->syms.syms != NULL);
  }
  accounting->syms.syms[accounting->syms.num_syms++] = sym;
}

void aom_accounting_dump(Accounting *accounting) {
  int i;
  AccountingSymbol *sym;
  printf("\n----- Number of recorded syntax elements = %d -----\n",
         accounting->syms.num_syms);
  printf("----- Total number of symbol calls = %d (%d binary) -----\n",
         accounting->syms.num_multi_syms + accounting->syms.num_binary_syms,
         accounting->syms.num_binary_syms);
  for (i = 0; i < accounting->syms.num_syms; i++) {
    sym = &accounting->syms.syms[i];
    printf("%s x: %d, y: %d bits: %f samples: %d\n",
           accounting->syms.dictionary.strs[sym->id], sym->context.x,
           sym->context.y, (float)sym->bits / 8.0, sym->samples);
  }
}

static void fput_u16(uint16_t x, FILE* file) {
  // little endian
  putc(x & 0xFF, file); // byte 0
  putc(x >> 8, file); // byte 1
}

static void fput_str(const char *str, FILE* file) {
  fputc(strlen(str), file);
  fputs(str, file);
}

void aom_accounting_log_symbol(Accounting *accounting, const char *str,
                               const char* src_name, int line,
                               const aom_cdf_prob *cdf,
                               int nsymbs, int symb, int update) {
  CDF_Record* record = NULL;
  int tmp = cdf[nsymbs + 1];
  HASH_FIND_INT(accounting->cdf_map, &tmp, record);

  int context_timestamp = cdf[nsymbs+2];
  if (accounting->current_frame_context != context_timestamp) {
    //fprintf(accounting->event_file, "%X%X\n", SET_CURRENT_CONTEXT, context_timestamp);
    fputc(SET_CURRENT_CONTEXT, accounting->event_file);
    fput_u16(context_timestamp, accounting->event_file);

    accounting->current_frame_context = context_timestamp;
  }

  if (record == NULL) {
    record = malloc(sizeof(CDF_Record));
    record->id = cdf[nsymbs + 1];
    //memcpy(record->cdf, cdf, sizeof(aom_cdf_prob) * CDF_SIZE(nsymbs));
    HASH_ADD_INT(accounting->cdf_map, id, record);

    /* Write cdf to file. INIT_CDF */
    //fprintf(accounting->event_file, "%X", INIT_CDF);
    //fprintf(accounting->event_file, "%X\n%s\n%X ", record->id, str, nsymbs);
    fputc(INIT_CDF, accounting->event_file);
    fput_u16(record->id, accounting->event_file);
    fputc(nsymbs, accounting->event_file);
    //fputc(strlen(str), accounting->event_file); // TODO: Add names for duplicates???
    //fputs(str, accounting->event_file);
    fput_str(str, accounting->event_file);
    fput_str(src_name, accounting->event_file);
    fput_u16(line, accounting->event_file); // This should fit...
    for (int i = 0; i < nsymbs; i++) {
      //fprintf(accounting->event_file, "%X,", cdf[i]);
      fput_u16(cdf[i], accounting->event_file);
    }
    //fprintf(accounting->event_file, "\n");
  }

  /* Write symbol to file. w/ or w/o updates */
  /* Format:
   * event_type: 1 byte (hex)
   * symbol: 1 byte (hex)
   * record id: hex number
   * terminator: new line
   */
  fputc((update ? SYMBOL_UPDATE : SYMBOL_NO_UPDATE), accounting->event_file);
  fputc(symb, accounting->event_file);
  fput_u16(record->id, accounting->event_file);
//  fprintf(
//      accounting->event_file,
//      "%X%X%X\n",
//      (update ? SYMBOL_UPDATE : SYMBOL_NO_UPDATE),
//      symb,
//      record->id);
}

void aom_accounting_log_context_move(Accounting *accounting,
                                     int parent_timestamp,
                                     int child_timestamp) {
  //fprintf(accounting->event_file, "%X%X %X\n", MOVE_CONTEXT, parent_timestamp, child_timestamp);
  fputc(MOVE_CONTEXT, accounting->event_file);
  fput_u16(parent_timestamp, accounting->event_file);
  fput_u16(child_timestamp, accounting->event_file);
}
