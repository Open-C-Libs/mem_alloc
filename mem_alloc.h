#ifndef MEMORY_ADAPTER_H
#define MEMORY_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

extern void* mem_malloc(uint64_t size)
__THROW __attribute_malloc__ __attribute_alloc_size__ ((1)) __wur;
extern void* mem_calloc(uint64_t num, uint64_t size)
__THROW __attribute_malloc__ __attribute_alloc_size__ ((1, 2)) __wur;
extern void* mem_realloc(void* ptr, uint64_t new_size)
__THROW __attribute_warn_unused_result__ __attribute_alloc_size__ ((2));
extern void  mem_free(void* ptr)
__THROW;


#endif //MEMORY_ADAPTER_H
