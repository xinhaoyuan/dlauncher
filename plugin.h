#ifndef __DLAUNCHER_PLUGIN_H__
#define __DLAUNCHER_PLUGIN_H__

#if __cplusplus
extern "C" {
#endif

    /* plugin interface */
    typedef struct dl_plugin_s *dl_plugin_t;
    typedef struct dl_plugin_s {
        /* initially provided once by the plugin */
        const char *name;   /*  */
        int priority;       /* priority in the combined result list */
        int hist;           /* whether the action to this plugin should be remembered in history */

        /* write once by dlauncher */
        int id;             /* unique id in runtime */

        /* write by the plugin */
        unsigned int item_count; /* number of result record from last query */
        void *priv;            /* opaque private data of the plugin */

        void (*init)     (dl_plugin_t self);
        
        /* called when there are some input */
        int  (*query)    (dl_plugin_t self, const char *input);
        /* called before every main update loop */
        int  (*before_update) (dl_plugin_t self);
        /* called when some events associated with this plugin happened */
        int  (*update)   (dl_plugin_t self);

        /* access the content of record */
        int  (*get_desc) (dl_plugin_t self, unsigned int index, const char **output_ptr);
        int  (*get_text) (dl_plugin_t self, unsigned int index, const char **output_ptr);

        /* submit the action, mode is set according to key modifiers (current only SHIFT is considered) */
        int  (*open)     (dl_plugin_t self, int index, const char *input, int mode);
    } dl_plugin_s;

    /* implemented in dlauncher.c */
    int register_plugin(dl_plugin_t plugin);
    
    #define DL_FD_EVENT_READ   1
    #define DL_FD_EVENT_WRITE  2
    #define DL_FD_EVENT_STATUS 4

    /* monitor a file descriptor, once a event occurs, update() will be called */
    /* return - a monitor id, further cancelling is in plan */
    int register_update_fd(dl_plugin_t plugin, int fd, int event);
    
    /* implemented in plugin.c */
    int external_plugin_create(const char *name, const char *entry, const char *opt);
    
#if __cplusplus
}
#endif

#endif
