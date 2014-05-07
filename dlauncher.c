/* See LICENSE file for copyright and license details. */
#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

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
#include "plugin.h"

#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define MIN(a,b)              ((a) < (b) ? (a) : (b))
#define MAX(a,b)              ((a) > (b) ? (a) : (b))

static void calcoffsets(void);
static char *cistrstr(const char *s, const char *sub);
static void drawmenu(void);
static void grabkeyboard(void);
static void insert(const char *str, ssize_t n);
static void keypress(XKeyEvent *ev);
static void update(void);
static size_t nextrune(int inc);
static void paste(void);
static void run(void);
static void usage(void);
static void init_history(void);
static void add_history(const char *plugin_name, const char *text);
static void add_history_line(const char *line);
static void jump_to_history(const char *line);
static void rewrite_history_file(void);
static void setup(void);
static void calc_geo(void);
static void show(void);
static void hide(void);
static void signal_show(int);
static void signal_update(int);

static int volatile to_show = 0;
static int volatile showed = 0;
static int volatile to_update = 0;

#define HISTORY_SIZE 8192
#define HISTORY_CMP_MAX 16
static FILE       *history_file;
static char       *history_file_path;
static const char *history[HISTORY_SIZE * 2];
static int         history_count;
static int         history_index;

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
static unsigned long normcol[ColLast];
static unsigned long selcol[ColLast];
static Atom clip, utf8;
static Bool topbar = True;
static DC *dc;
static Window win;
static XIC xic;

#define NPLUGIN 200

static unsigned int plugin_count = 0;
static dl_plugin_t  plugin_entry[NPLUGIN];

static int cur_plugin;
static int cur_pindex, prev_pindex, next_pindex, sel_index;

void register_plugin(dl_plugin_t plugin) {
    if (plugin_count >= NPLUGIN) return;
    /* no comma in name of a plugin is allowed */
    if (strchr(plugin->name,',')) return;
    plugin_entry[plugin_count ++] = plugin;
}

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;

int
main(int argc, char *argv[]) {
	int i;

	for(i = 1; i < argc; i++)
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
			font = argv[++i];
		else if(!strcmp(argv[i], "-nb"))  /* normal background color */
			normbgcolor = argv[++i];
		else if(!strcmp(argv[i], "-nf"))  /* normal foreground color */
			normfgcolor = argv[++i];
		else if(!strcmp(argv[i], "-sb"))  /* selected background color */
			selbgcolor = argv[++i];
		else if(!strcmp(argv[i], "-sf"))  /* selected foreground color */
			selfgcolor = argv[++i];
		else
			usage();

	dc = initdc();
	initfont(dc, font);

    init_history();
    
    cur_plugin = -1;
    prompt = prompt_empty;
    
    setup();

    signal(SIGCHLD, SIG_IGN);
    signal(SIGUSR1, signal_show);
    signal(SIGALRM, signal_update);

    ualarm(300000, 300000);
    
	run();

	return 1; /* unreachable */
}

