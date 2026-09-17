#ifndef LENOVO_REGION_LOCKOUT_BYPASS_H
#define LENOVO_REGION_LOCKOUT_BYPASS_H
#include <stdint.h>
int32_t patch_region_lockout_bypass(char* buffer, int32_t size);
int32_t patch_region_message_page_bypass(char* buffer, int32_t size);
#endif /* LENOVO_REGION_LOCKOUT_BYPASS_H */
