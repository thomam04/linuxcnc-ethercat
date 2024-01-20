//
//    Copyright (C) 2023 Sascha Ittner <sascha.ittner@modusoft.de>
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
/// @brief Driver for Beckhoff EL5032 Encoder modules

#include "../lcec.h"
#include "lcec_el5032.h"

static int lcec_el5032_init(int comp_id, struct lcec_slave *slave, ec_pdo_entry_reg_t *pdo_entry_regs);

static lcec_typelist_t types[]={
  { "EL5032", LCEC_BECKHOFF_VID, 0x13a83052, LCEC_EL5032_PDOS, 0, NULL, lcec_el5032_init},
  { NULL },
};
ADD_TYPES(types);

typedef struct {
  hal_bit_t *reset;
  hal_bit_t *abs_mode;
  hal_bit_t *warn;
  hal_bit_t *error;
  hal_bit_t *ready;
  hal_bit_t *diag;
  hal_bit_t *tx_state;
  hal_u32_t *cyc_cnt;
  hal_u32_t *raw_count_lo;
  hal_u32_t *raw_count_hi;
  hal_s32_t *count;
  hal_float_t *pos;
  hal_float_t *pos_scale;

  unsigned int warn_os;
  unsigned int warn_bp;
  unsigned int error_os;
  unsigned int error_bp;
  unsigned int ready_os;
  unsigned int ready_bp;
  unsigned int diag_os;
  unsigned int diag_bp;
  unsigned int tx_state_os;
  unsigned int tx_state_bp;
  unsigned int cyc_cnt_os;
  unsigned int cyc_cnt_bp;
  unsigned int count_pdo_os;

  int do_init;
  int64_t last_count;
  double old_scale;
  double scale;
} lcec_el5032_chan_t;

typedef struct {
  lcec_el5032_chan_t chans[LCEC_EL5032_CHANS];
  int last_operational;
} lcec_el5032_data_t;

static const lcec_pindesc_t slave_pins[] = {
  { HAL_BIT, HAL_IN, offsetof(lcec_el5032_chan_t, reset), "%s.%s.%s.enc-%d-reset" },
  { HAL_BIT, HAL_IN, offsetof(lcec_el5032_chan_t, abs_mode), "%s.%s.%s.enc-%d-abs-mode" },
  { HAL_BIT, HAL_OUT, offsetof(lcec_el5032_chan_t, warn), "%s.%s.%s.enc-%d-warn" },
  { HAL_BIT, HAL_OUT, offsetof(lcec_el5032_chan_t, error), "%s.%s.%s.enc-%d-error" },
  { HAL_BIT, HAL_OUT, offsetof(lcec_el5032_chan_t, ready), "%s.%s.%s.enc-%d-ready" },
  { HAL_BIT, HAL_OUT, offsetof(lcec_el5032_chan_t, diag), "%s.%s.%s.enc-%d-diag" },
  { HAL_BIT, HAL_OUT, offsetof(lcec_el5032_chan_t, tx_state), "%s.%s.%s.enc-%d-tx-state" },
  { HAL_S32, HAL_OUT, offsetof(lcec_el5032_chan_t, cyc_cnt), "%s.%s.%s.enc-%d-cyc-cnt" },
  { HAL_S32, HAL_OUT, offsetof(lcec_el5032_chan_t, raw_count_lo), "%s.%s.%s.enc-%d-raw-count-lo" },
  { HAL_S32, HAL_OUT, offsetof(lcec_el5032_chan_t, raw_count_hi), "%s.%s.%s.enc-%d-raw-count-hi" },
  { HAL_S32, HAL_OUT, offsetof(lcec_el5032_chan_t, count), "%s.%s.%s.enc-%d-count" },
  { HAL_FLOAT, HAL_OUT, offsetof(lcec_el5032_chan_t, pos), "%s.%s.%s.enc-%d-pos" },
  { HAL_FLOAT, HAL_IO, offsetof(lcec_el5032_chan_t, pos_scale), "%s.%s.%s.enc-%d-pos-scale" },
  { HAL_TYPE_UNSPECIFIED, HAL_DIR_UNSPECIFIED, -1, NULL }
};

static ec_pdo_entry_info_t lcec_el5032_channel1_in[] = {
   {0x6000, 0x01,  1}, // warning
   {0x6000, 0x02,  1}, // error
   {0x6000, 0x03,  1}, // ready
   {0x0000, 0x00,  5}, // Gap
   {0x0000, 0x00,  4}, // Gap
   {0x6000, 0x0d,  1}, // diag
   {0x6000, 0x0e,  1}, // TxPDO state
   {0x6000, 0x0f,  2}, // cycle counter
   {0x6000, 0x11, 64}, // counter
};

static ec_pdo_entry_info_t lcec_el5032_channel2_in[] = {
   {0x6010, 0x01,  1}, // warning
   {0x6010, 0x02,  1}, // error
   {0x6010, 0x03,  1}, // ready
   {0x0000, 0x00,  5}, // Gap
   {0x0000, 0x00,  4}, // Gap
   {0x6010, 0x0d,  1}, // diag
   {0x6010, 0x0e,  1}, // TxPDO state
   {0x6010, 0x0f,  2}, // cycle counter
   {0x6010, 0x11, 64}, // counter
};

static ec_pdo_info_t lcec_el5032_pdos_in[] = {
    {0x1A00, 9, lcec_el5032_channel1_in},
    {0x1A01, 9, lcec_el5032_channel2_in}
};

