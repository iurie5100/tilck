/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This is a TEMPLATE. The actual config file is generated by CMake and stored
 * in <BUILD_DIR>/tilck_gen_headers/.
 */

#pragma once
#include <tilck_gen_headers/config_global.h>

/* --------- Boolean config variables --------- */

#cmakedefine01 KMALLOC_FREE_MEM_POISONING
#cmakedefine01 KMALLOC_HEAVY_STATS
#cmakedefine01 KMALLOC_SUPPORT_DEBUG_LOG
#cmakedefine01 KMALLOC_SUPPORT_LEAK_DETECTOR

/*
 * --------------------------------------------------------------------------
 *                  Hard-coded global & derived constants
 * --------------------------------------------------------------------------
 *
 * Here below there are many dervied constants and convenience constants not
 * designed to be easily changed because of the code makes assumptions about
 * them. Because of that, those constants are hard-coded and not available as
 * CMake variables. With time, some of those constants get "promoted" and moved
 * in CMake, others remain here. See the comments and think about the potential
 * implications before promoting a hard-coded constant to a configurable CMake
 * variable.
 */

#if KERNEL_MAX_SIZE <= 1024 * KB
   #if TINY_KERNEL
      #define KMALLOC_FIRST_HEAP_SIZE    (  64 * KB)
   #else
      #define KMALLOC_FIRST_HEAP_SIZE    ( 128 * KB)
   #endif
#else
   #define KMALLOC_FIRST_HEAP_SIZE    ( 512 * KB)
#endif
