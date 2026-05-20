#pragma once

/* Returns a null-terminated path chosen by the user, or NULL if cancelled.
   The pointer is valid until the next call. */
const char * openFileDialog(const char * title);
void         openUrl(const char * url);
