#ifndef __DLAUNCHER_EXEC_H__
#define __DLAUNCHER_EXEC_H__

#if __cplusplus
extern "C" {
#endif
    
int fork_and_exec(char **argv, int fd_in, int fd_out, int fd_err);

#if __cplusplus
}
#endif

#endif
