#ifndef DOOR_CONFIG_H
#define DOOR_CONFIG_H

/* Parsed runtime settings loaded from arblink.cfg. */
struct door_config {
  char host[128];
  unsigned short port;
  char username_prefix[32];
  char remote_user[64];
  char terminal_type[32];
  unsigned short terminal_speed;
  unsigned short terminal_columns;
  unsigned short terminal_rows;
  char newline_mode[16];
  char debug_log[128];
  int debug_enabled;
  int disable_paging;
};

/* Defaulting and loading helpers for the flat text config file. */
void config_set_defaults(struct door_config *config);
int config_load_file(const char *path, struct door_config *config, char *error_text, int error_text_size);

#endif
