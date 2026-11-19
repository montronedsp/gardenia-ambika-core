# Threading contract (ABI v1)

* One instance is processed by **one audio thread at a time**.
* Concurrent `process_stage_a` / `process_stage_b_fuzz` on the same instance is unsupported.
* Separate instances may process concurrently (no mutable global synthesis state).
* `create` / `destroy` / `prepare` must not run concurrently with process on that instance.
* Static query functions (`get_abi_version_*`, `get_implementation_version`, `get_license_identifier`, `result_string`, `get_capabilities`) are thread-safe.
* No locks in audio processing.
* Resource wavetable and vowel tables are immutable after load.
