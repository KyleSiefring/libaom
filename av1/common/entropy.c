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

#include "config/aom_config.h"

#include "aom/aom_integer.h"
#include "aom_mem/aom_mem.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/scan.h"
#include "av1/common/token_cdfs.h"
#include "av1/common/txb_common.h"



#include "av1/decoder/accounting.h"



static int get_q_ctx(int q) {
  if (q <= 20) return 0;
  if (q <= 60) return 1;
  if (q <= 120) return 2;
  return 3;
}

void av1_default_coef_probs(AV1_COMMON *cm) {
  const int index = get_q_ctx(cm->quant_params.base_qindex);
#if CONFIG_ENTROPY_STATS
  cm->coef_cdf_category = index;
#endif

  av1_copy(cm->fc->txb_skip_cdf, av1_default_txb_skip_cdfs[index]);
  av1_copy(cm->fc->eob_extra_cdf, av1_default_eob_extra_cdfs[index]);
  av1_copy(cm->fc->dc_sign_cdf, av1_default_dc_sign_cdfs[index]);
  av1_copy(cm->fc->coeff_br_cdf, av1_default_coeff_lps_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_base_cdf, av1_default_coeff_base_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_base_eob_cdf,
           av1_default_coeff_base_eob_multi_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf16, av1_default_eob_multi16_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf32, av1_default_eob_multi32_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf64, av1_default_eob_multi64_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf128, av1_default_eob_multi128_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf256, av1_default_eob_multi256_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf512, av1_default_eob_multi512_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf1024, av1_default_eob_multi1024_cdfs[index]);
}

static AOM_INLINE void reset_cdf_symbol_counter(aom_cdf_prob *cdf_ptr,
                                                int num_cdfs, int cdf_stride,
                                                int nsymbs) {
  for (int i = 0; i < num_cdfs; i++) {
    cdf_ptr[i * cdf_stride + nsymbs] = 0;
  }
}

#define RESET_CDF_COUNTER(cname, nsymbs) \
  RESET_CDF_COUNTER_STRIDE(cname, nsymbs, CDF_SIZE(nsymbs))

#define RESET_CDF_COUNTER_STRIDE(cname, nsymbs, cdf_stride)          \
  do {                                                               \
    aom_cdf_prob *cdf_ptr = (aom_cdf_prob *)cname;                   \
    int array_size = (int)sizeof(cname) / sizeof(aom_cdf_prob);      \
    int num_cdfs = array_size / cdf_stride;                          \
    reset_cdf_symbol_counter(cdf_ptr, num_cdfs, cdf_stride, nsymbs); \
  } while (0)

static AOM_INLINE void reset_nmv_counter(nmv_context *nmv) {
  RESET_CDF_COUNTER(nmv->joints_cdf, 4);
  for (int i = 0; i < 2; i++) {
    RESET_CDF_COUNTER(nmv->comps[i].classes_cdf, MV_CLASSES);
    RESET_CDF_COUNTER(nmv->comps[i].class0_fp_cdf, MV_FP_SIZE);
    RESET_CDF_COUNTER(nmv->comps[i].fp_cdf, MV_FP_SIZE);
    RESET_CDF_COUNTER(nmv->comps[i].sign_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].class0_hp_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].hp_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].class0_cdf, CLASS0_SIZE);
    RESET_CDF_COUNTER(nmv->comps[i].bits_cdf, 2);
  }
}

