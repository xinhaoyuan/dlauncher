/* See LICENSE file for copyright and license details. */
#ifdef __linux__
#define _XOPEN_SOURCE 700
#define _GNU_SOURCE
#endif
#ifdef __FreeBSD__
#define _WITH_GETLINE
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include "draw.h"
#include "hist.h"
#include "plugin.h"
#include "defaults.h"

#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define MIN(a,b)              ((a) < (b) ? (a) : (b))
#define MAX(a,b)              ((a) > (b) ? (a) : (b))

static void calc_offsets(void);
static void item_sel_next(void);
static void complete_text(int update);
static char *cistrstr(const char *s, const char *sub);
static void drawmenu(void);
static void grabkeyboard(void);
static void insert(const char *str, ssize_t n);
static void keypress(XKeyEvent *ev);
static void update(int query);
static size_t nextrune(int inc);
static void paste(void);
static void run(void);
static void usage(void);
static void setup(void);
static void calc_geo(void);
static void show(void);
static void hide(void);
static void signal_show(int);

static int volatile to_show = 0;
static int volatile showed = 0;

static void hist_show_prev(void);
static void hist_show_next(void);
static void hist_rebuild_file(void);
static FILE       *hist_file;
static char       *hist_file_path;
       const char *hist_line[HIST_SIZE * 2];
       const char *hist_line_matched[HIST_SIZE * 2]; /* for plugin */
       int         hist_count;
       int         hist_index;

static void hist_plugin_init(dl_plugin_t self);
static int  hist_plugin_query(dl_plugin_t self, const char *input);
static int  hist_plugin_before_update(dl_plugin_t self);
static int  hist_plugin_get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr);
static int  hist_plugin_get_text(dl_plugin_t self, unsigned int index, const char **output_ptr);
static int  hist_plugin_open(dl_plugin_t self, int index, const char *input, int mode);

static dl_plugin_s hist_plugin = {
    .priv          = NULL,
    .name          = "hist",
    .priority      = 100,
    .init          = &hist_plugin_init,
    .query         = &hist_plugin_query,
    .before_update = &hist_plugin_before_update,
    .update        = NULL,      /* never called */
    .get_desc      = &hist_plugin_get_desc,
    .get_text      = &hist_plugin_get_text,
    .open          = &hist_plugin_open
};

static const char *prompt_empty = "DLauncher-"VERSION;
static char prompt_buf[BUFSIZ] = "";
static char text[BUFSIZ] = "";
static char text_cached[BUFSIZ] = "";
static int bh, mx, my, mw, mh;
static int inputw, promptw;
static size_t cursor = 0;
static const char *font = NULL;
static const char *prompt = NULL;
static const char *normbgcolor = "#222222";
static const char *normfgcolor = "#bbbbbb";
static const char *selbgcolor  = "#005577";
static const char *selfgcolor  = "#eeeeee";
static unsigned int lines = 0;
static ColorSet *normcol;
static ColorSet *selcol;
static Atom clip, utf8;
static Bool topbar = True;
static DC *dc;
static Window win;
static XIC xic;

#define NPLUGIN 200

static unsigned int plugin_count = 0;
static dl_plugin_t  plugin_entry[NPLUGIN];
static int          plugin_update[NPLUGIN];

/* for modifying title */
static int          pt_begin[NPLUGIN];
static int          pt_end[NPLUGIN];

static const char  *psummary_desc[NPLUGIN];
static const char  *psummary_text[NPLUGIN];
static int          psummary_index[NPLUGIN];

static int psummary_get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    *output_ptr = psummary_desc[psummary_index[index]];
    return 0;
}

static int psummary_get_text(dl_plugin_t self, unsigned int index, const char **output_ptr) {
    *output_ptr = psummary_text[psummary_index[index]];
    return 0;
}

static dl_plugin_s plugin_summary = {
    .id            = -1,
    .priv          = NULL,
    .name          = "DLauncher-"VERSION,
    .priority      = 0,
    .init          = NULL,
    .query         = NULL,
    .before_update = NULL,
    .update        = NULL,
    .get_desc      = &psummary_get_desc,
    .get_text      = &psummary_get_text,
    .open          = NULL
};


static dl_plugin_t cur_plugin;
static int cur_pindex, prev_pindex, next_pindex, sel_index;

int
register_plugin(dl_plugin_t plugin) {
    if (plugin_count >= NPLUGIN) return -1;
    /* no semicolon in the name of a plugin is allowed */
    if (strchr(plugin->name, ':')) return -1;
    plugin->id = plugin_count;
    plugin_entry[plugin_count ++] = plugin;
    return 0;
}

static void plugin_cycle_next(void);
static void plugin_cycle_prev(void);

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;

