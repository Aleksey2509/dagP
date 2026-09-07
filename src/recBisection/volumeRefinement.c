#include "volumeRefinement.h"
#include "utils.h"

/* Contractions used for edge cut do not preserve producer identities. Work on
 * the original graph, counting each producer once per remote destination part.
 * Weighted communication uses the largest payload to that destination. */
typedef struct {
    ecType *cost;
    ecType *scratch;
    idxType *affected;
    unsigned char *marked;
    idxType *rank;
    double *size;
} volumeWork;

static ecType sourceVolume(dgraph *G, idxType *part, idxType v,
                           ecType *scratch)
{
    ecType cost = 0;
    for (idxType e = G->outStart[v]; e <= G->outEnd[v]; ++e) {
        idxType p = part[G->out[e]];
        ecType weight = (G->frmt & DG_FRMT_EC) ? G->ecOut[e] : 1;
        if (p != part[v] && weight > scratch[p]) {
            cost += weight - scratch[p];
            scratch[p] = weight;
        }
    }
    /* Leave scratch zeroed for the next source without scanning all parts. */
    for (idxType e = G->outStart[v]; e <= G->outEnd[v]; ++e)
        scratch[part[G->out[e]]] = 0;
    return cost;
}

/* A fixed topological order of the quotient lets us check a move locally.
 * Count edges, not weights: even a zero-weight dependency imposes an order. */
static void partitionOrder(dgraph *G, idxType *part, idxType k, idxType *rank)
{
    idxType *degree = (idxType*) calloc(k, sizeof(idxType));
    idxType *queue = (idxType*) malloc(k * sizeof(idxType));
    idxType *head = (idxType*) malloc(k * sizeof(idxType));
    idxType *next = (idxType*) malloc((G->nVrtx + 1) * sizeof(idxType));
    if (!degree || !queue || !head || !next)
        u_errexit("partitionOrder: allocation failed\n");
    for (idxType p = 0; p < k; ++p)
        head[p] = -1;
    for (idxType v = 1; v <= G->nVrtx; ++v) {
        next[v] = head[part[v]];
        head[part[v]] = v;
        for (idxType e = G->outStart[v]; e <= G->outEnd[v]; ++e)
            if (part[v] != part[G->out[e]])
                ++degree[part[G->out[e]]];
    }
    idxType count = 0;
    for (idxType p = 0; p < k; ++p)
        if (!degree[p])
            queue[count++] = p;
    for (idxType i = 0; i < count; ++i) {
        idxType p = queue[i];
        rank[p] = i;
        for (idxType v = head[p]; v != -1; v = next[v])
            for (idxType e = G->outStart[v]; e <= G->outEnd[v]; ++e) {
                idxType dest = part[G->out[e]];
                if (dest != p && --degree[dest] == 0)
                    queue[count++] = dest;
            }
    }
    if (count != k)
        u_errexit("refineVolume: initial partition is cyclic\n");
    free(degree);
    free(queue);
    free(head);
    free(next);
}

static int ordered(dgraph *G, idxType *part, idxType v, idxType *rank)
{
    for (idxType e = G->inStart[v]; e <= G->inEnd[v]; ++e)
        if (rank[part[G->in[e]]] > rank[part[v]])
            return 0;
    for (idxType e = G->outStart[v]; e <= G->outEnd[v]; ++e)
        if (rank[part[v]] > rank[part[G->out[e]]])
            return 0;
    return 1;
}

static void addSource(volumeWork *work, idxType v, idxType *count)
{
    if (!work->marked[v]) {
        work->marked[v] = 1;
        work->affected[(*count)++] = v;
    }
}

/* v moves to dest; w == 0 means a single move, otherwise w swaps with v.
 * Only moved producers and their predecessors can change contribution. */
