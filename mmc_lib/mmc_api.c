//
//  Multi Media Card - Application Program Interface
//  
//  originally written by TI dude
//  these functions call the mmc_hwif routines
//
#include "mmc_hwif.h"
#include "dma.h"
#include "mmc_api.h"
#include "string.h"

extern unsigned int mmc0_get_cmd_status(mmcCtrlInfo *ctrl);
extern unsigned int mmc0_get_xfer_status(mmcCtrlInfo *ctrl);
extern void MMCXferSetup(mmcCtrlInfo *ctrl, unsigned char rwFlag, void *ptr,
                             unsigned int blkSize, unsigned int nBlks);

#define DATA_RESPONSE_WIDTH       (SOC_CACHELINE_SIZE)
//  Cache size aligned data buffer (minimum of 64 bytes) for command response
static unsigned char dataBuffer[DATA_RESPONSE_WIDTH]
                               __attribute__((aligned(SOC_CACHELINE_SIZE)));

unsigned int MMC0CmdSend(mmcCtrlInfo *ctrl, mmcCmd *c) {
  unsigned int cmd, cmdType, rspType, cmdDir, nblks;
  unsigned int dataPresent, status = 0;

  // set CMD_TYPE field of SD_CMD register   see spruh73n Table 18-24
  cmdType = 0x0;               // other commands
  if (c->flags & 0x2) {
    cmdType = 0x1 < 0x16;      // bus suspend
  } else if (c->flags & 0x4) {
    cmdType = 0x2 << 0x16;     // function select
  } else if (c->flags & 0x8) {
    cmdType = 0x3 << 0x16;     // i/o abort
  }
  // set DDIR field of SD_CMD register   see spruh73n Table 18-24
  if (c->flags & 0x80) cmdDir = 0x1 << 0x4;  // Read Card to Host
  else cmdDir = 0x0;                         // Write Host to Card
  if (c->flags & 0x40) {  // data present  (DP field)
    dataPresent = 0x1;
    nblks = c->nblks;
  } else {
    dataPresent = 0x0;
    nblks = 0;
  }
  // set field RSP_TYPE of SD_CMD register    see spruh73n Table 18-24
  if (c->flags & 0x1) {
    rspType = 0x0;             // no response
  } else if (c->flags & 0x20) {
    rspType = 1 << 0x10;       // response length 136 bits
  } else if (c->flags & 0x10) {
    rspType = 3 << 0x10;       // response length 48 bits with BUSY
  } else rspType = 2 << 0x10;  // response length 48 bits
  cmd = (c->idx << 0x18) | cmdType | rspType | cmdDir;  // spruh73n 18.4.1.10
  if (dataPresent) {
    mmc0_clear_status(MMC_STAT_TC);
    mmc0_set_dto(27);  // set data_timeout  prob only need to do this only in init - TODO
            // data present select   bit21
            // multi/single block select   bit5
            // block count enable   bit1
    cmd |= (MMC_CMD_DP | MMC_CMD_MSBS | MMC_CMD_BCE);
  }
  if (1 == ctrl->dmaEnable) cmd |= MMC_CMD_DE;  // dma enable bit0 spruh73n 18.4.1.10
  mmc0_send_cmd(cmd, c->arg, nblks);  // this is assembler
  status = mmc0_get_cmd_status(ctrl);  // a flag routine in main()
  if (status == 1) mmc0_get_resp(c->rsp);
  return status;
}

unsigned int MMCAppCmdSend(mmcCtrlInfo *ctrl, mmcCmd *c) {
  unsigned int status = 0;
  mmcCmd capp;

  capp.idx = SD_CMD(55);  // APP cmd preceded by CMD55
  capp.flags = 0;
  capp.arg = ctrl->card->rca << 16;
  status = MMC0CmdSend(ctrl, &capp);
  if (status == 0) return 0;  // CMD55 failed must return
  status = MMC0CmdSend(ctrl, c);
  return status;
}