static void
process_args(int argc, char *argv[]) {
    int i;
    for (i = 0; i < argc; i++) {
		/* these options take no arguments */
		if(!strcmp(argv[i], "-v")) {      /* prints version information */
			puts("dlauncher-"VERSION", © 2006-2012 dmenu engineers, see LICENSE for details");
			exit(EXIT_SUCCESS);
		}
		else if(!strcmp(argv[i], "-b"))   /* appears at the bottom of the screen */
			topbar = False;
		else if(!strcmp(argv[i], "-i")) { /* case-insensitive item matching */
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		}
		else if(i+1 == argc)
			usage();
        /* these options take one argument */
		else if(!strcmp(argv[i], "-l"))   /* number of lines in vertical list */
			lines = atoi(argv[++i]);
		else if(!strcmp(argv[i], "-fn"))  /* font or font set */
			font = strdup(argv[++i]);
		else if(!strcmp(argv[i], "-nb"))  /* normal background color */
			normbgcolor = strdup(argv[++i]);
		else if(!strcmp(argv[i], "-nf"))  /* normal foreground color */
			normfgcolor = strdup(argv[++i]);
		else if(!strcmp(argv[i], "-sb"))  /* selected background color */
			selbgcolor = strdup(argv[++i]);
		else if(!strcmp(argv[i], "-sf"))  /* selected foreground color */
			selfgcolor = strdup(argv[++i]);
		else if (!strcmp(argv[i], "-pl")) { /* external plugin */
            char *desc = strdup(argv[++ i]);
            /* format: name:entry[:opt] */
            char *name = desc;
            
            char *entry = desc;
            while (*entry && *entry != ':') {
                if (*entry == '\\' && entry[1] == ':') ++ entry;
                ++ entry;
            }
            
            if (*entry == ':') {
                *entry = 0;
                ++ entry;
            } else {
                fprintf(stderr, "invalid arg for %s\n", argv[i]);
                usage();
                exit(EXIT_FAILURE);
            }

            char *opt = entry;
            while (*opt && *opt != ':') {
                if (*opt == '\\' && opt[1] == ':') ++ opt;
                ++ opt;
            }
            if (*opt == ':') {
                *opt = 0;
                ++ opt;
            }
            
            if (external_plugin_create(name, entry, opt) != 0) {
                fprintf(stderr, "failed to create plugin\n");
                exit(EXIT_FAILURE);
            }
            free(desc);
        } else if (!strcmp(argv[i], "-args")) { /* extra args in file, one per line */
            const char *fn = argv[++ i];
            FILE *f = fopen(fn, "r");
            if (!f) {
                fprintf(stderr, "cannot open file %s\n", fn);
                usage();
                exit(EXIT_FAILURE);
            }

            int   buf_alloc = BUFSIZ;
            int   buf_size  = 0;
            char *buf = (char *)malloc(buf_alloc);

            if (!buf) {
                fprintf(stderr, "malloc failed\n");
                exit(EXIT_FAILURE);
            }

            int nargc = 0;
            char *line = NULL; size_t line_size; ssize_t gl_ret;
            while ((gl_ret = getline(&line, &line_size, f)) >= 0) {
                /* left trim the line */
                char *line_start = line;
                while (*line_start && *line_start == ' ') ++ line_start;
                if (*line_start == 0 || *line_start == '#' || *line_start == '\n') continue;
                gl_ret -= line_start - line;
                
                ++ nargc;
                if (line[gl_ret - 1] == '\n') -- gl_ret;
                while (buf_size + gl_ret + 1 > buf_alloc) {
                    buf = (char *)realloc(buf, buf_alloc << 1);
                    if (!buf) {
                        fprintf(stderr, "malloc failed\n");
                        exit(EXIT_FAILURE);
                    }
                    buf_alloc <<= 1;
                }
                memcpy(buf + buf_size, line_start, gl_ret);
                buf[buf_size + gl_ret] = 0;
                buf_size += gl_ret + 1;
            }
            free(line);
            
            fclose(f);

            char **nargv = (char **)malloc(sizeof(char *) * nargc);
            if (!nargv) {
                fprintf(stderr, "malloc failed\n");
                exit(EXIT_FAILURE);
            }
            
            line = buf;
            int j;
            for (j = 0; j < nargc; ++ j) {
                nargv[j] = line; line = line + strlen(line) + 1;
            }

            process_args(nargc, nargv);
            
            free(nargv);
            free(buf);
        } else {
            usage();
            exit(EXIT_FAILURE);
        }
    }
}

int
main(int argc, char *argv[]) {
	int i;

    process_args(argc - 1, argv + 1);
	
	dc = initdc();
	initfont(dc, font ? font : DEFAULT_FONT);
    normcol = initcolor(dc, normfgcolor, normbgcolor);
	selcol = initcolor(dc, selfgcolor, selbgcolor);

    register_plugin(&hist_plugin);
    
    setup();

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGUSR1, signal_show);

    for (i = 0; i < plugin_count; ++ i) {
        plugin_entry[i]->init(plugin_entry[i]);
    }

    cur_plugin = &plugin_summary;

	run();

	return 1; /* unreachable */
}

