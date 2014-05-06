#include "plugin.h"
#include <stdlib.h>
#include <stdio.h>

static int _update(dl_plugin_t self, const char *input) {
    self->item_count = 100;
    return 0;
}

static int _get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    *output_ptr = "hello text";
    return 0;
}

static int _open(dl_plugin_t self, unsigned int index) {
    return 0;
}

dl_plugin_s _self;

static __attribute__((constructor)) void _dummy_init(void) {
    _self.priv       = NULL;
    _self.name       = "dummy";
    _self.item_count = 0;
    _self.item_default_sel = 0;
    _self.update     = &_update;
    _self.get_text   = &_get_text;
    _self.open       = &_open;
    
    register_plugin(&_self);
}
