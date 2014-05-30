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
#include <fcntl.h>

#define PL_TYPE_EXEC 0
#define PL_TYPE_SOCK 1

#define NDEBUG

#ifndef NDEBUG
#define DEBUG(x ...) x
#else
#define DEBUG(x ...)
#endif

typedef struct ep_priv_s *ep_priv_t;
typedef struct ep_priv_s {
    int    type;
    char  *entry;
    char  *opt;
    char  *retry_cmd;
    int    retry_delay;

    /* ready for next query */
    int    ready;
    /* for exec */
    int    stdin_fd;
    int    stdout_fd;
    /* for sock */
    int    conn;
    
    int    ts_init_flag;
    time_t last_retry_timestamp;

    int    async;
    
    int    item_alloc;
    int    item_count;
    int    filter_count;
    int   *desc;
    int   *text;
    int   *filter;

    char  *recv_buf;
    int    rb_alloc;
    int    rb_size;
    int    rb_stamp;
} ep_priv_s;

static void _init     (dl_plugin_t self);
static int  _query    (dl_plugin_t self, const char *input);
static int  _before_update (dl_plugin_t self);
static int  _update   (dl_plugin_t self);
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
    if (!type || !strcmp(type, "EXEC")) {
        p->type  = PL_TYPE_EXEC;
        p->entry = strdup(entry);
    } else if (!strcmp(type, "UNIXSOCK")) {
        p->type  = PL_TYPE_SOCK;
        p->entry = strdup(entry);
    } else {
        fprintf(stderr, "unknown type of external plugin\n");
        return -1;
    }
    if (type) free(type);
    
    p->opt       = strdup(opt);
    p->retry_cmd = _get_opt(opt, "RETRY_CMD");
    plugin->name = strdup(name);
    char *pri = _get_opt(opt, "PRIORITY");
    plugin->priority = pri ? atoi(pri) : 0;
    free(pri);
    
    char *hist = _get_opt(opt, "HIST");
    plugin->hist = hist && *hist;
    free(hist);

    char *async = _get_opt(opt, "ASYNC");
    p->async = async && *async;
    free(async);
    
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
    
    plugin->init     = &_init;
    plugin->query    = &_query;
    plugin->before_update = &_before_update;
    plugin->update   = &_update;
    plugin->get_desc = &_get_desc;
    plugin->get_text = &_get_text;
    plugin->open     = &_open;

    return register_plugin(plugin);
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
            p->ready = 1;
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

        p->ready = 1;
        return 0;
    
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
        int r = read(p->stdout_fd, buf, size);
        if (r == -1) return -errno;
        else return r;
    } else if (p->type == PL_TYPE_SOCK) {
        int r = recv(p->conn, buf, size, 0);
        if (r == -1) return -errno;
        else return r;
    } else return -1;
}

static ssize_t
_write(ep_priv_t p, const void *buf, size_t size) {
    if (p->type == PL_TYPE_EXEC) {
        int r = write(p->stdin_fd, buf, size);
        if (r == -1) return -errno;
        else return r;
    } else if (p->type == PL_TYPE_SOCK) {
        int r = send(p->conn, buf, size, 0);
        if (r == -1) return -errno;
        else return r;
    } else return -1;
}

static int
_setnonblocking(ep_priv_t p, int nonblocking) {
    if (p->type == PL_TYPE_SOCK) {
        int flag = fcntl(p->conn, F_GETFL);
        if (nonblocking)
            flag |= O_NONBLOCK;
        else flag &= ~O_NONBLOCK;
        fcntl(p->conn, F_SETFL, flag);
        return 0;
    } else if (p->type == PL_TYPE_EXEC) {
        int flag = fcntl(p->stdout_fd, F_GETFL);
        if (nonblocking)
            flag |= O_NONBLOCK;
        else flag &= ~O_NONBLOCK;
        fcntl(p->stdout_fd, F_SETFL, flag);
        return 0;
    } else return -1;
}

static int
_register_fd(dl_plugin_t self, ep_priv_t p) {
    if (p->type == PL_TYPE_SOCK) {
        _setnonblocking(p, 1);
        register_update_fd(self, p->conn, DL_FD_EVENT_READ | DL_FD_EVENT_STATUS);
        return 0;
    } else if (p->type == PL_TYPE_EXEC) {
        _setnonblocking(p, 1);
        register_update_fd(self, p->stdout_fd, DL_FD_EVENT_READ | DL_FD_EVENT_STATUS);
        return 0;
    } else return -1;
}

