/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef AARCH64_TARGET_FCNTL_H
#define AARCH64_TARGET_FCNTL_H

#define TARGET_O_DIRECTORY      040000
#define TARGET_O_NOFOLLOW      0100000
#define TARGET_O_DIRECT        0200000

#include "../generic/fcntl.h"
#endif
