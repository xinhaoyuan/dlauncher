#ifndef __DLAUNCHER_EXEC_H__
#define __DLAUNCHER_EXEC_H__

#if __cplusplus

#include <vector>
#include <string>

int execute(const std::vector<std::string> &args);
int execute_and_gather(char **argv, const std::string &input, std::string &output);
int execute_and_gather(const std::vector<std::string> &args, const std::string &input, std::string &output);

extern "C" {
#endif
    int fork_and_exec(char **argv, int fd_in, int fd_out, int fd_err);
#if __cplusplus
}
#endif

#endif
