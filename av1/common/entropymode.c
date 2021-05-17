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

#include "aom_mem/aom_mem.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/reconinter.h"
#include "av1/common/scan.h"
#include "av1/common/seg_common.h"
#include "av1/common/txb_common.h"
#include "av1/common/token_cdfs.h"

#define MAX_COLOR_CONTEXT_HASH 8
// Negative values are invalid
static const int palette_color_index_context_lookup[MAX_COLOR_CONTEXT_HASH +
                                                    1] = { -1, -1, 0, -1, -1,
                                                           4,  3,  2, 1 };

#define NUM_PALETTE_NEIGHBORS 3  // left, top-left and top.
int av1_get_palette_color_index_context(const uint8_t *color_map, int stride,
                                        int r, int c, int palette_size,
                                        uint8_t *color_order, int *color_idx) {
  assert(palette_size <= PALETTE_MAX_SIZE);
  assert(r > 0 || c > 0);

  // Get color indices of neighbors.
  int color_neighbors[NUM_PALETTE_NEIGHBORS];
  color_neighbors[0] = (c - 1 >= 0) ? color_map[r * stride + c - 1] : -1;
  color_neighbors[1] =
      (c - 1 >= 0 && r - 1 >= 0) ? color_map[(r - 1) * stride + c - 1] : -1;
  color_neighbors[2] = (r - 1 >= 0) ? color_map[(r - 1) * stride + c] : -1;

  // The +10 below should not be needed. But we get a warning "array subscript
  // is above array bounds [-Werror=array-bounds]" without it, possibly due to
  // this (or similar) bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=59124
  int scores[PALETTE_MAX_SIZE + 10] = { 0 };
  int i;
  static const int weights[NUM_PALETTE_NEIGHBORS] = { 2, 1, 2 };
  for (i = 0; i < NUM_PALETTE_NEIGHBORS; ++i) {
    if (color_neighbors[i] >= 0) {
      scores[color_neighbors[i]] += weights[i];
    }
  }

  int inverse_color_order[PALETTE_MAX_SIZE];
  for (i = 0; i < PALETTE_MAX_SIZE; ++i) {
    color_order[i] = i;
    inverse_color_order[i] = i;
  }

  // Get the top NUM_PALETTE_NEIGHBORS scores (sorted from large to small).
  for (i = 0; i < NUM_PALETTE_NEIGHBORS; ++i) {
    int max = scores[i];
    int max_idx = i;
    for (int j = i + 1; j < palette_size; ++j) {
      if (scores[j] > max) {
        max = scores[j];
        max_idx = j;
      }
    }
    if (max_idx != i) {
      // Move the score at index 'max_idx' to index 'i', and shift the scores
      // from 'i' to 'max_idx - 1' by 1.
      const int max_score = scores[max_idx];
      const uint8_t max_color_order = color_order[max_idx];
      for (int k = max_idx; k > i; --k) {
        scores[k] = scores[k - 1];
        color_order[k] = color_order[k - 1];
        inverse_color_order[color_order[k]] = k;
      }
      scores[i] = max_score;
      color_order[i] = max_color_order;
      inverse_color_order[color_order[i]] = i;
    }
  }

  if (color_idx != NULL)
    *color_idx = inverse_color_order[color_map[r * stride + c]];

  // Get hash value of context.
  int color_index_ctx_hash = 0;
  static const int hash_multipliers[NUM_PALETTE_NEIGHBORS] = { 1, 2, 2 };
  for (i = 0; i < NUM_PALETTE_NEIGHBORS; ++i) {
    color_index_ctx_hash += scores[i] * hash_multipliers[i];
  }
  assert(color_index_ctx_hash > 0);
  assert(color_index_ctx_hash <= MAX_COLOR_CONTEXT_HASH);

  // Lookup context from hash.
  const int color_index_ctx =
      palette_color_index_context_lookup[color_index_ctx_hash];
  assert(color_index_ctx >= 0);
  assert(color_index_ctx < PALETTE_COLOR_INDEX_CONTEXTS);
  return color_index_ctx;
}

