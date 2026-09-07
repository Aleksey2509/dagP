# Partitioning algorithms

Scope: commit `333c3f2822cd300983c9ad71ed6c8961e4d11f1d`.
See [architecture](architecture.md) for the source map and data structures.

## Problem and invariants

For a DAG `G = (V, E)`, let `w(v)` be vertex weight, `c(u,v)` edge cost,
and `p(v)` the assigned part in `0..k-1`. The objective used by the partitioning
path is:

```text
cut(p) = sum c(u,v) over edges (u,v) with p(u) != p(v)
W_i    = sum w(v) over vertices with p(v) == i
```

`edgeCut` scans outgoing adjacency, so every directed edge contributes once.
If the graph's format does not include edge costs, a crossing edge contributes
one. The `volume` utility and communication-volume option constants do not
change this path: initial selection, refinement, and API best-run selection
use edge cut in this revision.

The **quotient graph** has one vertex per part and an edge `i -> j` whenever
an original edge crosses from part `i` to part `j`. It must be acyclic. An
acyclic input alone does not ensure this: on `a -> b -> c`, assigning `{a,c}`
to one part and `{b}` to another creates edges in both directions between parts.
For a bisection, the invariant is especially simple: all crossing edges must
go in one direction, from an upstream part `A` to a downstream part `B`.

Unspecified bounds become `lb[i] = 1` and
`ub[i] = ratio * total_vertex_weight / k`. The default ratio is `1.03`.
These are heuristic targets: `partSizeChecker` accepts upper bounds plus the
largest vertex weight, and move tests also allow `lb - maxVW` and
`ub + maxVW`. At a coarse level, a vertex represents a whole cluster. Thus
strict balance must be measured on the returned original-vertex assignment;
it must not be inferred from successful termination.

## Recursive bisection: `rVCycle`

The outer algorithm in [rvcycle.c](../src/recBisection/rvcycle.c) behaves as follows:

```text
partition(G, k):
    optionally compute a constraint bisection and anchor graph
    if k == 1: assign all vertices to part 0
    if k == 2: multilevel-bisect and normalize upstream/downstream labels
    otherwise:
        a = floor(k / 2), b = k - a
        multilevel-bisect G with bounds for a upstream and b downstream parts
        construct induced subgraphs G_A and G_B
        recursively partition G_A into a parts and G_B into b parts
        map child assignments back, adding a to labels from G_B
```

`splitCoarsen` determines direction from a crossing edge (or chooses `0,1` if
there is none), renumbers child vertices, and copies only internal edges.
Crossing edges have already been decided by this split. They do not participate
in child optimization. There is no final global k-way refinement across those
recursive boundaries.

The parent split forbids downstream-to-upstream edges; each child partition
is acyclic. Consequently, combining them in upstream-first label order cannot
create a cycle between children. This is the reason bisection composes into an
acyclic k-way result. Positive non-power-of-two part counts are accepted: for
example, five parts split into two and three.

For `k > 2`, the implementation computes each bisection upper bound as:

```text
S_A = 0.5 + sum ub[i] for i in the first floor(k/2) requested parts
S_B = 0.5 + sum ub[i] for the remaining requested parts
U_X = (S_X / ratio) * (1 + 0.7 * (ratio - 1) / log2(k))
```

This spends less imbalance allowance at an early split. Child options retain
the parent's per-part bounds through `copyOpt`. A source-level caveat is that
`copyOpt` always copies the first `new_nbPart` bounds; the second child does not
receive an offset slice. Uniform bounds fit this implementation best; do not
assume arbitrary distinct per-part bounds are propagated correctly.

## Optional constraint partitioning and anchoring

`conpar` in [undirPartitioning.c](../src/common/undirPartitioning.c) computes a
two-way partition used as cluster flags. With repair enabled, it invokes a
bisection with coarsening disabled, an undirected-library initial cut, and
acyclicity/balance repair. Without repair it uses the library result directly.

Coarsening ordinarily merges only vertices with equal flags. The repaired
partition can therefore survive contraction and initialize the coarse solution
through `IP_CONPAR`. This provides a starting partition on the original graph
before multilevel improvement.

