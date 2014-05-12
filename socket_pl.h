#ifndef __DLAUNCHER_SOCKET_PL_H__
#define __DLAUNCHER_SOCKET_PL_H__

#if __cplusplus
extern "C" {
#endif

    int socket_plugin_register(const char *name, const char *socket_path, const char *opt);
    
#if __cplusplus
}
#endif

#endif
