#ifndef __DLAUNCHER_EXEC_HPP__
#define __DLAUNCHER_EXEC_HPP__

#include <vector>
#include <string>

int execute(const std::vector<std::string> &args);
int execute_and_gather(char **argv, const std::string &input, std::string &output);
int execute_and_gather(const std::vector<std::string> &args, const std::string &input, std::string &output);

#endif
