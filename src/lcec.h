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
#ifndef _LCEC_H_
#define _LCEC_H_

#include "ecrt.h"
#include "hal.h"
#include "lcec_conf.h"
#include "lcec_rtapi.h"
#include "rtapi_ctype.h"
#include "rtapi_math.h"
#include "rtapi_string.h"

// list macros
#define LCEC_LIST_APPEND(first, last, item) \
  do {                                      \
    (item)->prev = (last);                  \
    if ((item)->prev != NULL) {             \
      (item)->prev->next = (item);          \
    } else {                                \
      (first) = (item);                     \
    }                                       \
    (last) = (item);                        \
  } while (0);

// pdo macros
#define LCEC_PDO_INIT(pdo, pos, vid, pid, idx, sidx, off, bpos) \
  do {                                                          \
    pdo->position = pos;                                        \
    pdo->vendor_id = vid;                                       \
    pdo->product_code = pid;                                    \
    pdo->index = idx;                                           \
    pdo->subindex = sidx;                                       \
    pdo->offset = off;                                          \
    pdo->bit_position = bpos;                                   \
    pdo++;                                                      \
  } while (0);

#define LCEC_MSG_PFX "LCEC: "

// init macro; this will make GCC run calls to AddTypes() before
// main() is called.  This is used to register new slave types
// dynamically without needing a giant list in lcec_main.c.
#define ADD_TYPES(types)                                   \
  static void AddTypes(void) __attribute__((constructor)); \
  static void AddTypes(void) { lcec_addtypes(types); }

// vendor ids, please keep sorted.
#define LCEC_BECKHOFF_VID 0x00000002
#define LCEC_OMRON_VID    0x00000083
#define LCEC_STOEBER_VID  0x000000b9
#define LCEC_SMC_VID      0x00000114
#define LCEC_DELTA_VID    0x000001dd
#define LCEC_ABET_VID     0x0000079A
#define LCEC_MODUSOFT_VID 0x00000907

// State update period (ns)
#define LCEC_STATE_UPDATE_PERIOD 1000000000LL

// IDN builder
#define LCEC_IDN_TYPE_P 0x8000
#define LCEC_IDN_TYPE_S 0x0000

#define LCEC_IDN(type, set, block) (type | ((set & 0x07) << 12) | (block & 0x0fff))

#define LCEC_FSOE_CMD_LEN    1
#define LCEC_FSOE_CRC_LEN    2
#define LCEC_FSOE_CONNID_LEN 2

#define LCEC_FSOE_SIZE(ch_count, data_len) (LCEC_FSOE_CMD_LEN + ch_count * (data_len + LCEC_FSOE_CRC_LEN) + LCEC_FSOE_CONNID_LEN)

#define LCEC_MAX_PDO_ENTRY_COUNT 32
#define LCEC_MAX_PDO_INFO_COUNT  8
#define LCEC_MAX_SYNC_COUNT      4

struct lcec_master;
struct lcec_slave;

typedef int (*lcec_slave_preinit_t)(struct lcec_slave *slave);
typedef int (*lcec_slave_init_t)(int comp_id, struct lcec_slave *slave, ec_pdo_entry_reg_t *pdo_entry_regs);
typedef void (*lcec_slave_cleanup_t)(struct lcec_slave *slave);
typedef void (*lcec_slave_rw_t)(struct lcec_slave *slave, long period);

typedef enum {
  MODPARAM_TYPE_BIT,
  MODPARAM_TYPE_U32,
  MODPARAM_TYPE_S32,
  MODPARAM_TYPE_FLOAT,
  MODPARAM_TYPE_STRING
} lcec_modparam_type_t;

typedef struct {
  const char *name;
  int id;
  lcec_modparam_type_t type;
} lcec_modparam_desc_t;

typedef struct lcec_typelist {
  char *name;
  uint32_t vid;
  uint32_t pid;
  int pdo_entry_count;
  int is_fsoe_logic;
  lcec_slave_preinit_t proc_preinit;
  lcec_slave_init_t proc_init;
  const lcec_modparam_desc_t *modparams;
  uint64_t flags;
} lcec_typelist_t;

typedef struct lcec_typelinkedlist {
  const lcec_typelist_t *type;
  struct lcec_typelinkedlist *next;
} lcec_typelinkedlist_t;

typedef struct {
  int slave_data_len;
  int master_data_len;
  int data_channels;
} LCEC_CONF_FSOE_T;

typedef struct lcec_master_data {
  hal_u32_t *slaves_responding;
  hal_bit_t *state_init;
  hal_bit_t *state_preop;
  hal_bit_t *state_safeop;
  hal_bit_t *state_op;
  hal_bit_t *link_up;
  hal_bit_t *all_op;
#ifdef RTAPI_TASK_PLL_SUPPORT
  hal_s32_t *pll_err;
  hal_s32_t *pll_out;
  hal_u32_t pll_step;
  hal_u32_t pll_max_err;
  hal_u32_t *pll_reset_cnt;
#endif
} lcec_master_data_t;

typedef struct lcec_slave_state {
  hal_bit_t *online;
  hal_bit_t *operational;
  hal_bit_t *state_init;
  hal_bit_t *state_preop;
  hal_bit_t *state_safeop;
  hal_bit_t *state_op;
} lcec_slave_state_t;

