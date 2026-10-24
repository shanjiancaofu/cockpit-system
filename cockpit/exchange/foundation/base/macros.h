#pragma once

// TypeName is used in declaration syntax where parentheses are not valid.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define COCKPIT_DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;              \
  TypeName& operator=(const TypeName&) = delete
// NOLINTEND(bugprone-macro-parentheses)
