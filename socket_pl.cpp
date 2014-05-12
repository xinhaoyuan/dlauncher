#include "plugin.h"
#include "socket_pl.h"
#include "exec.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <vector>
#include <string>
#include <sstream>

using namespace std;

typedef struct sp_priv_s *sp_priv_t;
typedef struct sp_priv_s {
    int    conn;
    int    ts_init_flag;
    time_t last_conn_timestamp;
    char  *socket_path;
    char  *opt;

    vector<string> desc;
    vector<string> text;
    vector<int>    filter;
} sp_priv_s;

static void _init     (dl_plugin_t self);
static int  _update   (dl_plugin_t self, const char *input);
static int  _get_desc (dl_plugin_t self, unsigned int index, const char **output_ptr);
static int  _get_text (dl_plugin_t self, unsigned int index, const char **output_ptr);
static int  _open     (dl_plugin_t self, unsigned int index, int mode);

static char *_get_opt(const char *opt, const char *name) {
    int name_len = strlen(name);
    const char *start = opt;
    
    while (true) {
        if (!strncmp(start, name, name_len) && start[name_len] == '=') break;
        while (*start && *start != ':') {
            if (*start == '\\' && start[1] == ':') ++ start;
            ++ start;
        }
        if (*start == ':') ++ start;
        else return NULL;
    }
    
    start = start + strlen(name) + 1;
    const char *end = start;
    while (*end && *end != ':') {
        if (*end == '\\' && end[1] == ':') ++ end;
        ++ end;
    }
    return strndup(start, end - start);
}

int
socket_plugin_register(const char *name, const char *socket_path, const char *opt) {
    sp_priv_t p = (sp_priv_t)malloc(sizeof(sp_priv_s));
    if (!p) return -1;

    dl_plugin_t plugin = (dl_plugin_t)malloc(sizeof(dl_plugin_s));
    if (!plugin) {
        free(p);
        return -1;
    }

    p->socket_path = strdup(socket_path);
    p->opt = strdup(opt);
    plugin->name = strdup(name);
    char *pri = _get_opt(opt, "PRIORITY");
    plugin->priority = pri ? atoi(pri) : 0;
    if (pri) free(pri);

    if (!p->socket_path || !p->opt || !plugin->name) {
        free(p->socket_path);
        free(p->opt);
        free(p);
        free((void *)plugin->name);
        free(plugin);
        return -1;
    }

    plugin->priv = p;
    plugin->item_count = 0;
    plugin->item_default_sel = 0;
    
    plugin->init     = &_init;
    plugin->update   = &_update;
    plugin->get_desc = &_get_desc;
    plugin->get_text = &_get_text;
    plugin->open     = &_open;

    register_plugin(plugin);
}

void
_init(dl_plugin_t self) {
    sp_priv_t p = (sp_priv_t)self->priv;
    p->conn         = 0;
    p->ts_init_flag = 0;
}

static int
_connect(sp_priv_t p) {
    if (p->conn >= 0) return p->conn;

    struct sockaddr_un sa;
    int len;
    
    if ((p->conn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        goto err;
    }

    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, p->socket_path);
    len = strlen(sa.sun_path) + sizeof(sa.sun_family);

    if (connect(p->conn, (struct sockaddr *)&sa, len) == -1) {
        close(p->conn);
        goto err;
    }

    return p->conn;
    
  err:
    char *rcmd = _get_opt(p->opt, "RESTART_CMD");
    time_t ts; time(&ts);
    if (rcmd &&
        (p->ts_init_flag == 0 ||
         difftime(ts, p->last_conn_timestamp) > 3)) {
        p->ts_init_flag = 1;
        p->last_conn_timestamp = ts;
        fprintf(stderr, "restart using %s\n", rcmd);
        
        vector<string> cmd;
        cmd.push_back("sh");
        cmd.push_back("-c");
        cmd.push_back(rcmd);
        execute(cmd);
        
        free(rcmd);
    }
    return p->conn = -1;
}

