#include "../exec.h"
#include "exec.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/epoll.h>
#endif
#include <assert.h>

#include <sstream>

using namespace std;

#ifdef __linux__
int
execute_and_gather(char **argv, const std::string &input, std::string &output) {
    int in_pfd[2];
    int out_pfd[2];
    int ep;
    int err;
    std::ostringstream ss;
    pid_t c;

    if ((ep = epoll_create1(EPOLL_CLOEXEC)) < 0) {
        err = -errno;
        goto onerr_nofd;
    } else if ((err = pipe2(in_pfd, O_NONBLOCK | O_CLOEXEC)) < 0) {
        close(ep);
        goto onerr_nofd;
    } else if ((err = pipe2(out_pfd, O_NONBLOCK | O_CLOEXEC)) < 0) {
        close(in_pfd[0]);
        close(in_pfd[1]);
        close(ep);
        goto onerr_nofd;
    } else {
        fcntl(in_pfd[0], F_SETFD, fcntl(in_pfd[0], F_GETFD) & ~FD_CLOEXEC);
        fcntl(out_pfd[1], F_SETFD, fcntl(out_pfd[1], F_GETFD) & ~FD_CLOEXEC);
        c = fork_and_exec(argv, in_pfd[0], out_pfd[1], -1);
        close(in_pfd[0]);
        close(out_pfd[1]);

        if (c < 0) {
            err = c;
            goto onerr;
        }
        
        struct epoll_event eev;

        eev.data.fd = out_pfd[0];
        eev.events = EPOLLIN | EPOLLET;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, out_pfd[0], &eev) != 0) {
            err = -errno;
            goto onerr;
        }

        eev.data.fd = in_pfd[1];
        eev.events = EPOLLOUT | EPOLLET;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, in_pfd[1], &eev) != 0) {
            err = -errno;
            goto onerr;
        }
    
        ssize_t s;
        char buf[1024];
        size_t out_off = 0;
    
        while (true) {
            int n = epoll_wait(ep, &eev, 1, -1);
            if (n != 1) continue;
            if (eev.data.fd == out_pfd[0]) {
                if (!(eev.events & EPOLLIN)) {
                    err = 0;
                    goto succ;
                }
                // read
                while (true) {
                    s = read(out_pfd[0], buf, 1024);
                    if (s == 0) {
                        err = 0;
                        goto succ;
                    } else if (s == -1) {
                        err = -errno;
                        if (err == -EAGAIN || errno == -EINTR) break;
                        goto onerr;
                    } else if (s < 0) {
                        err = s;
                        goto onerr;
                    }
                    ss.write(buf, s);
                }
            } else if (eev.data.fd == in_pfd[1]) {
                if (!(eev.events & EPOLLOUT))
                    continue;
                // write
                while (out_off < input.length()) {
                    s = input.length() - out_off;
                    s = write(in_pfd[1], input.c_str() + out_off,
                              s > 1024 ? 1024 : s);
                    if (s <= 0) break;
                    out_off += s;
                }
                if (out_off == input.length()) {
                    close(in_pfd[1]);
                    in_pfd[1] = -1;
                }
            } else {
                assert(0);
            }
        }
    }
    
  succ:
  onerr:
    if (in_pfd[1] >= 0) close(in_pfd[1]);
    close(out_pfd[0]);
    close(ep);
  onerr_nofd:
    output = ss.str();
    return err;
}

int
execute_and_gather(const std::vector<std::string> &args, const std::string &input, std::string &output) {
    size_t buf_len = 0;
    for (int i = 0; i < args.size(); ++ i) {
        buf_len += args[i].length() + 1;
    }
    char **argv = new char *[args.size() + 1];
    char  *buf  = new char[buf_len];
    char  *cur  = buf;
    
    for (int i = 0; i < args.size(); ++ i) {
        memcpy(cur, args[i].c_str(), args[i].length());
        argv[i] = cur;
        cur += args[i].length();
        *cur = 0; ++ cur;
    }
    argv[args.size()] = NULL;

    int r = execute_and_gather(argv, input, output);
    
    delete[] buf;
    delete[] argv;
    return r;
}
#endif

int
execute(const std::vector<std::string> &args) {
    size_t buf_len = 0;
    for (int i = 0; i < args.size(); ++ i) {
        buf_len += args[i].length() + 1;
    }
    char **argv = new char *[args.size() + 1];
    char  *buf  = new char[buf_len];
    char  *cur  = buf;
    
    for (int i = 0; i < args.size(); ++ i) {
        memcpy(cur, args[i].c_str(), args[i].length());
        argv[i] = cur;
        cur += args[i].length();
        *cur = 0; ++ cur;
    }
    argv[args.size()] = NULL;

    int r = fork_and_exec(argv, -1, -1, STDERR_FILENO);
    
    delete[] buf;
    delete[] argv;
    return r;
}
