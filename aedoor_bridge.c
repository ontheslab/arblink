/*
 * AEDoor bridge for arblink.
 *
 * This module keeps all AEDoor-specific behaviour in one place so the rest of
 * the door can treat the caller connection as a simple terminal endpoint.
 *
 * Important gotchas:
 * - the door must be launched by AmiExpress/AEDoor, not directly from Shell
 * - the caller name prefix is applied here before the transport sees it
 * - page pausing is disabled temporarily for remote full-screen output
 */
#include "aedoor_bridge.h"
#include "aedoor_inline.h"
#include "door_config.h"

#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *AEDBase = NULL;

/* Small helper used throughout the bridge to keep caller-facing errors tidy. */
static void bridge_set_error(char *error_text, int error_text_size, const char *message)
{
  if ((error_text == NULL) || (error_text_size <= 0)) {
    return;
  }

  strncpy(error_text, message, (size_t) error_text_size - 1U);
  error_text[error_text_size - 1] = '\0';
}

static void bridge_copy_string_field(struct aedoor_context *context, char *buffer, size_t buffer_size)
{
  if ((buffer == NULL) || (buffer_size == 0U)) {
    return;
  }

  buffer[0] = '\0';
  if ((context == NULL) || (context->string_field == NULL)) {
    return;
  }

  strncpy(buffer, context->string_field, buffer_size - 1U);
  buffer[buffer_size - 1U] = '\0';
}

static void bridge_write_line(struct aedoor_context *context, const char *text)
{
  if ((context != NULL) && (context->diface != NULL) && (text != NULL)) {
    WriteStr(context->diface, (char *) text, WSF_LF);
  }
}

static void bridge_write_text(struct aedoor_context *context, const char *text)
{
  if ((context != NULL) && (context->diface != NULL) && (text != NULL)) {
    WriteStr(context->diface, (char *) text, NOLF);
  }
}

static void bridge_fetch_text_value(struct aedoor_context *context, unsigned long id, char *buffer, size_t buffer_size)
{
  if ((context == NULL) || (context->diface == NULL) || (buffer == NULL) || (buffer_size == 0U)) {
    return;
  }

  GetDT(context->diface, id, NULL);
  bridge_copy_string_field(context, buffer, buffer_size);
}

int aedoor_open(struct aedoor_context *context, int argc, char **argv, char *error_text, int error_text_size)
{
  unsigned long node;

  if (context == NULL) {
    bridge_set_error(error_text, error_text_size, "invalid AEDoor context");
    return -1;
  }

  memset(context, 0, sizeof(*context));
  node = 0;
  /*
   * XIM launches pass a node argument in argv[1]. AEDoor v2.x no longer makes
   * heavy use of the value itself, but CreateComm still expects the launch to
   * look like a proper AEDoor invocation.
   */
  if ((argc > 1) && (argv != NULL) && (argv[1] != NULL) && (argv[1][0] != '\0')) {
    node = (unsigned long) (unsigned char) argv[1][0];
  } else {
    bridge_set_error(error_text, error_text_size, "program was not launched with an AEDoor node argument");
    return -1;
  }

  AEDBase = OpenLibrary(AEDoorName, 0);
  if (AEDBase == NULL) {
    bridge_set_error(error_text, error_text_size, "could not open AEDoor.library");
    return -1;
  }

  context->library_base = AEDBase;
  context->diface = CreateComm(node);
  if (context->diface == NULL) {
    bridge_set_error(error_text, error_text_size, "CreateComm failed");
    CloseLibrary(AEDBase);
    AEDBase = NULL;
    memset(context, 0, sizeof(*context));
    return -1;
  }

  context->string_field = GetString(context->diface);
  if (context->string_field == NULL) {
    bridge_set_error(error_text, error_text_size, "GetString failed");
    DeleteComm(context->diface);
    CloseLibrary(AEDBase);
    AEDBase = NULL;
    memset(context, 0, sizeof(*context));
    return -1;
  }

  context->active = 1;
  error_text[0] = '\0';
  return 0;
}

int aedoor_fetch_username(struct aedoor_context *context, const struct door_config *config, char *error_text, int error_text_size)
{
  char full_name[64];
  char base_name[64];

  if ((context == NULL) || (config == NULL)) {
    bridge_set_error(error_text, error_text_size, "invalid AEDoor fetch request");
    return -1;
  }

  GetDT(context->diface, DT_NAME, NULL);
  bridge_copy_string_field(context, base_name, sizeof(base_name));
  if (base_name[0] == '\0') {
    bridge_set_error(error_text, error_text_size, "AEDoor returned an empty username");
    return -1;
  }

  /* Prefixing is the live multi-BBS routing rule for the shared remote service. */
  if (config->username_prefix[0] != '\0') {
    snprintf(full_name, sizeof(full_name), "%s%s", config->username_prefix, base_name);
    strncpy(context->username, full_name, sizeof(context->username) - 1U);
  } else {
    strncpy(context->username, base_name, sizeof(context->username) - 1U);
  }
  context->username[sizeof(context->username) - 1U] = '\0';

  error_text[0] = '\0';
  return 0;
}