The optional `anchored` branch copies the graph and adds a synthetic source
and target through `addSingleSourceTarget`, guided by the constraint labels.
It is disabled by default. The simpler ownership and execution path described
in the usage example leaves it disabled.

## Multilevel bisection: `VCycle2way`

In [vcycle2way.c](../src/recBisection/vcycle2way.c), coarsening repeatedly groups
vertices and constructs a smaller graph. It stops when the graph reaches
`co_stop_size`, reaches `co_stop_level`, or a completed contraction has
`new_vertex_count / old_vertex_count > co_stop_ratio`.

Defaults are `50 * requested_part_count` vertices, 20 levels, and a shrink
ratio of 0.9. The size threshold is initialized once and copied into recursive
options; it is not recomputed as 100 for each two-way call.

The coarsest graph receives an initial bisection. Refinement runs there and on
every finer level. Projection is simply:

```text
fine.part[v] = coarse.part[fine.new_index[v]]
```

The coarse representation preserves the cut of such a projected assignment.
Refinement at finer levels can then separate vertices formerly locked together
in a cluster.

## Acyclic coarsening

Contraction itself can create cycles. For example, contracting `a` and `c` in
`a -> b -> c`, even if there is also an edge `a -> c`, creates a cycle between
the new cluster and `b`. Selecting a heavy edge alone is therefore insufficient.

[clustering.c](../src/common/clustering.c) combines vertex traversal order,
level restrictions, weight limits, constraint flags, and cycle restrictions.
Top levels measure longest-path distance from sources; bottom levels measure
distance toward sinks. By default, odd coarsening levels use top levels and
even levels use bottom levels. The default traversal is randomized BFS
topological order; predecessor and successor candidates are both considered.

| `co_match` | Function | Strategy |
| --- | --- | --- |
| 0, `CO_ALG_MATCH_DIFF` | `matchingCoTop` | Pairwise matching with level and forbidden-neighbor restrictions; special single-degree cases allow additional candidates |
| 1, `CO_ALG_AGG_DIFF` | `clusteringCoTop` | Aggregate clusters with level differences at most one and forbidden-neighbor bookkeeping |
| 2, `CO_ALG_AGG_CHECK` | `clusteringCoCyc` | Aggregate with level/weight filtering and explicit cycle searches for tentative merges |
| 3, `CO_ALG_AGG_MIX` | `clusteringCoHyb` | Default hybrid: explicit checks for ordinary vertices, forbidden-neighbor rules for high-degree vertices |

`createCycleUp` and `createCycleDown` search for paths that leave and re-enter
a tentative cluster. The hybrid uses `isBig` to select the cheaper restriction
scheme around high-degree vertices. The cycle-checking variants still restrict
candidates; they are not exhaustive searches over all possible contractions.

Aggregation candidates are limited to cluster weight
`0.1 * graph_total_weight / opt.nbPart`. The default neighbor score is edge
cost divided by the neighbor vertex weight. Alternatives favor edge cost,
aggregate connection cost, or aggregate connection cost divided by cluster
weight. An optional final pass groups isolated vertices with compatible flags
subject to its weight limit.

`initializeNextGraph` assigns compact IDs to representatives, sums vertex
weights, drops within-cluster edges, and sums parallel edges between the same
ordered pair of clusters. Marker/work arrays combine incoming neighbors
without a dense vertex-by-vertex matrix. It then rebuilds outgoing adjacency
and source/target metadata. Total vertex weight is conserved; internal edge
cost disappears because those edges cannot cross a projected partition.

## Initial bisection

[initialBisection.c](../src/recBisection/initialBisection.c) runs the selected
initializer several times and retains the lowest cut. `inipart_nrun` defaults
to five; `IP_CONPAR` uses one trial. The greedy wrappers try both the original
and reversed graph, refine both candidates, restore the graph direction, and
keep the lower cut.

**Greedy growth** builds an upstream set from an initially all-downstream
assignment. Only vertices whose predecessors have already entered the set are
eligible. A remaining-indegree counter and a heap maintain this frontier.
Every prefix is therefore predecessor-closed and has no backward crossing edge.