unsigned int MMCBusWidthSet(mmcCtrlInfo *ctrl) {
  mmcCardInfo *card = ctrl->card;
  unsigned int status = 0;
  mmcCmd capp;

  capp.idx = SD_CMD(6);
  capp.arg = SD_BUS_WIDTH_1BIT;
  capp.flags = 0;
  if (ctrl->busWidth & SD_BUS_WIDTH_4BIT) {
    if (card->busWidth & SD_BUS_WIDTH_4BIT) {
      capp.arg = SD_BUS_WIDTH_4BIT;
    }
  }
  capp.arg = capp.arg >> 1;
  status = MMCAppCmdSend(ctrl, &capp);
  if (1 == status) {
    if (capp.arg == 0) MMCSetBusWidth(ctrl->memBase, SD_BUS_WIDTH_1BIT);
    else MMCSetBusWidth(ctrl->memBase, SD_BUS_WIDTH_4BIT);  // 4 bit width only
  }
  return status;
}

unsigned int MMCTranSpeedSet(mmcCtrlInfo *ctrl) {
  mmcCardInfo *card = ctrl->card;
  mmcCmd cmd;
  int status;
  unsigned int speed, cmdStatus = 0;

  MMCXferSetup(ctrl, 1, dataBuffer, 64, 1);
  cmd.idx = SD_CMD(6);
  cmd.arg = ((SD_SWITCH_MODE & SD_CMD6_GRP1_SEL) | (SD_CMD6_GRP1_HS));
  cmd.flags = SD_CMDRSP_READ | SD_CMDRSP_DATA;
  cmd.nblks = 1;
  cmd.data = (signed char*)dataBuffer;
  cmdStatus = MMC0CmdSend(ctrl, &cmd);
  if (cmdStatus == 0) return 0;
  cmdStatus = mmc0_get_xfer_status(ctrl);
  if (cmdStatus == 0) return 0;
  // Invalidate data cache
  CP15DCacheFlushBuff((unsigned int) dataBuffer, DATA_RESPONSE_WIDTH);
  speed = card->tranSpeed;

// THIS IS HIGHLY SUSPICIOUS WHAT THE F!@#$ IS dataBuffer[16] ?    - TODO
  if ((dataBuffer[16] & 0xF) == SD_CMD6_GRP1_HS) card->tranSpeed = SD_TRANSPEED_50MBPS;
  if (speed == SD_TRANSPEED_50MBPS) {
    status = MMCSetBusFreq(ctrl->memBase, ctrl->ipClk, 50000000, 0);
    ctrl->opClk = 50000000;
  } else {                
// ConsolePrintf("MBPS 25\n");  // transpeed 25 MBPS normally
                                // 50MBPS spoof is no faster!  - TODO
                                // 8 bit bus no pinout - find maybe related
    status = MMCSetBusFreq(ctrl->memBase, ctrl->ipClk, 25000000, 0);
    ctrl->opClk = 25000000;
  }
  if (status != 0) return 0;  // fail
  return 1;
}

unsigned int MMCCardReset(mmcCtrlInfo *ctrl) {
  unsigned int status = 0;
  mmcCmd cmd;

  cmd.idx = SD_CMD(0);
  cmd.flags = SD_CMDRSP_NONE;
  cmd.arg = 0;
  status = MMC0CmdSend(ctrl, &cmd);
  return status;  // success 1   fail 0
}

unsigned int MMCStopCmdSend(mmcCtrlInfo *ctrl) {
  unsigned int status = 0;
  mmcCmd cmd;

  cmd.idx  = SD_CMD(12);
  cmd.flags = SD_CMDRSP_BUSY;
  cmd.arg = 0;
  MMC0CmdSend(ctrl, &cmd);
  status = mmc0_get_xfer_status(ctrl);
  return status;  // success 1    fail 0
}

unsigned int MMCCardTypeCheck(mmcCtrlInfo * ctrl) {
  unsigned int status;
  mmcCmd cmd;

  // Card type can be found by sending CMD55. If card responds,
  // it is a SD card. Else assume it is a MMC Card
  cmd.idx = SD_CMD(55);
  cmd.flags = 0;
  cmd.arg = 0;
  status = MMCAppCmdSend(ctrl, &cmd);
  return status;
}

