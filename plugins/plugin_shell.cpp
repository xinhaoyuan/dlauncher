#include "../plugin.h"
#include "../exec.h"
#include "../defaults.h"

#include "dirlist.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

using namespace std;

static void _init(dl_plugin_t self) { }

static int _update(dl_plugin_t self, const char *input) {
    if (strchr(input, '/') == NULL &&
        strchr(input, ' ') == NULL) {
        self->item_count = 0;
        return 0;
    }
    
    string *p = (string *)self->priv;
    p->assign(input);
    self->item_count = 1;
    return 0;
}

static int _get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    string *p = (string *)self->priv;
    *output_ptr = p->c_str();
    return 0;
}

static int _get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    string *p = (string *)self->priv;
    *output_ptr = p->c_str();
    return 0;
}

static int _open(dl_plugin_t self, int index, const char *input, int mode) {
    string *p = (string *)self->priv;
    vector<string> args;

    if (index == 0) input = p->c_str();
    
    if (mode) {
        args.push_back(DEFAULT_TERM);
        args.push_back("-e");
    }
    
    args.push_back("sh");
    args.push_back("-c");
    args.push_back(input);
    
    execute(args);
    return 0;
}

static dl_plugin_s _self;

static __attribute__((constructor)) void _register(void) {
    _self.priv       = new string();
    _self.name       = "sh";
    _self.priority   = -10;
    _self.item_count = 0;
    _self.item_default_sel = 0;
    _self.init       = &_init;
    _self.update     = &_update;
    _self.get_desc   = &_get_desc;
    _self.get_text   = &_get_text;
    _self.open       = &_open;
    
    register_plugin(&_self);
}
