---
graphlog
---

This utility creates a log of updates for the [GFE driver](https://github.com/cwida/gfe_driver). 

The [Original Graphlog](https://github.com/whatsthecraic/graphlog) was used to generate a 
log of updates based on an initial unsorted dataset. It would either randomly permute the input
edge list or generate temporary vertices that would be added and then later removed in the 
update log.

This version of Graphlog converts an input list of updates into the graphlog file format, 
which is used by the GFE Driver. It no longer generates any new edges and does not permute the
input. 

#### Requirements
- O.S. Linux
- CMake v 3.14 or newer
- A C++17 capable compiler

#### Fetch & build

```
<clone this repository>
cd graphlog
git submodule update --init
mkdir build && cd build
cmake ../ -DCMAKE_BUILD_TYPE=Release
make -j
```

The final artifact is the executable `graphlog`.

#### Usage

```
  ./graphlog [options] <input> <output>

  -e, --edges-final arg     Final number of edges in the input graph
  -h, --help                Show this help menu
  -v, --vertices-final arg  Final number of vertices in the input graph
```

The parameter `edges-final` and ``vertices-final` should reflect how many edges 
and vertices are contained in the graph, once all edge updates finish.

The `<input>` should be a graphalytics `.properties` file. 

The `<output>` is the name of the output file.

Example:
```
./graphlog -e 1000 -v 100 test-dataset.properties test-dataset.graphlog
```