unsigned int MMCCardInit(mmcCtrlInfo *ctrl) {
  mmcCardInfo *card = ctrl->card;
  unsigned int retry = 0xFFFF, status = 0;
  mmcCmd cmd;

  memset(ctrl->card, 0, sizeof(mmcCardInfo));
  card->ctrl = ctrl;
  status = MMCCardReset(ctrl);  // CMD0 reset card
  if (status == 0) return 0;
  status = MMCCardTypeCheck(ctrl);  // SDcard 1   non SDcard 0
  if (status == 1) {
    ctrl->card->cardType = MMC_CARD_SD;
    status = MMCCardReset(ctrl);
    if (status == 0) return 0;
    cmd.idx = SD_CMD(8);  // send operating voltage
    cmd.flags = 0;
    cmd.arg = (SD_CHECK_PATTERN | SD_VOLT_2P7_3P6);
    status = MMC0CmdSend(ctrl, &cmd);
    if (status == 0) {
      // cmd fail prob version < 2.0   support high voltage cards only
    }
    // Go ahead and send ACMD41, with host capabilities
    cmd.idx = SD_CMD(41);
    cmd.flags = 0;
    cmd.arg = SD_OCR_HIGH_CAPACITY | SD_OCR_VDD_WILDCARD;
    status = MMCAppCmdSend(ctrl,&cmd);
    if (status == 0) return 0;
    // Poll until card status (BIT31 of OCR) is powered up */
    do {
      cmd.idx = SD_CMD(41);
      cmd.flags = 0;
      cmd.arg = SD_OCR_HIGH_CAPACITY | SD_OCR_VDD_WILDCARD;
      MMCAppCmdSend(ctrl,&cmd);
    } while (!(cmd.rsp[0] & ((unsigned int)BIT(31))) && retry--);
    if (retry == 0) return 0;  // No point in continuing
    card->ocr = cmd.rsp[0];
    card->highCap = (card->ocr & SD_OCR_HIGH_CAPACITY) ? 1 : 0;
    cmd.idx = SD_CMD(2);  // Send CMD2 card id register
    cmd.flags = SD_CMDRSP_136BITS;
    cmd.arg = 0;
    status = MMC0CmdSend(ctrl, &cmd);
    memcpy(card->raw_cid, cmd.rsp, 16);
    if (status == 0) return 0;
    cmd.idx = SD_CMD(3);  // Send CMD3 card relative address
    cmd.flags = 0;
    cmd.arg = 0;
    status = MMC0CmdSend(ctrl, &cmd);
    card->rca = SD_RCA_ADDR(cmd.rsp[0]);
    if (status == 0) return 0;
    cmd.idx = SD_CMD(9);  // get card specific data
    cmd.flags = SD_CMDRSP_136BITS;
    cmd.arg = card->rca << 16;
    status = MMC0CmdSend(ctrl, &cmd);
    memcpy(card->raw_csd, cmd.rsp, 16);
    if (status == 0) return 0;
    if (SD_CARD_CSD_VERSION(card)) {
      card->tranSpeed = SD_CARD1_TRANSPEED(card);
      card->blkLen = 1 << (SD_CARD1_RDBLKLEN(card));
      card->size = SD_CARD1_SIZE(card);
      card->nBlks = card->size / card->blkLen;
    } else {
      card->tranSpeed = SD_CARD0_TRANSPEED(card);
      card->blkLen = 1 << (SD_CARD0_RDBLKLEN(card));
      card->nBlks = SD_CARD0_NUMBLK(card);
      card->size = SD_CARD0_SIZE(card);
    }
    // Set data block length to 512 (for byte addressing cards)
    if( !(card->highCap) ) {
      cmd.idx = SD_CMD(16);
      cmd.flags = SD_CMDRSP_NONE;
      cmd.arg = 512;
      status = MMC0CmdSend(ctrl, &cmd);
      if (status == 0) return 0;
      else card->blkLen = 512;
    }
    cmd.idx = SD_CMD(7);  // card select
    cmd.flags = SD_CMDRSP_BUSY;
    cmd.arg = card->rca << 16;
    status = MMC0CmdSend(ctrl, &cmd);
    if (status == 0) return 0;  // fail
    // Send ACMD51, to get the SD Configuration register details.
    // Note, this needs data transfer (on data lines).
    cmd.idx = SD_CMD(55);
    cmd.flags = 0;
    cmd.arg = card->rca << 16;
    status = MMC0CmdSend(ctrl, &cmd);
    if (status == 0) return 0;  // fail
    MMCXferSetup(ctrl, 1, dataBuffer, 8, 1);
    cmd.idx = SD_CMD(51);
    cmd.flags = SD_CMDRSP_READ | SD_CMDRSP_DATA;
    cmd.arg = card->rca << 16;
    cmd.nblks = 1;
    cmd.data = (signed char*)dataBuffer;
    status = MMC0CmdSend(ctrl, &cmd);
    if (status == 0) return 0;  // fail
    status = mmc0_get_xfer_status(ctrl);
    if (status == 0) return 0;  // fail
    CP15DCacheFlushBuff((unsigned int)dataBuffer, DATA_RESPONSE_WIDTH);
    card->raw_scr[0] = (dataBuffer[3] << 24) | (dataBuffer[2] << 16) | \
                       (dataBuffer[1] << 8) | (dataBuffer[0]);
    card->raw_scr[1] = (dataBuffer[7] << 24) | (dataBuffer[6] << 16) | \
	               (dataBuffer[5] << 8) | (dataBuffer[4]);
    card->sd_ver = SD_CARD_VERSION(card);
    card->busWidth = SD_CARD_BUSWIDTH(card);
  } else return 0;  // fail
  return 1;  // success
}

