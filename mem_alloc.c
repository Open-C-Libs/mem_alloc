#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "mem_adapter.h"

#define RBTREE_BLACK   0
#define RBTREE_RED     1

#define POOL_SIZE 4096
#define POOL_NUMBER 64

#define MEM_MAP(size) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)
#define MEM_PTR(pool) (*(void **) (pool))
#define list_next(data) (((mem_list *)data)->next)
#define list_prev(data) (((mem_list *)data)->prev)



typedef struct {
    void   *side[2];
    uint8_t color;
} mem_node;

typedef struct {
    void *next;
    void *prev;
} mem_list;

typedef struct {
    void *first;
    uint16_t size;
} mem_head;

typedef struct {
    uint16_t fill;
    void *free;
} mem_alloc;


static __inline__ void mem_list_put (mem_head *header, mem_list *node) {
    if (header->size++ == 0) *node = (mem_list){node, node};
    else {
        *node = (mem_list){header->first, list_prev(header->first)};
        list_next(list_prev(header->first)) = node;
        list_prev(header->first) = node;
    }
    header->first = node;
}
static __inline__ void mem_list_take(mem_head *header, mem_list *node) {
    list_next(list_prev(node)) = list_next(node);
    list_prev(list_next(node)) = list_prev(node);
    if (--header->size == 0) header->first = NULL;
    else if (header->first == node) header->first = list_next(node);
}
static __inline__ void mem_list_spin(mem_head *header) {
    header->first = list_next(header->first);
}


typedef struct {
    mem_list  lnode;
    mem_alloc alloc;

    uint16_t size;
    void *data;
} mem_pool;

typedef struct {
    mem_list  lnode;
    mem_node  tnode;
    mem_alloc alloc;

    mem_pool pools[POOL_NUMBER];
    void *data;
} mem_page;

typedef struct {
    mem_head pages;
    mem_head pools[10];

    mem_page *root;
} mem_ctx;

mem_ctx *ctx = NULL;

static mem_page *page_parents[256];
static uint8_t   page_sides  [256];


static __inline__ uint16_t pool_size(const uint64_t size) {
    uint16_t _size = 2;
    while (size > 1ULL << ++_size) {}
    return _size;
}

static __inline__ mem_page *mem_page_find  (const void *data) {
    mem_page *page = ctx->root;

    while (page != NULL) {
        if (data >= page->data && data < page->data + POOL_NUMBER * POOL_SIZE) break;
        page = page->tnode.side[page->data < data];
    }
    return page;
}
static __inline__ void      mem_page_insert(mem_page *new_page) {
    uint8_t side = 0;
    int8_t pos = -1;
    mem_page *page = ctx->root;

    // Find Position To be places
    while (page) {
        if (new_page->data == page->data) return;
        page_parents[++pos] = page;
        side = page_sides[pos] = page->data < new_page->data;
        page = page->tnode.side[side];
    }

    if (pos == -1) ctx->root = new_page;
    else page_parents[pos]->tnode.side[side] = new_page;

    while (--pos >= 0) {
        side = page_sides[pos];
        mem_page *g_ = page_parents[pos]; // Grand Parent
        mem_page *y_ = g_->tnode.side[1 - side]; // Unlce
        mem_page *x_ = page_parents[pos + 1]; // Parent

        if (x_->tnode.color == RBTREE_BLACK) break;

        if (y_ && y_->tnode.color == RBTREE_RED) {
            x_->tnode.color = RBTREE_BLACK;
            y_->tnode.color = RBTREE_BLACK;
            g_->tnode.color = RBTREE_RED;

            --pos;
            continue;
        }

        if (side == 1 - page_sides[pos + 1]) {
            y_ = x_->tnode.side[1 - side]; // y_ is child
            x_->tnode.side[1 - side] = y_->tnode.side[side];
            y_->tnode.side[side] = x_;
            x_ = g_->tnode.side[side] = y_;
        }
        g_->tnode.color = RBTREE_RED;
        x_->tnode.color = RBTREE_BLACK;
        g_->tnode.side[side] = x_->tnode.side[1 - side];
        x_->tnode.side[1 - side] = g_;

        if (pos == 0) ctx->root = x_;
        else page_parents[pos - 1]->tnode.side[page_sides[pos - 1]] = x_;
        break;
    }

    ctx->root->tnode.color = RBTREE_BLACK;
}

static __inline__ mem_page *mem_page_init() {
    mem_page *page = MEM_MAP(sizeof(mem_page));
    if (page == NULL) return NULL;
    memset(page, 0, sizeof(mem_page));

    page->data = MEM_MAP(POOL_SIZE * POOL_NUMBER);
    page->tnode.color = RBTREE_RED;

    for (int i = 0; i < POOL_NUMBER; i++)
        page->pools[i].data = page->data + POOL_SIZE * i;


    return page;
}

