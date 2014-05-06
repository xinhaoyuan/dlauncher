#ifndef __DLAUNCHER_PLUGIN_H__
#define __DLAUNCHER_PLUGIN_H__

#include "item.h"

#if __cplusplus
extern "C" {
#endif

    typedef struct dl_plugin_s *dl_plugin_t;
    typedef struct dl_plugin_s {
        void *priv;

        const char *name;

        unsigned int item_count;
        unsigned int item_default_sel;

        int (*update)   (dl_plugin_t self, const char *input);
        int (*get_text) (dl_plugin_t self, unsigned int index, const char **output_ptr);
        int (*open)  (dl_plugin_t self, unsigned int index);
    } dl_plugin_s;

    void register_plugin(dl_plugin_t plugin);
    
#if __cplusplus
}
#endif

#endif
