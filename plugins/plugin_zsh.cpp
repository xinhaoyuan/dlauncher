#include "plugin.h"
#include "exec.h"
#include "defaults.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <sstream>
#include <vector>
#include <string>

using namespace std;

static int _enabled = 0;
static char *_server;
static char *_basedir;
static char *_socket;

static vector<string> desc;
static vector<string> text;
static vector<int>    filter;
string cache_input;

static void
_start_server(void) {
    fprintf(stderr, "restart server\n");
    
    vector<string> cmd;
    cmd.push_back("zsh");
    cmd.push_back(_server);
    cmd.push_back(_basedir);
    execute(cmd);
}


static const char *
last_word(const char *line) {
    const char *result = line;
    char match = 0;
    for (const char *c = line; *c; ++ c) {
        // XXX this is hackish, may encounter unexpected effect
        if (*c == '\\' && c[1]) ++ c;
        else if (*c == ' ' && match == 0) result = c + 1;
        else if (*c == '"') {
            if (match == 0) {
                result = c + 1;
                match = '"';
            } else if (match == '"')
                match = 0;
        } else if (*c == '\'') {
            if (match == 0) {
                result = c + 1;
                match = '\'';
            } else if (match == '\'')
                match = 0;
        }
    }
    return result;
}

static void
_update_cache(const char *input) {
    cache_input = input;
    
    desc.clear();
    text.clear();
    filter.clear();
    
    struct sockaddr_un sa;
    int s;
    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        return;
    }

    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, _socket);
    int len = strlen(sa.sun_path) + sizeof(sa.sun_family);

    // fprintf(stderr, "connecting to %s\n", sa.sun_path);

    if (connect(s, (struct sockaddr *)&sa, len) == -1) {
        close(s);
        _start_server();
        return;
    }

    // fprintf(stderr, "connected\n");

    // send input
    const char *cur = input;
    const char *end = input + strlen(input);
    while (cur != end) {
        ssize_t r = send(s, cur, end - cur, 0);
        // fprintf(stderr, "send %d %d\n", r, end-cur);
        if (r > 0) cur += r;
        else {
            close(s);
            return;
        }
    }
    
    // send new line
    if (send(s, "\n", 1, 0) != 1) {
        close(s);
        return;
    }

    // fprintf(stderr, "sent\n");

    // recv output
    stringstream is;
    char buf[1024];
    while (true) {
        ssize_t r = recv(s, buf, sizeof buf, 0);
        // fprintf(stderr, "recv %d [%s]\n", r, string(buf, r).c_str());
        if (r < 0) {
            close(s);
            return;
        } else if (r == 0)
            break;
        
        if (buf[r - 1] == '\0') {
            is.write(buf, r - 1); break;
        } else is.write(buf, r);
    }

    close(s);
    
    string line;
    int lws = last_word(input) - input;
    
    while (getline(is, line)) {
        int i = line.length();
        while (i > 0 && (line[i - 1] == '\n' || line[i - 1] == '\r')) -- i;
        line = string(line, 0, i);
        if (line.length() == 0) continue;
        
        ostringstream os;
        os << string(input, lws) << line;

        desc.push_back(line);
        text.push_back(os.str());
        filter.push_back(filter.size());
    }
}

static void
_init(dl_plugin_t self) {
    const char *server  = getenv("DLAUNCHER_COMPLETION_SERVER");
    const char *basedir = getenv("DLAUNCHER_COMPLETION_BASEDIR");
    
    if (basedir == NULL) return;
    _enabled = 1;

    if (server) _server  = strdup(server);
    _basedir = strdup(basedir);
    asprintf(&_socket, "%s/socket", _basedir);

    _start_server();
}

static int
_update(dl_plugin_t self, const char *input) {
    if (_enabled == 0) {
        self->item_count = 0;
        return 0;
    }

    if (*input == 0) {
        self->item_count = 0;
        return 0;
    }

    if (cache_input.length() > 0 &&
        strncmp(input, cache_input.c_str(), cache_input.length() == 0) &&
        last_word(input) - input < cache_input.length()) {

        filter.clear();
        for (int i = 0; i < text.size(); ++ i) {
            if (strncmp(input, text[i].c_str(), text[i].length()) == 0)
                filter.push_back(i);
        }

    } else _update_cache(input);
    
    self->item_count = filter.size();
    self->item_default_sel = 0;
    return 0;
}

static int
_get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    *output_ptr = desc[filter[index]].c_str();
    return 0;
}

static int
_get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    *output_ptr = text[filter[index]].c_str();
    return 0;
}

static int
_open(dl_plugin_t self, unsigned int index, int mode) {
    vector<string> args;
    if (mode) {
        args.push_back(DEFAULT_TERM);
        args.push_back("-e");
    }

    args.push_back("zsh");
    args.push_back("-c");
    args.push_back(text[filter[index]]);
    execute(args);
    return 0;
}

static dl_plugin_s _self;

static __attribute__((constructor)) void _register(void) {
    _self.priv       = NULL;
    _self.name       = "zsh";
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
