#ifndef __DLAUNCHER_PLUGIN_H__
#define __DLAUNCHER_PLUGIN_H__

#if __cplusplus
extern "C" {
#endif

    typedef struct dl_plugin_s *dl_plugin_t;
    typedef struct dl_plugin_s {
        void *priv;

        int id;
        int enabled;
        const char *name;
        int priority;

        unsigned int item_count;
        int          cookie;

        void (*init)     (dl_plugin_t self);
        int  (*query)    (dl_plugin_t self, const char *input);
        int  (*before_update) (dl_plugin_t self);
        int  (*update)   (dl_plugin_t self);
        int  (*get_desc) (dl_plugin_t self, unsigned int index, const char **output_ptr);
        int  (*get_text) (dl_plugin_t self, unsigned int index, const char **output_ptr);
        int  (*open)     (dl_plugin_t self, int index, const char *input, int mode);
    } dl_plugin_s;

    /* implemented in dlauncher.c */
    int register_plugin(dl_plugin_t plugin);
    #define DL_FD_EVENT_READ   1
    #define DL_FD_EVENT_WRITE  2
    #define DL_FD_EVENT_STATUS 4
    int register_update_fd(dl_plugin_t plugin, int fd, int event);
    /* implemented in plugin.c */
    int external_plugin_create(const char *name, const char *entry, const char *opt);
    
#if __cplusplus
}
#endif

#endif
