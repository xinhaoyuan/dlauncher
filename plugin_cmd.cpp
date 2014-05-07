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

struct priv_s {
    vector<string> candidates;
};

static bool
cmpString(const string &a, const string &b)
{
    return strcmp(a.c_str(), b.c_str()) < 0;
}

static int _update(dl_plugin_t self, const char *input) {
    priv_s *p = (priv_s *)self->priv;
    
    char *path = strdup(getenv("PATH"));
    char *dir = path, *nextdir;
    vector<string> comp_all, comp_prefix, comp_contain;
    vector<string> comp;
        
    while (dir != NULL)
    {
        nextdir = strchr(dir, ':');
        if (nextdir)
        {
            *(nextdir ++) = 0;
        }

        int r = dirlist(dir, comp, "/tmp/dircache_");
        if (r == 0)
        {
            for (int i = 0; i < comp.size(); ++ i) {
                ostringstream oss;
                oss << dir << "/" << comp[i];
                string filename = oss.str();
                struct stat statbuf;
                if (stat(filename.c_str(), &statbuf)) continue;
                if (!S_ISREG(statbuf.st_mode)) continue;
                if (!(statbuf.st_mode & 0111)) continue;
                // a regular and executable item now

                size_t idx = comp[i].find(input);
                if (idx == 0)
                    comp_prefix.push_back(comp[i]);
                else if (idx != string::npos)
                    comp_contain.push_back(comp[i]);
            }
        }

        dir = nextdir;
    }        

    sort(comp_prefix.begin(), comp_prefix.end(), cmpString);
    sort(comp_contain.begin(), comp_contain.end(), cmpString);

    vector<string>::iterator it = unique(comp_prefix.begin(), comp_prefix.end());
    comp_prefix.resize(distance(comp_prefix.begin(), it));
    it = unique(comp_contain.begin(), comp_contain.end());
    comp_contain.resize(distance(comp_contain.begin(), it));


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

static int _get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    priv_s *p = (priv_s *)self->priv;
    *output_ptr = p->candidates[index].c_str();
    return 0;
}

static int _open(dl_plugin_t self, unsigned int index) {
    priv_s *p = (priv_s *)self->priv;
    vector<string> args;
    args.push_back(p->candidates[index]);
    execute(args);
    return 0;
}

dl_plugin_s _self;

static __attribute__((constructor)) void _dummy_init(void) {
    _self.priv       = new priv_s();
    _self.name       = "cmd";
    _self.item_count = 0;
    _self.item_default_sel = 0;
    _self.update     = &_update;
    _self.get_text   = &_get_text;
    _self.open       = &_open;
    
    register_plugin(&_self);
}