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

#include "av1/common/av1_common_int.h"
#include "av1/common/entropymv.h"
#include "av1/common/token_cdfs.h"

void av1_init_mv_probs(AV1_COMMON *cm) {
  // NB: this sets CDFs too
  cm->fc->nmvc = default_nmv_context1;
  cm->fc->ndvc = default_nmv_context2;
}