static void
_disconnect(sp_priv_t p) {
    if (p->conn < 0) return;
    close(p->conn);
    p->conn = -1;
}

static void
_update_cache(sp_priv_t p, const char *input) {
#define CLEAR do { p->desc.clear(); p->text.clear(); p->filter.clear(); } while (0)
    
    int s = _connect(p);
    if (s < 0) {
        CLEAR;
        return;
    }

    // fprintf(stderr, "connected\n");

    // send query prefix
    if (send(s, "\0", 1, 0) != 1) {
        CLEAR;
        _disconnect(p);
        return;
    }

    // send input
    const char *cur = input;
    const char *end = input + strlen(input);
    while (cur != end) {
        ssize_t r = send(s, cur, end - cur, 0);
        // fprintf(stderr, "send %d %d\n", r, end-cur);
        if (r > 0) cur += r;
        else {
            CLEAR;
            _disconnect(p);
            return;
        }
    }
    
    // send new line
    if (send(s, "\n", 1, 0) != 1) {
        CLEAR;
        _disconnect(p);
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
            CLEAR;
            _disconnect(p);
            return;
        } else if (r == 0)
            break;
        
        if (buf[r - 1] == '\0') {
            is.write(buf, r - 1); break;
        } else is.write(buf, r);
    }

    // fprintf(stderr, "recv\n");

    string line;
    int pos = -1;
    string _desc;
    while (getline(is, line)) {
        int i = line.length();
        while (i > 0 && (line[i - 1] == '\n' || line[i - 1] == '\r')) -- i;
        line = string(line, 0, i);
        if (line.length() == 0) continue;

        if (pos == -1) {
            if (line == "filter") {
                p->filter.clear();
                for (int i = 0; i < p->text.size(); ++ i) {
                    if (!strncmp(p->text[i].c_str(), input, strlen(input)))
                        p->filter.push_back(i);
                }
                break;
            } else if (line == "clear") {
                CLEAR;
                pos = 0;
            } else break;
        } else if (pos == 0) {
            _desc = line;
            pos   = 1;
        } else {
            p->desc.push_back(_desc);
            p->text.push_back(line);
            p->filter.push_back(p->filter.size());
            pos  = 0;
        }
    }
    
    return;
#undef CLEAR
}

static void
_send_cmd(sp_priv_t p, const char *cmd) {
    int s = _connect(p);
    if (s < 0) {
        return;
    }

    // send command prefix
    if (send(s, "\1", 1, 0) != 1) {
        _disconnect(p);
        return;
    }

    // send input
    const char *cur = cmd;
    const char *end = cmd + strlen(cmd);
    while (cur != end) {
        ssize_t r = send(s, cur, end - cur, 0);
        // fprintf(stderr, "send %d %d\n", r, end-cur);
        if (r > 0) cur += r;
        else {
            _disconnect(p);
            return;
        }
    }
    
    // send new line
    if (send(s, "\n", 1, 0) != 1) {
        _disconnect(p);
        return;
    }

    // fprintf(stderr, "sent\n");
    return;
}

int
_update(dl_plugin_t self, const char *input) {
    sp_priv_t p = (sp_priv_t)self->priv;
    _update_cache(p, input);
    self->item_count = p->filter.size();
    self->item_default_sel = self->item_count > 0 ? 0 : -1;
    return 0;
}

int
_get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    sp_priv_t p = (sp_priv_t)self->priv;
    *output_ptr = p->desc[p->filter[index]].c_str();
    return 0;
}

int
_get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    sp_priv_t p = (sp_priv_t)self->priv;
    *output_ptr = p->text[p->filter[index]].c_str();
    return 0;
}

int
_open(dl_plugin_t self, unsigned int index, int mode) {
    sp_priv_t p = (sp_priv_t)self->priv;
    _send_cmd(p, p->text[p->filter[index]].c_str());
}
