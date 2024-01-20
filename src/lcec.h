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
/// @brief Header file for LinuxCNC-Ethercat

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
  static void AddTypes(void) { lcec_addtypes(types, __FILE__); }

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
  MODPARAM_TYPE_BIT,    ///< Modparam value is a single bit.
  MODPARAM_TYPE_U32,    ///< Modparam value is an unsigned 32-bit int.
  MODPARAM_TYPE_S32,    ///< Modparam value is a signed 32-bit int.
  MODPARAM_TYPE_FLOAT,  ///< Modparam value is a float.
  MODPARAM_TYPE_STRING  ///< Modparam value is a string.
} lcec_modparam_type_t;

typedef struct {
  const char *name;           ///< the name that appears in the XML.
  int id;                     ///< Numeric ID, should be unique per device driver.
  lcec_modparam_type_t type;  ///< The type (bit, int, float, string) of this modParam.
} lcec_modparam_desc_t;

/// \brief Definition of a device that LinuxCNC-Ethercat supports.
typedef struct {
  char *name;                             ///< The device's name ("EL1008")
  uint32_t vid;                           ///< The EtherCAT vendor ID
  uint32_t pid;                           ///< The EtherCAT product ID
  int pdo_entry_count;                    ///< The number of PDOs registered by this device.
  int is_fsoe_logic;                      ///< Does this device use Safety-over-EtherCAT?
  lcec_slave_preinit_t proc_preinit;      ///< pre-init function, if any
  lcec_slave_init_t proc_init;            ///< init function.  Sets up the device.
  const lcec_modparam_desc_t *modparams;  ///< XML modparams, if any
  uint64_t flags;                         ///< Flags, passed through to `proc_init` as `slave->flags`.
  char *sourcefile;                       ///< Source filename, autopopulated.
} lcec_typelist_t;

/// \brief Linked list for holding device type definitions.
typedef struct lcec_typelinkedlist {
  const lcec_typelist_t *type;       ///< The type definition.
  struct lcec_typelinkedlist *next;  ///< Pointer to the next `lcec_typelinkedlist` in the linked list.
} lcec_typelinkedlist_t;

typedef struct {
  int slave_data_len;   ///< Length of slave data.
  int master_data_len;  ///< Length of master data.
  int data_channels;    ///< Number of data channels.
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
  hal_bit_t *online;        ///< Is device online?  Equivalent to the `.slave-online` HAL pin.
  hal_bit_t *operational;   ///< Is device operational?  Equivalent to the `.slave-oper` HAL pin.
  hal_bit_t *state_init;    ///< Is the device in state `INIT`?  Equivalant to the `.slave-state-init` HAL pin.
  hal_bit_t *state_preop;   ///< Is the device in state `PREOP`?  Equivalant to the `.slave-state-preop` HAL pin.
  hal_bit_t *state_safeop;  ///< Is the device in state `SAFEOP`?  Equivalant to the `.slave-state-safeop` HAL pin.
  hal_bit_t *state_op;      ///< Is the device in state `OP`?  Equivalant to the `.slave-state-op` HAL pin.
} lcec_slave_state_t;

