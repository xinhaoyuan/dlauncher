#ifndef __DLAUNCHER_EXEC_PL_H__
#define __DLAUNCHER_EXEC_PL_H__

#if __cplusplus
extern "C" {
#endif

    int exec_plugin_register(const char *name, const char *cmd, const char *opt);
    
#if __cplusplus
}
#endif

#endif
