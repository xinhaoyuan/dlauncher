#ifdef __FreeBSD__
#define _WITH_GETLINE
#endif

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

struct priv_s {
    vector<string> candidates;
};

static bool
cmpString(const string &a, const string &b)
{
    return strcmp(a.c_str(), b.c_str()) < 0;
}

static void _init(dl_plugin_t self) { }

static int init_flag = 0;
static time_t cache_timestamp;
static vector<string> cache;

static void
update_cache(void) {
    time_t nts;
    time(&nts);

    if (init_flag == 1 && difftime(nts, cache_timestamp) <= 10)
        return;
    init_flag = 1;
    cache_timestamp = nts;

    cache.clear();

    char *path;
    asprintf(&path, "%s/.ssh/config", getenv("HOME"));
    FILE *ssh_config = fopen(path, "r");

    if (ssh_config) {

        char *line = NULL; size_t line_size; ssize_t gl_ret;
        while ((gl_ret = getline(&line, &line_size, ssh_config)) >= 0) {
            if (strncasecmp(line, "host", 4) == 0 &&
                (line[4] == '\t' || line[4] == ' ')) {

                // get the trim part of host alias
                int s = 4;
                int skip;
                while (true) {
                    skip = 0;
                    while (line[s] == '\t' || line[s] == ' ') ++ s;
                    int e = s;
                    while (line[e] != '\t' && line[e] != ' ' && line[e] != '\n' && line[e] != 0) {
                        // keep only exact pattern
                        if (line[e] == '!' || line[e] == '?' || line[e] == '*')
                            skip = 1;
                        ++ e;
                    }
                    char ec = line[e];

                    if (!skip && e != s) {
                        line[e] = 0;
                        cache.push_back(line + s);
                    }
                    
                    if (ec == 0 || ec == '\n') break;
                    s = e + 1;
                }
            }
        }
        if (line) free(line);
        
        fclose(ssh_config);
    }

    free(path);

    sort(cache.begin(), cache.end(), cmpString);
    vector<string>::iterator it =
        unique(cache.begin(), cache.end());
    cache.resize(distance(cache.begin(), it));
}

static int _update(dl_plugin_t self, const char *input) {
    update_cache();
    priv_s *p = (priv_s *)self->priv;
    
    vector<string> comp_prefix, comp_contain;
        
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

static int _open(dl_plugin_t self, int index, const char *input, int mode) {
    priv_s *p = (priv_s *)self->priv;
    if (index >= 0 && index < p->candidates.size())
        input = p->candidates[index].c_str();

    vector<string> args;
    // use urxvt here
    args.push_back(DEFAULT_TERM);
    args.push_back("-e");
    args.push_back("ssh");
    args.push_back(input);
    execute(args);
    return 0;
}

static dl_plugin_s _self;

static __attribute__((constructor)) void _register(void) {
    _self.priv       = new priv_s();
    _self.name       = "ssh";
    _self.priority   = 80;
    _self.item_count = 0;
    _self.item_default_sel = 0;
    _self.init       = &_init;
    _self.update     = &_update;
    _self.get_desc   = &_get_desc;
    _self.get_text   = &_get_text;
    _self.open       = &_open;
    
    register_plugin(&_self);
}

