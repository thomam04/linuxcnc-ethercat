//
//    Copyright (C) 2012 Sascha Ittner <sascha.ittner@modusoft.de>
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
/// @brief Ethercat library code

#include "lcec.h"

static int lcec_param_newfv(hal_type_t type, hal_pin_dir_t dir, void *data_addr, const char *fmt, va_list ap);
static int lcec_param_newfv_list(void *base, const lcec_pindesc_t *list, va_list ap);
int lcec_comp_id = -1;

/// @brief Find the slave with a specified index underneath a specific master.
lcec_slave_t *lcec_slave_by_index(struct lcec_master *master, int index) {
  lcec_slave_t *slave;

  for (slave = master->first_slave; slave != NULL; slave = slave->next) {
    if (slave->index == index) {
      return slave;
    }
  }

  return NULL;
}

/// @brief Copy FSoE (Safety over EtherCAT / FailSafe over EtherCAT) data between slaves and masters.
void copy_fsoe_data(struct lcec_slave *slave, unsigned int slave_offset, unsigned int master_offset) {
  lcec_master_t *master = slave->master;
  uint8_t *pd = master->process_data;
  const LCEC_CONF_FSOE_T *fsoeConf = slave->fsoeConf;

  if (fsoeConf == NULL) {
    return;
  }

  if (slave->fsoe_slave_offset != NULL) {
    memcpy(&pd[*(slave->fsoe_slave_offset)], &pd[slave_offset], LCEC_FSOE_SIZE(fsoeConf->data_channels, fsoeConf->slave_data_len));
  }

  if (slave->fsoe_master_offset != NULL) {
    memcpy(&pd[master_offset], &pd[*(slave->fsoe_master_offset)], LCEC_FSOE_SIZE(fsoeConf->data_channels, fsoeConf->master_data_len));
  }
}

/// @brief Initialize syncs to 0.
void lcec_syncs_init(lcec_syncs_t *syncs) { memset(syncs, 0, sizeof(lcec_syncs_t)); }

/// @brief Add a new EtherCAT sync manager configuration.
void lcec_syncs_add_sync(lcec_syncs_t *syncs, ec_direction_t dir, ec_watchdog_mode_t watchdog_mode) {
  syncs->curr_sync = &syncs->syncs[syncs->sync_count];

  syncs->curr_sync->index = syncs->sync_count;
  syncs->curr_sync->dir = dir;
  syncs->curr_sync->watchdog_mode = watchdog_mode;

  (syncs->sync_count)++;
  syncs->syncs[syncs->sync_count].index = 0xff;
}

/// @brief Add a new PDO to an existing sync manager.
void lcec_syncs_add_pdo_info(lcec_syncs_t *syncs, uint16_t index) {
  syncs->curr_pdo_info = &syncs->pdo_infos[syncs->pdo_info_count];

  if (syncs->curr_sync->pdos == NULL) {
    syncs->curr_sync->pdos = syncs->curr_pdo_info;
  }
  (syncs->curr_sync->n_pdos)++;

  syncs->curr_pdo_info->index = index;

  (syncs->pdo_info_count)++;
}

/// @brief Add a new PDO entry to an existing PDO.
void lcec_syncs_add_pdo_entry(lcec_syncs_t *syncs, uint16_t index, uint8_t subindex, uint8_t bit_length) {
  syncs->curr_pdo_entry = &syncs->pdo_entries[syncs->pdo_entry_count];

  if (syncs->curr_pdo_info->entries == NULL) {
    syncs->curr_pdo_info->entries = syncs->curr_pdo_entry;
  }
  (syncs->curr_pdo_info->n_entries)++;

  syncs->curr_pdo_entry->index = index;
  syncs->curr_pdo_entry->subindex = subindex;
  syncs->curr_pdo_entry->bit_length = bit_length;

  (syncs->pdo_entry_count)++;
}

/// @brief Read an SDO configuration from a slave device.
int lcec_read_sdo(struct lcec_slave *slave, uint16_t index, uint8_t subindex, uint8_t *target, size_t size) {
  lcec_master_t *master = slave->master;
  int err;
  size_t result_size;
  uint32_t abort_code;

  if ((err = ecrt_master_sdo_upload(master->master, slave->index, index, subindex, target, size, &result_size, &abort_code))) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "slave %s.%s: Failed to execute SDO upload (0x%04x:0x%02x, error %d, abort_code %08x)\n",
        master->name, slave->name, index, subindex, err, abort_code);
    return -1;
  }

  if (result_size != size) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "slave %s.%s: Invalid result size on SDO upload (0x%04x:0x%02x, req: %u, res: %u)\n",
        master->name, slave->name, index, subindex, (unsigned int)size, (unsigned int)result_size);
    return -1;
  }

  return 0;
}