int av1_fast_palette_color_index_context(const uint8_t *color_map, int stride,
                                         int r, int c, int *color_idx) {
  assert(r > 0 || c > 0);

  // This goes in the order of left, top, and top-left. This has the advantage
  // that unless anything here are not distinct or invalid, this will already
  // be in sorted order. Furthermore, if either of the first two are not
  // invalid, we know the last one is also invalid.
  int color_neighbors[NUM_PALETTE_NEIGHBORS];
  color_neighbors[0] = (c - 1 >= 0) ? color_map[r * stride + c - 1] : -1;
  color_neighbors[1] = (r - 1 >= 0) ? color_map[(r - 1) * stride + c] : -1;
  color_neighbors[2] =
      (c - 1 >= 0 && r - 1 >= 0) ? color_map[(r - 1) * stride + c - 1] : -1;

  // Since our array is so small, using a couple if statements is faster
  int scores[NUM_PALETTE_NEIGHBORS] = { 2, 2, 1 };
  if (color_neighbors[0] == color_neighbors[1]) {
    scores[0] += scores[1];
    color_neighbors[1] = -1;

    if (color_neighbors[0] == color_neighbors[2]) {
      scores[0] += scores[2];
      color_neighbors[2] = -1;
    }
  } else if (color_neighbors[0] == color_neighbors[2]) {
    scores[0] += scores[2];
    color_neighbors[2] = -1;
  } else if (color_neighbors[1] == color_neighbors[2]) {
    scores[1] += scores[2];
    color_neighbors[2] = -1;
  }

  int color_rank[NUM_PALETTE_NEIGHBORS] = { -1, -1, -1 };
  int score_rank[NUM_PALETTE_NEIGHBORS] = { 0, 0, 0 };
  int num_valid_colors = 0;
  for (int idx = 0; idx < NUM_PALETTE_NEIGHBORS; idx++) {
    if (color_neighbors[idx] != -1) {
      score_rank[num_valid_colors] = scores[idx];
      color_rank[num_valid_colors] = color_neighbors[idx];
      num_valid_colors++;
    }
  }

  // Sort everything
  // We need to swap the first two elements if they have the same score but
  // the color indices are not in the right order
  if (score_rank[0] < score_rank[1] ||
      (score_rank[0] == score_rank[1] && color_rank[0] > color_rank[1])) {
    const int tmp_score = score_rank[0];
    const int tmp_color = color_rank[0];
    score_rank[0] = score_rank[1];
    color_rank[0] = color_rank[1];
    score_rank[1] = tmp_score;
    color_rank[1] = tmp_color;
  }
  if (score_rank[0] < score_rank[2]) {
    const int tmp_score = score_rank[0];
    const int tmp_color = color_rank[0];
    score_rank[0] = score_rank[2];
    color_rank[0] = color_rank[2];
    score_rank[2] = tmp_score;
    color_rank[2] = tmp_color;
  }
  if (score_rank[1] < score_rank[2]) {
    const int tmp_score = score_rank[1];
    const int tmp_color = color_rank[1];
    score_rank[1] = score_rank[2];
    color_rank[1] = color_rank[2];
    score_rank[2] = tmp_score;
    color_rank[2] = tmp_color;
  }

  if (color_idx != NULL) {
    // If any of the neighbor color has higher index than current color index,
    // then we move up by 1 unless the current color is the same as one of the
    // neighbor
    const int current_color = *color_idx = color_map[r * stride + c];
    int same_neighbor = -1;
    for (int idx = 0; idx < NUM_PALETTE_NEIGHBORS; idx++) {
      if (color_rank[idx] > current_color) {
        (*color_idx)++;
      } else if (color_rank[idx] == current_color) {
        same_neighbor = idx;
      }
    }
    if (same_neighbor != -1) {
      *color_idx = same_neighbor;
    }
  }

  // Get hash value of context.
  int color_index_ctx_hash = 0;
  static const int hash_multipliers[NUM_PALETTE_NEIGHBORS] = { 1, 2, 2 };
  for (int idx = 0; idx < NUM_PALETTE_NEIGHBORS; ++idx) {
    color_index_ctx_hash += score_rank[idx] * hash_multipliers[idx];
  }
  assert(color_index_ctx_hash > 0);
  assert(color_index_ctx_hash <= MAX_COLOR_CONTEXT_HASH);

  // Lookup context from hash.
  const int color_index_ctx =
      palette_color_index_context_lookup[color_index_ctx_hash];
  assert(color_index_ctx >= 0);
  assert(color_index_ctx < PALETTE_COLOR_INDEX_CONTEXTS);
  return color_index_ctx;
}
#undef NUM_PALETTE_NEIGHBORS
#undef MAX_COLOR_CONTEXT_HASH

