# dagP code guide

This guide describes committed revision
`333c3f2822cd300983c9ad71ed6c8961e4d11f1d`. It was prepared from a separate
`git archive HEAD` export; working-tree edits and untracked implementation files
were excluded. Source links open the corresponding repository files; use
`git show 333c3f2:<path>` to see the exact version described here.

Start with [architecture](architecture.md) for the data structures and execution
path, then read [algorithms](algorithms.md) for the partitioning procedure.
[Usage and API](usage.md) covers building, inputs, options, integration, and
verification. These notes describe the implementation, not a claim that every
algorithm constant in a header represents an available feature.

dagP divides a directed acyclic graph (DAG) into approximately balanced groups
while minimizing the cost of edges between groups. The graph of groups must
also be acyclic. Vertices can represent tasks, vertex weights their computational
cost, and edge weights the cost of transferring data between tasks.

The implementation is a randomized heuristic: it combines recursive bisection
with multilevel graph contraction and local improvement. It does not compute a
provably optimal partition or guarantee strict user bounds for every input.

The existing repository README contains the project's publication citations and
contact information. This guide derives its algorithm descriptions directly
from the committed source.
