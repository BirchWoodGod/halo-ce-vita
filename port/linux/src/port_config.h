/*
PORT_CONFIG.H

The native ports' settings, read from config.toml (port_config.c): next to
the executable on the desktop, in the data folder (the one holding maps/)
on Android. A missing file is written with the defaults. Each setting can
also be set for one run with its HALO_* environment variable, which wins
over the file (the tools and the Android app pass settings that way).

Settings are named "section.key", as in the file: "display.vsync".
*/

#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#include <stddef.h>

int config_boolean(const char *name);
/* whether this build offers ad hoc play (network.adhoc): the Vitas', not
the PC build's; network.adhoc is always off where it does not */
int config_adhoc_offered(void);
long config_integer(const char *name);
double config_real(const char *name);
/* never NULL; "" when unset */
const char *config_string(const char *name);
/* the folder config.toml is in, with its separator */
void config_folder(char *path, size_t size);
/* a whole file, NUL terminated, or NULL; free() it */
char *config_file_read(const char *path, size_t *size);
/* a whole file written (replaced); 1 on success */
int config_file_write(const char *path, const char *text, size_t size);
/* sets a boolean setting, and writes it into config.toml (only its line
changes); 1 on success */
int config_write_boolean(const char *name, int value);
/* the same for a string setting (written as a TOML string, escaped) and a
real one; each write also bumps halo_settings_generation, so the readers
that keep a setting read it again */
int config_write_string(const char *name, const char *value);
int config_write_real(const char *name, double value);
/* a setting's default as text (a string's without its quotes); 1 if the
setting exists */
int config_default_text_of(const char *name, char *text, size_t size);
/* the brightness setting as the strength of the display pass's curve
(port_config.c); 0: the picture as drawn */
float halo_brightness_curve(void);
/* bumped whenever a setting changes mid-game (port_config.c) */
extern volatile unsigned long halo_settings_generation;
/* sets a setting for this run only (the file is left alone), from text as
its environment variable would; 1 if the setting exists */
int config_set_override(const char *name, const char *text);

#endif
