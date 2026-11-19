# Building Gardenia Ambika Core

## Requirements

- CMake 3.22 or newer
- A C++17 compiler (Visual Studio 2022 on Windows is the validated configuration)
- A C11 compiler for the C API smoke test

No JUCE dependency is required.

## Windows (Visual Studio 2022 x64)

From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --build build --config Debug
```

Shared library outputs:

- `build/Release/gardenia-core.dll`
- `build/Debug/gardenia-core.dll`

## Tests

Enable and run CTest after a successful build:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Golden synthesis and fuzz tests compare against an external baseline corpus.
Pass the corpus directory at configure time:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DGARDENIA_BASELINE_OUTPUT="D:/path/to/baseline-output"
```

Without a valid baseline directory, configure still succeeds, but golden tests
will fail at runtime. Non-golden tests (C API smoke, ABI layout, invalid input,
dynamic loading, symbol audit, mixer, compile smoke, JUCE scan) do not require
the corpus.

Release builds are the reference configuration for bit-identical golden results.

## Artifacts

| Target | Output |
|--------|--------|
| `GardeniaCoreShared` | `gardenia-core` shared library |
| `GardeniaCoreStatic` | Internal static library for in-tree tests |
| `GardeniaCoreAbiStatic` | Static link of the C ABI for in-tree tests |
