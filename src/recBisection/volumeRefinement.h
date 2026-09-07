#ifndef VOLUME_REFINEMENT_H_
#define VOLUME_REFINEMENT_H_

#include "option.h"

/* Exact original-graph refinement; returns the final communication volume. */
ecType refineVolume(dgraph *G, idxType *part, const MLGP_option *opt);

#endif
