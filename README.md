# ConsensusRecSplit

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
![Build status](https://github.com/ByteHamster/ConsensusRecSplit/actions/workflows/build.yml/badge.svg)

A minimal perfect hash function (MPHF) maps a set S of n keys to the first n integers without collisions.
Perfect hash functions have applications in databases, bioinformatics, and as a building block of various space-efficient data structures.

ConsensusRecSplit is a perfect hash function with very small space consumption.
It is based on *Combined Search and Encoding of Successful Seeds* (Consensus), applied to
the recursive splitting idea of [RecSplit](https://github.com/vigna/sux/blob/master/sux/function/RecSplit.hpp).

### Library usage

Clone this repository (with submodules) and add the following to your `CMakeLists.txt`.

```
add_subdirectory(path/to/ConsensusRecSplit)
target_link_libraries(YourTarget PRIVATE ConsensusRecSplit)
```

You can construct a ConsensusRecSplit perfect hash function as follows.

```cpp
std::vector<std::string> keys = {"abc", "def", "123", "456"};
consensus::ConsensusRecSplit<> hashFunc(keys, /* overhead = */ 0.01f);
std::cout << hashFunc("abc") << std::endl;
```

### Licensing
This code is licensed under the [GPLv3](/LICENSE).