typedef struct lcec_master {
  struct lcec_master *prev;
  struct lcec_master *next;
  int index;
  char name[LCEC_CONF_STR_MAXLEN];
  ec_master_t *master;
  unsigned long mutex;
  int pdo_entry_count;
  ec_pdo_entry_reg_t *pdo_entry_regs;
  ec_domain_t *domain;
  uint8_t *process_data;
  int process_data_len;
  struct lcec_slave *first_slave;
  struct lcec_slave *last_slave;
  lcec_master_data_t *hal_data;
  uint64_t app_time_base;
  uint32_t app_time_period;
  long period_last;
  int sync_ref_cnt;
  int sync_ref_cycles;
  long long state_update_timer;
  ec_master_state_t ms;
#ifdef RTAPI_TASK_PLL_SUPPORT
  uint64_t dc_ref;
  uint32_t app_time_last;
  int dc_time_valid_last;
#endif
} lcec_master_t;

typedef struct {
  uint16_t assignActivate;
  uint32_t sync0Cycle;
  int32_t sync0Shift;
  uint32_t sync1Cycle;
  int32_t sync1Shift;
} lcec_slave_dc_t;

typedef struct {
  uint16_t divider;
  uint16_t intervals;
} lcec_slave_watchdog_t;

typedef struct {
  uint16_t index;
  int16_t subindex;
  size_t length;
  uint8_t data[];
} lcec_slave_sdoconf_t;

typedef struct {
  uint8_t drive;
  uint16_t idn;
  ec_al_state_t state;
  size_t length;
  uint8_t data[];
} lcec_slave_idnconf_t;

typedef struct {
  int id;
  LCEC_CONF_MODPARAM_VAL_T value;
} lcec_slave_modparam_t;

typedef struct lcec_slave {
  struct lcec_slave *prev;
  struct lcec_slave *next;
  struct lcec_master *master;
  int index;
  char name[LCEC_CONF_STR_MAXLEN];
  uint32_t vid;
  uint32_t pid;
  int pdo_entry_count;
  ec_sync_info_t *sync_info;
  ec_slave_config_t *config;
  ec_slave_config_state_t state;
  lcec_slave_dc_t *dc_conf;
  lcec_slave_watchdog_t *wd_conf;
  lcec_slave_preinit_t proc_preinit;
  lcec_slave_init_t proc_init;
  lcec_slave_cleanup_t proc_cleanup;
  lcec_slave_rw_t proc_read;
  lcec_slave_rw_t proc_write;
  lcec_slave_state_t *hal_state_data;
  void *hal_data;
  ec_pdo_entry_info_t *generic_pdo_entries;
  ec_pdo_info_t *generic_pdos;
  ec_sync_info_t *generic_sync_managers;
  lcec_slave_sdoconf_t *sdo_config;
  lcec_slave_idnconf_t *idn_config;
  lcec_slave_modparam_t *modparams;
  const LCEC_CONF_FSOE_T *fsoeConf;
  int is_fsoe_logic;
  unsigned int *fsoe_slave_offset;
  unsigned int *fsoe_master_offset;
  uint64_t flags;
} lcec_slave_t;

typedef struct {
  hal_type_t type;
  hal_pin_dir_t dir;
  int offset;
  const char *fmt;
} lcec_pindesc_t;

typedef struct {
  int sync_count;
  ec_sync_info_t *curr_sync;
  ec_sync_info_t syncs[LCEC_MAX_SYNC_COUNT + 1];

  int pdo_info_count;
  ec_pdo_info_t *curr_pdo_info;
  ec_pdo_info_t pdo_infos[LCEC_MAX_PDO_INFO_COUNT];

  int pdo_entry_count;
  ec_pdo_entry_info_t *curr_pdo_entry;
  ec_pdo_entry_info_t pdo_entries[LCEC_MAX_PDO_ENTRY_COUNT];
} lcec_syncs_t;

int lcec_read_sdo(struct lcec_slave *slave, uint16_t index, uint8_t subindex, uint8_t *target, size_t size);
int lcec_read_idn(struct lcec_slave *slave, uint8_t drive_no, uint16_t idn, uint8_t *target, size_t size);

int lcec_pin_newf(hal_type_t type, hal_pin_dir_t dir, void **data_ptr_addr, const char *fmt, ...);
int lcec_pin_newf_list(void *base, const lcec_pindesc_t *list, ...);
int lcec_param_newf(hal_type_t type, hal_pin_dir_t dir, void *data_addr, const char *fmt, ...);
int lcec_param_newf_list(void *base, const lcec_pindesc_t *list, ...);

LCEC_CONF_MODPARAM_VAL_T *lcec_modparam_get(struct lcec_slave *slave, int id);

lcec_slave_t *lcec_slave_by_index(struct lcec_master *master, int index);

void copy_fsoe_data(struct lcec_slave *slave, unsigned int slave_offset, unsigned int master_offset);

void lcec_syncs_init(lcec_syncs_t *syncs);
void lcec_syncs_add_sync(lcec_syncs_t *syncs, ec_direction_t dir, ec_watchdog_mode_t watchdog_mode);
void lcec_syncs_add_pdo_info(lcec_syncs_t *syncs, uint16_t index);
void lcec_syncs_add_pdo_entry(lcec_syncs_t *syncs, uint16_t index, uint8_t subindex, uint8_t bit_length);
const lcec_typelist_t *lcec_findslavetype(const char *name);
void lcec_addtype(const lcec_typelist_t *type);
void lcec_addtypes(const lcec_typelist_t types[]);

#endif
