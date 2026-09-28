#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>

typedef enum {
  APP_EVENT_TASK_STARTED = 1
} AppEventKind;

typedef enum {
  APP_SOURCE_AUDIO = 1u << 0,
  APP_SOURCE_STORAGE = 1u << 1
} AppEventSource;

typedef struct {
  AppEventKind kind;
  AppEventSource source;
  uint32_t detail;
} AppEvent;

typedef enum {
  APP_FAULT_NONE = 0,
  APP_FAULT_QUEUE_CREATE,
  APP_FAULT_QUEUE_SEND,
  APP_FAULT_QUEUE_RECEIVE
} AppFault;

#endif /* APP_EVENTS_H */
