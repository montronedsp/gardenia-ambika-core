/*
  gardenia_core_version.h — ABI and implementation version (plain C).
*/
#ifndef GARDENIA_CORE_VERSION_H
#define GARDENIA_CORE_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define GARDENIA_CORE_ABI_VERSION_MAJOR 1u
#define GARDENIA_CORE_ABI_VERSION_MINOR 0u
#define GARDENIA_CORE_ABI_VERSION_PATCH 0u

#define GARDENIA_CORE_ABI_VERSION \
    ((GARDENIA_CORE_ABI_VERSION_MAJOR << 16) | \
     (GARDENIA_CORE_ABI_VERSION_MINOR << 8)  | \
     (GARDENIA_CORE_ABI_VERSION_PATCH))

/* Product/implementation version string (not ABI compatibility). */
#define GARDENIA_CORE_IMPLEMENTATION_VERSION "0.1.0-phase4"

#define GARDENIA_CORE_LICENSE_IDENTIFIER "GPL-3.0-or-later"

#ifdef __cplusplus
}
#endif

#endif /* GARDENIA_CORE_VERSION_H */
