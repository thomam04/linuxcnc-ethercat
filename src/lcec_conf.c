//
//  Copyright (C) 2012 Sascha Ittner <sascha.ittner@modusoft.de>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//

/// @file
/// @brief Code for `lcec_conf` configuration tool.

#include "lcec_conf.h"

#include <ctype.h>
#include <expat.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "hal.h"
#include "lcec.h"
#include "lcec_conf_priv.h"
#include "lcec_rtapi.h"
#include "rtapi.h"

typedef struct {
  hal_u32_t *master_count;
  hal_u32_t *slave_count;
} LCEC_CONF_HAL_T;

static int hal_comp_id;
static LCEC_CONF_HAL_T *conf_hal_data;
static int shmem_id;

static int exitEvent;

typedef struct {
  LCEC_CONF_XML_INST_T xml;

  LCEC_CONF_MASTER_T *currMaster;
  const lcec_typelist_t *currSlaveType;
  LCEC_CONF_SLAVE_T *currSlave;
  LCEC_CONF_SYNCMANAGER_T *currSyncManager;
  LCEC_CONF_PDO_T *currPdo;
  LCEC_CONF_SDOCONF_T *currSdoConf;
  LCEC_CONF_IDNCONF_T *currIdnConf;
  LCEC_CONF_PDOENTRY_T *currPdoEntry;
  uint8_t currComplexBitOffset;

  LCEC_CONF_OUTBUF_T outputBuf;
} LCEC_CONF_XML_STATE_T;

static void parseMasterAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseSlaveAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseDcConfAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseWatchdogAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseSdoConfigAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseIdnConfigAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseDataRawAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseInitCmdsAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseSyncManagerAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parsePdoAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parsePdoEntryAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseComplexEntryAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);
static void parseModParamAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr);

static const LCEC_CONF_XML_HANLDER_T xml_states[] = {
    {"masters", lcecConfTypeNone, lcecConfTypeMasters, NULL, NULL},
    {"master", lcecConfTypeMasters, lcecConfTypeMaster, parseMasterAttrs, NULL},
    {"slave", lcecConfTypeMaster, lcecConfTypeSlave, parseSlaveAttrs, NULL},
    {"dcConf", lcecConfTypeSlave, lcecConfTypeDcConf, parseDcConfAttrs, NULL},
    {"watchdog", lcecConfTypeSlave, lcecConfTypeWatchdog, parseWatchdogAttrs, NULL},
    {"sdoConfig", lcecConfTypeSlave, lcecConfTypeSdoConfig, parseSdoConfigAttrs, NULL},
    {"sdoDataRaw", lcecConfTypeSdoConfig, lcecConfTypeSdoDataRaw, parseDataRawAttrs, NULL},
    {"idnConfig", lcecConfTypeSlave, lcecConfTypeIdnConfig, parseIdnConfigAttrs, NULL},
    {"idnDataRaw", lcecConfTypeIdnConfig, lcecConfTypeIdnDataRaw, parseDataRawAttrs, NULL},
    {"initCmds", lcecConfTypeSlave, lcecConfTypeInitCmds, parseInitCmdsAttrs, NULL},
    {"syncManager", lcecConfTypeSlave, lcecConfTypeSyncManager, parseSyncManagerAttrs, NULL},
    {"pdo", lcecConfTypeSyncManager, lcecConfTypePdo, parsePdoAttrs, NULL},
    {"pdoEntry", lcecConfTypePdo, lcecConfTypePdoEntry, parsePdoEntryAttrs, NULL},
    {"complexEntry", lcecConfTypePdoEntry, lcecConfTypeComplexEntry, parseComplexEntryAttrs, NULL},
    {"modParam", lcecConfTypeSlave, lcecConfTypeModParam, parseModParamAttrs, NULL},
    {"NULL", -1, -1, NULL, NULL},
};

static int parseSyncCycle(LCEC_CONF_XML_STATE_T *state, const char *nptr);

static void exitHandler(int sig) {
  uint64_t u = 1;
  if (write(exitEvent, &u, sizeof(uint64_t)) < 0) {
    fprintf(stderr, "%s: ERROR: error writing exit event\n", modname);
  }
}

