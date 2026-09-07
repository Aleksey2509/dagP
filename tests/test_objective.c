#ifdef NDEBUG
#undef NDEBUG
#endif
#include "dagP.h"
#include "volumeRefinement.h"
#include "rvcycle.h"
#include "utils.h"

typedef struct { idxType from, to; ecType weight; } edge;

static void makeGraph(dgraph *G, int n, edge *edges, int m, int weighted)
{
    memset(G, 0, sizeof(*G));
    allocateDGraphData(G, n, m, DG_FRMT_VWEC);
    int cursor = 0;
    for (int v = 1; v <= n; ++v) {
        G->vw[v] = 1;
        G->inStart[v] = cursor;
        for (int e = 0; e < m; ++e)
            if (edges[e].to == v) {
                G->in[cursor] = edges[e].from;
                G->ecIn[cursor++] = edges[e].weight;
            }
        G->inEnd[v] = cursor - 1;
    }
    G->inStart[n + 1] = m;
    G->inEnd[n + 1] = m - 1;
    fillOutFromIn(G);
    set_dgraph_info(G);
    G->frmt = weighted ? DG_FRMT_VWEC : DG_FRMT_UN;
}

static void freeGraph(dgraph *G)
{
    G->frmt = DG_FRMT_VWEC; /* makeGraph always allocates all weight arrays. */
    freeDGraphData(G);
}

/* Independent reference: explicitly inspect each producer/destination pair. */
static ecType reference(dgraph *G, idxType *part, int k)
{
    ecType result = 0;
    for (int v = 1; v <= G->nVrtx; ++v)
        for (int p = 0; p < k; ++p) {
            if (p == part[v])
                continue;
            ecType maximum = 0;
            for (int e = G->outStart[v]; e <= G->outEnd[v]; ++e)
                if (part[G->out[e]] == p) {
                    ecType weight = (G->frmt & DG_FRMT_EC) ? G->ecOut[e] : 1;
                    if (weight > maximum)
                        maximum = weight;
                }
            result += maximum;
        }
    return result;
}

/* Boolean transitive closure, independent of weighted quotient utilities. */
static void assertAcyclic(edge *edges, int m, idxType *part, int k)
{
    int reach[4][4] = {{0}};
    assert(k <= 4);
    for (int e = 0; e < m; ++e) {
        int from = part[edges[e].from], to = part[edges[e].to];
        if (from != to)
            reach[from][to] = 1;
    }
    for (int via = 0; via < k; ++via)
        for (int from = 0; from < k; ++from)
            for (int to = 0; to < k; ++to)
                reach[from][to] |= reach[from][via] && reach[via][to];
    for (int p = 0; p < k; ++p)
        assert(!reach[p][p]);
}

static void testMetric(void)
{
    edge edges[] = {{1,2,0}, {1,3,7}, {1,4,3}, {1,5,11}, {2,5,1LL << 35}};
    idxType part[] = {0,0,1,1,1,2};
    dgraph G;
    makeGraph(&G, 5, edges, 5, 1);
    assert(volume(&G, part, 3) == 18 + (1LL << 35));
    assert(edgeCut(&G, part) == 21 + (1LL << 35));
    for (int e = 0; e < 2; ++e) {
        idxType tmp = G.out[e];
        G.out[e] = G.out[3-e];
        G.out[3-e] = tmp;
        ecType weight = G.ecOut[e];
        G.ecOut[e] = G.ecOut[3-e];
        G.ecOut[3-e] = weight;
    }
    assert(volume(&G, part, 3) == reference(&G, part, 3));
    G.frmt = DG_FRMT_UN;
    assert(volume(&G, part, 3) == 3);
    assert(edgeCut(&G, part) == 5);
    /* Empty destination slots and successive calls must not retain maxima. */
    part[5] = 4095;
    assert(volume(&G, part, 4096) == reference(&G, part, 4096));
    part[5] = 2;
    memset(part, 0, sizeof(part));
    assert(volume(&G, part, 1) == 0);
    freeGraph(&G);
}

