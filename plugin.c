#include "plugin.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>

#define PL_TYPE_EXEC 0
#define PL_TYPE_SOCK 1

typedef struct ep_priv_s *ep_priv_t;
typedef struct ep_priv_s {
    int    type;
    char  *entry;
    char  *opt;
    char  *retry_cmd;
    int    retry_delay;

    /* for exec */
    int    stdin_fd;
    int    stdout_fd;
    /* for sock */
    int    conn;
    
    int    ts_init_flag;
    time_t last_retry_timestamp;
    int    item_count;
    int    filter_count;
    char  *recv_buf;
    char **desc;
    char **text;
    int   *filter;
} ep_priv_s;

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
external_plugin_create(const char *name, const char *entry, const char *opt) {
    ep_priv_t p = (ep_priv_t)malloc(sizeof(ep_priv_s));
    if (!p) return -1;

    dl_plugin_t plugin = (dl_plugin_t)malloc(sizeof(dl_plugin_s));
    if (!plugin) {
        free(p);
        return -1;
    }

    char *type = _get_opt(opt, "TYPE");
    fprintf(stderr, "type: %s\n", type);
    
    if (!type || !strcmp(type, "EXEC")) {
        p->type  = PL_TYPE_EXEC;
        p->entry = strdup(entry);
    } else if (!strcmp(type, "UNIXSOCK")) {
        p->type  = PL_TYPE_SOCK;
        p->entry = strdup(entry);
    }

    if (type) free(type);
    
    p->opt       = strdup(opt);
    p->retry_cmd = _get_opt(opt, "RETRY_CMD");
    plugin->name = strdup(name);
    char *pri = _get_opt(opt, "PRIORITY");
    plugin->priority = pri ? atoi(pri) : 0;
    if (pri) free(pri);

    char *retry_delay = _get_opt(opt, "RETRY_DELAY");
    p->retry_delay = retry_delay ? atoi(retry_delay) : 3;
    if (retry_delay) free(retry_delay);

    if (!p->entry || !p->opt || !plugin->name) {
        free(p->entry);
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
    return -1;
}

static int
_connect(ep_priv_t p) {
    if (p->type == PL_TYPE_EXEC) {
        int in_pfd[2];
        int out_pfd[2];
        int err;
        
        if (p->stdin_fd >= 0 && p->stdout_fd >= 0) return 0;
        
        if ((err = pipe2(in_pfd, O_CLOEXEC)) < 0) {
            goto onerr_nofd;
        } else if ((err = pipe2(out_pfd, O_CLOEXEC)) < 0) {
            close(in_pfd[0]);
            close(in_pfd[1]);
            goto onerr_nofd;
        } else {
            fcntl(in_pfd[0], F_SETFD, fcntl(in_pfd[0], F_GETFD) & ~FD_CLOEXEC);
            fcntl(out_pfd[1], F_SETFD, fcntl(out_pfd[1], F_GETFD) & ~FD_CLOEXEC);
            char *cmd[] = { "sh", "-c", p->entry, NULL };
            int c = fork_and_exec(cmd, in_pfd[0], out_pfd[1], STDERR_FILENO);
            close(in_pfd[0]);
            close(out_pfd[1]);
            
            if (c < 0) {
                err = c;
                goto onerr;
            }
            
            p->stdin_fd  = in_pfd[1];
            p->stdout_fd = out_pfd[0];        
            return 0;
        }
        
      onerr:
        close(in_pfd[1]);
        close(out_pfd[0]);
      onerr_nofd:
        p->stdin_fd  = -1;
        p->stdout_fd = -1;
        return err;
    } else if (p->type == PL_TYPE_SOCK) {
        if (p->conn >= 0) return 0;

        struct sockaddr_un sa;
        int len;
        char *rcmd;
        time_t ts;
    
        if ((p->conn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            goto err;
        }

        sa.sun_family = AF_UNIX;
        strcpy(sa.sun_path, p->entry);
        len = SUN_LEN(&sa);

        if (connect(p->conn, (struct sockaddr *)&sa, len) == -1) {
            close(p->conn);
            goto err;
        }

        return p->conn;
    
      err:
        return p->conn = -1;
    } else return -1;
}

static void
_reset_for_retry(ep_priv_t p) {
    if (p->type == PL_TYPE_EXEC) {
        if (p->stdin_fd >= 0)  close(p->stdin_fd); p->stdin_fd = -1;
        if (p->stdout_fd >= 0) close(p->stdout_fd); p->stdout_fd = -1;
    } else if (p->type == PL_TYPE_SOCK) {
        if (p->conn >= 0) close(p->conn); p->conn = -1;
    }

    time_t ts;
    time(&ts);
    if (p->retry_cmd &&
        (p->ts_init_flag == 0 ||
         difftime(ts, p->last_retry_timestamp) > p->retry_delay)) {
        p->ts_init_flag = 1;
        p->last_retry_timestamp = ts;

        char *cmd[] = { "sh", "-c", p->retry_cmd, NULL };
        fprintf(stderr, "retry using cmd: %s\n", p->retry_cmd);
        fork_and_exec(cmd, -1, -1, STDERR_FILENO);
    }
}

static ssize_t
_read(ep_priv_t p, void *buf, size_t size) {
    if (p->type == PL_TYPE_EXEC) {
        return read(p->stdout_fd, buf, size);
    } else if (p->type == PL_TYPE_SOCK) {
        return recv(p->conn, buf, size, 0);
    } else return -1;
}

static ssize_t
_write(ep_priv_t p, const void *buf, size_t size) {
    if (p->type == PL_TYPE_EXEC) {
        return write(p->stdin_fd, buf, size);
    } else if (p->type == PL_TYPE_SOCK) {
        return send(p->conn, buf, size, 0);
    } else return -1;
}

void
_init(dl_plugin_t self) {
    ep_priv_t p = (ep_priv_t)self->priv;

    p->conn      = -1;
    p->stdin_fd  = -1;
    p->stdout_fd = -1;

    p->ts_init_flag = 0;
    p->filter_count = 0;
    p->item_count   = 0;
    p->recv_buf     = NULL;
    p->desc         = NULL;
    p->text         = NULL;
    p->filter       = NULL;
    
    _connect(p);
}

int
_update_cache(ep_priv_t p, const char *input) {
#define CLEAR do { free(p->recv_buf); free(p->desc); free(p->text); free(p->filter); \
        p->recv_buf = NULL; p->desc = NULL; p->text = NULL; p->filter = NULL; \
        p->item_count = p->filter_count = 0; } while (0)

    if (_connect(p) != 0) {
        CLEAR;
        _reset_for_retry(p);
        return;
    }

    // send query prefix
    if (_write(p, "q", 1) != 1) {
        CLEAR;
        _reset_for_retry(p);
        return;
    }

    // send input
    const char *cur = input;
    const char *end = input + strlen(input);
    while (cur != end) {
        ssize_t r = _write(p, cur, end - cur);
        // fprintf(stderr, "send %d %d\n", r, end-cur);
        if (r > 0) cur += r;
        else {
            CLEAR;
            _reset_for_retry(p);
            return;
        }
    }
    
    // send new line
    if (_write(p, "\n", 1) != 1) {
        CLEAR;
        _reset_for_retry(p);
        return;
    }

    // fprintf(stderr, "sent\n");

    // recv output
    unsigned int _recv_buf_size = 1024;
    unsigned int _recv_buf_ptr  = 0;
    char *_recv_buf = (char *)malloc(sizeof(char) * _recv_buf_size);

    if (_recv_buf == NULL) {
        CLEAR;
        _reset_for_retry(p);
    }

    char buf[1024];
    while (1) {
        ssize_t r = _read(p, buf, sizeof(buf));
        // fprintf(stderr, "recv %d [%s]\n", r, string(buf, r).c_str());
        if (r < 0) {
            CLEAR;
            _reset_for_retry(p);
            free(_recv_buf);
            return;
        } else if (r == 0) {
            _reset_for_retry(p);
            break;
        }

        while (_recv_buf_size < _recv_buf_ptr + r) {
            _recv_buf = (char *)realloc(_recv_buf, _recv_buf_size << 1);
            if (_recv_buf == NULL) {
                CLEAR;
                _reset_for_retry(p);
                return;
            } else _recv_buf_size <<= 1;
        }

        memcpy(_recv_buf + _recv_buf_ptr, buf, r);
        _recv_buf_ptr += r;
        
        if (buf[r - 1] == '\0') break;
    }

    /* fprintf(stderr, "recv\n"); */
    /* fprintf(stderr, "%s\n", _recv_buf); */

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
        _reset_for_retry(p);
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
_send_cmd(ep_priv_t p, const char *cmd, int mode) {
    if (_connect(p) != 0) {
        _reset_for_retry(p);
        return;
    }

    // send command prefix
    if (_write(p, mode ? "O" : "o", 1) != 1) {
        _reset_for_retry(p);
        return;
    }

    // send input
    const char *cur = cmd;
    const char *end = cmd + strlen(cmd);
    while (cur != end) {
        ssize_t r = _write(p, cur, end - cur);
        // fprintf(stderr, "send %d %d\n", r, end-cur);
        if (r > 0) cur += r;
        else {
            _reset_for_retry(p);
            return;
        }
    }
    
    // send new line
    if (_write(p, "\n", 1) != 1) {
        _reset_for_retry(p);
        return;
    }

    // fprintf(stderr, "sent\n");
    return;
}

int
_update(dl_plugin_t self, const char *input) {
    ep_priv_t p = (ep_priv_t)self->priv;
    _update_cache(p, input);
    self->item_count = p->filter_count;
    self->item_default_sel = -1;
    return 0;
}

int
_get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    ep_priv_t p = (ep_priv_t)self->priv;
    *output_ptr = p->desc[p->filter[index]];
    return 0;
}

int
_get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    ep_priv_t p = (ep_priv_t)self->priv;
    *output_ptr = p->text[p->filter[index]];
    return 0;
}

int
_open(dl_plugin_t self, int index, const char *input, int mode) {
    ep_priv_t p = (ep_priv_t)self->priv;
    if (index >= 0 && index < p->filter_count)
        _send_cmd(p, p->text[p->filter[index]], mode);
    else _send_cmd(p, input, mode);
}
