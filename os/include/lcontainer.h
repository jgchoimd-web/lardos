#pragma once

#include <stdint.h>
#include "syscall.h"

#define LCONTAINER_MAX       6
#define LCONTAINER_NAME_MAX  16

void lcontainer_init(void);
int lcontainer_create(const char* name, uint32_t caps);
int lcontainer_remove(const char* name);
int lcontainer_use(const char* name);
void lcontainer_exit(void);
int lcontainer_has_active(void);
const char* lcontainer_active_name(void);
uint32_t lcontainer_active_caps(void);
uint32_t lcontainer_profile_caps(const char* profile);
uint32_t lcontainer_count(void);
int lcontainer_get(uint32_t idx, const char** name, uint32_t* caps, uint32_t* runs, int* active);
int lcontainer_run(const char* name, const char* path, int argc, const char** argv);
const char* lcontainer_profile_name(uint32_t caps);
void lcontainer_caps_text(uint32_t caps, char* out, uint32_t cap);