void
calcoffsets(void) {
    if (cur_plugin < 0) {
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
        next_pindex < plugin_entry[cur_plugin]->item_count;
        ++ next_pindex) {
        const char *_text;
        plugin_entry[cur_plugin]->get_text(plugin_entry[cur_plugin], next_pindex, &_text);
        int tw = textw(dc, _text);
		if((i += (lines > 0) ? bh : MIN(tw, n)) > n)
			break;
    }

	for(i = 0, prev_pindex = cur_pindex - 1; prev_pindex > 0; -- prev_pindex) {
        const char *_text;
        plugin_entry[cur_plugin]->get_text(plugin_entry[cur_plugin], prev_pindex, &_text);
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

void
drawmenu(void) {
	int curpos;
    int index;

	dc->x = 0;
	dc->y = 0;
	dc->h = bh;
	drawrect(dc, 0, 0, mw, mh, True, BG(dc, normcol));

	if (prompt) {
        promptw = textw(dc, prompt);
		dc->w = promptw;
		drawtext(dc, prompt, selcol);
		dc->x = dc->w;
	}

    inputw = textw(dc, text);
    if (inputw)
        inputw = 1 << (32 - __builtin_clz(inputw));
    
	/* draw input field */
	dc->w = (lines > 0 || cur_plugin < 0) ? mw - dc->x : inputw;
	drawtext(dc, text, normcol);
	if((curpos = textnw(dc, text, cursor) + dc->h/2 - 2) < dc->w)
		drawrect(dc, curpos, 2, 1, dc->h - 4, True, FG(dc, normcol));

	if(lines > 0) {
		/* draw vertical list */
		dc->w = mw - dc->x;
		for(index = cur_pindex; index != next_pindex; ++ index) {
			dc->y += dc->h;
            const char *_text;
            plugin_entry[cur_plugin]->get_text(plugin_entry[cur_plugin], index, &_text);            
			drawtext(dc, _text,
                     (index == sel_index) ? selcol : normcol);
		}
	}
	else if(cur_plugin >= 0) {
		/* draw horizontal list */
		dc->x += inputw;
		dc->w = textw(dc, "<");
		if(cur_pindex > 0)
			drawtext(dc, "<", normcol);
		for(index = cur_pindex; index != next_pindex; ++ index) {
            const char *_text;
            plugin_entry[cur_plugin]->get_text(plugin_entry[cur_plugin], index, &_text);            

            int tw = textw(dc, _text);
			dc->x += dc->w;
			dc->w = MIN(tw, mw - dc->x - textw(dc, ">"));
			drawtext(dc, _text,
                     (index == sel_index) ? selcol : normcol);
		}
		dc->w = textw(dc, ">");
		dc->x = mw - dc->w;
		if(next_pindex < plugin_entry[cur_plugin]->item_count)
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
		case XK_g: ksym = XK_Home;  break;
		case XK_G: ksym = XK_End;   break;
		case XK_h: ksym = XK_Up;    break;
		case XK_j: ksym = XK_Next;  break;
		case XK_k: ksym = XK_Prior; break;
		case XK_l: ksym = XK_Down;  break;
        case XK_Tab:
            if (cur_plugin < 0) break;
            int p = (cur_plugin + 1) % plugin_count;
            while (p != cur_plugin && plugin_entry[p]->item_count == 0)
                p = (cur_plugin + 1) % plugin_count;
            cur_plugin = p;
            drawmenu();
            return;

        case XK_Up:
            if (history_index == -1) history_index = history_count;
            if (history_index == -1) break;
            if (history_index > 0) -- history_index;
            jump_to_history(history[history_index]);
            update();
            return;
            
        case XK_Down:
            if (history_index == -1) history_index = history_count - 1;
            if (history_index == -1) break;
            if (history_index < history_count - 1) ++ history_index;
            jump_to_history(history[history_index]);
            update();
            return;

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
        if (cur_plugin >= 0) {
            cur_pindex = plugin_entry[cur_plugin]->item_count;
            calcoffsets();
            sel_index = cur_pindex = prev_pindex;
            calcoffsets();
        }
		break;
	case XK_Escape:
		hide();
        return;
        
	case XK_Home:
		if(sel_index < 0 || sel_index == 0) {
			cursor = 0;
			break;
		}
		sel_index = cur_pindex = 0;
		calcoffsets();
		break;
	case XK_Left:
		if(cursor > 0 && (sel_index < 0 || sel_index == 0 || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if(lines > 0)
			return;
		/* fallthrough */
	case XK_Up:
        if (cur_plugin < 0) break;
        if (sel_index < 0) sel_index = cur_pindex;
        else if (sel_index > 0)
            -- sel_index;
        if (sel_index < cur_pindex &&
            prev_pindex >= 0) {
            cur_pindex = prev_pindex;
            calcoffsets();
        }
		break;
	case XK_Next:
		if(cur_plugin < 0 ||
           next_pindex >= plugin_entry[cur_plugin]->item_count)
			return;
		sel_index = cur_pindex = next_pindex;
		calcoffsets();
		break;
	case XK_Prior:
		if(cur_plugin < 0 ||
           prev_pindex < 0)
			return;
		sel_index = cur_pindex = prev_pindex;
		calcoffsets();
		break;
	case XK_Return:
	case XK_KP_Enter:
		if (cur_plugin >= 0 &&
            sel_index >= 0 && sel_index < plugin_entry[cur_plugin]->item_count &&
            !(ev->state & ShiftMask)) {
            const char *_text;
            plugin_entry[cur_plugin]->get_text(plugin_entry[cur_plugin], sel_index, &_text);
            add_history(plugin_entry[cur_plugin]->name, _text);
            plugin_entry[cur_plugin]->open(plugin_entry[cur_plugin], sel_index);
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
	case XK_Down:
        if (cur_plugin < 0) break;
        if (sel_index < 0) sel_index = cur_pindex;
        else if (sel_index + 1 < plugin_entry[cur_plugin]->item_count)
            ++ sel_index;
        if (sel_index == next_pindex &&
            next_pindex < plugin_entry[cur_plugin]->item_count) {
            cur_pindex = next_pindex;
            calcoffsets();
        }
		break;
	case XK_Tab:
        update();
		if(cur_plugin < 0 ||
           sel_index < 0 ||
           sel_index >= plugin_entry[cur_plugin]->item_count)
			return;
        
        const char *_text;
        plugin_entry[cur_plugin]->get_text(plugin_entry[cur_plugin], sel_index, &_text);            

		strncpy(text, _text, sizeof text);
		cursor = strlen(text);
		break;
	}
	drawmenu();
}

void
update(void) {
    if (strcmp(text_cached, text) == 0) return;
        
    char *prompt_ptr = prompt_buf;
    char *prompt_sel_begin = prompt_buf, *prompt_sel_end = prompt_buf;
    int  plugin_first = -1;

    strcpy(text_cached, text);

    prompt = prompt_empty;

    int p;
    for (p = 0; p < plugin_count; ++ p) {
        if (plugin_entry[p]->update(plugin_entry[p], text)) goto skip;
        if (plugin_entry[p]->item_count == 0) goto skip;
        
        if (cur_plugin == p) prompt_sel_begin = prompt_ptr;
        int w = snprintf(prompt_ptr, sizeof(prompt_buf) - (prompt_ptr - prompt_buf),
                         " %s ", plugin_entry[p]->name);
        prompt_ptr += w - 1;
        if (cur_plugin == p) prompt_sel_end = prompt_ptr;

        if (plugin_first == -1) {
            plugin_first = p;
            prompt_sel_begin = prompt_buf;
            prompt_sel_end = prompt_ptr;
        }
        
        continue;

      skip:
        /* current plugin no longer available */
        plugin_entry[p]->item_count = 0;
        if (cur_plugin == p) cur_plugin = -1;
    }
    
    if (cur_plugin < 0)
        cur_plugin = plugin_first;
    
    if (cur_plugin >= 0) {
        *prompt_sel_begin = '[';
        *prompt_sel_end   = ']';

        cur_pindex = 0;
        sel_index = plugin_entry[cur_plugin]->item_default_sel;
        prompt = prompt_buf;

        calcoffsets();
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
init_history(void) {
    history_index = -1;
    history_count = 0;
    history_file = NULL;
    
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) goto skip_history;

    history_file_path = NULL;
    asprintf(&history_file_path, "%s/.dlauncher_history", home_dir);
    if (history_file_path == NULL) goto skip_history;
    FILE *his_r = fopen(history_file_path, "r");
    if (his_r == NULL) goto skip_history;
    
    char *line = NULL; size_t line_size; ssize_t gl_ret;
    while ((gl_ret = getline(&line, &line_size, his_r)) >= 0) {
        if (gl_ret > 0 && line[gl_ret - 1] == '\n')
            line[gl_ret - 1] = 0;
        char *h = strdup(line);
        if (h) add_history_line(h);
        else break;
    }
    if (line) free(line);

    fclose(his_r);
    
  skip_history:
    history_file = fopen(history_file_path, "a");
}

void
add_history(const char *name, const char *text) {
    char *line;
    asprintf(&line, "%s:%s", name, text);
    if (line) add_history_line(line);
}

void
add_history_line(const char *line) {
    int i;
    for (i = history_count - 1;
         i >= 0 && i >= history_count - HISTORY_CMP_MAX; -- i) {
        if (strcmp(line, history[i]) == 0) {
            free((void *)line);

            int j;
            line = history[i];
            for (j = i; j < history_count - 1; ++ j)
                history[j] = history[j + 1];
            history[history_count - 1] = line;

            rewrite_history_file();
            return;
        }
    }
    
    if (history_count >= HISTORY_SIZE * 2) {
        int i;
        for (i = 0; i < HISTORY_SIZE; ++ i) {
            free((void *)history[i]);
            history[i] = history[i + HISTORY_SIZE];
        }
        history_count -= HISTORY_SIZE;
        rewrite_history_file();
    }

    history[history_count] = line;
    ++ history_count;
    history_index = -1;

    if (history_file) {
        fseek(history_file, 0, SEEK_END);
        fputs(line, history_file);
        fputc('\n', history_file);
        fflush(history_file);
    }
}

void
rewrite_history_file(void) {
    int i;
    if (history_file) {
        history_file = freopen(history_file_path, "w", history_file);
        if (history_file) {
            for (i = 0; i < history_count; ++ i) {
                fputs(history[i], history_file);
                fputc('\n', history_file);
            }
        }
    }
}

void
jump_to_history(const char *line) {
    int p_index;
    for (p_index = 0; p_index < plugin_count; ++ p_index) {
        int len = strlen(plugin_entry[p_index]->name);
        /* for completely matching */
        if (strncmp(plugin_entry[p_index]->name, line, len) == 0 &&
            line[len] == ':') {
            cur_plugin = p_index;
            strncpy(text, line + len + 1, sizeof text);
            cursor = strlen(text);
            break;
        }
    }
}


void
run(void) {
	XEvent ev;
    int x11_fd = ConnectionNumber(dc->dpy);
    fd_set in_fds;
    struct timeval tv;

	while(1) {
        if (to_show) {
            to_show = 0;
            show();
        }

        if (to_update && showed) {
            to_update = 0;
            update();
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

        FD_ZERO(&in_fds);
        FD_SET(x11_fd, &in_fds);

        tv.tv_usec = 0;
        tv.tv_sec = 1;

        select(x11_fd + 1, &in_fds, 0, 0, &tv);
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

	normcol[ColBG] = getcolor(dc, normbgcolor);
	normcol[ColFG] = getcolor(dc, normfgcolor);
	selcol[ColBG]  = getcolor(dc, selbgcolor);
	selcol[ColFG]  = getcolor(dc, selfgcolor);

	clip = XInternAtom(dc->dpy, "CLIPBOARD",   False);
	utf8 = XInternAtom(dc->dpy, "UTF8_STRING", False);

	/* calculate menu geometry */
	bh = dc->font.height + 2;
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;
    
    calc_geo();
    
	/* create menu window */
	swa.override_redirect = True;
	swa.background_pixel = normcol[ColBG];
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
signal_show(int signo) {
    to_show = 1;
}

void
signal_update(int signo) {
    to_update = 1;
}

void
show(void) {
    grabkeyboard();

    calc_geo();

    XMoveWindow(dc->dpy, win, mx, my);
    XResizeWindow(dc->dpy, win, mw, mh);
    XMapRaised(dc->dpy, win);
	resizedc(dc, mw, mh);
	drawmenu();

    showed = 1;
}

void
hide(void) {
    text[0] = 0;
    cursor = 0;
    cur_plugin = -1;
    prompt = prompt_empty;
    history_index = -1;
    showed = 0;
    
    XUnmapWindow(dc->dpy, win);
    XUngrabKeyboard(dc->dpy, CurrentTime);
}

void
usage(void) {
	fputs("usage: dlauncher [-b] [-i] [-l lines] [-fn font]\n"
	      "                 [-nb color] [-nf color] [-sb color] [-sf color] [-v]\n", stderr);
	exit(EXIT_FAILURE);
}
