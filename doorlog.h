#ifndef DOORLOG_H
#define DOORLOG_H

/* Minimal logger state so the rest of the code does not touch DOS handles directly. */
struct doorlog {
  int enabled;
  long handle;
  char path[128];
};

/* Open, write, and close the optional runtime log. */
int doorlog_open(struct doorlog *log, const char *path, int enabled);
void doorlog_write(struct doorlog *log, const char *text);
void doorlog_writef(struct doorlog *log, const char *format_text, ...);
void doorlog_close(struct doorlog *log);

#endif