static __inline__ mem_pool *mem_pool_init() {
    mem_page *page = ctx->pages.first;
    if (page == NULL || page->alloc.fill == POOL_NUMBER) {
        page = mem_page_init();
        if (page == NULL) return NULL;
        mem_page_insert(page);

        mem_list_put(&ctx->pages, &page->lnode);
    }

    mem_pool *pool = &page->pools[page->alloc.fill];
    if (page->alloc.free != NULL)
        page->alloc.free = (pool = page->alloc.free)->alloc.free;

    pool->alloc.free = NULL;
    if (++page->alloc.fill == POOL_NUMBER) mem_list_spin(&ctx->pages);
    return pool;
}
static __inline__ void      mem_pool_free(mem_page *page, mem_pool *pool) {
    pool->alloc.free = page->alloc.free;
    page->alloc.free = pool;
    page->alloc.fill--;

    mem_list_take(&ctx->pages, &page->lnode);
    mem_list_put(&ctx->pages, &page->lnode);
}

static __inline__ void mem_ctx_init() {
    ctx = MEM_MAP(sizeof(mem_ctx));
    memset(ctx, 0, sizeof(mem_ctx));
}

void *mem_malloc(const uint64_t size) {
    if (ctx == NULL) mem_ctx_init();
    const uint64_t _size = pool_size(size);
    if (_size > 12) return malloc(size);

    mem_pool *pool = ctx->pools[_size - 3].first;
    if (pool == NULL || pool->alloc.fill == POOL_SIZE) {
        pool = mem_pool_init();
        if (pool == NULL) return NULL;

        pool->size = _size;
        mem_list_put(&ctx->pools[_size - 3], &pool->lnode);
    }

    void *ptr = pool->data + pool->alloc.fill;

    if (pool->alloc.free != NULL) pool->alloc.free = MEM_PTR(ptr = pool->alloc.free);
    if ((pool->alloc.fill += 1 << pool->size) == POOL_SIZE) mem_list_spin(&ctx->pools[_size - 3]);
    return ptr;
}
void* mem_calloc(const uint64_t num, const uint64_t size) {
    if (ctx == NULL) mem_ctx_init();
    const uint64_t _size = pool_size(num * size);
    if (_size > 12) return calloc(num, size);

    mem_pool *pool = ctx->pools[_size - 3].first;
    if (pool == NULL || pool->alloc.fill == POOL_SIZE) {
        pool = mem_pool_init();
        if (pool == NULL) return NULL;

        pool->size = _size;
        mem_list_put(&ctx->pools[_size - 3], &pool->lnode);
    }

    void *ptr = pool->data + pool->alloc.fill;
    memset(ptr, 0, num * size);

    if (pool->alloc.free != NULL) pool->alloc.free = MEM_PTR(ptr = pool->alloc.free);

    if ((pool->alloc.fill += 1 << pool->size) == POOL_SIZE) mem_list_spin(&ctx->pools[_size - 3]);
    return ptr;
}
void* mem_realloc(void* ptr, const uint64_t new_size) {
    if (ctx == NULL) mem_ctx_init();
    mem_page *page = mem_page_find(ptr);
    if (page == NULL) return realloc(ptr, new_size);

    mem_pool *pool = &page->pools[((uint64_t) ptr - (uint64_t) page->data) / POOL_SIZE];
    const uint64_t _size = pool_size(new_size);

    if (_size <= pool->size) return ptr;

    void *new_ptr = _size > 12? malloc(new_size) : mem_malloc(new_size);
    memcpy(new_ptr, ptr, 1 << pool->size);

    MEM_PTR(ptr) = pool->alloc.free;
    pool->alloc.free = ptr;

    mem_list_take(&ctx->pools[pool->size - 3], &pool->lnode);

    if ((pool->alloc.fill -= 1 << pool->size) == 0) mem_pool_free(page, pool);
    else mem_list_put(&ctx->pools[pool->size - 3], &pool->lnode);

    return new_ptr;
}
void  mem_free(void *ptr) {
    if (ctx == NULL) return;
    mem_page *page = mem_page_find(ptr);
    if (page == NULL) return free(ptr);

    mem_pool *pool = &page->pools[((uint64_t) ptr - (uint64_t) page->data) / POOL_SIZE];

    MEM_PTR(ptr) = pool->alloc.free;
    pool->alloc.free = ptr;

    mem_list_take(&ctx->pools[pool->size - 3], &pool->lnode);

    if ((pool->alloc.fill -= 1 << pool->size) == 0) mem_pool_free(page, pool);
    else mem_list_put(&ctx->pools[pool->size - 3], &pool->lnode);
}