/// @brief Read IDN data from a slave device.
int lcec_read_idn(struct lcec_slave *slave, uint8_t drive_no, uint16_t idn, uint8_t *target, size_t size) {
  lcec_master_t *master = slave->master;
  int err;
  size_t result_size;
  uint16_t error_code;

  if ((err = ecrt_master_read_idn(master->master, slave->index, drive_no, idn, target, size, &result_size, &error_code))) {
    rtapi_print_msg(RTAPI_MSG_ERR,
        LCEC_MSG_PFX "slave %s.%s: Failed to execute IDN read (drive %u idn %c-%u-%u, error %d, error_code %08x)\n", master->name,
        slave->name, drive_no, (idn & 0x8000) ? 'P' : 'S', (idn >> 12) & 0x0007, idn & 0x0fff, err, error_code);
    return -1;
  }

  if (result_size != size) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "slave %s.%s: Invalid result size on IDN read (drive %u idn %c-%d-%d, req: %u, res: %u)\n",
        master->name, slave->name, drive_no, (idn & 0x8000) ? 'P' : 'S', (idn >> 12) & 0x0007, idn & 0x0fff, (unsigned int)size,
        (unsigned int)result_size);
    return -1;
  }

  return 0;
}

static int lcec_param_newfv(hal_type_t type, hal_pin_dir_t dir, void *data_addr, const char *fmt, va_list ap) {
  char name[HAL_NAME_LEN + 1];
  int sz;
  int err;

  sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap);
  if (sz == -1 || sz > HAL_NAME_LEN) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "length %d too long for name starting '%s'\n", sz, name);
    return -ENOMEM;
  }

  err = hal_param_new(name, type, dir, data_addr, lcec_comp_id);
  if (err) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exporting param %s failed\n", name);
    return err;
  }

  switch (type) {
    case HAL_BIT:
      *((hal_bit_t *)data_addr) = 0;
      break;
    case HAL_FLOAT:
      *((hal_float_t *)data_addr) = 0.0;
      break;
    case HAL_S32:
      *((hal_s32_t *)data_addr) = 0;
      break;
    case HAL_U32:
      *((hal_u32_t *)data_addr) = 0;
      break;
    default:
      break;
  }

  return 0;
}

/// @brief Create a new LinuxCNC `param` dynamically.
int lcec_param_newf(hal_type_t type, hal_pin_dir_t dir, void *data_addr, const char *fmt, ...) {
  va_list ap;
  int err;

  va_start(ap, fmt);
  err = lcec_param_newfv(type, dir, data_addr, fmt, ap);
  va_end(ap);

  return err;
}

static int lcec_param_newfv_list(void *base, const lcec_pindesc_t *list, va_list ap) {
  va_list ac;
  int err;
  const lcec_pindesc_t *p;

  for (p = list; p->type != HAL_TYPE_UNSPECIFIED; p++) {
    va_copy(ac, ap);
    err = lcec_param_newfv(p->type, p->dir, (void *)(base + p->offset), p->fmt, ac);
    va_end(ac);
    if (err) {
      return err;
    }
  }

  return 0;
}

/// @brief Create a list of new LinuxCNC params dynamically, using sprintf() to create names.
int lcec_param_newf_list(void *base, const lcec_pindesc_t *list, ...) {
  va_list ap;
  int err;

  va_start(ap, list);
  err = lcec_param_newfv_list(base, list, ap);
  va_end(ap);

  return err;
}

/// @brief Get an XML `<modParam>` value for a specified slave.
LCEC_CONF_MODPARAM_VAL_T *lcec_modparam_get(struct lcec_slave *slave, int id) {
  lcec_slave_modparam_t *p;

  if (slave->modparams == NULL) {
    return NULL;
  }

  for (p = slave->modparams; p->id >= 0; p++) {
    if (p->id == id) {
      return &p->value;
    }
  }

  return NULL;
}
