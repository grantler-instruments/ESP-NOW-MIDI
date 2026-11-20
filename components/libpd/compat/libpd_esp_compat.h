/* ESP-IDF glue for libpd. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Register the pure-data/extra objects compiled into this component
 * (bob~, bonk~, choice, fiddle~, loop~, lrshift~, pique, sigmund~).
 * Call once, right after libpd_init(). */
void libpd_esp_extras_setup(void);

#ifdef __cplusplus
}
#endif
