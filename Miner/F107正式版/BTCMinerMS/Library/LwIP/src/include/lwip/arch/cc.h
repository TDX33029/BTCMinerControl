#ifndef __LWIP_ARCH_CC_H__
#define __LWIP_ARCH_CC_H__

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ARMCC (Keil MDK v5) specific */
#if defined(__CC_ARM)
  #define LWIP_PLATFORM_DIAG(x)  do { } while(0)
  #define LWIP_PLATFORM_ASSERT(x)
  #define PACK_STRUCT_FIELD(x)    x
  #define PACK_STRUCT_STRUCT      __attribute__((packed))
  #define PACK_STRUCT_BEGIN
  #define PACK_STRUCT_END
#elif defined(__GNUC__)
  #define LWIP_PLATFORM_DIAG(x)  do { } while(0)
  #define LWIP_PLATFORM_ASSERT(x)
  #define PACK_STRUCT_FIELD(x)    x
  #define PACK_STRUCT_STRUCT      __attribute__((packed))
  #define PACK_STRUCT_BEGIN
  #define PACK_STRUCT_END
#endif

typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uint64_t  u64_t;
typedef int64_t   s64_t;
typedef uintptr_t mem_ptr_t;

#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_PLATFORM_BYTESWAP 1

/* LwIP NO_SYS requires sys_prot_t for critical sections */
typedef u32_t sys_prot_t;
#define LWIP_PLATFORM_HTONS(x) ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))
#define LWIP_PLATFORM_HTONL(x) ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | (((x) >> 8) & 0xFF00) | (((x) >> 24) & 0xFF))

#endif
