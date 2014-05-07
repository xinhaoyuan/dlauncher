#include "plugin.h"
#include "dirlist.hpp"
#include "exec.h"

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

static int _update(dl_plugin_t self, const char *input) {
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

static int _open(dl_plugin_t self, unsigned int index) {
    string *p = (string *)self->priv;
    vector<string> args;
    // use urxvt here
    args.push_back("urxvt");
    args.push_back("-e");
    args.push_back("sh");
    args.push_back("-c");
    args.push_back(*p);
    execute(args);
    return 0;
}

static dl_plugin_s _self;

static __attribute__((constructor)) void _init(void) {
    _self.priv       = new string();
    _self.name       = "shell";
    _self.priority   = 10;
    _self.item_count = 0;
    _self.item_default_sel = 0;
    _self.update     = &_update;
    _self.get_desc   = &_get_desc;
    _self.get_text   = &_get_text;
    _self.open       = &_open;
    
    register_plugin(&_self);
}
