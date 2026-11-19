# Real-time safety (ABI v1)

## Allowed in process

* Deterministic DSP on caller-owned buffers
* Event application and sample-offset segmentation
* Scoped FTZ|DAZ establish/restore around process calls

## Forbidden in process

* Heap allocation / free
* Mutexes or other locks
* Logging / printf
* File or network I/O
* Dynamic library loading
* C++ exceptions crossing the ABI (caught at the boundary; numeric errors returned)

## Allocation windows

* `gardenia_core_create` / `gardenia_core_prepare` may allocate (scratch, instance).
* `gardenia_core_reset` does not allocate.
* Prefer sizing scratch at create/prepare for the configured maximum block size.

## FTZ / DAZ

Golden results assume FTZ|DAZ (CSR ≈ `0x8040`).

ABI behavior:

* Save caller MXCSR
* Enable FTZ|DAZ for the process call
* Restore prior MXCSR before return

Callers may run with FTZ|DAZ off; process still enables them for the duration of
the call so Release goldens remain matched for denormal-sensitive cases.
