// Counts malloc and free calls in a process. Load it with LD_PRELOAD and send SIGURG to print the counts to stderr.
#include <unistd.h>
#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>

extern "C" void* __libc_malloc(std::size_t);
extern "C" void __libc_free(void*);

namespace {

std::atomic<std::int64_t> n_malloc { 0 };
std::atomic<std::int64_t> n_free { 0 };

} // end of anonymous namespace

extern "C" void on_usr1(std::int32_t sig)
{
    (void)sig;
    char buf[128];
    const std::int32_t len = std::snprintf(buf, sizeof buf, "ALLOCS malloc=%ld free=%ld\n", n_malloc.load(), n_free.load());
    if (write(2, buf, len) < 0) {
    }
}

__attribute__((constructor)) static void init()
{
    std::signal(SIGURG, on_usr1);
}

extern "C" void* malloc(std::size_t n) noexcept
{
    n_malloc.fetch_add(1);
    return __libc_malloc(n);
}

extern "C" void free(void* p) noexcept
{
    if (p) {
        n_free.fetch_add(1);
    }
    __libc_free(p);
}
