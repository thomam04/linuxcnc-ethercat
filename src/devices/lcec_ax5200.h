//
//    Copyright (C) 2018 Sascha Ittner <sascha.ittner@modusoft.de>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//

/// @file
/// @brief Driver for Beckhoff AX5200 Servo controllers

#ifndef _LCEC_AX5200_H_
#define _LCEC_AX5200_H_

#include "../lcec.h"
#include "lcec_class_ax5.h"

#define LCEC_AX5200_CHANS 2
#define LCEC_AX5200_PDOS (LCEC_AX5200_CHANS * LCEC_CLASS_AX5_PDOS)

/*static*/ int lcec_ax5200_preinit(struct lcec_slave *slave);
#endif
