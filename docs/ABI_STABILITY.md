# ABI stability (v1)

## Versioning

| Channel | Meaning |
|---------|---------|
| `GARDENIA_CORE_ABI_VERSION_*` | Wire/layout compatibility. Major mismatch → reject. |
| `gardenia_core_get_implementation_version()` | Product/implementation string (independent of ABI). |

Do not conflate ABI and implementation versions into one integer.

## Struct evolution

Every extensible public struct begins with:

```c
uint32_t struct_size;
uint32_t abi_version;
```

Rules:

* Caller sets both fields.
* Library rejects `struct_size` below the ABI-v1 known size.
* Larger compatible structs may be accepted; unknown trailing fields are ignored.
* Reserved fields must be zero.
* Fixed-width types only (`uint32_t`, `int32_t`, `uint64_t`, `float`, `double`, pointers).
* No `bool`, no `size_t` in persistent ABI structs, no C++ types.

## Compatibility policy

* ABI major bump: incompatible layout or semantic break.
* ABI minor/patch: additive fields/flags within reserved space when documented.
* Public consumers include only `gardenia_core/gardenia_core.h`.
