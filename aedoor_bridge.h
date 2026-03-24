#ifndef AEDOOR_BRIDGE_H
#define AEDOOR_BRIDGE_H

/* Shared caller-session state gathered from AEDoor and AmiExpress. */
struct door_config;
struct DIFace;
struct Library;

struct aedoor_context {
  int active;
  int nonstop_was_enabled;
  int paging_state_known;
  int ansi_capable;
  int raw_arrow_enabled;
  int node_unit;
  int baud_rate;
  struct Library *library_base;
  struct DIFace *diface;
  char *string_field;
  char node_device[64];
  char username[64];
};

/* Open and tear down the live AEDoor link. */
int aedoor_open(struct aedoor_context *context, int argc, char **argv, char *error_text, int error_text_size);
int aedoor_fetch_username(struct aedoor_context *context, const struct door_config *config, char *error_text, int error_text_size);
int aedoor_prepare_session(struct aedoor_context *context, const struct door_config *config, char *error_text, int error_text_size);

/* Output and input helpers used by the terminal pump. */
void aedoor_write_line(struct aedoor_context *context, const char *text);
void aedoor_write_text(struct aedoor_context *context, const char *text);
void aedoor_write_bytes(struct aedoor_context *context, const unsigned char *data, int length);
int aedoor_poll_key(struct aedoor_context *context, long *key_value);
void aedoor_set_cursor(struct aedoor_context *context, int visible);

/* Restore any temporary caller-session changes before returning to the BBS. */
void aedoor_restore_session(struct aedoor_context *context);
void aedoor_close(struct aedoor_context *context);

#endif
