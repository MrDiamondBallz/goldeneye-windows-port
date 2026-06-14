#pragma once

// Wrapper around N64Recomp's generated-code ABI. The local port adds explicit
// declarations for functions that are intentionally replaced by host runtime
// shims during the native-port spike.
#if defined(N64RECOMP_RECOMP_H)
#include N64RECOMP_RECOMP_H
#elif defined(__GNUC__) || defined(__clang__)
#include_next "recomp.h"
#else
#error "Define N64RECOMP_RECOMP_H to the upstream N64Recomp include/recomp.h path."
#endif

// Some original game symbols collide with host libc/libm names. N64Recomp emits
// those names literally, so rename only the generated-code tokens after the host
// headers have already been included by upstream recomp.h.
#define strtol ge_recomp_strtol
#define asin ge_recomp_asin
#define acos ge_recomp_acos
#define isdigit ge_recomp_isdigit
#define toupper ge_recomp_toupper
#define strncmp ge_recomp_strncmp
#define strncpy ge_recomp_strncpy
#define isalpha ge_recomp_isalpha
#define isspace ge_recomp_isspace

#ifdef __cplusplus
extern "C" {
#endif

void sizepropdef(uint8_t* rdram, recomp_context* ctx);
void boot(uint8_t* rdram, recomp_context* ctx);
void resolve_TLBaddress_for_InvalidHit(uint8_t* rdram, recomp_context* ctx);
void initTLBPrepareContext(uint8_t* rdram, recomp_context* ctx);
void debFind(uint8_t* rdram, recomp_context* ctx);
void debAllocate(uint8_t* rdram, recomp_context* ctx);
void debAdd(uint8_t* rdram, recomp_context* ctx);
void debTryAdd(uint8_t* rdram, recomp_context* ctx);
void debInit(uint8_t* rdram, recomp_context* ctx);
void alFxNew(uint8_t* rdram, recomp_context* ctx);
void mempAllocBytesInBank(uint8_t* rdram, recomp_context* ctx);
void mempAddEntryOfSizeToBank(uint8_t* rdram, recomp_context* ctx);
void musicSeqPlayerInit(uint8_t* rdram, recomp_context* ctx);
void decompressdata(uint8_t* rdram, recomp_context* ctx);
void alCSeqNew(uint8_t* rdram, recomp_context* ctx);
void waitForNextFrame(uint8_t* rdram, recomp_context* ctx);
void eqpower(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif
