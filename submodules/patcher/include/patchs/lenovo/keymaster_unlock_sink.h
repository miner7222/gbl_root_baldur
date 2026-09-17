#ifndef LENOVO_KEYMASTER_UNLOCK_SINK_H
#define LENOVO_KEYMASTER_UNLOCK_SINK_H
#include <stdint.h>
#include <stdbool.h>
bool patch_keymaster_unlock_sink(char* buffer, int32_t size, int32_t anchor_off);
#endif /* LENOVO_KEYMASTER_UNLOCK_SINK_H */