void av1_reset_cdf_symbol_counters(FRAME_CONTEXT *fc) {
  RESET_CDF_COUNTER(fc->txb_skip_cdf, 2);
  RESET_CDF_COUNTER(fc->eob_extra_cdf, 2);
  RESET_CDF_COUNTER(fc->dc_sign_cdf, 2);
  RESET_CDF_COUNTER(fc->eob_flag_cdf16, 5);
  RESET_CDF_COUNTER(fc->eob_flag_cdf32, 6);
  RESET_CDF_COUNTER(fc->eob_flag_cdf64, 7);
  RESET_CDF_COUNTER(fc->eob_flag_cdf128, 8);
  RESET_CDF_COUNTER(fc->eob_flag_cdf256, 9);
  RESET_CDF_COUNTER(fc->eob_flag_cdf512, 10);
  RESET_CDF_COUNTER(fc->eob_flag_cdf1024, 11);
  RESET_CDF_COUNTER(fc->coeff_base_eob_cdf, 3);
  RESET_CDF_COUNTER(fc->coeff_base_cdf, 4);
  RESET_CDF_COUNTER(fc->coeff_br_cdf, BR_CDF_SIZE);
  RESET_CDF_COUNTER(fc->newmv_cdf, 2);
  RESET_CDF_COUNTER(fc->zeromv_cdf, 2);
  RESET_CDF_COUNTER(fc->refmv_cdf, 2);
  RESET_CDF_COUNTER(fc->drl_cdf, 2);
  RESET_CDF_COUNTER(fc->inter_compound_mode_cdf, INTER_COMPOUND_MODES);
  RESET_CDF_COUNTER(fc->compound_type_cdf, MASKED_COMPOUND_TYPES);
  RESET_CDF_COUNTER(fc->wedge_idx_cdf, 16);
  RESET_CDF_COUNTER(fc->interintra_cdf, 2);
  RESET_CDF_COUNTER(fc->wedge_interintra_cdf, 2);
  RESET_CDF_COUNTER(fc->interintra_mode_cdf, INTERINTRA_MODES);
  RESET_CDF_COUNTER(fc->motion_mode_cdf, MOTION_MODES);
  RESET_CDF_COUNTER(fc->obmc_cdf, 2);
  RESET_CDF_COUNTER(fc->palette_y_size_cdf, PALETTE_SIZES);
  RESET_CDF_COUNTER(fc->palette_uv_size_cdf, PALETTE_SIZES);
  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    RESET_CDF_COUNTER_STRIDE(fc->palette_y_color_index_cdf[j], nsymbs,
                             CDF_SIZE(PALETTE_COLORS));
    RESET_CDF_COUNTER_STRIDE(fc->palette_uv_color_index_cdf[j], nsymbs,
                             CDF_SIZE(PALETTE_COLORS));
  }
  RESET_CDF_COUNTER(fc->palette_y_mode_cdf, 2);
  RESET_CDF_COUNTER(fc->palette_uv_mode_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_inter_cdf, 2);
  RESET_CDF_COUNTER(fc->single_ref_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_ref_type_cdf, 2);
  RESET_CDF_COUNTER(fc->uni_comp_ref_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_ref_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_bwdref_cdf, 2);
  RESET_CDF_COUNTER(fc->txfm_partition_cdf, 2);
  RESET_CDF_COUNTER(fc->compound_index_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_group_idx_cdf, 2);
  RESET_CDF_COUNTER(fc->skip_mode_cdfs, 2);
  RESET_CDF_COUNTER(fc->skip_txfm_cdfs, 2);
  RESET_CDF_COUNTER(fc->intra_inter_cdf, 2);
  reset_nmv_counter(&fc->nmvc);
  reset_nmv_counter(&fc->ndvc);
  RESET_CDF_COUNTER(fc->intrabc_cdf, 2);
  RESET_CDF_COUNTER(fc->seg.tree_cdf, MAX_SEGMENTS);
  RESET_CDF_COUNTER(fc->seg.pred_cdf, 2);
  RESET_CDF_COUNTER(fc->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
  RESET_CDF_COUNTER(fc->filter_intra_cdfs, 2);
  RESET_CDF_COUNTER(fc->filter_intra_mode_cdf, FILTER_INTRA_MODES);
  RESET_CDF_COUNTER(fc->switchable_restore_cdf, RESTORE_SWITCHABLE_TYPES);
  RESET_CDF_COUNTER(fc->wiener_restore_cdf, 2);
  RESET_CDF_COUNTER(fc->sgrproj_restore_cdf, 2);
  RESET_CDF_COUNTER(fc->y_mode_cdf, INTRA_MODES);
  RESET_CDF_COUNTER_STRIDE(fc->uv_mode_cdf[0], UV_INTRA_MODES - 1,
                           CDF_SIZE(UV_INTRA_MODES));
  RESET_CDF_COUNTER(fc->uv_mode_cdf[1], UV_INTRA_MODES);
  for (int i = 0; i < PARTITION_CONTEXTS; i++) {
    if (i < 4) {
      RESET_CDF_COUNTER_STRIDE(fc->partition_cdf[i], 4, CDF_SIZE(10));
    } else if (i < 16) {
      RESET_CDF_COUNTER(fc->partition_cdf[i], 10);
    } else {
      RESET_CDF_COUNTER_STRIDE(fc->partition_cdf[i], 8, CDF_SIZE(10));
    }
  }
  RESET_CDF_COUNTER(fc->switchable_interp_cdf, SWITCHABLE_FILTERS);
  RESET_CDF_COUNTER(fc->kf_y_cdf, INTRA_MODES);
  RESET_CDF_COUNTER(fc->angle_delta_cdf, 2 * MAX_ANGLE_DELTA + 1);
  RESET_CDF_COUNTER_STRIDE(fc->tx_size_cdf[0], MAX_TX_DEPTH,
                           CDF_SIZE(MAX_TX_DEPTH + 1));
  RESET_CDF_COUNTER(fc->tx_size_cdf[1], MAX_TX_DEPTH + 1);
  RESET_CDF_COUNTER(fc->tx_size_cdf[2], MAX_TX_DEPTH + 1);
  RESET_CDF_COUNTER(fc->tx_size_cdf[3], MAX_TX_DEPTH + 1);
  RESET_CDF_COUNTER(fc->delta_q_cdf, DELTA_Q_PROBS + 1);
  RESET_CDF_COUNTER(fc->delta_lf_cdf, DELTA_LF_PROBS + 1);
  for (int i = 0; i < FRAME_LF_COUNT; i++) {
    RESET_CDF_COUNTER(fc->delta_lf_multi_cdf[i], DELTA_LF_PROBS + 1);
  }
  RESET_CDF_COUNTER_STRIDE(fc->intra_ext_tx_cdf[1], 7, CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->intra_ext_tx_cdf[2], 5, CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[1], 16, CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[2], 12, CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[3], 2, CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER(fc->cfl_sign_cdf, CFL_JOINT_SIGNS);
  RESET_CDF_COUNTER(fc->cfl_alpha_cdf, CFL_ALPHABET_SIZE);
}

static AOM_INLINE void update_cdf_timestamp(aom_cdf_prob *cdf_ptr, int num_cdfs,
                                        int cdf_stride, int nsymbs, int time) {
  for (int i = 0; i < num_cdfs; i++) {
    cdf_ptr[i * cdf_stride + nsymbs + 2] = time;
  }
}

#define UPDATE_CDF_TIMESTAMP(cname, nsymbs) \
  UPDATE_CDF_TIMESTAMP_STRIDE(cname, nsymbs, CDF_SIZE(nsymbs))

#define UPDATE_CDF_TIMESTAMP_STRIDE(cname, nsymbs, cdf_stride)       \
  do {                                                               \
    aom_cdf_prob *cdf_ptr = (aom_cdf_prob *)cname;                   \
    int array_size = (int)sizeof(cname) / sizeof(aom_cdf_prob);      \
    int num_cdfs = array_size / cdf_stride;                          \
    update_cdf_timestamp(cdf_ptr, num_cdfs, cdf_stride, nsymbs, timestamp); \
  } while (0)

static AOM_INLINE void update_nvm_timestamp(nmv_context *nmv, int timestamp) {
  UPDATE_CDF_TIMESTAMP(nmv->joints_cdf, 4);
  for (int i = 0; i < 2; i++) {
    UPDATE_CDF_TIMESTAMP(nmv->comps[i].classes_cdf, MV_CLASSES);
    UPDATE_CDF_TIMESTAMP(nmv->comps[i].class0_fp_cdf, MV_FP_SIZE);
    UPDATE_CDF_TIMESTAMP(nmv->comps[i].fp_cdf, MV_FP_SIZE);
    UPDATE_CDF_TIMESTAMP(nmv->comps[i].sign_cdf, 2);
    UPDATE_CDF_TIMESTAMP(nmv->comps[i].class0_hp_cdf, 2);
    UPDATE_CDF_TIMESTAMP(nmv->comps[i].hp_cdf, 2);
    UPDATE_CDF_TIMESTAMP(nmv->comps[i].class0_cdf, CLASS0_SIZE);
    UPDATE_CDF_TIMESTAMP(nmv->comps[i].bits_cdf, 2);
  }
}

void av1_move_to_new_contexts(FRAME_CONTEXT* fc, const FRAME_CONTEXT* src) {
  *fc = *src;
  fc->timestamp = ++FRAME_CONTEXT_TIME;
  int timestamp = fc->timestamp;


  aom_accounting_log_context_move(GLOBAL_ACCOUNTING, src->timestamp, timestamp);


  UPDATE_CDF_TIMESTAMP(fc->txb_skip_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->eob_extra_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->dc_sign_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->eob_flag_cdf16, 5);
  UPDATE_CDF_TIMESTAMP(fc->eob_flag_cdf32, 6);
  UPDATE_CDF_TIMESTAMP(fc->eob_flag_cdf64, 7);
  UPDATE_CDF_TIMESTAMP(fc->eob_flag_cdf128, 8);
  UPDATE_CDF_TIMESTAMP(fc->eob_flag_cdf256, 9);
  UPDATE_CDF_TIMESTAMP(fc->eob_flag_cdf512, 10);
  UPDATE_CDF_TIMESTAMP(fc->eob_flag_cdf1024, 11);
  UPDATE_CDF_TIMESTAMP(fc->coeff_base_eob_cdf, 3);
  UPDATE_CDF_TIMESTAMP(fc->coeff_base_cdf, 4);
  UPDATE_CDF_TIMESTAMP(fc->coeff_br_cdf, BR_CDF_SIZE);
  UPDATE_CDF_TIMESTAMP(fc->newmv_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->zeromv_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->refmv_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->drl_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->inter_compound_mode_cdf, INTER_COMPOUND_MODES);
  UPDATE_CDF_TIMESTAMP(fc->compound_type_cdf, MASKED_COMPOUND_TYPES);
  UPDATE_CDF_TIMESTAMP(fc->wedge_idx_cdf, 16);
  UPDATE_CDF_TIMESTAMP(fc->interintra_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->wedge_interintra_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->interintra_mode_cdf, INTERINTRA_MODES);
  UPDATE_CDF_TIMESTAMP(fc->motion_mode_cdf, MOTION_MODES);
  UPDATE_CDF_TIMESTAMP(fc->obmc_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->palette_y_size_cdf, PALETTE_SIZES);
  UPDATE_CDF_TIMESTAMP(fc->palette_uv_size_cdf, PALETTE_SIZES);
  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    UPDATE_CDF_TIMESTAMP_STRIDE(fc->palette_y_color_index_cdf[j], nsymbs,
                             CDF_SIZE(PALETTE_COLORS));
    UPDATE_CDF_TIMESTAMP_STRIDE(fc->palette_uv_color_index_cdf[j], nsymbs,
                             CDF_SIZE(PALETTE_COLORS));
  }
  UPDATE_CDF_TIMESTAMP(fc->palette_y_mode_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->palette_uv_mode_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->comp_inter_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->single_ref_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->comp_ref_type_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->uni_comp_ref_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->comp_ref_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->comp_bwdref_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->txfm_partition_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->compound_index_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->comp_group_idx_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->skip_mode_cdfs, 2);
  UPDATE_CDF_TIMESTAMP(fc->skip_txfm_cdfs, 2);
  UPDATE_CDF_TIMESTAMP(fc->intra_inter_cdf, 2);
  update_nvm_timestamp(&fc->nmvc, timestamp);
  update_nvm_timestamp(&fc->ndvc, timestamp);
  UPDATE_CDF_TIMESTAMP(fc->intrabc_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->seg.tree_cdf, MAX_SEGMENTS);
  UPDATE_CDF_TIMESTAMP(fc->seg.pred_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
  UPDATE_CDF_TIMESTAMP(fc->filter_intra_cdfs, 2);
  UPDATE_CDF_TIMESTAMP(fc->filter_intra_mode_cdf, FILTER_INTRA_MODES);
  UPDATE_CDF_TIMESTAMP(fc->switchable_restore_cdf, RESTORE_SWITCHABLE_TYPES);
  UPDATE_CDF_TIMESTAMP(fc->wiener_restore_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->sgrproj_restore_cdf, 2);
  UPDATE_CDF_TIMESTAMP(fc->y_mode_cdf, INTRA_MODES);
  UPDATE_CDF_TIMESTAMP_STRIDE(fc->uv_mode_cdf[0], UV_INTRA_MODES - 1,
                           CDF_SIZE(UV_INTRA_MODES));
  UPDATE_CDF_TIMESTAMP(fc->uv_mode_cdf[1], UV_INTRA_MODES);
  for (int i = 0; i < PARTITION_CONTEXTS; i++) {
    if (i < 4) {
      UPDATE_CDF_TIMESTAMP_STRIDE(fc->partition_cdf[i], 4, CDF_SIZE(10));
    } else if (i < 16) {
      UPDATE_CDF_TIMESTAMP(fc->partition_cdf[i], 10);
    } else {
      UPDATE_CDF_TIMESTAMP_STRIDE(fc->partition_cdf[i], 8, CDF_SIZE(10));
    }
  }
  UPDATE_CDF_TIMESTAMP(fc->switchable_interp_cdf, SWITCHABLE_FILTERS);
  UPDATE_CDF_TIMESTAMP(fc->kf_y_cdf, INTRA_MODES);
  UPDATE_CDF_TIMESTAMP(fc->angle_delta_cdf, 2 * MAX_ANGLE_DELTA + 1);
  UPDATE_CDF_TIMESTAMP_STRIDE(fc->tx_size_cdf[0], MAX_TX_DEPTH,
                           CDF_SIZE(MAX_TX_DEPTH + 1));
  UPDATE_CDF_TIMESTAMP(fc->tx_size_cdf[1], MAX_TX_DEPTH + 1);
  UPDATE_CDF_TIMESTAMP(fc->tx_size_cdf[2], MAX_TX_DEPTH + 1);
  UPDATE_CDF_TIMESTAMP(fc->tx_size_cdf[3], MAX_TX_DEPTH + 1);
  UPDATE_CDF_TIMESTAMP(fc->delta_q_cdf, DELTA_Q_PROBS + 1);
  UPDATE_CDF_TIMESTAMP(fc->delta_lf_cdf, DELTA_LF_PROBS + 1);
  for (int i = 0; i < FRAME_LF_COUNT; i++) {
    UPDATE_CDF_TIMESTAMP(fc->delta_lf_multi_cdf[i], DELTA_LF_PROBS + 1);
  }
  UPDATE_CDF_TIMESTAMP_STRIDE(fc->intra_ext_tx_cdf[1], 7, CDF_SIZE(TX_TYPES));
  UPDATE_CDF_TIMESTAMP_STRIDE(fc->intra_ext_tx_cdf[2], 5, CDF_SIZE(TX_TYPES));
  UPDATE_CDF_TIMESTAMP_STRIDE(fc->inter_ext_tx_cdf[1], 16, CDF_SIZE(TX_TYPES));
  UPDATE_CDF_TIMESTAMP_STRIDE(fc->inter_ext_tx_cdf[2], 12, CDF_SIZE(TX_TYPES));
  UPDATE_CDF_TIMESTAMP_STRIDE(fc->inter_ext_tx_cdf[3], 2, CDF_SIZE(TX_TYPES));
  UPDATE_CDF_TIMESTAMP(fc->cfl_sign_cdf, CFL_JOINT_SIGNS);
  UPDATE_CDF_TIMESTAMP(fc->cfl_alpha_cdf, CFL_ALPHABET_SIZE);
}
