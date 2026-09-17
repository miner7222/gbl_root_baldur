#ifndef LENOVO_CMDLINE_REGION_OVERRIDE_H
#define LENOVO_CMDLINE_REGION_OVERRIDE_H
#include <stdint.h>
int32_t patch_pcbaidinfo_override(char* buffer, int32_t size, const char* region);
int32_t patch_hqsysfs_pcba_config_override(char* buffer, int32_t size,
                                           const char* region);
#endif /* LENOVO_CMDLINE_REGION_OVERRIDE_H */
