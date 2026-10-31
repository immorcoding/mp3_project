#ifndef RAMDISK_H
#define RAMDISK_H
void ramdisk_init(unsigned long total_sectors);
unsigned long ramdisk_allocated_bytes(void);
void ramdisk_reset_counters(void);
unsigned long long ramdisk_calls(void);
unsigned long long ramdisk_sectors(void);
int ramdisk_dump(const char *file);
#endif