void
calc_offsets(void) {
    if (!cur_plugin) {
        cur_pindex = prev_pindex = next_pindex = 0;
        return;
    }
    
	int i, n;

	if(lines > 0)
		n = lines * bh;
	else
		n = mw - (promptw + inputw + textw(dc, "<") + textw(dc, ">"));
    
	/* calculate which items will begin the next page and previous page */    
	for(i = 0, next_pindex = cur_pindex;
        next_pindex < cur_plugin->item_count;
        ++ next_pindex) {
        const char *_text;
        cur_plugin->get_desc(cur_plugin, next_pindex, &_text);
        int tw = textw(dc, _text);
		if((i += (lines > 0) ? bh : MIN(tw, n)) > n)
			break;
    }

	for(i = 0, prev_pindex = cur_pindex - 1; prev_pindex > 0; -- prev_pindex) {
        const char *_text;
        cur_plugin->get_desc(cur_plugin, prev_pindex, &_text);
        int tw = textw(dc, _text);
		if((i += (lines > 0) ? bh : MIN(tw, n)) > n) {
            ++ prev_pindex;
			break;
        }
    }
}

char *
cistrstr(const char *s, const char *sub) {
	size_t len;

	for(len = strlen(sub); *s; s++)
		if(!strncasecmp(s, sub, len))
			return (char *)s;
	return NULL;
}

static int ru2p(int i) {
    if (i == 0) return 0;
    else return 1 << (32 - __builtin_clz(i));
}

void
drawmenu(void) {
	int curpos;
    int index;

	dc->x = 0;
	dc->y = 0;
	dc->h = bh;
	drawrect(dc, 0, 0, mw, mh, True, normcol->BG);

    if (cur_plugin == &plugin_summary) {
        int i;
        for (i = 0; i < plugin_count; ++ i) {
            if (pt_begin[i] < 0) continue;
            prompt_buf[pt_begin[i]] = ' ';
            prompt_buf[pt_end[i]] = ' ';
        }
        if (sel_index >= 0 && sel_index < plugin_summary.item_count) {
            prompt_buf[pt_begin[psummary_index[sel_index]]] = '(';
            prompt_buf[pt_end[psummary_index[sel_index]]] = ')';
        }
    }

	if (prompt) {
        promptw = textw(dc, prompt);
		dc->w = promptw;
		drawtext(dc, prompt, selcol);
		dc->x = dc->w;
	} else promptw = 0;

    inputw = ru2p(MAX(mw / 10, textw(dc, text) + promptw)) - promptw;
    inputw = MIN(mw / 2, inputw);
    
	/* draw input field */
	dc->w = (lines > 0 || !cur_plugin) ? mw - dc->x : inputw;
	drawtext(dc, text, normcol);
	if((curpos = textnw(dc, text, cursor) + dc->h/2 - 2) < dc->w)
		drawrect(dc, curpos, 2, 1, dc->h - 4, True, normcol->FG);

	if(lines > 0 && cur_plugin) {
		/* draw vertical list */
        dc->x = 0;
		dc->w = mw;
		for(index = cur_pindex; index != next_pindex; ++ index) {
			dc->y += dc->h;
            const char *_text;
            cur_plugin->get_desc(cur_plugin, index, &_text);            
			drawtext(dc, _text,
                     (index == sel_index) ? selcol : normcol);
		}
	} else if (cur_plugin) {
		/* draw horizontal list */
		dc->x += inputw;
		dc->w = textw(dc, "<");
		if(cur_pindex > 0)
			drawtext(dc, "<", normcol);
		for(index = cur_pindex; index != next_pindex; ++ index) {
            const char *_text;
            cur_plugin->get_desc(cur_plugin, index, &_text);            

            int tw = textw(dc, _text);
			dc->x += dc->w;
			dc->w = MIN(tw, mw - dc->x - textw(dc, ">"));
			drawtext(dc, _text,
                     (index == sel_index) ? selcol : normcol);
		}
		dc->w = textw(dc, ">");
		dc->x = mw - dc->w;
		if(next_pindex < cur_plugin->item_count)
			drawtext(dc, ">", normcol);
	}
	mapdc(dc, win, mw, mh);
}