static ec_sync_info_t lcec_el5032_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL},
    {1, EC_DIR_INPUT,  0, NULL},
    {2, EC_DIR_OUTPUT, 0, NULL},
    {3, EC_DIR_INPUT,  2, lcec_el5032_pdos_in},
    {0xff}
};

static void lcec_el5032_read(struct lcec_slave *slave, long period);

static int lcec_el5032_init(int comp_id, struct lcec_slave *slave, ec_pdo_entry_reg_t *pdo_entry_regs) {
  lcec_master_t *master = slave->master;
  lcec_el5032_data_t *hal_data;
  int i;
  lcec_el5032_chan_t *chan;
  int err;

  // initialize callbacks
  slave->proc_read = lcec_el5032_read;

  // alloc hal memory
  if ((hal_data = hal_malloc(sizeof(lcec_el5032_data_t))) == NULL) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "hal_malloc() for slave %s.%s failed\n", master->name, slave->name);
    return -EIO;
  }
  memset(hal_data, 0, sizeof(lcec_el5032_data_t));
  slave->hal_data = hal_data;

  // initialize sync info
  slave->sync_info = lcec_el5032_syncs;

  // initialize global data
  hal_data->last_operational = 0;

  // initialize pins
  for (i=0; i<LCEC_EL5032_CHANS; i++) {
    chan = &hal_data->chans[i];

    // initialize POD entries
    LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000 + (i << 4), 0x01, &chan->warn_os, &chan->warn_bp);
    LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000 + (i << 4), 0x02, &chan->error_os, &chan->error_bp);
    LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000 + (i << 4), 0x03, &chan->ready_os, &chan->ready_bp);
    LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000 + (i << 4), 0x0d, &chan->diag_os, &chan->diag_bp);
    LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000 + (i << 4), 0x0e, &chan->tx_state_os, &chan->tx_state_bp);
    LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000 + (i << 4), 0x0f, &chan->cyc_cnt_os, &chan->cyc_cnt_bp);
    LCEC_PDO_INIT(pdo_entry_regs, slave->index, slave->vid, slave->pid, 0x6000 + (i << 4), 0x11, &chan->count_pdo_os, NULL);

    // export pins
    if ((err = lcec_pin_newf_list(chan, slave_pins, LCEC_MODULE_NAME, master->name, slave->name, i)) != 0) {
      return err;
    }

    // initialize pins
    *(chan->pos_scale) = 1.0;

    // initialize variables
    chan->do_init = 1;
    chan->last_count = 0;
    chan->old_scale = *(chan->pos_scale) + 1.0;
    chan->scale = 1.0;
  }

  return 0;
}

static void lcec_el5032_read(struct lcec_slave *slave, long period) {
  lcec_master_t *master = slave->master;
  lcec_el5032_data_t *hal_data = (lcec_el5032_data_t *) slave->hal_data;
  uint8_t *pd = master->process_data;
  int i;
  lcec_el5032_chan_t *chan;
  int64_t raw_count, raw_delta;

  // wait for slave to be operational
  if (!slave->state.operational) {
    hal_data->last_operational = 0;
    return;
  }

  // check inputs
  for (i=0; i<LCEC_EL5032_CHANS; i++) {
    chan = &hal_data->chans[i];

    // check for change in scale value
    if (*(chan->pos_scale) != chan->old_scale) {
      // scale value has changed, test and update it
      if ((*(chan->pos_scale) < 1e-20) && (*(chan->pos_scale) > -1e-20)) {
        // value too small, divide by zero is a bad thing
        *(chan->pos_scale) = 1.0;
      }
      // save new scale to detect future changes
      chan->old_scale = *(chan->pos_scale);
      // we actually want the reciprocal
      chan->scale = 1.0 / *(chan->pos_scale);
    }

    // get bit states
    *(chan->warn) = EC_READ_BIT(&pd[chan->warn_os], chan->warn_bp);
    *(chan->error) = EC_READ_BIT(&pd[chan->error_os], chan->error_bp);
    *(chan->ready) = EC_READ_BIT(&pd[chan->ready_os], chan->ready_bp);
    *(chan->diag) = EC_READ_BIT(&pd[chan->diag_os], chan->diag_bp);
    *(chan->tx_state) = EC_READ_BIT(&pd[chan->tx_state_os], chan->tx_state_bp);

    // get cycle counter
    *(chan->cyc_cnt) = (EC_READ_U8(&pd[chan->cyc_cnt_os]) >> chan->cyc_cnt_bp) & 0x03;

    // read raw values
    raw_count = EC_READ_S64(&pd[chan->count_pdo_os]);

    // check for operational change of slave
    if (!hal_data->last_operational) {
      chan->last_count = raw_count;
    }

    // update raw values
    *(chan->raw_count_lo) = raw_count;
    *(chan->raw_count_hi) = raw_count >> 32;

    // handle initialization
    if (chan->do_init || *(chan->reset)) {
      chan->do_init = 0;
      chan->last_count = raw_count;
      *(chan->count) = 0;
    }

    // compute net counts
    raw_delta = raw_count - chan->last_count;
    chan->last_count = raw_count;
    *(chan->count) += raw_delta;

    // scale count to make floating point position
    if (*(chan->abs_mode)) {
      *(chan->pos) = raw_count * chan->scale;
    } else {
      *(chan->pos) = *(chan->count) * chan->scale;
    }
  }

  hal_data->last_operational = 1;
}

