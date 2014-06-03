#ifndef __DLAUNCHER_PLUGIN_H__
#define __DLAUNCHER_PLUGIN_H__

#if __cplusplus
extern "C" {
#endif

    typedef struct dl_plugin_s *dl_plugin_t;
    typedef struct dl_plugin_s {
        /* set by dlauncher */
        int id;
        int enabled;

        /* constant for options*/
        const char *name;
        int priority;           /* order in summary mode */
        int hist;               /* record open history(bool) */
        int input_privacy;      /* privacy model */
#define DL_INPUT_PRIVACY_PUBLIC  0
#define DL_INPUT_PRIVACY_PRIVATE 1
        int input_mask;         /* filter mask for input event */
#define DL_INPUT_EVENT_MASK_OPEN   1
#define DL_INPUT_EVENT_MASK_SELECT 2

        /* internal data pointer */
        void *priv;

        /* current number of items */
        unsigned int item_count;

        void (*init)     (dl_plugin_t self);
        int  (*query)    (dl_plugin_t self, const char *input);
        int  (*before_update) (dl_plugin_t self); /* can be null */
        int  (*update)   (dl_plugin_t self);
        int  (*get_desc) (dl_plugin_t self, unsigned int index, const char **output_ptr);
        int  (*get_text) (dl_plugin_t self, unsigned int index, const char **output_ptr);
        void (*select)   (dl_plugin_t self, int index); /* can be null if input mask for select is off */
        void (*open)     (dl_plugin_t self, int index,
                          const char *input, int mode); /* can be null if input mask for open is off */
    } dl_plugin_s;

    /* implemented in dlauncher */
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
