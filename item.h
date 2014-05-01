#ifndef __DLAUNCHER_ITEM_H__
#define __DLAUNCHER_ITEM_H__

#if __cplusplus
extern "C" {
#endif

    typedef struct dl_item_s *dl_item_t;
    typedef struct dl_item_s {
        const char *name;
    } dl_item_s;
    
#if __cplusplus
}
#endif

#endif
