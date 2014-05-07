#ifndef __DLAUNCHER_HIST_H__
#define __DLAUNCHER_HIST_H__

void hist_init(void);
void hist_add(const char *plugin_name, const char *text);
void hist_add_line(const char *line);
void hist_apply(const char *line);

#define HIST_SIZE 8192
#define HIST_CMP_MAX 16

extern const char *hist_line[];
extern int         hist_count;
extern int         hist_index;

#endif