static void testDifferentObjective(void)
{
    edge edges[15];
    int m = 0;
    for (int v = 1; v <= 4; ++v) {
        edges[m++] = (edge){v,5,1};
        edges[m++] = (edge){v,6,1};
        edges[m++] = (edge){v,9,1};
    }
    for (int v = 6; v <= 8; ++v)
        edges[m++] = (edge){5,v,1};
    dgraph G;
    makeGraph(&G, 9, edges, m, 0);
    idxType part[] = {0,0,0,0,0,0,1,1,1,0};
    MLGP_option opt;
    initMLGPoptions(&opt, 2);
    opt.lb[0] = 5; opt.ub[0] = 6;
    opt.lb[1] = 3; opt.ub[1] = 4;
    assert(edgeCut(&G, part) == 7);
    assert(volume(&G, part, 2) == 5);
    assert(refineVolume(&G, part, &opt) == 4);
    assert(part[5] == 1);
    assert(edgeCut(&G, part) == 8); /* Volume must win even if edge cut worsens. */
    assert(checkAcyclicity(&G, part, 2));
    free_opt(&opt);
    freeGraph(&G);
}

static void testRandomRefinement(void)
{
    srand(41);
    int improved = 0, swapsImproved = 0;
    for (int trial = 0; trial < 400; ++trial) {
        edge edges[120];
        int m = 0, n = 12, k = trial % 3 + 2;
        for (int u = 1; u < n; ++u)
            for (int v = u + 1; v <= n; ++v)
                if ((u == 1 && v == n) || rand() % 3 == 0)
                    edges[m++] = (edge){u, v, rand() % 8};
        dgraph G;
        makeGraph(&G, n, edges, m, trial % 2);
        /* Exercise unequal vertex weights, including swaps with unequal mass. */
        for (int v = 1; v <= n; ++v)
            G.vw[v] = trial % 2 ? 1 + rand() % 5 : 1;
        set_dgraph_info(&G);
        MLGP_option opt;
        initMLGPoptions(&opt, k);
        opt.refinement = trial % 5;
        opt.ref_step = 20;
        idxType part[13], original[13];
        int size[4] = {0};
        for (int v = 1; v <= n; ++v) {
            /* Reverse labels to ensure the refiner uses quotient order. */
            part[v] = original[v] = k - 1 - (v - 1) * k / n;
            size[part[v]] += G.vw[v];
        }
        for (int p = 0; p < k; ++p) {
            opt.lb[p] = opt.refinement == REF_KL ? size[p] : 1;
            opt.ub[p] = opt.refinement == REF_KL ? size[p] : size[p] + 2;
            /* Some seeds are infeasible: refinement may repair but never
             * increase either bound violation for any individual part. */
            if (trial % 7 == 0) {
                opt.lb[p] = size[p] + 1;
                opt.ub[p] = size[p] + 2;
            }
            else if (trial % 7 == 1)
                opt.ub[p] = size[p] - 1;
        }
        int initialSize[4];
        memcpy(initialSize, size, sizeof(size));
        ecType before = reference(&G, part, k);
        ecType after = refineVolume(&G, part, &opt);
        assert(after == reference(&G, part, k));
        assert(after == volume(&G, part, k));
        assert(after <= before);
        assert(checkAcyclicity(&G, part, k));
        /* Structural check includes zero-cost dependencies, which the
         * baseline weighted quotient checker can overlook. */
        assertAcyclic(edges, m, part, k);
        improved += after < before;
        swapsImproved += opt.refinement == REF_KL && after < before;
        memset(size, 0, sizeof(size));
        for (int v = 1; v <= n; ++v) {
            size[part[v]] += G.vw[v];
            if (opt.refinement == REF_NONE)
                assert(part[v] == original[v]);
        }
        for (int p = 0; p < k; ++p)
            assert(size[p] >= fmin(opt.lb[p], initialSize[p]) &&
                   size[p] <= fmax(opt.ub[p], initialSize[p]));
        free_opt(&opt);
        freeGraph(&G);
    }
    assert(improved > 0);
    assert(swapsImproved > 0);
}

