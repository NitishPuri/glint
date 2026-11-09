// The one translation unit that compiles VMA's implementation (vk_mem_alloc.h is header-only: every other file
// includes it for declarations only). Built in its own target without Glint's warning flags.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
