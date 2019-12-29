/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/mods/tracing.h>

extern const struct syscall_info *tracing_metadata;

extern const struct sys_param_type ptype_int;
extern const struct sys_param_type ptype_voidp;
extern const struct sys_param_type ptype_oct;
extern const struct sys_param_type ptype_errno_or_val;
extern const struct sys_param_type ptype_buffer;
extern const struct sys_param_type ptype_path;