/* The seed has volume zero, so no strictly improving move or swap exists.
 * A candidate can nevertheless cost 2 * weight, beyond ecType_MAX. Do not
 * evaluate that candidate with the ecType reference accumulator: it would
 * reproduce the implementation's overflow instead of detecting it. */
static void testLargeCandidate(int refinement, ecType weight)
{
    edge edges[] = {{1,3,weight}, {2,3,weight}};
    idxType part[] = {0,0,0,0,1};
    const idxType original[] = {0,0,0,0,1};
    dgraph G;
    makeGraph(&G, 4, edges, 2, 1);
    MLGP_option opt;
    initMLGPoptions(&opt, 2);
    opt.refinement = refinement;
    opt.lb[0] = opt.lb[1] = 1;
    opt.ub[0] = opt.ub[1] = 3;
    assert(volume(&G, part, 2) == 0);
    ecType after = refineVolume(&G, part, &opt);
    assert(after == 0);
    assert(memcmp(part, original, sizeof(part)) == 0);
    assertAcyclic(edges, 2, part, 2);
    free_opt(&opt);
    freeGraph(&G);
}

static void testAPI(void)
{
    edge edges[] = {{1,3,1}, {1,4,1}, {2,3,1}, {2,4,1},
                    {3,5,1}, {3,6,1}, {4,5,1}, {4,6,1}, {5,7,1}, {6,8,1}};
    for (int objective = 0; objective <= 1; ++objective) {
        dgraph G;
        makeGraph(&G, 8, edges, 10, 1);
        MLGP_option opt;
        initMLGPoptions(&opt, 2);
        opt.obj = objective;
        opt.conpar = 0;
        opt.inipart = IP_GGG_TWO;
        opt.seed = 17;
        opt.runs = 3;
        opt.ratio = 1.5;
        strcpy(opt.file_name, "objective-test");
        idxType part[9], expected[9];
        ecType score = dagP_partition_from_dgraph(&G, &opt, part);
        assert(score == (objective ? reference(&G, part, 2) : edgeCut(&G, part)));
        assert(checkAcyclicity(&G, part, 2));
        /* Replay the random stream and independently select the best run. */
        usRandom(opt.seed);
        ecType best = ecType_MAX;
        for (int run = 0; run < opt.runs; ++run) {
            rMLGP_info *info = (rMLGP_info*) malloc(sizeof(rMLGP_info));
            initRInfoPart(info);
            rcoarsen *r = rVCycle(&G, opt, info);
            ecType value = objective ? reference(&G, r->coars->part, 2) : edgeCut(&G, r->coars->part);
            if (value < best) {
                best = value;
                memcpy(expected + 1, r->coars->part + 1, 8 * sizeof(idxType));
            }
            freeRCoarsenHeader(r);
            freeRInfoPart(info);
        }
        assert(score == best);
        assert(memcmp(part + 1, expected + 1, 8 * sizeof(idxType)) == 0);
        free_opt(&opt);
        freeGraph(&G);
    }
}

int main(int argc, char **argv)
{
    /* Run potentially aborting regressions separately so the Python runner
     * reports every failing case rather than stopping at the first assert. */
    if (argc == 4 && strcmp(argv[1], "--large-candidate") == 0) {
        testLargeCandidate(atoi(argv[2]), strtoll(argv[3], NULL, 10));
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--invalid-objective") == 0) {
        MLGP_option opt = {0};
        dgraph G = {0};
        idxType part[1];
        opt.nbPart = 1;
        opt.runs = 1;
        opt.obj = 2;
        dagP_partition_from_dgraph(&G, &opt, part);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--zero-runs") == 0) {
        MLGP_option opt = {0};
        dgraph G = {0};
        idxType part[1];
        opt.nbPart = 1;
        dagP_partition_from_dgraph(&G, &opt, part);
        return 0;
    }
    testMetric();
    testDifferentObjective();
    testRandomRefinement();
    if (argc == 1 || strcmp(argv[1], "--no-api") != 0)
        testAPI();
    puts("Objective unit tests passed");
    return 0;
}
