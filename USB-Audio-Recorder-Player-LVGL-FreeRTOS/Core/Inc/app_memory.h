#ifndef APP_MEMORY_H
#define APP_MEMORY_H

#include <stdint.h>

typedef struct {
  uint32_t sample_count;
  uint32_t free_heap_bytes;
  uint32_t min_free_heap_bytes;
  /* C malloc grows a separate heap above the linker's _end symbol. */
  uint32_t c_heap_current_bytes;
  uint32_t c_heap_peak_bytes;
  /* Stack values are the lowest unused space seen since task creation. */
  uint32_t storage_stack_min_free_bytes;
  uint32_t audio_stack_min_free_bytes;
  uint32_t ui_stack_min_free_bytes;
  uint32_t usb_task_present;
  uint32_t usb_stack_min_free_bytes;
  uint32_t queue_capacity;
  uint32_t queue_item_bytes;
  uint32_t queue_pending;
} AppMemoryStats;

#endif /* APP_MEMORY_H */