int main(int argc, char **argv) {
  int ret = 1;
  char *filename;
  int done;
  char buffer[BUFFSIZE];
  FILE *file;
  LCEC_CONF_NULL_T *end;
  void *shmem_ptr;
  LCEC_CONF_HEADER_T *header;
  uint64_t u;
  LCEC_CONF_XML_STATE_T state;

  // initialize component
  hal_comp_id = hal_init(modname);
  if (hal_comp_id < 1) {
    fprintf(stderr, "%s: ERROR: hal_init failed\n", modname);
    goto fail0;
  }

  // allocate hal memory
  conf_hal_data = hal_malloc(sizeof(LCEC_CONF_HAL_T));
  if (conf_hal_data == NULL) {
    fprintf(stderr, "%s: ERROR: unable to allocate HAL shared memory\n", modname);
    goto fail1;
  }

  // register pins
  if (hal_pin_u32_newf(HAL_OUT, &(conf_hal_data->master_count), hal_comp_id, "%s.conf.master-count", LCEC_MODULE_NAME) != 0) {
    fprintf(stderr, "%s: ERROR: unable to register pin %s.conf.master-count\n", modname, LCEC_MODULE_NAME);
    goto fail1;
  }
  if (hal_pin_u32_newf(HAL_OUT, &(conf_hal_data->slave_count), hal_comp_id, "%s.conf.slave-count", LCEC_MODULE_NAME) != 0) {
    fprintf(stderr, "%s: ERROR: unable to register pin %s.conf.slave-count\n", modname, LCEC_MODULE_NAME);
    goto fail1;
  }
  *(conf_hal_data->master_count) = 0;
  *(conf_hal_data->slave_count) = 0;

  // initialize signal handling
  exitEvent = eventfd(0, 0);
  if (exitEvent == -1) {
    fprintf(stderr, "%s: ERROR: unable to create exit event\n", modname);
    goto fail1;
  }
  signal(SIGINT, exitHandler);
  signal(SIGTERM, exitHandler);

  // get config file name
  if (argc != 2) {
    fprintf(stderr, "%s: ERROR: invalid arguments\n", modname);
    goto fail2;
  }
  filename = argv[1];

  // open file
  file = fopen(filename, "r");
  if (file == NULL) {
    fprintf(stderr, "%s: ERROR: unable to open config file %s\n", modname, filename);
    goto fail2;
  }

  // create xml parser
  memset(&state, 0, sizeof(state));
  if (initXmlInst((LCEC_CONF_XML_INST_T *)&state, xml_states)) {
    fprintf(stderr, "%s: ERROR: Couldn't allocate memory for parser\n", modname);
    goto fail3;
  }

  initOutputBuffer(&state.outputBuf);
  for (done = 0; !done;) {
    // read block
    int len = fread(buffer, 1, BUFFSIZE, file);
    if (ferror(file)) {
      fprintf(stderr, "%s: ERROR: Couldn't read from file %s\n", modname, filename);
      goto fail4;
    }

    // check for EOF
    done = feof(file);

    // parse current block
    if (!XML_Parse(state.xml.parser, buffer, len, done)) {
      fprintf(stderr, "%s: ERROR: Parse error at line %u: %s\n", modname, (unsigned int)XML_GetCurrentLineNumber(state.xml.parser),
          XML_ErrorString(XML_GetErrorCode(state.xml.parser)));
      goto fail4;
    }
  }

  // set end marker
  end = addOutputBuffer(&state.outputBuf, sizeof(LCEC_CONF_NULL_T));
  if (end == NULL) {
    goto fail4;
  }
  end->confType = lcecConfTypeNone;

  // setup shared mem for config
  shmem_id = rtapi_shmem_new(LCEC_CONF_SHMEM_KEY, hal_comp_id, sizeof(LCEC_CONF_HEADER_T) + state.outputBuf.len);
  if (shmem_id < 0) {
    fprintf(stderr, "%s: ERROR: couldn't allocate user/RT shared memory\n", modname);
    goto fail4;
  }
  if (lcec_rtapi_shmem_getptr(shmem_id, &shmem_ptr) < 0) {
    fprintf(stderr, "%s: ERROR: couldn't map user/RT shared memory\n", modname);
    goto fail5;
  }

  // setup header
  header = shmem_ptr;
  shmem_ptr += sizeof(LCEC_CONF_HEADER_T);
  header->magic = LCEC_CONF_SHMEM_MAGIC;
  header->length = state.outputBuf.len;

  // copy data and free buffer
  copyFreeOutputBuffer(&state.outputBuf, shmem_ptr);

  // everything is fine
  ret = 0;
  hal_ready(hal_comp_id);

  // wait for SIGTERM
  if (read(exitEvent, &u, sizeof(uint64_t)) < 0) {
    fprintf(stderr, "%s: ERROR: error reading exit event\n", modname);
  }

fail5:
  rtapi_shmem_delete(shmem_id, hal_comp_id);
fail4:
  copyFreeOutputBuffer(&state.outputBuf, NULL);
  XML_ParserFree(state.xml.parser);
fail3:
  fclose(file);
fail2:
  close(exitEvent);
fail1:
  hal_exit(hal_comp_id);
fail0:
  return ret;
}