void
_init(dl_plugin_t self) {
    ep_priv_t p = (ep_priv_t)self->priv;

    p->conn      = -1;
    p->stdin_fd  = -1;
    p->stdout_fd = -1;

    p->ts_init_flag = 0;

    p->ready        = 1;
    
    p->item_alloc   = 0;
    p->item_count   = 0;
    p->filter_count = 0;
    p->desc         = NULL;
    p->text         = NULL;
    p->filter       = NULL;

    p->recv_buf     = NULL;
    p->rb_alloc     = 0;
    p->rb_size      = 0;
    p->rb_stamp     = 0;
    
    _connect(p);
}

#define CLEAR do { free(p->desc); free(p->text); free(p->filter); free(p->recv_buf); \
        p->desc = NULL; p->text = NULL; p->filter = NULL; p->recv_buf = NULL; \
        p->item_alloc = p->item_count = p->filter_count = p->rb_alloc = 0; } while (0)

int
_update_cache(ep_priv_t p) {
    if (p->ready) return 0;
    
    /* create recv buf */
    if (!p->recv_buf) {
        p->recv_buf = (char *)malloc(1024);
        if (!p->recv_buf) return -1;
        p->rb_alloc = 1024;
        p->rb_size = 0;
        p->rb_stamp = 0;
    }

    DEBUG(fprintf(stderr, "uc: recv\n"));

    /* read as much data as possible */
    char buf[1024];
    while (1) {
        ssize_t r = _read(p, buf, sizeof(buf));
        /* fprintf(stderr, "recv %d [%s]\n", r, string(buf, r).c_str()); */
        if (r < 0) {
            if (r == -EAGAIN || r == -EWOULDBLOCK) {
                break;
            } else {
                CLEAR;
                _reset_for_retry(p);
                return -1;
            }
        } else if (r == 0) {
            CLEAR;
            _reset_for_retry(p);
            return -1;
        }

        while (p->rb_alloc < p->rb_size + r) {
            p->recv_buf = (char *)realloc(p->recv_buf, p->rb_alloc << 1);
            if (p->recv_buf == NULL) {
                CLEAR;
                _reset_for_retry(p);
                return -1;
            } else p->rb_alloc <<= 1;
        }

        memcpy(p->recv_buf + p->rb_size, buf, r);
        p->rb_size += r;
        
        if (buf[r - 1] == '\0') {
            break;
        }
    }

    /* create item and filter space */
    if (p->item_alloc == 0) {
        p->text   = malloc(sizeof(int) * 16);
        p->desc   = malloc(sizeof(int) * 16);
        p->filter = malloc(sizeof(int) * 16);

        if (!p->text || !p->desc || !p->filter) {
            free(p->text); p->text = NULL;
            free(p->desc); p->desc = NULL;
            free(p->filter); p->filter = NULL;
            return -1;
        }

        p->item_alloc = 16;
        p->item_count = p->filter_count = 0;
        
    }

    DEBUG(fprintf(stderr, "uc: parse %d %d\n", p->rb_stamp, p->rb_size));

    char *f = NULL, *s = NULL;
    char *c;
    for (c = p->recv_buf + p->rb_stamp; c < p->recv_buf + p->rb_size; ++ c) {
        if (*c == '\n') {
            if (!f) {
                f = c + 1;
            } else {
                /* change newline to null */
                *(f - 1) = 0;
                *c = 0;

                /* find two lines, add item */
                s = f;
                f = p->recv_buf + p->rb_stamp;
                
                DEBUG(fprintf(stderr, "find lines:\n%s\n%s\n", f, s));

                while (p->item_alloc <= p->item_count) {
                    p->text   = realloc(p->text, sizeof(int) * (p->item_alloc << 1));
                    p->desc   = realloc(p->desc, sizeof(int) * (p->item_alloc << 1));
                    p->filter = realloc(p->filter, sizeof(int) * (p->item_alloc << 1));

                    if (!p->text || !p->desc || !p->filter) {
                        free(p->text); p->text = NULL;
                        free(p->desc); p->desc = NULL;
                        free(p->filter); p->filter = NULL;
                        p->item_alloc = 0;
                        return -1;
                    }

                    p->item_alloc <<= 1;
                }

                int id = p->item_count ++;
                ++ p->filter_count;
                
                p->desc[id]   = f - p->recv_buf;
                p->text[id]   = s - p->recv_buf;
                p->filter[id] = id;
                
                f = s = NULL;
                p->rb_stamp = c - p->recv_buf + 1;
            }
        } else if (*c == 0) {
            p->ready = 1;
            return 0;
        }
    }

    return 0;
}