void av1_init_mode_probs(FRAME_CONTEXT *fc) {
  av1_copy(fc->palette_y_size_cdf, default_palette_y_size_cdf);
  av1_copy(fc->palette_uv_size_cdf, default_palette_uv_size_cdf);
  av1_copy(fc->palette_y_color_index_cdf, default_palette_y_color_index_cdf);
  av1_copy(fc->palette_uv_color_index_cdf, default_palette_uv_color_index_cdf);
  av1_copy(fc->kf_y_cdf, default_kf_y_mode_cdf);
  av1_copy(fc->angle_delta_cdf, default_angle_delta_cdf);
  av1_copy(fc->comp_inter_cdf, default_comp_inter_cdf);
  av1_copy(fc->comp_ref_type_cdf, default_comp_ref_type_cdf);
  av1_copy(fc->uni_comp_ref_cdf, default_uni_comp_ref_cdf);
  av1_copy(fc->palette_y_mode_cdf, default_palette_y_mode_cdf);
  av1_copy(fc->palette_uv_mode_cdf, default_palette_uv_mode_cdf);
  av1_copy(fc->comp_ref_cdf, default_comp_ref_cdf);
  av1_copy(fc->comp_bwdref_cdf, default_comp_bwdref_cdf);
  av1_copy(fc->single_ref_cdf, default_single_ref_cdf);
  av1_copy(fc->txfm_partition_cdf, default_txfm_partition_cdf);
  av1_copy(fc->compound_index_cdf, default_compound_idx_cdfs);
  av1_copy(fc->comp_group_idx_cdf, default_comp_group_idx_cdfs);
  av1_copy(fc->newmv_cdf, default_newmv_cdf);
  av1_copy(fc->zeromv_cdf, default_zeromv_cdf);
  av1_copy(fc->refmv_cdf, default_refmv_cdf);
  av1_copy(fc->drl_cdf, default_drl_cdf);
  av1_copy(fc->motion_mode_cdf, default_motion_mode_cdf);
  av1_copy(fc->obmc_cdf, default_obmc_cdf);
  av1_copy(fc->inter_compound_mode_cdf, default_inter_compound_mode_cdf);
  av1_copy(fc->compound_type_cdf, default_compound_type_cdf);
  av1_copy(fc->wedge_idx_cdf, default_wedge_idx_cdf);
  av1_copy(fc->interintra_cdf, default_interintra_cdf);
  av1_copy(fc->wedge_interintra_cdf, default_wedge_interintra_cdf);
  av1_copy(fc->interintra_mode_cdf, default_interintra_mode_cdf);
  av1_copy(fc->seg.pred_cdf, default_segment_pred_cdf);
  av1_copy(fc->seg.tree_cdf, default_seg_tree_cdf);
  av1_copy(fc->filter_intra_cdfs, default_filter_intra_cdfs);
  av1_copy(fc->filter_intra_mode_cdf, default_filter_intra_mode_cdf);
  av1_copy(fc->switchable_restore_cdf, default_switchable_restore_cdf);
  av1_copy(fc->wiener_restore_cdf, default_wiener_restore_cdf);
  av1_copy(fc->sgrproj_restore_cdf, default_sgrproj_restore_cdf);
  av1_copy(fc->y_mode_cdf, default_if_y_mode_cdf);
  av1_copy(fc->uv_mode_cdf, default_uv_mode_cdf);
  av1_copy(fc->switchable_interp_cdf, default_switchable_interp_cdf);
  av1_copy(fc->partition_cdf, default_partition_cdf);
  av1_copy(fc->intra_ext_tx_cdf, default_intra_ext_tx_cdf);
  av1_copy(fc->inter_ext_tx_cdf, default_inter_ext_tx_cdf);
  av1_copy(fc->skip_mode_cdfs, default_skip_mode_cdfs);
  av1_copy(fc->skip_txfm_cdfs, default_skip_txfm_cdfs);
  av1_copy(fc->intra_inter_cdf, default_intra_inter_cdf);
  for (int i = 0; i < SPATIAL_PREDICTION_PROBS; i++)
    av1_copy(fc->seg.spatial_pred_seg_cdf[i],
             default_spatial_pred_seg_tree_cdf[i]);
  av1_copy(fc->tx_size_cdf, default_tx_size_cdf);
  av1_copy(fc->delta_q_cdf, default_delta_q_cdf);
  av1_copy(fc->delta_lf_cdf, default_delta_lf_cdf);
  av1_copy(fc->delta_lf_multi_cdf, default_delta_lf_multi_cdf);
  av1_copy(fc->cfl_sign_cdf, default_cfl_sign_cdf);
  av1_copy(fc->cfl_alpha_cdf, default_cfl_alpha_cdf);
  av1_copy(fc->intrabc_cdf, default_intrabc_cdf);
}