static void parseMasterAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  LCEC_CONF_MASTER_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_MASTER_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  p->confType = lcecConfTypeMaster;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse index
    if (strcmp(name, "idx") == 0) {
      p->index = atoi(val);
      continue;
    }

    // parse name
    if (strcmp(name, "name") == 0) {
      strncpy(p->name, val, LCEC_CONF_STR_MAXLEN);
      p->name[LCEC_CONF_STR_MAXLEN - 1] = 0;
      continue;
    }

    // parse appTimePeriod
    if (strcmp(name, "appTimePeriod") == 0) {
      p->appTimePeriod = atol(val);
      continue;
    }

    // parse refClockSyncCycles
    if (strcmp(name, "refClockSyncCycles") == 0) {
      p->refClockSyncCycles = atoll(val);
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid master attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // set default name
  if (p->name[0] == 0) {
    snprintf(p->name, LCEC_CONF_STR_MAXLEN, "%d", p->index);
  }

  (*(conf_hal_data->master_count))++;
  state->currMaster = p;
}

static void parseSlaveAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  const lcec_typelist_t *slaveType;

  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  LCEC_CONF_SLAVE_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_SLAVE_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  p->confType = lcecConfTypeSlave;

  int valid = 0;

  // pre parse slave type to avoid attribute ordering problems
  const char **iter = attr;
  while (*iter) {
    const char *name = *(iter++);
    const char *val = *(iter++);

    // parse slave type
    if (strcmp(name, "type") == 0) {
      slaveType = lcec_findslavetype(val);
      if (slaveType == NULL) {
        fprintf(stderr, "%s: ERROR: Cannot find slave type %s, verify type in XML file\n", modname, val);
        XML_StopParser(inst->parser, 0);
        return;
      }

      if (slaveType->name == NULL) {
        fprintf(stderr, "%s: ERROR: Invalid slave type %s\n", modname, val);
        XML_StopParser(inst->parser, 0);
        return;
      }
      valid = 1;
      continue;
    }
  }

  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // set slave typename
    if (strcmp(name, "type") == 0) {
      strncpy(p->typename, val, LCEC_CONF_STR_MAXLEN);
      continue;
    }

    // parse index
    if (strcmp(name, "idx") == 0) {
      p->index = atoi(val);
      continue;
    }

    // parse name
    if (strcmp(name, "name") == 0) {
      strncpy(p->name, val, LCEC_CONF_STR_MAXLEN);
      p->name[LCEC_CONF_STR_MAXLEN - 1] = 0;
      continue;
    }

    // generic only attributes
    if (!strcmp(p->typename, "generic")) {
      // parse vid (hex value)
      if (strcmp(name, "vid") == 0) {
        p->vid = strtol(val, NULL, 16);
        continue;
      }

      // parse pid (hex value)
      if (strcmp(name, "pid") == 0) {
        p->pid = strtol(val, NULL, 16);
        continue;
      }

      // parse configPdos
      if (strcmp(name, "configPdos") == 0) {
        p->configPdos = (strcasecmp(val, "true") == 0);
        continue;
      }
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid slave attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // set default name
  if (p->name[0] == 0) {
    snprintf(p->name, LCEC_CONF_STR_MAXLEN, "%d", p->index);
  }

  // type is required
  if (!valid) {
    fprintf(stderr, "%s: ERROR: Slave type is invalid\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  (*(conf_hal_data->slave_count))++;
  state->currSlaveType = slaveType;
  state->currSlave = p;
}

static void parseDcConfAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  LCEC_CONF_DC_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_DC_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  p->confType = lcecConfTypeDcConf;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse assignActivate (hex value)
    if (strcmp(name, "assignActivate") == 0) {
      p->assignActivate = strtol(val, NULL, 16);
      continue;
    }

    // parse sync0Cycle
    if (strcmp(name, "sync0Cycle") == 0) {
      p->sync0Cycle = parseSyncCycle(state, val);
      continue;
    }

    // parse sync0Shift
    if (strcmp(name, "sync0Shift") == 0) {
      p->sync0Shift = atoi(val);
      continue;
    }

    // parse sync1Cycle
    if (strcmp(name, "sync1Cycle") == 0) {
      p->sync1Cycle = parseSyncCycle(state, val);
      continue;
    }

    // parse sync1Shift
    if (strcmp(name, "sync1Shift") == 0) {
      p->sync1Shift = atoi(val);
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid dcConfig attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }
}

static void parseWatchdogAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  LCEC_CONF_WATCHDOG_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_WATCHDOG_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  p->confType = lcecConfTypeWatchdog;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse divider
    if (strcmp(name, "divider") == 0) {
      p->divider = atoi(val);
      continue;
    }

    // parse intervals
    if (strcmp(name, "intervals") == 0) {
      p->intervals = atoi(val);
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid watchdog attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }
}

