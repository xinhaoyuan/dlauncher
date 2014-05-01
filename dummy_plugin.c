#include "plugin.h"
#include <stdlib.h>
#include <stdio.h>

static int _update(dl_plugin_t self, const char *input) {
    self->item_count = 100;
    int i;
    for (i = 0; i < 100; ++ i) {
        self->item_entry[i].name = "nihao";
    }
    return 0;
}

static int _describe(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    *output_ptr = self->item_entry[index].name;
    return 0;
}

dl_plugin_s _self;

static __attribute__((constructor)) void _dummy_init(void) {
    _self.priv       = NULL;
    _self.name       = "dummy";
    _self.item_count = 0;
    _self.item_default_sel = 0;
    _self.item_entry = (dl_item_t)malloc(sizeof(dl_item_s) * 100);
    _self.update     = &_update;
    _self.describe   = &_describe;
    
    register_plugin(&_self);
}
