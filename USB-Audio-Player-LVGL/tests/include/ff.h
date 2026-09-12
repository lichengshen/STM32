#ifndef TEST_FF_H
#define TEST_FF_H

#include <stdint.h>
#include <string.h>

typedef uint8_t BYTE;
typedef uint32_t UINT;
typedef uint32_t FSIZE_t;

typedef enum {
  FR_OK = 0,
  FR_DISK_ERR,
} FRESULT;

typedef struct {
  const uint8_t *data;
  FSIZE_t size;
  FSIZE_t position;
} FIL;

static inline FRESULT f_lseek(FIL *file, FSIZE_t offset)
{
  if (offset > file->size) return FR_DISK_ERR;
  file->position = offset;
  return FR_OK;
}

static inline FRESULT f_read(FIL *file, void *buffer, UINT length, UINT *read)
{
  const FSIZE_t remaining = file->size - file->position;
  const UINT count = remaining < length ? (UINT)remaining : length;
  memcpy(buffer, &file->data[file->position], count);
  file->position += count;
  *read = count;
  return FR_OK;
}

#define f_size(file) ((file)->size)

#endif
