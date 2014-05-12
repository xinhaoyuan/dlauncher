#include "exec.h"

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
#include <assert.h>

int
fork_and_exec(char **argv, int fd_in, int fd_out, int fd_err) {
    pid_t ret = fork();
    if (ret < 0) return ret;
    if (ret == 0) {
        if (fd_in != STDIN_FILENO) {
            if (fd_in < 0)
                fd_in = open("/dev/null", O_RDONLY);
            if (dup2(fd_in, STDIN_FILENO) < 0) {
                _exit(-1);
            }
            close(fd_in);
        }

        int fd_null = open("/dev/null", O_WRONLY);
        if (fd_out != STDOUT_FILENO) {
            if (fd_out < 0)
                fd_out = fd_null;
            if (dup2(fd_out, STDOUT_FILENO) < 0)
                _exit(-1);
            if (fd_out != fd_null)
                close(fd_out);
        }

        if (fd_err != STDERR_FILENO) {
            if (fd_err < 0)
                fd_err = fd_null;
            if (dup2(fd_err, STDERR_FILENO) < 0)
                _exit(-1);
            if (fd_err != fd_null)
                close(fd_err);
        }

        close(fd_null);
        execvp(argv[0], argv);
        // shall not return
        _exit(-1);
    } else {
        return ret;
    }
}
