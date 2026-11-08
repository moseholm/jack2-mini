#define _GNU_SOURCE
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
extern void *__libc_malloc(size_t);
extern void __libc_free(void *);
static atomic_long n_malloc, n_free;
static void on_usr1(int sig) {
    (void)sig;
    char buf[128];
    int len = snprintf(buf, sizeof buf, "ALLOCS malloc=%ld free=%ld\n", atomic_load(&n_malloc), atomic_load(&n_free));
    if (write(2, buf, len) < 0) { }
}
__attribute__((constructor)) static void init(void) { signal(SIGURG, on_usr1); }
void *malloc(size_t n) { atomic_fetch_add(&n_malloc, 1); return __libc_malloc(n); }
void free(void *p) { if (p) atomic_fetch_add(&n_free, 1); __libc_free(p); }