static void
_new_query(ep_priv_t p, const char *input) {
    if (_connect(p) != 0) {
        CLEAR;
        _reset_for_retry(p);
        return;
    }

    DEBUG(fprintf(stderr, "connected\n"));
    _setnonblocking(p, 0);

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

    DEBUG(fprintf(stderr, "sent %d\n", p->ready));

    while (!p->ready) {
        if (_update_cache(p)) {
            CLEAR;
            _reset_for_retry(p);
            return;
        }
    }

    /* start new query */
    p->ready = 0;

    DEBUG(fprintf(stderr, "recv\n"));
    
    // recv reply
    char reply;
    if (_read(p, &reply, 1) != 1) {
        CLEAR;
        _reset_for_retry(p);
        return;
    }

    DEBUG(fprintf(stderr, "reply %c\n", reply));

    if (reply == 'f') {
        // reuse the old candidates
        int i, input_len = strlen(input);
        p->filter_count = 0;
        for (i = 0; i < p->item_count; ++ i) {
            if (!strncmp(p->recv_buf + p->text[i], input, input_len))
                p->filter[p->filter_count ++] = i;
        }
        p->ready = 1;
        return;
    } else if (reply == 'c') {
        CLEAR;
        /* rebuild the candidates */
        if (!p->async) {
            DEBUG(fprintf(stderr, "sync recving data\n"));
            /* sync building */
            while (!p->ready) {
                if (_update_cache(p)) {
                    CLEAR;
                    _reset_for_retry(p);
                    return;
                }
            }
        }
        return;
    } else {
        /* invalid reply */
        CLEAR;
        p->ready = 1;
        _reset_for_retry(p);
        return;
    }
}

static void
_send_cmd(ep_priv_t p, const char *cmd, int mode) {
    if (_connect(p) != 0) {
        CLEAR;
        _reset_for_retry(p);
        return;
    }

    // fprintf(stderr, "connected\n");
    _setnonblocking(p, 0);

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

    // fprintf(stderr, "cmd sent\n");
    return;
}

int
_query(dl_plugin_t self, const char *input) {
    ep_priv_t p = (ep_priv_t)self->priv;
    _new_query(p, input);
    self->item_count = p->filter_count;
    DEBUG(fprintf(stderr, "!!! %d\n", self->item_count));
    return 0;
}

int
_before_update(dl_plugin_t self) {
    ep_priv_t p = (ep_priv_t)self->priv;
    if (!p->ready && p->async) {
        DEBUG(fprintf(stderr, "add hook\n"));
        _register_fd(self, p);
    }
}

int
_update(dl_plugin_t self) {
    ep_priv_t p = (ep_priv_t)self->priv;
    if (!p->ready) {
        if (_update_cache(p)) {
            CLEAR;
            _reset_for_retry(p);
            return -1;
        } else {
            self->item_count = p->filter_count;
        }
    }
    DEBUG(fprintf(stderr, "!!! %d\n", self->item_count));
    return 0;
}

int
_get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    ep_priv_t p = (ep_priv_t)self->priv;
    if (index < 0 || index >= p->filter_count) {
        *output_ptr = "";
        return -1;
    } else {
        *output_ptr = p->recv_buf + p->desc[p->filter[index]];
        return 0;
    }
}

int
_get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    ep_priv_t p = (ep_priv_t)self->priv;
    if (index < 0 || index >= p->filter_count) {
        *output_ptr = "";
        return -1;
    } else {
        *output_ptr = p->recv_buf + p->text[p->filter[index]];
        return 0;
    }
}

int
_open(dl_plugin_t self, int index, const char *input, int mode) {
    ep_priv_t p = (ep_priv_t)self->priv;
    if (index >= 0 && index < p->filter_count)
        _send_cmd(p, p->recv_buf + p->text[p->filter[index]], mode);
    else _send_cmd(p, input, mode);
}
