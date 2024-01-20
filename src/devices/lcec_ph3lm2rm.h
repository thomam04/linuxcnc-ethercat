//
//    Copyright (C) 2011 Sascha Ittner <sascha.ittner@modusoft.de>
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

#ifndef _LCEC_PH3LM2RM_H_
#define _LCEC_PH3LM2RM_H_

#include "../lcec.h"

#define LCEC_PH3LM2RM_VID LCEC_MODUSOFT_VID

#define LCEC_PH3LM2RM_RM_COUNT 2
#define LCEC_PH3LM2RM_LM_COUNT 3

#define LCEC_PH3LM2RM_RM_PDOS 8
#define LCEC_PH3LM2RM_LM_PDOS 8

#define LCEC_PH3LM2RM_PDOS \
  (2 + (LCEC_PH3LM2RM_RM_PDOS * LCEC_PH3LM2RM_RM_COUNT) + (LCEC_PH3LM2RM_LM_PDOS * LCEC_PH3LM2RM_LM_COUNT))
#endif