For an eligible vertex, moving it upstream reduces cut by:

```text
gain(v) = sum incoming edge costs - sum outgoing edge costs
```

Its predecessors are already upstream and its successors are downstream,
which makes that gain static while it waits in the frontier. The algorithm
tracks cut and part weights incrementally, records the best acceptable prefix,
and undoes moves beyond that prefix.

`IP_GGG` uses this gain directly. `IP_GGG_IN` prioritizes incoming cost.
`IP_GGG_TWO`, the CLI fallback without external partition libraries, first
grows using incoming cost and distance to an anchor source; near its target
weight it switches to gain and vertex-weight heap keys.

**Undirected initialization** ignores direction for the initial grouping,
using METIS, Scotch, random assignment, or undirected greedy growth. A good
undirected cut may violate the quotient-DAG requirement. `fixUndirBisection`
tries both part orientations and both repairs:

- `fixAcyclicityBottom` propagates downstream membership to successors.
- `fixAcyclicityUp` propagates upstream membership to predecessors.

Each repair is followed by balance-oriented FM and ordinary refinement. The
best candidate passing the implementation's balance test is retained.

## Refinement

[refinementBis.c](../src/recBisection/refinementBis.c) uses the upstream/downstream
order of the quotient. For strictly positive edge costs, its weighted
connectivity tests implement these structural move rules:

- `A -> B`: a vertex in upstream part `A` can move only if it has no successor
  still in `A`; otherwise the move creates `B -> A`.
- `B -> A`: a vertex in downstream part `B` can move only if it has no
  predecessor still in `B`.

`inFromOutToCnts` maintains incoming/outgoing costs by part. Two indexed
max-heaps contain eligible vertices and their cut reductions. After a move,
`bisMoveAndUpdate` changes part weights, cut, neighbor connectivity, and heap
eligibility. Moved vertices are locked during the pass; some neighbors are
also locked when their moves would invalidate the direction constraint.

FM can take a move that temporarily worsens cut. It records the best prefix
of moves, favoring balance repair while overloaded and otherwise lower cut
with a maximum-part-weight tie-break, then rolls back the trailing moves.
The pass stops when candidates run out, at most `n` moves have occurred, or
the no-improvement count exceeds `n/4` once balance is acceptable.

`REF_bFM` selects between eligible heap maxima subject to balance rules.
`REF_bFM_MAXW` chooses from the part with the larger weight/upper-bound ratio.
Before ordinary passes, `recBisRefinementStep` can run one forced-balance pass
with a tighter derived bound. It then performs up to `ref_step` passes
(default 10), stopping when the new cut is at least 99% of the previous cut.

`REF_KL` swaps vertices across the bisection and checks the tentative quotient
for cycles. `REF_KL_bFM_MAXW` runs FM followed by swaps in the actual dispatcher,
despite the opposite order suggested by its header comment. `REF_NONE` returns
before forced balance as well as ordinary refinement.

## Cost model and guarantees

These are operation-based estimates, not measured scalability guarantees.
With `n` vertices and `m` edges, adjacency storage, cut evaluation, topological
traversal, and one graph-construction pass use `O(n+m)` storage or work as
applicable. A greedy frontier pass is approximately `O(m+n log n)`. An indexed
heap FM pass is approximately `O((n+m) log n)`; multiple trials, levels, and
refinement passes multiply the work.

Explicit cycle searches in coarsening and swap refinement can revisit graph
regions, so the complete algorithm has no simple linear-time guarantee.
Recursive depth is approximately `log2(k)`, but edge distributions, contraction
quality, and external partitioning costs govern actual runtime. The multilevel
chain holds several graph copies at once; retained memory depends on shrinkage.

The heuristics aim to preserve acyclicity and find a low-cut balanced result.
They do not prove optimality or feasibility under strict bounds. Positive
weights are the practical assumption: zero edge costs can make a zero weighted
connectivity test differ from the absence of an edge, and nonpositive vertex
weights undermine ratio scoring and balance logic.
