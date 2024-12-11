# Consensus

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
![Build status](https://github.com/ByteHamster/Consensus/actions/workflows/build.yml/badge.svg)

A minimal perfect hash function (MPHF) maps a set S of n keys to the first n integers without collisions.
Perfect hash functions have applications in databases, bioinformatics, and as a building block of various space-efficient data structures.

Consensus is a perfect hash function with very small space consumption.

### Library usage

Clone this repository (with submodules) and add the following to your `CMakeLists.txt`.

```
add_subdirectory(path/to/Consensus)
target_link_libraries(YourTarget PRIVATE Consensus::consensus)
```

You can construct a Consensus perfect hash function as follows.

```cpp
std::vector<std::string> keys = {"abc", "def", "123", "456"};
consensus::Consensus<> hashFunc(keys, /* overhead = */ 0.01f);
std::cout << hashFunc("abc") << std::endl;
```

### Licensing
This code is licensed under the [GPLv3](/LICENSE).