typedef struct lcec_master {
  struct lcec_master *prev;         ///< Next master.
  struct lcec_master *next;         ///< Previous master.
  int index;                        ///< Index of this mater.
  char name[LCEC_CONF_STR_MAXLEN];  ///< Name of master.
  ec_master_t *master;              ///< EtherCAT master structure.
  unsigned long mutex;              ///< Mutex for locking operations.
  int pdo_entry_count;              ///< Number of PDO entry counts registered for master.
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

/// @brief Slave Distributed Clock configuration.
typedef struct {
  uint16_t assignActivate;
  uint32_t sync0Cycle;
  int32_t sync0Shift;
  uint32_t sync1Cycle;
  int32_t sync1Shift;
} lcec_slave_dc_t;

/// @brief Slave Watchdog configuration.
typedef struct {
  uint16_t divider;
  uint16_t intervals;
} lcec_slave_watchdog_t;

/// @brief Slave SDO configuration.
typedef struct {
  uint16_t index;
  int16_t subindex;
  size_t length;
  uint8_t data[];
} lcec_slave_sdoconf_t;

/// @brief Slave IDN configuration.
typedef struct {
  uint8_t drive;
  uint16_t idn;
  ec_al_state_t state;
  size_t length;
  uint8_t data[];
} lcec_slave_idnconf_t;

/// @brief ModParam definition.
typedef struct {
  int id;
  LCEC_CONF_MODPARAM_VAL_T value;
} lcec_slave_modparam_t;

/// @brief EtherCAT slave.
typedef struct lcec_slave {
  struct lcec_slave *prev;                   ///< Next slave
  struct lcec_slave *next;                   ///< Previous slave
  struct lcec_master *master;                ///< Master for this slave
  int index;                                 ///< Index of this slave.
  char name[LCEC_CONF_STR_MAXLEN];           ///< Slave name.
  uint32_t vid;                              ///< Slave's vendor ID
  uint32_t pid;                              ///< Slave's EtherCAT PID/device ID.
  int pdo_entry_count;                       ///< Number of PDO entries for this device.
  ec_sync_info_t *sync_info;                 ///< Sync Manager configuration.
  ec_slave_config_t *config;                 ///< Configuration data.
  ec_slave_config_state_t state;             ///< Slave state.
  lcec_slave_dc_t *dc_conf;                  ///< Distributed Clock configuration.
  lcec_slave_watchdog_t *wd_conf;            ///< Watchdog configuration.
  lcec_slave_preinit_t proc_preinit;         ///< Callback for pre-init, if any.
  lcec_slave_init_t proc_init;               ///< Callback for initializing device.
  lcec_slave_cleanup_t proc_cleanup;         ///< Calback for cleaning up the device.
  lcec_slave_rw_t proc_read;                 ///< Callback for reading from the device.
  lcec_slave_rw_t proc_write;                ///< Callback for writing to the device.
  lcec_slave_state_t *hal_state_data;        ///< HAL state data.
  void *hal_data;                            ///< HAL data, device driver specific.
  ec_pdo_entry_info_t *generic_pdo_entries;  ///< Generic PDO entries.
  ec_pdo_info_t *generic_pdos;               ///< Generic PDOs.
  ec_sync_info_t *generic_sync_managers;     ///< Generic sync managers.
  lcec_slave_sdoconf_t *sdo_config;          ///< SDO config.
  lcec_slave_idnconf_t *idn_config;          ///< IDN config.
  lcec_slave_modparam_t *modparams;          ///< modParams.
  const LCEC_CONF_FSOE_T *fsoeConf;          ///< Safety config.
  int is_fsoe_logic;                         ///< Device supports FSoE safety logic.
  unsigned int *fsoe_slave_offset;           ///< FSoE slave offset.
  unsigned int *fsoe_master_offset;          ///< FSoE master offset.
  uint64_t flags;                            ///< Flags, as defined by the driver itself.
} lcec_slave_t;

/// \brief HAL pin description.
typedef struct {
  hal_type_t type;    ///< HAL type of this pin (`HAL_BIT`, `HAL_FLOAT`, `HAL_S32`, or `HAL_U32`).
  hal_pin_dir_t dir;  ///< Direction for this pin (`HAL_IN`, `HAL_OUT`, or `HAL_IO`).
  int offset;         ///< Offset for this pin's data in `hal_data`.
  const char *fmt;    ///< Format string for generating pin names via sprintf().
} lcec_pindesc_t;

/// \brief Sync manager configuration.
typedef struct {
  int sync_count;                                 ///< Number of syncs.
  ec_sync_info_t *curr_sync;                      ///< Current sync.
  ec_sync_info_t syncs[LCEC_MAX_SYNC_COUNT + 1];  ///< Sync definitions.

  int pdo_info_count;                                ///< Number of PDO infos.
  ec_pdo_info_t *curr_pdo_info;                      ///< Current PDO info.
  ec_pdo_info_t pdo_infos[LCEC_MAX_PDO_INFO_COUNT];  ///< PDO info definitions.

  int pdo_entry_count;                                        ///< Number of PDO entries.
  ec_pdo_entry_info_t *curr_pdo_entry;                        ///< Current PDO entry.
  ec_pdo_entry_info_t pdo_entries[LCEC_MAX_PDO_ENTRY_COUNT];  ///< PDO entry definitions.
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
void lcec_addtype(lcec_typelist_t *type, char *sourcefile);
void lcec_addtypes(lcec_typelist_t types[], char *sourcefile);

#endif
