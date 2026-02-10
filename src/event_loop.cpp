#include "shnet/event_loop.h"

#include <unistd.h>

#include <array>
#include <functional>
#include <stdexcept>
#include <system_error>

#include "shnet/tcp_socket.h"

namespace shnet {
EventLoop::EventLoop() : running_{false} {
    epfd_ = epoll_create1(0);
    if (epfd_ < 0) {
        throw std::system_error(errno, std::system_category(), "epoll_create1 failed");
    }
}

EventLoop::~EventLoop() {
    stop();
    if (epfd_ != -1) {
        ::close(epfd_);
        epfd_ = -1;
    }
}

int EventLoop::addEvent(int fd, uint32_t events, void* ptr) {
    epoll_event ev{};
    ev.events = events;
    ev.data.ptr = ptr;
    int ret = ::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);
    if (ret < 0) {
        printf("epoll_ctl ADD failed for fd: %d\n", fd);
    }
    return -1;
}

int EventLoop::modEvent(int fd, uint32_t events, void* ptr) {
    epoll_event ev{};
    ev.events = events;
    ev.data.ptr = ptr;
    int ret = ::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
    if (ret < 0) {
        printf("epoll_ctl MOD failed for fd: %d\n", fd);
    }
    return -1;
}

int EventLoop::delEvent(int fd) {
    int ret = ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    if (ret < 0) {
        printf("epoll_ctl DEL failed for fd: %d\n", fd);
    }
    return ret;
}

void EventLoop::run() {
    running_ = true;

    static std::array<epoll_event, MAX_EVENTS> events;

    while (running_) {
        int nfds = epoll_wait(epfd_, events.data(), MAX_EVENTS, 100);

        if (nfds == -1) {
            if (errno == EINTR) {
                continue;
            }
            printf("epoll_wait failed\n");
            continue;
        }

        for (int i = 0; i < nfds; ++i) {
            auto handler = static_cast<EventHandler*>(events[i].data.ptr);
            (*handler)(events[i].events);
        }
    }
}

void EventLoop::stop() { running_ = false; }
}