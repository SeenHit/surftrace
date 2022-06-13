#ifndef CLCC_H
#define CLCC_H

#include <dlfcn.h>
#include <errno.h>

#define PERF_MAX_STACK_DEPTH 127

struct clcc_call_stack{
    unsigned long stack[PERF_MAX_STACK_DEPTH];
    int depth;
};

struct ksym {
    long addr;
    char *name;
};

static const char * clcc_funcs[] = {
    "lbc_bpf_init",
    "lbc_bpf_exit",
    "lbc_bpf_get_maps_id",
    "lbc_set_event_cb",
    "lbc_event_loop",
    "lbc_map_lookup_elem",
    "lbc_map_lookup_and_delete_elem",
    "lbc_map_delete_elem",
    "lbc_map_get_next_key",
    "lbc_get_map_types",
    "ksym_search",
};

struct clcc_struct{
    void* handle;
    int status;
    int  (*init)(int);
    void (*exit)(void);
    int  (*get_maps_id)(char* event);
    int  (*set_event_cb)(int id,
                       void (*cb)(void *ctx, int cpu, void *data, unsigned int size),
                       void (*lost)(void *ctx, int cpu, unsigned long long cnt));
    int  (*event_loop)(int id, int timeout);
    int  (*map_lookup_elem)(int id, const void *key, void *value);
    int  (*map_lookup_and_delete_elem)(int id, const void *key, void *value);
    int  (*map_delete_elem)(int id, const void *key);
    int  (*map_get_next_key)(int id, const void *key, void *next_key);
    const char* (*get_map_types)(void);
    struct ksym* (*ksym_search)(unsigned long addr);
};

inline int clcc_setup_syms(void* handle, struct clcc_struct *pclcc)
{
    void** head = (void** )&(pclcc->init);
    void* func = NULL;
    int nums = sizeof(clcc_funcs) / sizeof(const char *);
    int i = 0;

    for (i = 0; i < nums; i ++) {
        func = dlsym(handle, clcc_funcs[i]);
        if (func == NULL) {
            printf("can not find %s on so.", clcc_funcs[i]);
            return -1;
        }
        *(head ++) = func;
    }
    return 0;
}

inline struct clcc_struct* clcc_init(const char* so_path)
{
    void *handle = NULL;
    struct clcc_struct *pclcc = NULL;

    if ((handle = dlopen(so_path, RTLD_NOW)) == NULL) {
        printf("dlopen - %sn", dlerror());
        errno = -EPERM;
        goto open_failed;
    }

    pclcc = (struct clcc_struct *)malloc(sizeof(struct clcc_struct));
    if (pclcc == NULL) {
        errno = -ENOMEM;
        goto malloc_failed;
    }

    pclcc->handle = handle;
    if (clcc_setup_syms(handle, pclcc)) {
        errno = -ESRCH;
        goto setup_failed;
    }
    return pclcc;

setup_failed:
    free(pclcc);
malloc_failed:
    dlclose(handle);
open_failed:
    return NULL;
}

inline void clcc_deinit(struct clcc_struct* pclcc)
{
    void *handle = pclcc->handle;

    free(pclcc);
    dlclose(handle);
}

inline int clcc_get_call_stack(int table_id,
                               int stack_id,
                               struct clcc_call_stack *pstack,
                               struct clcc_struct *pclcc) {
    int i;
    int ret;

    ret = pclcc->map_lookup_elem(table_id, &stack_id, &(pstack->stack[0]));
    if (ret != 0) {
        printf("get stack id %d return %d\n", stack_id, ret);
        return 1;
    }

    pstack->depth = PERF_MAX_STACK_DEPTH;
    for (i = 0; i < PERF_MAX_STACK_DEPTH; i ++) {
        if (pstack->stack[i] == 0) {
            pstack->depth = i;
            break;
        }
    }
    return 0;
}

inline void clcc_print_stack(struct clcc_call_stack *pstack,
                             struct clcc_struct *pclcc){
    int i;
    struct ksym* sym;

    for (i = 0; i < pstack->depth; i ++) {
        sym = pclcc->ksym_search(pstack->stack[i]);
        printf("\t0x%lx: %s+0x%lx\n", pstack->stack[i], sym->name, pstack->stack[i] - sym->addr);
    }
}

#endif