static void parseSdoConfigAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  int tmp;
  LCEC_CONF_SDOCONF_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_SDOCONF_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  p->confType = lcecConfTypeSdoConfig;
  p->index = 0xffff;
  p->subindex = 0xff;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse index
    if (strcmp(name, "idx") == 0) {
      tmp = strtol(val, NULL, 16);
      if (tmp < 0 || tmp >= 0xffff) {
        fprintf(stderr, "%s: ERROR: Invalid sdoConfig idx %d\n", modname, tmp);
        XML_StopParser(inst->parser, 0);
        return;
      }
      p->index = tmp;
      continue;
    }

    // parse subIdx
    if (strcmp(name, "subIdx") == 0) {
      if (strcasecmp(val, "complete") == 0) {
        p->subindex = LCEC_CONF_SDO_COMPLETE_SUBIDX;
        continue;
      }
      tmp = strtol(val, NULL, 16);
      if (tmp < 0 || tmp >= 0xff) {
        fprintf(stderr, "%s: ERROR: Invalid sdoConfig subIdx %d\n", modname, tmp);
        XML_StopParser(inst->parser, 0);
        return;
      }
      p->subindex = tmp;
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid sdoConfig attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // idx is required
  if (p->index == 0xffff) {
    fprintf(stderr, "%s: ERROR: sdoConfig has no idx attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // subIdx is required
  if (p->subindex == 0xff) {
    fprintf(stderr, "%s: ERROR: sdoConfig has no subIdx attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  state->currSdoConf = p;
  state->currSlave->sdoConfigLength += sizeof(LCEC_CONF_SDOCONF_T);
}

static void parseIdnConfigAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  int tmp;
  LCEC_CONF_IDNCONF_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_IDNCONF_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  p->confType = lcecConfTypeIdnConfig;
  p->drive = 0;
  p->idn = 0xffff;
  p->state = 0;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse index
    if (strcmp(name, "drive") == 0) {
      tmp = atoi(val);
      if (tmp < 0 || tmp > 7) {
        fprintf(stderr, "%s: ERROR: Invalid idnConfig drive %d\n", modname, tmp);
        XML_StopParser(inst->parser, 0);
        return;
      }

      p->drive = tmp;
      continue;
    }

    // parse idn
    if (strcmp(name, "idn") == 0) {
      char pfx = val[0];
      if (pfx == 0) {
        fprintf(stderr, "%s: ERROR: Missing idnConfig idn value\n", modname);
        XML_StopParser(inst->parser, 0);
        return;
      }

      pfx = toupper(pfx);

      tmp = 0xffff;
      if (pfx == 'S' || pfx == 'P') {
        int set;
        int block;
        if (sscanf(val, "%c-%d-%d", &pfx, &set, &block) == 3) {
          if (set < 0 || set >= (1 << 3)) {
            fprintf(stderr, "%s: ERROR: Invalid idnConfig idn set %d\n", modname, set);
            XML_StopParser(inst->parser, 0);
            return;
          }

          if (block < 0 || block >= (1 << 12)) {
            fprintf(stderr, "%s: ERROR: Invalid idnConfig idn block %d\n", modname, block);
            XML_StopParser(inst->parser, 0);
            return;
          }

          tmp = (set << 12) | block;
          if (pfx == 'P') {
            tmp |= (15 << 1);
          }
        }
      } else if (pfx >= '0' && pfx <= '9') {
        tmp = atoi(val);
      }

      if (tmp == 0xffff) {
        fprintf(stderr, "%s: ERROR: Invalid idnConfig idn value '%s'\n", modname, val);
        XML_StopParser(inst->parser, 0);
        return;
      }

      p->idn = tmp;
      continue;
    }

    // parse state
    if (strcmp(name, "drive") == 0) {
      if (strcmp(val, "PREOP") == 0) {
        p->state = EC_AL_STATE_PREOP;
      } else if (strcmp(val, "SAFEOP") == 0) {
        p->state = EC_AL_STATE_SAFEOP;
      } else {
        fprintf(stderr, "%s: ERROR: Invalid idnConfig state '%s'\n", modname, val);
        XML_StopParser(inst->parser, 0);
        return;
      }

      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid idnConfig attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // idn is required
  if (p->idn == 0xffff) {
    fprintf(stderr, "%s: ERROR: idnConfig has no idn attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // state is required
  if (p->state == 0) {
    fprintf(stderr, "%s: ERROR: idnConfig has no state attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  state->currIdnConf = p;
  state->currSlave->idnConfigLength += sizeof(LCEC_CONF_IDNCONF_T);
}

static void parseDataRawAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  int len;
  uint8_t *p;

  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse data
    if (strcmp(name, "data") == 0) {
      len = parseHex(val, -1, NULL);
      if (len < 0) {
        fprintf(stderr, "%s: ERROR: Invalid dataRaw data\n", modname);
        XML_StopParser(inst->parser, 0);
        return;
      }
      if (len > 0) {
        p = (uint8_t *)addOutputBuffer(&state->outputBuf, len);
        if (p != NULL) {
          parseHex(val, -1, p);
          switch (inst->state) {
            case lcecConfTypeSdoConfig:
              state->currSdoConf->length += len;
              state->currSlave->sdoConfigLength += len;
              break;
            case lcecConfTypeIdnConfig:
              state->currIdnConf->length += len;
              state->currSlave->idnConfigLength += len;
              break;
          }
        }
      }
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid pdoEntry attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }
}

static void parseInitCmdsAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  const char *filename = NULL;

  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse filename
    if (strcmp(name, "filename") == 0) {
      filename = val;
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid syncManager attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // filename is required
  if (filename == NULL || *filename == 0) {
    fprintf(stderr, "%s: ERROR: initCmds has no filename attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // try to parse initCmds
  if (parseIcmds(state->currSlave, &state->outputBuf, filename)) {
    XML_StopParser(inst->parser, 0);
    return;
  }
}

static void parseSyncManagerAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  int tmp;
  LCEC_CONF_SYNCMANAGER_T *p;

  // only allowed on generic slave
  if (strcmp(state->currSlave->typename, "generic")) {
    fprintf(stderr, "%s: ERROR: syncManager is only allowed on generic slaves\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_SYNCMANAGER_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  p->confType = lcecConfTypeSyncManager;
  p->index = 0xff;
  p->dir = EC_DIR_INVALID;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse index
    if (strcmp(name, "idx") == 0) {
      tmp = atoi(val);
      if (tmp < 0 || tmp >= EC_MAX_SYNC_MANAGERS) {
        fprintf(stderr, "%s: ERROR: Invalid syncManager idx %d\n", modname, tmp);
        XML_StopParser(inst->parser, 0);
        return;
      }
      p->index = tmp;
      continue;
    }

    // parse dir
    if (strcmp(name, "dir") == 0) {
      if (strcasecmp(val, "in") == 0) {
        p->dir = EC_DIR_INPUT;
        continue;
      }
      if (strcasecmp(val, "out") == 0) {
        p->dir = EC_DIR_OUTPUT;
        continue;
      }
      fprintf(stderr, "%s: ERROR: Invalid syncManager dir %s\n", modname, val);
      XML_StopParser(inst->parser, 0);
      return;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid syncManager attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // idx is required
  if (p->index == 0xff) {
    fprintf(stderr, "%s: ERROR: syncManager has no idx attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // dir is required
  if (p->dir == EC_DIR_INVALID) {
    fprintf(stderr, "%s: ERROR: syncManager has no dir attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  (state->currSlave->syncManagerCount)++;
  state->currSyncManager = p;
}

static void parsePdoAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  int tmp;
  LCEC_CONF_PDO_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_PDO_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  p->confType = lcecConfTypePdo;
  p->index = 0xffff;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse index
    if (strcmp(name, "idx") == 0) {
      tmp = strtol(val, NULL, 16);
      if (tmp < 0 || tmp >= 0xffff) {
        fprintf(stderr, "%s: ERROR: Invalid pdo idx %d\n", modname, tmp);
        XML_StopParser(inst->parser, 0);
        return;
      }
      p->index = tmp;
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid pdo attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // idx is required
  if (p->index == 0xffff) {
    fprintf(stderr, "%s: ERROR: pdo has no idx attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  (state->currSlave->pdoCount)++;
  (state->currSyncManager->pdoCount)++;
  state->currPdo = p;
}

static void parsePdoEntryAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  int tmp;
  int floatReq;
  LCEC_CONF_PDOENTRY_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_PDOENTRY_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  floatReq = 0;
  p->confType = lcecConfTypePdoEntry;
  p->index = 0xffff;
  p->subindex = 0xff;
  p->floatScale = 1.0;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse index
    if (strcmp(name, "idx") == 0) {
      tmp = strtol(val, NULL, 16);
      if (tmp < 0 || tmp >= 0xffff) {
        fprintf(stderr, "%s: ERROR: Invalid pdoEntry idx %d\n", modname, tmp);
        XML_StopParser(inst->parser, 0);
        return;
      }
      p->index = tmp;
      continue;
    }

    // parse subIdx
    if (strcmp(name, "subIdx") == 0) {
      tmp = strtol(val, NULL, 16);
      if (tmp < 0 || tmp >= 0xff) {
        fprintf(stderr, "%s: ERROR: Invalid pdoEntry subIdx %d\n", modname, tmp);
        XML_StopParser(inst->parser, 0);
        return;
      }
      p->subindex = tmp;
      continue;
    }

    // parse bitLen
    if (strcmp(name, "bitLen") == 0) {
      tmp = atoi(val);
      if (tmp <= 0 || tmp > LCEC_CONF_GENERIC_MAX_BITLEN) {
        fprintf(stderr, "%s: ERROR: Invalid pdoEntry bitLen %d\n", modname, tmp);
        XML_StopParser(inst->parser, 0);
        return;
      }
      p->bitLength = tmp;
      continue;
    }

    // parse halType
    if (strcmp(name, "halType") == 0) {
      if (strcasecmp(val, "bit") == 0) {
        p->halType = HAL_BIT;
        continue;
      }
      if (strcasecmp(val, "s32") == 0) {
        p->subType = lcecPdoEntTypeSimple;
        p->halType = HAL_S32;
        continue;
      }
      if (strcasecmp(val, "u32") == 0) {
        p->subType = lcecPdoEntTypeSimple;
        p->halType = HAL_U32;
        continue;
      }
      if (strcasecmp(val, "float") == 0) {
        p->subType = lcecPdoEntTypeFloatSigned;
        p->halType = HAL_FLOAT;
        continue;
      }
      if (strcasecmp(val, "float-unsigned") == 0) {
        p->subType = lcecPdoEntTypeFloatUnsigned;
        p->halType = HAL_FLOAT;
        continue;
      }
      if (strcasecmp(val, "complex") == 0) {
        p->subType = lcecPdoEntTypeComplex;
        continue;
      }
      if (strcasecmp(val, "float-ieee") == 0) {
        p->subType = lcecPdoEntTypeFloatIeee;
        p->halType = HAL_FLOAT;
        continue;
      }
      if (strcasecmp(val, "float-double-ieee") == 0) {
        p->subType = lcecPdoEntTypeFloatDoubleIeee;
        p->halType = HAL_FLOAT;
        continue;
      }
      fprintf(stderr, "%s: ERROR: Invalid pdoEntry halType %s\n", modname, val);
      XML_StopParser(inst->parser, 0);
      return;
    }

    // parse scale
    if (strcmp(name, "scale") == 0) {
      floatReq = 1;
      p->floatScale = atof(val);
      continue;
    }

    // parse offset
    if (strcmp(name, "offset") == 0) {
      floatReq = 1;
      p->floatOffset = atof(val);
      continue;
    }

    // parse halPin
    if (strcmp(name, "halPin") == 0) {
      strncpy(p->halPin, val, LCEC_CONF_STR_MAXLEN);
      p->halPin[LCEC_CONF_STR_MAXLEN - 1] = 0;
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid pdoEntry attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // idx is required
  if (p->index == 0xffff) {
    fprintf(stderr, "%s: ERROR: pdoEntry has no idx attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // subIdx is required
  if (p->subindex == 0xff) {
    fprintf(stderr, "%s: ERROR: pdoEntry has no subIdx attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // bitLen is required
  if (p->bitLength == 0) {
    fprintf(stderr, "%s: ERROR: pdoEntry has no bitLen attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // pin name must not be given for complex subtype
  if (p->subType == lcecPdoEntTypeComplex && p->halPin[0] != 0) {
    fprintf(stderr, "%s: ERROR: pdoEntry has halPin attributes but pin type is 'complex'\n", modname);
    XML_StopParser(inst->parser, 0);
  }

  // check for float type if required
  if (floatReq && p->halType != HAL_FLOAT) {
    fprintf(stderr, "%s: ERROR: pdoEntry has scale/offset attributes but pin type is not 'float'\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  (state->currSlave->pdoEntryCount)++;
  if (p->halPin[0] != 0) {
    (state->currSlave->pdoMappingCount)++;
  }
  (state->currPdo->pdoEntryCount)++;
  state->currPdoEntry = p;
  state->currComplexBitOffset = 0;
}

static void parseComplexEntryAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  int tmp;
  int floatReq;
  LCEC_CONF_COMPLEXENTRY_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_COMPLEXENTRY_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  floatReq = 0;
  p->confType = lcecConfTypeComplexEntry;
  p->bitOffset = state->currComplexBitOffset;
  p->floatScale = 1.0;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // parse bitLen
    if (strcmp(name, "bitLen") == 0) {
      tmp = atoi(val);
      if (tmp <= 0 || tmp > LCEC_CONF_GENERIC_MAX_SUBPINS) {
        fprintf(stderr, "%s: ERROR: Invalid complexEntry bitLen %d\n", modname, tmp);
        XML_StopParser(inst->parser, 0);
        return;
      }
      if ((state->currComplexBitOffset + tmp) > state->currPdoEntry->bitLength) {
        fprintf(stderr, "%s: ERROR: complexEntry bitLen sum exceeded pdoEntry bitLen %d\n", modname, state->currPdoEntry->bitLength);
        XML_StopParser(inst->parser, 0);
        return;
      }
      p->bitLength = tmp;
      continue;
    }

    // parse halType
    if (strcmp(name, "halType") == 0) {
      if (strcasecmp(val, "bit") == 0) {
        p->subType = lcecPdoEntTypeSimple;
        p->halType = HAL_BIT;
        continue;
      }
      if (strcasecmp(val, "s32") == 0) {
        p->subType = lcecPdoEntTypeSimple;
        p->halType = HAL_S32;
        continue;
      }
      if (strcasecmp(val, "u32") == 0) {
        p->subType = lcecPdoEntTypeSimple;
        p->halType = HAL_U32;
        continue;
      }
      if (strcasecmp(val, "float") == 0) {
        p->subType = lcecPdoEntTypeFloatSigned;
        p->halType = HAL_FLOAT;
        continue;
      }
      if (strcasecmp(val, "float-unsigned") == 0) {
        p->subType = lcecPdoEntTypeFloatUnsigned;
        p->halType = HAL_FLOAT;
        continue;
      }
      if (strcasecmp(val, "float-ieee") == 0) {
        p->subType = lcecPdoEntTypeFloatIeee;
        p->halType = HAL_FLOAT;
        continue;
      }
      if (strcasecmp(val, "float-double-ieee") == 0) {
        p->subType = lcecPdoEntTypeFloatDoubleIeee;
        p->halType = HAL_FLOAT;
        continue;
      }
      fprintf(stderr, "%s: ERROR: Invalid complexEntry halType %s\n", modname, val);
      XML_StopParser(inst->parser, 0);
      return;
    }

    // parse scale
    if (strcmp(name, "scale") == 0) {
      floatReq = 1;
      p->floatScale = atof(val);
      continue;
    }

    // parse offset
    if (strcmp(name, "offset") == 0) {
      floatReq = 1;
      p->floatOffset = atof(val);
      continue;
    }

    // parse halPin
    if (strcmp(name, "halPin") == 0) {
      strncpy(p->halPin, val, LCEC_CONF_STR_MAXLEN);
      p->halPin[LCEC_CONF_STR_MAXLEN - 1] = 0;
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid complexEntry attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // bitLen is required
  if (p->bitLength == 0) {
    fprintf(stderr, "%s: ERROR: complexEntry has no bitLen attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // check for float type if required
  if (floatReq && p->halType != HAL_FLOAT) {
    fprintf(stderr, "%s: ERROR: complexEntry has scale/offset attributes but pin type is not 'float'\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  if (p->halPin[0] != 0) {
    (state->currSlave->pdoMappingCount)++;
  }
  state->currComplexBitOffset += p->bitLength;
}

static void parseModParamAttrs(LCEC_CONF_XML_INST_T *inst, int next, const char **attr) {
  LCEC_CONF_XML_STATE_T *state = (LCEC_CONF_XML_STATE_T *)inst;

  const char *pname, *pval;
  const lcec_modparam_desc_t *modparams;

  if (state->currSlaveType->modparams == NULL) {
    fprintf(stderr, "%s: ERROR: modparam not allowed for this slave\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  LCEC_CONF_MODPARAM_T *p = addOutputBuffer(&state->outputBuf, sizeof(LCEC_CONF_MODPARAM_T));
  if (p == NULL) {
    XML_StopParser(inst->parser, 0);
    return;
  }

  p->confType = lcecConfTypeModParam;

  pname = NULL;
  pval = NULL;
  while (*attr) {
    const char *name = *(attr++);
    const char *val = *(attr++);

    // get name
    if (strcmp(name, "name") == 0) {
      pname = val;
      continue;
    }

    // get value
    if (strcmp(name, "value") == 0) {
      pval = val;
      continue;
    }

    // handle error
    fprintf(stderr, "%s: ERROR: Invalid modparam attribute %s\n", modname, name);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // name is required
  if (pname == NULL || pname[0] == 0) {
    fprintf(stderr, "%s: ERROR: modparam has no name attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // value is required
  if (pval == NULL || pval[0] == 0) {
    fprintf(stderr, "%s: ERROR: modparam has no value attribute\n", modname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // search for matching param name
  for (modparams = state->currSlaveType->modparams; modparams->name != NULL; modparams++) {
    if (strcmp(pname, modparams->name) == 0) {
      break;
    }
  }
  if (modparams->name == NULL) {
    fprintf(stderr, "%s: ERROR: Invalid modparam '%s'\n", modname, pname);
    XML_StopParser(inst->parser, 0);
    return;
  }

  // set id
  p->id = modparams->id;

  // try to parse value
  char *s = NULL;
  switch (modparams->type) {
    case MODPARAM_TYPE_BIT:
      if ((strcmp("1", pval) == 0) || (strcasecmp("TRUE", pval) == 0)) {
        p->value.bit = 1;
      } else if ((strcmp("0", pval) == 0) || (strcasecmp("FALSE", pval)) == 0) {
        p->value.bit = 0;
      } else {
        fprintf(stderr, "%s: ERROR: Invalid modparam bit value '%s' for param '%s'\n", modname, pval, pname);
        XML_StopParser(inst->parser, 0);
        return;
      }
      break;

    case MODPARAM_TYPE_U32:
      p->value.u32 = strtoul(pval, &s, 0);
      if (*s != 0) {
        fprintf(stderr, "%s: ERROR: Invalid modparam u32 value '%s' for param '%s'\n", modname, pval, pname);
        XML_StopParser(inst->parser, 0);
        return;
      }
      break;

    case MODPARAM_TYPE_S32:
      p->value.s32 = strtol(pval, &s, 0);
      if (*s != 0) {
        fprintf(stderr, "%s: ERROR: Invalid modparam s32 value '%s' for param '%s'\n", modname, pval, pname);
        XML_StopParser(inst->parser, 0);
        return;
      }
      break;

    case MODPARAM_TYPE_FLOAT:
      p->value.flt = strtod(pval, &s);
      if (*s != 0) {
        fprintf(stderr, "%s: ERROR: Invalid modparam float value '%s' for param '%s'\n", modname, pval, pname);
        XML_StopParser(inst->parser, 0);
        return;
      }
      break;

    case MODPARAM_TYPE_STRING:
      strncpy(p->value.str, pval, LCEC_CONF_STR_MAXLEN - 1);
      break;

    default:
      p->value.u32 = 0;
      break;
  }

  (state->currSlave->modParamCount)++;
}

static int parseSyncCycle(LCEC_CONF_XML_STATE_T *state, const char *nptr) {
  // chack for master period multiples
  if (*nptr == '*') {
    nptr++;
    return atoi(nptr) * state->currMaster->appTimePeriod;
  }

  // custom value
  return atoi(nptr);
}
