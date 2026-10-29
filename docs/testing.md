# Testing

Configure, build, and run all tests with:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`gb_container_tests` creates its binary fixtures in a uniquely named temporary directory and
removes them at exit. Fixtures cover a valid nested ISO BMFF hierarchy, a truncated box, a box
whose 32-bit size exceeds the file, an extended-size box smaller than its header, and a size-zero
box extending to the end of its parent. No camera media or location data is stored in the tree.

The test suite also invokes `gb-inspect --help` and `--version`, generates isolated CLI fixtures,
checks the JSON schema/type output, and verifies that corrupt input fails. Future work should add
full JSON Schema validation, fuzzing, sanitizers, and curated public or fingerprint-only regression
cases derived from real camera evidence.
