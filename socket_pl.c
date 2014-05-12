#include "plugin.h"
#include "socket_pl.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

typedef struct sp_priv_s *sp_priv_t;
typedef struct sp_priv_s {
    int    conn;
    int    ts_init_flag;
    time_t last_conn_timestamp;
    char  *socket_path;
    char  *opt;

    char  *recv_buf;
    int    item_count;
    char **desc;
    char **text;
    int    filter_count;
    int   *filter;
} sp_priv_s;

static void _init     (dl_plugin_t self);
static int  _update   (dl_plugin_t self, const char *input);
static int  _get_desc (dl_plugin_t self, unsigned int index, const char **output_ptr);
static int  _get_text (dl_plugin_t self, unsigned int index, const char **output_ptr);
static int  _open     (dl_plugin_t self, int index, const char *input, int mode);

static char *_get_opt(const char *opt, const char *name) {
    int name_len = strlen(name);
    const char *start = opt;
    
    while (1) {
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

static int
_connect(sp_priv_t p) {
    if (p->conn >= 0) return p->conn;

    struct sockaddr_un sa;
    int len;
    char *rcmd;
    time_t ts;
    
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
    rcmd = _get_opt(p->opt, "RESTART_CMD");
    time(&ts);
    if (rcmd &&
        (p->ts_init_flag == 0 ||
         difftime(ts, p->last_conn_timestamp) > 3)) {
        p->ts_init_flag = 1;
        p->last_conn_timestamp = ts;
        // fprintf(stderr, "restart using %s\n", rcmd);

        char *cmd[] = { "sh", "-c", rcmd, NULL };
        fork_and_exec(cmd, -1, -1, STDERR_FILENO);
        free(rcmd);
    }
    return p->conn = -1;
}

void
_init(dl_plugin_t self) {
    sp_priv_t p = (sp_priv_t)self->priv;
    p->conn         = -1;
    p->ts_init_flag = 0;
    p->filter_count = 0;
    p->item_count   = 0;
    p->recv_buf     = NULL;
    p->desc         = NULL;
    p->text         = NULL;
    p->filter       = NULL;

    _connect(p);
}

static void
_disconnect(sp_priv_t p) {
    if (p->conn < 0) return;
    close(p->conn);
    p->conn = -1;
}

static void
_update_cache(sp_priv_t p, const char *input) {
#define CLEAR do { free(p->recv_buf); free(p->desc); free(p->text); free(p->filter); \
        p->recv_buf = NULL; p->desc = NULL; p->text = NULL; p->filter = NULL; \
        p->item_count = p->filter_count = 0; } while (0)
    
    int s = _connect(p);
    if (s < 0) {
        CLEAR;
        return;
    }

    // fprintf(stderr, "connected\n");

    // send query prefix
    if (send(s, "q", 1, 0) != 1) {
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
    unsigned int _recv_buf_size = 1024;
    unsigned int _recv_buf_ptr  = 0;
    char *_recv_buf = (char *)malloc(sizeof(char) * _recv_buf_size);

    if (_recv_buf == NULL) {
        CLEAR;
        _disconnect(p);
    }

    char buf[1024];
    while (1) {
        ssize_t r = recv(s, buf, sizeof buf, 0);
        // fprintf(stderr, "recv %d [%s]\n", r, string(buf, r).c_str());
        if (r < 0) {
            CLEAR;
            _disconnect(p);
            free(_recv_buf);
            return;
        } else if (r == 0)
            break;

        while (_recv_buf_size < _recv_buf_ptr + r) {
            _recv_buf = (char *)realloc(_recv_buf, _recv_buf_size << 1);
            if (_recv_buf == NULL) {
                CLEAR;
                _disconnect(p);
                return;
            } else _recv_buf_size <<= 1;
        }

        memcpy(_recv_buf + _recv_buf_ptr, buf, r);
        _recv_buf_ptr += r;
        
        if (buf[r - 1] == '\0') break;
    }

    // fprintf(stderr, "recv\n");

    char *line_start = _recv_buf;
    char *line_end = line_start;
    
    int  pos = -1;
    char line_end_c = -1;
    for (line_start = line_end = _recv_buf; line_end_c; line_start = (++ line_end)) {
        // find current line
        while (*line_end && *line_end != '\n') ++ line_end;
        line_end_c = *line_end;
        *line_end = 0;
        if (line_start == line_end) continue;

        if (pos == -1) {
            if (!strcmp(line_start, "filter")) {
                // reuse the old candidates
                int i, input_len = strlen(input);
                p->filter_count = 0;
                for (i = 0; i < p->item_count; ++ i) {
                    if (!strncmp(p->text[i], input, input_len))
                        p->filter[p->filter_count ++] = i;
                }
                free(_recv_buf);
                return;
            } else if (!strcmp(line_start, "clear")) {
                CLEAR;
                // rebuild the candidates
                pos = 0;
            } else {
                CLEAR;
                free(_recv_buf);
                return;
            }
        } else if (pos == 0) {
            pos = 1;
        } else {
            ++ p->item_count;
            pos = 0;
        }
    }

    p->desc   = (char **)malloc(sizeof(char *) * p->item_count);
    p->text   = (char **)malloc(sizeof(char *) * p->item_count);
    p->filter = (int *)malloc(sizeof(int) * p->item_count);

    if (!p->desc || !p->text || !p->filter) {
        CLEAR;
        _disconnect(p);
        free(_recv_buf);
        return;
    }

    int i;
    char *ptr = _recv_buf; ptr += strlen(ptr) + 1; /* skip header */
    for (i = 0; i < p->item_count; ++ i) {
        p->desc[i] = ptr; ptr += strlen(ptr) + 1;
        p->text[i] = ptr; ptr += strlen(ptr) + 1;
        p->filter[i] = i;
    }

    p->recv_buf = _recv_buf;
    p->filter_count = p->item_count;
#undef CLEAR
}

static void
_send_cmd(sp_priv_t p, const char *cmd, int mode) {
    int s = _connect(p);
    if (s < 0) {
        return;
    }

    // send command prefix
    if (send(s, mode ? "O" : "o", 1, 0) != 1) {
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
    self->item_count = p->filter_count;
    self->item_default_sel = -1;
    return 0;
}

int
_get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    sp_priv_t p = (sp_priv_t)self->priv;
    *output_ptr = p->desc[p->filter[index]];
    return 0;
}

int
_get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    sp_priv_t p = (sp_priv_t)self->priv;
    *output_ptr = p->text[p->filter[index]];
    return 0;
}

int
_open(dl_plugin_t self, int index, const char *input, int mode) {
    sp_priv_t p = (sp_priv_t)self->priv;
    if (index >= 0 && index < p->filter_count)
        _send_cmd(p, p->text[p->filter[index]], mode);
    else _send_cmd(p, input, mode);
}