static ecType tryMove(dgraph *G, idxType *part, const MLGP_option *opt,
                      volumeWork *work, idxType v, idxType dest, idxType w)
{
    idxType origin = part[v];
    double delta = G->vw[v] - (w ? G->vw[w] : 0);
    double from = work->size[origin] - delta;
    double to = work->size[dest] + delta;
    /* Do not worsen pre-existing imbalance in a multilevel seed. */
    if (from < fmin(opt->lb[origin], work->size[origin]) ||
        from > fmax(opt->ub[origin], work->size[origin]) ||
        to < fmin(opt->lb[dest], work->size[dest]) ||
        to > fmax(opt->ub[dest], work->size[dest]))
        return 0;
    part[v] = dest;
    if (w)
        part[w] = origin;
    if (!ordered(G, part, v, work->rank) ||
        (w && !ordered(G, part, w, work->rank))) {
        part[v] = origin;
        if (w)
            part[w] = dest;
        return 0;
    }
    idxType count = 0;
    addSource(work, v, &count);
    for (idxType e = G->inStart[v]; e <= G->inEnd[v]; ++e)
        addSource(work, G->in[e], &count);
    if (w) {
        addSource(work, w, &count);
        for (idxType e = G->inStart[w]; e <= G->inEnd[w]; ++e)
            addSource(work, G->in[e], &count);
    }
    ecType gain = 0;
    for (idxType i = 0; i < count; ++i) {
        idxType source = work->affected[i];
        gain += work->cost[source] -
            sourceVolume(G, part, source, work->scratch);
    }
    if (gain > 0) {
        work->size[origin] = from;
        work->size[dest] = to;
        for (idxType i = 0; i < count; ++i) {
            idxType source = work->affected[i];
            work->cost[source] = sourceVolume(G, part, source, work->scratch);
        }
    }
    else {
        part[v] = origin;
        if (w)
            part[w] = dest;
        gain = 0;
    }
    for (idxType i = 0; i < count; ++i)
        work->marked[work->affected[i]] = 0;
    return gain;
}

ecType refineVolume(dgraph *G, idxType *part, const MLGP_option *opt)
{
    volumeWork work;
    idxType n = G->nVrtx, k = opt->nbPart;
    if (k < 1)
        u_errexit("refineVolume: number of partitions must be positive\n");
    if (opt->refinement < REF_NONE || opt->refinement > REF_KL_bFM_MAXW)
        u_errexit("refineVolume: unsupported refinement method\n");
    work.cost = (ecType*) calloc(n + 1, sizeof(ecType));
    work.scratch = (ecType*) calloc(k, sizeof(ecType));
    work.affected = (idxType*) malloc((n + 1) * sizeof(idxType));
    work.marked = (unsigned char*) calloc(n + 1, 1);
    work.rank = (idxType*) malloc(k * sizeof(idxType));
    work.size = (double*) calloc(k, sizeof(double));
    if (!work.cost || !work.scratch || !work.affected || !work.marked ||
        !work.rank || !work.size)
        u_errexit("refineVolume: allocation failed\n");
    ecType score = 0;
    for (idxType v = 1; v <= n; ++v) {
        if (part[v] < 0 || part[v] >= k)
            u_errexit("refineVolume: invalid partition id\n");
        work.size[part[v]] += G->vw[v];
    }
    partitionOrder(G, part, k, work.rank);
    for (idxType v = 1; v <= n; ++v) {
        work.cost[v] = sourceVolume(G, part, v, work.scratch);
        score += work.cost[v];
    }
    for (int pass = 0; opt->refinement != REF_NONE && pass < opt->ref_step; ++pass) {
        ecType previous = score;
        if (opt->refinement != REF_KL)
            for (idxType v = 1; v <= n; ++v)
                for (idxType dest = 0; dest < k; ++dest)
                    if (dest != part[v])
                        score -= tryMove(G, part, opt, &work, v, dest, 0);
        /* Swaps allow improvement when exact balance forbids single moves. */
        if (opt->refinement == REF_KL || opt->refinement == REF_KL_bFM_MAXW)
            for (idxType v = 1; v <= n; ++v)
                for (idxType w = v + 1; w <= n; ++w)
                    if (part[v] != part[w])
                        score -= tryMove(G, part, opt, &work, v, part[w], w);
        if (score == previous)
            break;
    }
    free(work.cost);
    free(work.scratch);
    free(work.affected);
    free(work.marked);
    free(work.rank);
    free(work.size);
    return score;
}