//
// arg    mmcCtrlInfo      mmc control information
// arg    ptr              address where data is to write
// arg    block            start block where data is to be written
// arg    nblks            number of blocks to write
//
// return  1 success | 0 fail
//
unsigned int MMCWriteCmdSend(mmcCtrlInfo *ctrl, void *ptr, unsigned int block,
                               unsigned int nblks) {
  mmcCardInfo *card = ctrl->card;
  unsigned int status = 0, address;
  mmcCmd cmd;
  // Address is in blks for high cap cards and bytes for std cards
  if (card->highCap) address = block;
  else address = block * card->blkLen;
  CP15DCacheCleanBuff((unsigned int) ptr, (512 * nblks));  // clean data cache
  MMCXferSetup(ctrl, 0, ptr, 512, nblks);
  cmd.flags = SD_CMDRSP_WRITE | SD_CMDRSP_DATA;
  cmd.arg = address;
  cmd.nblks = nblks;
  if (nblks > 1) {
    cmd.idx = SD_CMD(25);
    cmd.flags |= SD_CMDRSP_ABORT;
  } else cmd.idx = SD_CMD(24);
  status = MMC0CmdSend(ctrl, &cmd);
  if (status == 0) return 0;
  status = mmc0_get_xfer_status(ctrl);
  if (status == 0) return 0;
  if (cmd.nblks > 1) {
    status = MMCStopCmdSend(ctrl);  // send a stop
    if (status == 0) return 0;  //  fail
  }
  return 1;  // success
}

//
// arg    mmcCtrlInfo      mmc control information
// arg    ptr              address where read data is to be stored 
// arg    block            start block where data is to be read
// arg    nblks            number of blocks to read
//
// return  1 success | 0 fail
//
unsigned int MMCReadCmdSend(mmcCtrlInfo *ctrl, void *ptr, unsigned int block,
                              unsigned int nblks) {
  mmcCardInfo *card = ctrl->card;
  unsigned int status = 0, address;
  mmcCmd cmd;

  // Address is in blks for high cap cards and in actual bytes
  // for standard capacity cards
  if (card->highCap) address = block;
  else address = block * card->blkLen;
  MMCXferSetup(ctrl, 1, ptr, 512, nblks);
  cmd.flags = SD_CMDRSP_READ | SD_CMDRSP_DATA;
  cmd.arg = address;
  cmd.nblks = nblks;
  if (nblks > 1) {
    cmd.flags |= SD_CMDRSP_ABORT;
    cmd.idx = SD_CMD(18);
  } else cmd.idx = SD_CMD(17);
  status = MMC0CmdSend(ctrl, &cmd);
  if (status == 0) return 0;
  status = mmc0_get_xfer_status(ctrl);
  if (status == 0) return 0;
  if (cmd.nblks > 1) {
    status = MMCStopCmdSend(ctrl);  // send a stop
    if (status == 0) return 0;  // fail
  }
  CP15DCacheFlushBuff((unsigned int) ptr, (512 * nblks));  // invalidate data cache
  return 1;  // success
}

// eof
