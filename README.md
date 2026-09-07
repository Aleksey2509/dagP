## LICENSE

See LICENSE file

## CITATION AND CONTACT

For updates, latest version, please check: [http://tda.gatech.edu](http://tda.gatech.edu)


### Contact
    `tdalab@cc.gatech.edu`


Citation for the partitioner (BibTeX):

```
    @article { Herrmann19-SISC,
        author =  {Julien Herrmann and M. Yusuf {\"O}zkaya and Bora U{\c{c}}ar and Kamer Kaya and {\"{U}}mit V. {\c{C}}ataly{\"{u}}rek},
        title = {Multilevel Algorithms for Acyclic Partitioning of Directed Acyclic Graphs},
        journal = {{SIAM} Journal on Scientific Computing (SISC)},
        KEYWORDS = {directed graph ;  acyclic partitioning ;  multilevel partitioning},
        year = {2019},
        volume = {41},
        number = {4},
        pages = {A2117-A2145},
        doi = {10.1137/18M1176865},
        URL = {https://doi.org/10.1137/18M1176865},
    }
```

Citation for the Scheduler (BibTeX):

```
    @inproceedings { Ozkaya19-IPDPS,
        title = {A scalable clustering-based task scheduler for homogeneous processors using DAG partitioning},
        booktitle = {33rd IEEE International Parallel and Distributed Processing Symposium},
        author = {M. Yusuf \"Ozkaya and Anne Benoit and Bora U{\c{c}}ar and Julien Herrmann and {\"{U}}mit V. {\c{C}}ataly{\"{u}}rek},
        month = {May},
        year = {2019},
    }
```

## HOW TO BUILD

1.  install `scons`

2.  Prepare `config.py` file.

    if undirected initial partitioning will be used:

    - set `metis` and/or `scotch` to 1.

    - set up the paths for `metis`/`scotch` external libraries

    set up the compilers of choice

3.  Run `scons` to build

## HOW TO EXECUTE

1.  Build the project

2.  For the partitioner:
        run `./exe/rMLGP`

    if trying to run scheduler:
        run `./exe/rMLGPschedule`

    the applications will show the command line parameter usage

    The file name and number of parts are required. All other paramters are optional.

    Example usage:

    ./exe/rMLGP 2mm_10_20_30_40.dot 4 --print 1 --ratio 1.1


## PARTITIONING OBJECTIVE

Use `--obj 0` (the default) to minimize edge cut, or `--obj 1` to
minimize communication volume. Other values are rejected.

Edge cut sums the weights of all edges crossing partition boundaries.
Communication volume groups outgoing crossing edges by **source vertex and
destination partition**, and sums the maximum edge weight in each group.
For unweighted graphs each group costs one. For example, a producer with ten
consumers in one remote partition and four in another contributes fourteen
to unweighted edge cut but only two to volume. Vertex weights affect balance,
not communication cost. The weighted definition assumes a single transfer of
the largest required payload can serve all consumers in that destination.

```sh
./exe/rMLGP data/2mm_10_20_30_40.dot 4 --obj 1 --seed 17 --ratio 1.1
scons test
```

Volume mode uses the existing multilevel edge-cut algorithm to generate a
starting partition, then performs exact volume refinement on the original
graph across all final partitions. Existing contractions and recursive split
scores do not preserve producer identities and therefore cannot directly
represent this objective. The final search preserves the starting quotient's
topological order and accepts strictly improving moves. It respects balance
bounds for feasible seeds and never worsens a seed's existing bound violations.
This is a local-search heuristic, not a guarantee of a global minimum.

For the volume phase, `--refinement 1` and `2` use single-vertex moves,
`3` uses pair swaps, and `4` uses both. Swaps can improve partitions when
exact balance prevents single moves, but examine quadratically many vertex
pairs and can be expensive on large graphs. `--refinement 0` disables
refinement; `--ref_step` limits the number of volume passes (default ten).

Both objective modes report communication volume for each run and its mean and
standard deviation, alongside edge cut, and identify the optimized objective.
Detailed multilevel diagnostics still
describe the edge-cut starting partition. With the API, set `opt.obj = 1`;
`dagP_partition_from_dgraph` returns the best selected objective across
`opt.runs` and writes the corresponding assignment to `parts`.

## GRAPH FORMAT

The application accepts a number of different formats:

  - `.dgraph` (`.dg`),
  - `.dot` (graphviz dot language), and
  - `.mtx` (Matrix Market File Format)

You can check out sample graphs or the websites for the respective graph formats.

The application has an option to generate binary versions of the input files.
This is, by default, enabled. If there is a binary version of the input,
the application prefers reading it instead of actual file.
Thus, if there is a change in the input file, where there is also a binary
version of a file with the same name, it will not be visible to the program.
The user should remove any `.bin` files for the inputs in this case.


## API USAGE

The dagP API provides five functions.

```
int dagP_init_parameters(MLGP_option *opt, const int nbPart);
int dagP_init_filename(MLGP_option* opt, char* file_name);
int dagP_opt_reallocUBLB(MLGP_option *opt, const int nbPart);
int dagP_read_graph(char* file_name, dgraph *G, const MLGP_option *opt);
ecType dagP_partition_from_dgraph(dgraph *G, const MLGP_option *opt, idxType* parts);
int dagP_free_graph(dgraph* G);
int dagP_free_option(MLGP_option *opt);s
```

First function initializes dagP options.

 - Options can be updated after initializing it using this command.

Second function initializes a file name for output files: (`file_name + "_dagP.out"`)

Third function can be used in case the number of parts to partition to is changed after parameter initialization.


Fourth function reads a directed graph from a file

 - The input graph format is inferred from the file extension.

 - If there is a binary version of the input file and the `use_binary_input` option `true`, then the partitioner will give priority to binary version of the input file.

Fifth function runs the partitioning algorithm, returns the best selected objective
(edge cut or communication volume), and writes its node assignments to `parts`.

Last two functions free the respective variables.


An example code snipped using the API is available, named `useapi.cpp`.
