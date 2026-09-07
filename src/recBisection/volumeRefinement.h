#ifndef VOLUME_REFINEMENT_H_
#define VOLUME_REFINEMENT_H_

#include "option.h"

/* Local search with exact original-graph volume gains, not a global optimum.
 * Requires an acyclic partition, resolved bounds, and refinement mode 0..4.
 * Returns the final communication volume. */
ecType refineVolume(dgraph *G, idxType *part, const MLGP_option *opt);

#endif
