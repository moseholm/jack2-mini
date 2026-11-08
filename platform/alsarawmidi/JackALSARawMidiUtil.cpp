#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

#include "JackALSARawMidiUtil.h"

void
jackmini::CreateNonBlockingPipe(int *fds)
{
    if (pipe(fds) == -1) {
        throw std::runtime_error(strerror(errno));
    }
    try {
        SetNonBlocking(fds[0]);
        SetNonBlocking(fds[1]);
    } catch (...) {
        close(fds[1]);
        close(fds[0]);
        throw;
    }
}

void
jackmini::DestroyNonBlockingPipe(int *fds)
{
    close(fds[1]);
    close(fds[0]);
}

void
jackmini::SetNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        throw std::runtime_error(strerror(errno));
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error(strerror(errno));
    }
}