void av1_set_default_ref_deltas(int8_t *ref_deltas) {
  assert(ref_deltas != NULL);

  ref_deltas[INTRA_FRAME] = 1;
  ref_deltas[LAST_FRAME] = 0;
  ref_deltas[LAST2_FRAME] = ref_deltas[LAST_FRAME];
  ref_deltas[LAST3_FRAME] = ref_deltas[LAST_FRAME];
  ref_deltas[BWDREF_FRAME] = ref_deltas[LAST_FRAME];
  ref_deltas[GOLDEN_FRAME] = -1;
  ref_deltas[ALTREF2_FRAME] = -1;
  ref_deltas[ALTREF_FRAME] = -1;
}

void av1_set_default_mode_deltas(int8_t *mode_deltas) {
  assert(mode_deltas != NULL);

  mode_deltas[0] = 0;
  mode_deltas[1] = 0;
}

static void set_default_lf_deltas(struct loopfilter *lf) {
  lf->mode_ref_delta_enabled = 1;
  lf->mode_ref_delta_update = 1;

  av1_set_default_ref_deltas(lf->ref_deltas);
  av1_set_default_mode_deltas(lf->mode_deltas);
}

void av1_setup_frame_contexts(AV1_COMMON *cm) {
  // Store the frame context into a special slot (not associated with any
  // reference buffer), so that we can set up cm->pre_fc correctly later
  // This function must ONLY be called when cm->fc has been initialized with
  // default probs, either by av1_setup_past_independence or after manually
  // initializing them
  *cm->default_frame_context = *cm->fc;
  // TODO(jack.haughton@argondesign.com): don't think this should be necessary,
  // but could do with fuller testing
  if (cm->tiles.large_scale) {
    for (int i = LAST_FRAME; i <= ALTREF_FRAME; ++i) {
      RefCntBuffer *const buf = get_ref_frame_buf(cm, i);
      if (buf != NULL) buf->frame_context = *cm->fc;
    }
    for (int i = 0; i < FRAME_BUFFERS; ++i)
      cm->buffer_pool->frame_bufs[i].frame_context = *cm->fc;
  }
}

void av1_setup_past_independence(AV1_COMMON *cm) {
  // Reset the segment feature data to the default stats:
  // Features disabled, 0, with delta coding (Default state).
  av1_clearall_segfeatures(&cm->seg);

  if (cm->cur_frame->seg_map) {
    memset(cm->cur_frame->seg_map, 0,
           (cm->cur_frame->mi_rows * cm->cur_frame->mi_cols));
  }

  // reset mode ref deltas
  av1_set_default_ref_deltas(cm->cur_frame->ref_deltas);
  av1_set_default_mode_deltas(cm->cur_frame->mode_deltas);
  set_default_lf_deltas(&cm->lf);

  av1_default_coef_probs(cm);
  av1_init_mode_probs(cm->fc);
  av1_init_mv_probs(cm);
  cm->fc->initialized = 1;


  cm->fc->timestamp = 0;


  av1_setup_frame_contexts(cm);
}
