#pragma once

#include "cockpit/core/base/macros.h"

#define COCKPIT_DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;              \
  TypeName& operator=(const TypeName&) = delete
