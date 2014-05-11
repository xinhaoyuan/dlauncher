#ifdef __linux__
#define _XOPEN_SOURCE 700
#endif

#include "plugin.h"
#include "dirlist.hpp"
#include "exec.h"
#include "defaults.h"

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

struct priv_s {
    vector<string> candidates;
};

static bool
cmpString(const string &a, const string &b)
{
    return strcmp(a.c_str(), b.c_str()) < 0;
}

static int init_flag = 0;
static time_t cache_timestamp;
static vector<string> cache;

static void _init(dl_plugin_t self) { }

static int _update(dl_plugin_t self, const char *input) {
    priv_s *p = (priv_s *)self->priv;
    
    char *base_dir;
    char *dup_input = strdup(input);
    for (int i = strlen(dup_input) - 1; i >= 0; -- i) {
        if (dup_input[i] == '/') break;
        dup_input[i] = 0;
    }

    const char *home = getenv("HOME");
    int home_len = strlen(home);
    if (input[0] != '/') {
        asprintf(&base_dir, "%s/%s", home, dup_input);
        free(dup_input);
    } else base_dir = dup_input;

    int len = strlen(base_dir);
    if (len > 0 && base_dir[len - 1] == '/') {
        base_dir[len - 1] = 0;
        -- len;
    }

    vector<string> cache;
    vector<string> comp;

    int r = dirlist(base_dir[0] ? base_dir : "/", comp, "/tmp/dircache_");
    struct stat statbuf;
    if (r == 0)
    {
        for (int i = 0; i < comp.size(); ++ i) {
            ostringstream oss;
            // skip dot files
            if (comp[i].c_str()[0] == '.') continue;
            oss << base_dir << "/" << comp[i];
            string filename = oss.str();
            if (stat(filename.c_str(), &statbuf)) continue;
            if (!S_ISDIR(statbuf.st_mode)) continue;
            // only directory
            if (strncmp(filename.c_str(), home, home_len) == 0 && home_len < filename.length()) {
                // remove $HOME prefix
                cache.push_back(filename.c_str() + home_len + 1);
            } else cache.push_back(filename);
        }
    }

    free(base_dir);
    
    vector<string> comp_prefix, comp_contain;

    if (strncmp(input, home, home_len) == 0 && home_len < strlen(input))
        input += home_len + 1;

    for (int i = 0; i < cache.size(); ++ i) {
        size_t idx = cache[i].find(input);
        if (idx == 0)
            comp_prefix.push_back(cache[i]);
        else if (idx != string::npos)
            comp_contain.push_back(cache[i]);
    }

    sort(comp_prefix.begin(), comp_prefix.end(), cmpString);
    sort(comp_contain.begin(), comp_contain.end(), cmpString);

    p->candidates.clear();

    // put the suggestion as first element
    p->candidates.insert(p->candidates.end(), comp_prefix.begin(), comp_prefix.end());
    p->candidates.insert(p->candidates.end(), comp_contain.begin(), comp_contain.end());

    self->item_count = p->candidates.size();
    if (self->item_count > 0)
        self->item_default_sel = 0;
    else self->item_default_sel = -1;
    return 0;
}

static int _get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    priv_s *p = (priv_s *)self->priv;
    *output_ptr = p->candidates[index].c_str();
    return 0;
}

static int _get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    priv_s *p = (priv_s *)self->priv;
    *output_ptr = p->candidates[index].c_str();
    return 0;
}

static int _open(dl_plugin_t self, unsigned int index, int mode) {
    priv_s *p = (priv_s *)self->priv;
    vector<string> args;
    const char *path = p->candidates[index].c_str();
    if (path[0] != '/') {
        char *r;
        asprintf(&r, "%s/%s", getenv("HOME"), path);
        path = r;
    } else path = strdup(path);

    args.push_back(DEFAULT_FILE_MANAGER);
    args.push_back(path);

    free((void *)path);
    
    execute(args);
    return 0;
}

static dl_plugin_s _self;

static __attribute__((constructor)) void _register(void) {
    _self.priv       = new priv_s();
    _self.name       = "dir";
    _self.priority   = 40;
    _self.item_count = 0;
    _self.item_default_sel = 0;
    _self.init       = &_init;
    _self.update     = &_update;
    _self.get_desc   = &_get_desc;
    _self.get_text   = &_get_text;
    _self.open       = &_open;
    
    register_plugin(&_self);
}