int aedoor_prepare_session(struct aedoor_context *context, const struct door_config *config, char *error_text, int error_text_size)
{
  char number_buffer[32];
  char state_buffer[16];

  if ((context == NULL) || (config == NULL)) {
    bridge_set_error(error_text, error_text_size, "invalid AEDoor session setup");
    return -1;
  }

  /* Disable local pause prompts so the remote side controls the screen flow. */
  if (config->disable_paging) {
    GetDT(context->diface, BB_NONSTOPTEXT, NULL);
    bridge_copy_string_field(context, state_buffer, sizeof(state_buffer));
    if (state_buffer[0] != '\0') {
      context->nonstop_was_enabled = atoi(state_buffer) != 0;
      context->paging_state_known = 1;
    }
    SetDT(context->diface, BB_NONSTOPTEXT, "1");
  }

  bridge_fetch_text_value(context, NODE_DEVICE, context->node_device, sizeof(context->node_device));
  bridge_fetch_text_value(context, NODE_UNIT, number_buffer, sizeof(number_buffer));
  context->node_unit = number_buffer[0] != '\0' ? atoi(number_buffer) : -1;

  bridge_fetch_text_value(context, NODE_BAUDRATE, number_buffer, sizeof(number_buffer));
  context->baud_rate = number_buffer[0] != '\0' ? atoi(number_buffer) : 0;

  bridge_fetch_text_value(context, DT_ISANSI, number_buffer, sizeof(number_buffer));
  context->ansi_capable = number_buffer[0] != '\0' ? atoi(number_buffer) != 0 : 0;

  /*
   * RAWARROW makes AmiExpress return arrow presses as local key codes. The
   * terminal session later translates those into ANSI escape sequences.
   */
  if (context->ansi_capable) {
    SendDataCmd(context->diface, RAWARROW, 0);
    context->raw_arrow_enabled = 1;
  }

  error_text[0] = '\0';
  return 0;
}

void aedoor_write_line(struct aedoor_context *context, const char *text)
{
  bridge_write_line(context, text);
}

void aedoor_write_text(struct aedoor_context *context, const char *text)
{
  bridge_write_text(context, text);
}

void aedoor_write_bytes(struct aedoor_context *context, const unsigned char *data, int length)
{
  char chunk[201];
  int i;
  int out_length;

  if ((context == NULL) || (data == NULL) || (length <= 0)) {
    return;
  }

  /* WriteStr wants clean C strings, so embedded NUL bytes are skipped. */
  out_length = 0;
  for (i = 0; i < length; i++) {
    if (data[i] == 0) {
      if (out_length > 0) {
        chunk[out_length] = '\0';
        bridge_write_text(context, chunk);
        out_length = 0;
      }
      continue;
    }

    chunk[out_length++] = (char) data[i];
    if (out_length >= 200) {
      chunk[out_length] = '\0';
      bridge_write_text(context, chunk);
      out_length = 0;
    }
  }

  if (out_length > 0) {
    chunk[out_length] = '\0';
    bridge_write_text(context, chunk);
  }
}

int aedoor_poll_key(struct aedoor_context *context, long *key_value)
{
  long key;

  if ((context == NULL) || (context->diface == NULL) || (context->string_field == NULL) || (key_value == NULL)) {
    return -1;
  }

  SendCmd(context->diface, GETKEY);
  if (context->string_field[0] != '1') {
    return 0;
  }

  /* Hotkey is only called once AEDoor says a key is already waiting. */
  key = Hotkey(context->diface, "");
  SendDataCmd(context->diface, CON_CURSOR, 1);
  if (key < 0) {
    return -1;
  }

  *key_value = key;
  return 1;
}

void aedoor_set_cursor(struct aedoor_context *context, int visible)
{
  if ((context != NULL) && (context->diface != NULL)) {
    SendDataCmd(context->diface, CON_CURSOR, visible ? 1U : 0U);
  }
}

void aedoor_restore_session(struct aedoor_context *context)
{
  if ((context != NULL) && context->active && context->diface != NULL) {
    /* Put AEDoor back the way we found it before returning to the BBS. */
    if (context->raw_arrow_enabled) {
      SendDataCmd(context->diface, RAWARROW, 0);
      context->raw_arrow_enabled = 0;
    }
    SendDataCmd(context->diface, CON_CURSOR, 1);
    if (context->paging_state_known) {
      SetDT(context->diface, BB_NONSTOPTEXT, context->nonstop_was_enabled ? "1" : "0");
    }
  }
}

void aedoor_close(struct aedoor_context *context)
{
  if (context == NULL) {
    return;
  }

  /* DeleteComm must happen before the library is closed. */
  if (context->diface != NULL) {
    DeleteComm(context->diface);
  }
  if (context->library_base != NULL) {
    CloseLibrary(context->library_base);
  }

  context->active = 0;
  context->diface = NULL;
  context->string_field = NULL;
  context->library_base = NULL;
  AEDBase = NULL;
}