void
grabkeyboard(void) {
	int i;

	/* try to grab keyboard, we may have to wait for another process to ungrab */
	for(i = 0; i < 1000; i++) {
		if(XGrabKeyboard(dc->dpy, DefaultRootWindow(dc->dpy), True,
		                 GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		usleep(1000);
	}
	eprintf("cannot grab keyboard\n");
}

void
insert(const char *str, ssize_t n) {
	if(strlen(text) + n > sizeof text - 1)
		return;
	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&text[cursor + n], &text[cursor], sizeof text - cursor - MAX(n, 0));
	if(n > 0)
		memcpy(&text[cursor], str, n);
	cursor += n;
    update(1);
}

void
keypress(XKeyEvent *ev) {
	char buf[32];
	int len;
	KeySym ksym = NoSymbol;
	Status status;

	len = XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
	if(status == XBufferOverflow)
		return;
	if(ev->state & ControlMask)
		switch(ksym) {
		case XK_a: ksym = XK_Home;      break;
		case XK_b: ksym = XK_Left;      break;
		case XK_c: ksym = XK_Escape;    break;
		case XK_d: ksym = XK_Delete;    break;
		case XK_e: ksym = XK_End;       break;
		case XK_f: ksym = XK_Right;     break;
		case XK_h: ksym = XK_BackSpace; break;
		case XK_i: ksym = XK_Tab;       break;
		case XK_j: ksym = XK_Return;    break;
		case XK_m: ksym = XK_Return;    break;
		case XK_n: ksym = XK_Down;      break;
		case XK_p: ksym = XK_Up;        break;
        case XK_Tab: complete_text(0);  return;
        case XK_g: /* cancel selection */
            sel_index = -1;
            break;
            
		case XK_k: /* delete right */
			text[cursor] = '\0';
			break;
		case XK_u: /* delete left */
			insert(NULL, 0 - cursor);
			break;
		case XK_w: /* delete word */
			while(cursor > 0 && text[nextrune(-1)] == ' ')
				insert(NULL, nextrune(-1) - cursor);
			while(cursor > 0 && text[nextrune(-1)] != ' ')
				insert(NULL, nextrune(-1) - cursor);
			break;
		case XK_y: /* paste selection */
			XConvertSelection(dc->dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
			                  utf8, utf8, win, CurrentTime);
			return;
		default:
			return;
		}
	else if(ev->state & Mod1Mask)
		switch(ksym) {
		case XK_g:     ksym = XK_Home;  break;
		case XK_G:     ksym = XK_End;   break;
        case XK_a:
		case XK_h:     ksym = XK_Up;    break;
        case XK_d:
		case XK_l:     ksym = XK_Down;  break;
		case XK_j:     ksym = XK_Next;  break;
		case XK_k:     ksym = XK_Prior; break;
        case XK_r:
        case XK_R:     ksym = XK_Return; break;
        case XK_Tab:   plugin_cycle_next(); return;
        case XK_w:
        case XK_Up:    hist_show_prev(); return;
        case XK_s:
        case XK_Down:  hist_show_next(); return;
        case XK_slash: complete_text(0); return;
		default:
			return;
		}
	switch(ksym) {
	default:
		if(!iscntrl(*buf))
			insert(buf, len);
		break;
	case XK_Delete:
		if(text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
		/* fallthrough */
	case XK_BackSpace:
		if(cursor == 0)
			return;
		insert(NULL, nextrune(-1) - cursor);
		break;
	case XK_End:
		if(text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
        if (cur_plugin) {
            cur_pindex = cur_plugin->item_count;
            calc_offsets();
            sel_index = cur_pindex = prev_pindex;
            calc_offsets();
        }
		break;
	case XK_Escape:
        if (cur_plugin != &plugin_summary) {
            cur_plugin = &plugin_summary;
            update(0);
        } else hide();
        return;
        
	case XK_Home:
		if (!cur_plugin || sel_index < 0 || sel_index == 0) {
			cursor = 0;
			break;
		}
		sel_index = cur_pindex = 0;
		calc_offsets();
		break;
	case XK_Left:
		if(cursor > 0 && (!cur_plugin || sel_index < 0 || sel_index == 0 || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if(lines > 0)
			return;
		/* fallthrough */
	case XK_Up:
        if (!cur_plugin) break;
        if (sel_index < 0) sel_index = cur_pindex;
        else if (sel_index > 0)
            -- sel_index;
        if (sel_index < cur_pindex &&
            prev_pindex >= 0) {
            cur_pindex = prev_pindex;
            calc_offsets();
        }
		break;
	case XK_Next:
		if(!cur_plugin ||
           next_pindex >= cur_plugin->item_count)
			return;
		sel_index = cur_pindex = next_pindex;
		calc_offsets();
		break;
	case XK_Prior:
		if(!cur_plugin ||
           prev_pindex < 0)
			return;
		sel_index = cur_pindex = prev_pindex;
		calc_offsets();
		break;
	case XK_Return:
	case XK_KP_Enter:
    open:
        if (cur_plugin == &plugin_summary) {
            if (sel_index < 0) sel_index = 0;
            if (sel_index < cur_plugin->item_count) {
                const char *_text;
                cur_plugin->get_text(cur_plugin, sel_index, &_text);
                strncpy(text, _text, sizeof text);
                cursor = strlen(text);
                cur_plugin = plugin_entry[psummary_index[sel_index]];
                update(0);
                goto open;
            }
            return;
        } else if (cur_plugin) {
            if (sel_index >= 0 && sel_index < cur_plugin->item_count) {
                const char *_text;
                cur_plugin->get_text(cur_plugin, sel_index, &_text);
                hist_add(cur_plugin->name, _text);
                cur_plugin->open(cur_plugin, sel_index, _text, !!(ev->state & ShiftMask));
            } else {
                /* no selected item */
                hist_add(cur_plugin->name, text);
                cur_plugin->open(cur_plugin, -1, text, !!(ev->state & ShiftMask));
            }
        }
        hide();
        return;
        
	case XK_Right:
		if(text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		if(lines > 0)
			return;
		/* fallthrough */
	case XK_Down: item_sel_next(); break;
	case XK_Tab: complete_text(1); return;
	}
	drawmenu();
}

void
item_sel_next(void) {
    if (cur_plugin < 0) return;
    if (sel_index < 0) sel_index = cur_pindex;
    else if (sel_index + 1 < cur_plugin->item_count) {
        ++ sel_index;
        if (sel_index == next_pindex &&
            next_pindex < cur_plugin->item_count) {
            cur_pindex = next_pindex;
            calc_offsets();
        }
    } else {
        sel_index = cur_pindex = 0;
        calc_offsets();
    }

}

void
complete_text(int to_update) {
    if (!cur_plugin || cur_plugin->item_count == 0)
        return;
    if (sel_index < 0 ||
        sel_index >= cur_plugin->item_count)
        sel_index = 0;

    const char *_text;

    if (to_update == 0) {
        int moved = 0;
        while (1) {
            cur_plugin->get_text(cur_plugin, sel_index, &_text);
            if (!strcmp(text, _text) && !moved) {
                item_sel_next();
                moved = 1;
            } else {
                strncpy(text, _text, sizeof text);
                cursor = strlen(text);
                break;
            }
        }
        drawmenu();
    } else if (cur_plugin == &plugin_summary) {
        const char *_text;
        cur_plugin->get_text(cur_plugin, sel_index, &_text);
        strncpy(text, _text, sizeof text);
        cursor = strlen(text);
        cur_plugin = plugin_entry[psummary_index[sel_index]];
        update(0);
    } else {
        cur_plugin->get_text(cur_plugin, sel_index, &_text);
        strncpy(text, _text, sizeof text);
        cursor = strlen(text);
        update(1);
    }
}

static int
psummary_comp(const void *a, const void *b) {
    int pa = plugin_entry[*(int *)a]->priority;
    int pb = plugin_entry[*(int *)b]->priority;
    /* larger priority first */
    return (pb - pa);
}

void
update(int query) {
    char *prompt_ptr = prompt_buf;
    int   plugin_best = -1;

    prompt = prompt_empty;
    char *plugin_filter = strchr(text, ':');
    char *input = text;
    if (plugin_filter) {
        input = plugin_filter + 1;
        *plugin_filter = 0;
    }

    int p;
    plugin_summary.item_count = 0;
    for (p = 0; p < plugin_count; ++ p) {
        if (plugin_filter && strstr(plugin_entry[p]->name, text) == NULL) goto skip;
        if (query) {
            if (plugin_entry[p]->query(plugin_entry[p], input)) goto skip;
        }

        if (plugin_entry[p]->item_count > 0) {
            plugin_entry[p]->get_desc(plugin_entry[p],
                                      0, &psummary_desc[p]);
            plugin_entry[p]->get_text(plugin_entry[p],
                                      0, &psummary_text[p]);
            psummary_index[plugin_summary.item_count] = p;
            ++ plugin_summary.item_count;
        }
        
        pt_begin[p] = prompt_ptr - prompt_buf;
        int w = snprintf(prompt_ptr, sizeof(prompt_buf) - (prompt_ptr - prompt_buf),
                         " %s ", plugin_entry[p]->name);
        prompt_ptr += w - 1;
        pt_end[p] = prompt_ptr - prompt_buf;

        if (plugin_best == -1 ||
            plugin_entry[p]->priority > plugin_entry[plugin_best]->priority) {
            plugin_best = p;
        }

        plugin_entry[p]->enabled = 1;
        continue;

      skip:
        pt_begin[p] = pt_end[p] = -1;
        plugin_entry[p]->enabled = 0;
        if (cur_plugin == plugin_entry[p]) cur_plugin = NULL;
    }

    if (plugin_filter) *plugin_filter = ':';

    if (!cur_plugin) {
        cur_plugin = plugin_entry[plugin_best];
    }

    prompt = prompt_buf;
    
    if (cur_plugin == &plugin_summary) {
        qsort(psummary_index,
              plugin_summary.item_count, sizeof(int),
              psummary_comp);
        
        cur_pindex = 0;
        sel_index = -1;

        calc_offsets();
        
    } else if (cur_plugin && cur_plugin->id >= 0) {
        prompt_buf[pt_begin[cur_plugin->id]] = '[';
        prompt_buf[pt_end[cur_plugin->id]] = ']';

        cur_pindex = 0;
        sel_index = -1;

        calc_offsets();
    }

    drawmenu();
}

size_t
nextrune(int inc) {
	ssize_t n;

	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for(n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc);
	return n;
}

void
paste(void) {
	char *p, *q;
	int di;
	unsigned long dl;
	Atom da;

	/* we have been given the current selection, now insert it into input */
	XGetWindowProperty(dc->dpy, win, utf8, 0, (sizeof text / 4) + 1, False,
	                   utf8, &da, &di, &dl, &dl, (unsigned char **)&p);
	insert(p, (q = strchr(p, '\n')) ? q-p : (ssize_t)strlen(p));
	XFree(p);
	drawmenu();
}

void
hist_plugin_init(dl_plugin_t self) {
    hist_index = -1;
    hist_count = 0;
    hist_file = NULL;
    
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) goto skip_history;

    hist_file_path = NULL;
    asprintf(&hist_file_path, "%s/.dlauncher_history", home_dir);
    if (hist_file_path == NULL) goto skip_history;
    FILE *his_r = fopen(hist_file_path, "r");
    if (his_r == NULL) goto skip_history;
    
    char *line = NULL; size_t line_size; ssize_t gl_ret;
    while ((gl_ret = getline(&line, &line_size, his_r)) >= 0) {
        if (gl_ret > 0 && line[gl_ret - 1] == '\n')
            line[gl_ret - 1] = 0;
        char *h = strdup(line);
        if (h) hist_add_line(h);
        else break;
    }
    if (line) free(line);

    fclose(his_r);
    
  skip_history:
    hist_file = fopen(hist_file_path, "a");
}

void
hist_add(const char *name, const char *text) {
    if (strcmp(name, "hist") == 0) return;

    char *line;
    asprintf(&line, "%s:%s", name, text);
    if (line) hist_add_line(line);
}

void
hist_add_line(const char *line) {
    int i;
    for (i = hist_count - 1;
         i >= 0 && i >= hist_count - HIST_CMP_MAX; -- i) {
        if (strcmp(line, hist_line[i]) == 0) {
            free((void *)line);

            int j;
            line = hist_line[i];
            for (j = i; j < hist_count - 1; ++ j)
                hist_line[j] = hist_line[j + 1];
            hist_line[hist_count - 1] = line;

            hist_rebuild_file();
            return;
        }
    }
    
    if (hist_count >= HIST_SIZE * 2) {
        int i;
        for (i = 0; i < HIST_SIZE; ++ i) {
            free((void *)hist_line[i]);
            hist_line[i] = hist_line[i + HIST_SIZE];
        }
        hist_count -= HIST_SIZE;
        hist_rebuild_file();
    }

    hist_line[hist_count] = line;
    ++ hist_count;
    hist_index = -1;

    if (hist_file) {
        fputs(line, hist_file);
        fputc('\n', hist_file);
        fflush(hist_file);
    }
}

void
hist_rebuild_file(void) {
    int i;
    if (hist_file) {
        hist_file = freopen(hist_file_path, "w", hist_file);
        if (hist_file) {
            for (i = 0; i < hist_count; ++ i) {
                fputs(hist_line[i], hist_file);
                fputc('\n', hist_file);
            }
            fflush(hist_file);
        }
    }
}

void
hist_apply(const char *line) {
    int p_index;
    for (p_index = 0; p_index < plugin_count; ++ p_index) {
        int len = strlen(plugin_entry[p_index]->name);
        /* for completely matching */
        if (strncmp(plugin_entry[p_index]->name, line, len) == 0 &&
            line[len] == ':') {
            cur_plugin = plugin_entry[p_index];
            strncpy(text, line + len + 1, sizeof text);
            cursor = strlen(text);
            break;
        }
    }
}

void
hist_show_prev(void) {
    if (hist_index == -1) hist_index = hist_count;
    if (hist_index == -1) return;
    if (hist_index > 0) -- hist_index;
    hist_apply(hist_line[hist_index]);
    update(1);
}

void
hist_show_next(void) {
    if (hist_index == -1) hist_index = hist_count - 1;
    if (hist_index == -1) return;
    if (hist_index < hist_count - 1) ++ hist_index;
    hist_apply(hist_line[hist_index]);
    update(1);
}

int
hist_plugin_query(dl_plugin_t self, const char *input) {
    int count = 0;
    int i;
    for (i = hist_count - 1; i >= 0; -- i)
        if (strstr(strchr(hist_line[i], ':') + 1, input))
            hist_line_matched[count ++] = hist_line[i];

    self->item_count = count;
    return 0;
}

int
hist_plugin_before_update(dl_plugin_t self) { return 0; }

int
hist_plugin_get_desc(dl_plugin_t self, unsigned int index, const char **output_ptr)
{
    if (index >= self->item_count) {
        *output_ptr = NULL;
        fprintf(stderr, "get_text out of bound\n");
        return 1;
    }
    
    *output_ptr = hist_line_matched[index];
    return 0;
}

int
hist_plugin_get_text(dl_plugin_t self, unsigned int index, const char **output_ptr)
{
    if (index >= self->item_count) {
        *output_ptr = NULL;
        fprintf(stderr, "get_text out of bound\n");
        return 1;
    }

    *output_ptr = strchr(hist_line_matched[index], ':') + 1;
    return 0;
}

int
hist_plugin_open(dl_plugin_t self, int index, const char *input, int mode) {
    if (index < 0) index = 0;
    if (index >= self->item_count) {
        fprintf(stderr, "open out of bound\n");
        return 1;
    }

    hist_apply(hist_line_matched[index]);
    if (cur_plugin != self) {
        hist_add(cur_plugin->name, text);
        cur_plugin->open(cur_plugin, -1, text, mode);
    }
	return 0;
}

fd_set in_fds, out_fds, stat_fds;
int    max_fd;
int    fd_alloc;
int    fd_size;
int   *fd_plugin;
int   *fds;
int   *fd_flags;

int
register_update_fd(dl_plugin_t plugin, int fd, int event) {
    if (fd_size == fd_alloc) {
        fd_plugin = (int *)realloc(fd_plugin, sizeof(int) * (fd_alloc << 1));
        fds       = (int *)realloc(fds, sizeof(int) * (fd_alloc << 1));
        fd_flags  = (int *)realloc(fd_flags, sizeof(int) * (fd_alloc << 1));

        if (!fd_plugin || !fds || !fd_flags) {
            /* :( */
            exit(EXIT_FAILURE);
        }

        fd_alloc <<= 1;
    }

    int id = fd_size ++;
        
    fd_plugin[id] = plugin->id;
    fds[id] = fd;
    fd_flags[id] = event;

    if (max_fd < fd) max_fd = fd;
    if (event & DL_FD_EVENT_READ)
        FD_SET(fd, &in_fds);
    if (event & DL_FD_EVENT_WRITE)
        FD_SET(fd, &out_fds);
    if (event & DL_FD_EVENT_STATUS)
        FD_SET(fd, &stat_fds);

    return 0;
}

void
run(void) {
	XEvent ev;
    int x11_fd = ConnectionNumber(dc->dpy);
    int i;
    struct timeval tv;

    fd_alloc = NPLUGIN;
    fd_size  = 0;
    fd_plugin= (int *)malloc(sizeof(int) * fd_alloc);
    fds      = (int *)malloc(sizeof(int) * fd_alloc);
    fd_flags = (int *)malloc(sizeof(int) * fd_alloc);

    if (!fd_plugin || !fds || !fd_flags) return;

	while(1) {
        if (to_show) {
            to_show = 0;
            show();
        }

        while (XPending(dc->dpy)) {
            XNextEvent(dc->dpy, &ev);
            if(XFilterEvent(&ev, win))
                continue;
            switch(ev.type) {
            case Expose:
                if(ev.xexpose.count == 0)
                    mapdc(dc, win, mw, mh);
                break;
            case KeyPress:
                keypress(&ev.xkey);
                break;
            case SelectionNotify:
                if(ev.xselection.property == utf8)
                    paste();
                break;
            case VisibilityNotify:
                if(ev.xvisibility.state != VisibilityUnobscured)
                    XRaiseWindow(dc->dpy, win);
                break;
            }
        }

        FD_ZERO(&in_fds); FD_ZERO(&out_fds); FD_ZERO(&stat_fds);
        FD_SET(x11_fd, &in_fds);
        max_fd = x11_fd;

        tv.tv_usec = 0;
        tv.tv_sec = 3;

        fd_size = 0;
        for (i = 0; i < plugin_count; ++ i) {
            plugin_update[i] = 0;
            if (plugin_entry[i]->before_update)
                plugin_entry[i]->before_update(plugin_entry[i]);
        }

        select(max_fd + 1, &in_fds, &out_fds, &stat_fds, &tv);
        
        for (i = 0; i < fd_size; ++ i) {
            if ((fd_flags[i] & DL_FD_EVENT_READ) &&
                FD_ISSET(fds[i], &in_fds))
                plugin_update[fd_plugin[i]] = 1;
            else if ((fd_flags[i] & DL_FD_EVENT_WRITE) &&
                     FD_ISSET(fds[i], &out_fds))
                plugin_update[fd_plugin[i]] = 1;
            else if ((fd_flags[i] & DL_FD_EVENT_STATUS) &&
                     FD_ISSET(fds[i], &stat_fds))
                plugin_update[fd_plugin[i]] = 1;
        }

        int to_update = 0;
        for (i = 0; i < plugin_count; ++ i) {
            if (plugin_update[i]) {
                plugin_entry[i]->update(plugin_entry[i]);
                to_update = 1;
            }
        }

        if (showed && to_update) {
            update(0);
        }
            
    }
}

void
calc_geo(void) {
    int x, y, screen = DefaultScreen(dc->dpy);
    Window root = RootWindow(dc->dpy, screen);
    
#ifdef XINERAMA
	int n;
	XineramaScreenInfo *info;

	if((info = XineramaQueryScreens(dc->dpy, &n))) {
		int a, j, di, i = 0, area = 0;
		unsigned int du;
		Window w, pw, dw, *dws;
		XWindowAttributes wa;

		XGetInputFocus(dc->dpy, &w, &di);
		if(w != root && w != PointerRoot && w != None) {
			/* find top-level window containing current input focus */
			do {
				if(XQueryTree(dc->dpy, (pw = w), &dw, &w, &dws, &du) && dws)
					XFree(dws);
			} while(w != root && w != pw);
			/* find xinerama screen with which the window intersects most */
			if(XGetWindowAttributes(dc->dpy, pw, &wa))
				for(j = 0; j < n; j++)
					if((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
						area = a;
						i = j;
					}
		}
		/* no focused window is on screen, so use pointer location instead */
		if(!area && XQueryPointer(dc->dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
			for(i = 0; i < n; i++)
				if(INTERSECT(x, y, 1, 1, info[i]))
					break;

		mx = info[i].x_org;
		my = info[i].y_org + (topbar ? 0 : info[i].height - mh);
		mw = info[i].width;
		XFree(info);
	}
	else
#endif
	{
		mx = 0;
		my = topbar ? 0 : DisplayHeight(dc->dpy, screen) - mh;
		mw = DisplayWidth(dc->dpy, screen);
	}
}

void
setup(void) {
	int screen = DefaultScreen(dc->dpy);
	Window root = RootWindow(dc->dpy, screen);
	XSetWindowAttributes swa;
	XIM xim;

	clip = XInternAtom(dc->dpy, "CLIPBOARD",   False);
	utf8 = XInternAtom(dc->dpy, "UTF8_STRING", False);

	/* calculate menu geometry */
	bh = dc->font.height + 2;
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;
    
    calc_geo();
    
	/* create menu window */
	swa.override_redirect = True;
	swa.background_pixel = normcol->BG;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;
	win = XCreateWindow(dc->dpy, root, mx, my, mw, mh, 0,
	                    DefaultDepth(dc->dpy, screen), CopyFromParent,
	                    DefaultVisual(dc->dpy, screen),
	                    CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);

	/* open input methods */
	xim = XOpenIM(dc->dpy, NULL, NULL, NULL);
	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, win, XNFocusWindow, win, NULL);
}

void
plugin_cycle_next(void) {
    if (!cur_plugin || cur_plugin->id < 0) return;
    int id = cur_plugin->id;
    int p = (id + 1) % plugin_count;
    while (p != id && !plugin_entry[p]->enabled)
        p = (p + 1) % plugin_count;
    cur_plugin = plugin_entry[p];
    update(0);
}

void
plugin_cycle_prev(void) {
    if (!cur_plugin || cur_plugin->id < 0) return;
    int id = cur_plugin->id;
    int step = plugin_count - 1;
    int p = (id + step) % plugin_count;
    while (p != id && !plugin_entry[p]->enabled)
        p = (p + step) % plugin_count;
    cur_plugin = plugin_entry[p];
    update(0);
}

void
signal_show(int signo) {
    to_show = 1;
}

void
show(void) {
    grabkeyboard();
    calc_geo();
    XMoveWindow(dc->dpy, win, mx, my);
    XResizeWindow(dc->dpy, win, mw, mh);
    XMapRaised(dc->dpy, win);
	resizedc(dc, mw, mh);
	update(1);

    showed = 1;
}

void
hide(void) {
    text[0] = 0;
    cursor = 0;
    cur_plugin = &plugin_summary;
    prompt = prompt_empty;
    hist_index = -1;
    showed = 0;

    XUnmapWindow(dc->dpy, win);
    XUngrabKeyboard(dc->dpy, CurrentTime);
}

void
usage(void) {
	fputs("usage: dlauncher [-b] [-i] [-l lines] [-fn font]\n"
	      "                 [-nb color] [-nf color] [-sb color] [-sf color] [-v]\n"
          "                 [-args external_args_file]*\n"
          "                 [-pl name:entry[:opt]]*\n"
          , stderr);
	exit(EXIT_FAILURE);
}
