//
// BeagleboneBlack MMC Demo Program - full FAT32 filesystem
//
// mixed C & assembly 
// demonstrates MMC, MMU, DMA, Cache, IRQ, UART0, RTC and GPIO usage
// requires no external h/w
//
// built with GNU tools :) on (Raspberry) Pi-Top laptop running Linux :) 
//
// BBB-MMC initial drivers from [ti.com , see StarterWare code for copyright]
// Someone there did a very good job!  Kudos to the dude that wrote it.  
// Having said that, it was very difficult to trace execution and detangle. 
// 
// FAT32 filesystem utilities from www.elm-chan.org/fsw/ff/00index_p.html 
// Chan wrote a great multi-platform micro-filesystem utility.
//
// Boot sequence:  
//   bootloader "MLO" resides on MMC then bootloader loads image "app_mmc" 
//   via UART0/xmodem (terminal emulator minicom)
//   image is loaded to 0x80000000 and execution begins there
//   per Fast External Booting 26.1.6 spruh73n
//   - or -    boot via MMC card
//   rename "MLO_MMC" to "MLO" and load/run "app" automatically on power-up
//   all these are provided in demo BBB_MMC.zip
//   
// known bugs/TODO:
//   MMC file copy only 1Mbyte/sec - needs further debugging
//   move MLO bootloader to ROM - for dev/debug
//   move image to on-board NAND - for runtime release
//   UART xmodem file transfer
//  
// get involved    www.baremetal.tech
//
// LEGAL NOTICE:  This is abandoned software.  It does not work whatsoever.  
//
#define am335x

#include "soc_AM335x.h"
#include "hw_control_AM335x.h"
#include "interrupt.h"
#include "string.h"
#include "dma.h"
#include "dma_event.h"
#include "mmc_hwif.h"
#include "mmc_api.h"

// Frequency 
#define MMC_IN_FREQ                96000000 // 96MHz 
#define MMC_INIT_FREQ              400000   // 400kHz 

#define MMC_CARD_DETECT_PINNUM     6

// DMA Event queue number
#define EVT_QUEUE_NUM                  0
 
// DMA Region Number 
#define REGION_NUMBER                  0

// Block size config 
#define MMC_BLK_SIZE               512

// Global data pointers 
#define MMC_DATA_SIZE              512

// SD card info structure 
mmcCardInfo sdCard;

// SD Controller info structure 
mmcCtrlInfo  ctrlInfo;

// DMA callback function array 
static void (*cb_Fxn[DMA_NUM_TCC]) (unsigned int tcc, unsigned int status);

// Global flags for interrupt handling 
volatile unsigned int sdBlkSize = MMC_BLK_SIZE;
volatile unsigned int callbackOccured = 0; 
volatile unsigned int xferCompFlag = 0; 
volatile unsigned int dataTimeout = 0;
volatile unsigned int cmdCompFlag = 0;
volatile unsigned int cmdTimeout = 0; 
volatile unsigned int errFlag = 0;

unsigned char data[MMC_DATA_SIZE] 
                    __attribute__ ((aligned (SOC_CACHELINE_SIZE)))= {0};

extern unsigned int uart0_irq_count, uart0_rbuf;
unsigned int uart0_irq_old=0;
extern unsigned int rtc_irq_count, year, month, day, hour, min, sec;
unsigned char CmdBufIdx;
#define CMD_BUF_SIZE    512
static char CmdBuf[CMD_BUF_SIZE];
extern char *CwdBuf;
extern void FATFsMount(unsigned int driveNum, void *ptr, char *CmdBuf);
extern void CmdLineProcess(char *CmdBuf);
extern int Cmd_help(int argc, char *argv[]);

//
// check command status
//
unsigned int mmc0_get_cmd_status(mmcCtrlInfo *ctrl) {
  unsigned int status = 0;

  while ((cmdCompFlag == 0) && (cmdTimeout == 0));  // block here
  if (cmdCompFlag) {
    status = 1;
    cmdCompFlag = 0;
  }
  if (cmdTimeout) {
    status = 0;
    cmdTimeout = 0;
  }
  return status;
}

//
// check transfer status
//
unsigned int mmc0_get_xfer_status(mmcCtrlInfo *ctrl) {
  unsigned int status = 0;
  volatile unsigned int timeOut = 0xFFFF;

  while ((xferCompFlag == 0) && (dataTimeout == 0));  // block here
  if (xferCompFlag) {
    status = 1;
    xferCompFlag = 0;
  }
  if (dataTimeout) {
    status = 0;
    dataTimeout = 0;
  }
  if (HWREG(ctrl->memBase + MMC_CMD) & MMC_CMD_DP) {  // poll for callback 
nothing(); //  this is necessary, prog hangs!  - TODO
    while(callbackOccured == 0 && ((timeOut--) != 0));  // block here
    callbackOccured = 0;
    if(timeOut == 0) status = 0;
  }
  ctrlInfo.dmaEnable = 0;
  return status;
}

//
// callback from DMA Completion Handler.
//
// status arg is unused may need for ethernet   delete later - TODO
static void callback(unsigned int tccNum, unsigned int status) {
    callbackOccured = 1;
    DMADisableTransfer(SOC_DMA0CC_0_REGS, tccNum, DMA_TRIG_MODE_EVENT);
}

//
// dma config
//
static void dma_config(void) {
  dma_clk_cfg();
  DMAInit(SOC_DMA0CC_0_REGS, EVT_QUEUE_NUM);
  // Request DMA Channel and TCC for MMC Transmit
  DMARequestChannel(SOC_DMA0CC_0_REGS, DMA_CHANNEL_TYPE_DMA,
                      DMA_CHA_MMC0_TX, DMA_CHA_MMC0_TX, EVT_QUEUE_NUM);
  // Registering Callback Function for TX
  cb_Fxn[DMA_CHA_MMC0_TX] = &callback;
  // Request DMA Channel and TCC for MMC Receive 
  DMARequestChannel(SOC_DMA0CC_0_REGS, DMA_CHANNEL_TYPE_DMA,
                      DMA_CHA_MMC0_RX, DMA_CHA_MMC0_RX, EVT_QUEUE_NUM);
  // Registering Callback Function for RX
  cb_Fxn[DMA_CHA_MMC0_RX] = &callback;
}

//
// setup a DMA receive transfer
//
void MMCRxDMAsetup(void *ptr, unsigned int blkSize, unsigned int nblks) {
  DMACCPaRAMEntry paramSet;
  paramSet.srcAddr    = ctrlInfo.memBase + MMC_DATA;
  paramSet.destAddr   = (unsigned int)ptr;
  paramSet.srcBIdx    = 0;
  paramSet.srcCIdx    = 0;
  paramSet.destBIdx   = 4;
  paramSet.destCIdx   = (unsigned short)blkSize;
  paramSet.aCnt       = 0x4;
  paramSet.bCnt       = (unsigned short)blkSize/4;
  paramSet.cCnt       = (unsigned short)nblks;
  paramSet.bCntReload = 0x0;
  paramSet.linkAddr   = 0xffff;
  paramSet.opt        = 0;
  // Set OPT 
  paramSet.opt |= ((DMA_CHA_MMC0_RX << DMACC_OPT_TCC_SHIFT) & DMACC_OPT_TCC);
  // Transmission completion interrupt enable 
  paramSet.opt |= (1 << DMACC_OPT_TCINTEN_SHIFT);
  // Read FIFO : SRC Constant addr mode 
  paramSet.opt |= (1 << 0);
  // SRC FIFO width is 32 bit 
  paramSet.opt |= (2 << 8);
  // AB-Sync mode 
  paramSet.opt |= (1 << 2);
  // configure PaRAM Set 
  DMASetPaRAM(SOC_DMA0CC_0_REGS, DMA_CHA_MMC0_RX, &paramSet);
  // Enable transfer 
  DMAEnableTransfer(SOC_DMA0CC_0_REGS, DMA_CHA_MMC0_RX, DMA_TRIG_MODE_EVENT);
}

//
// setup a DMA transmit transfer
//
void MMCTxDMAsetup(void *ptr, unsigned int blkSize, unsigned int blks) {
  DMACCPaRAMEntry paramSet;
  paramSet.srcAddr    = (unsigned int)ptr;
  paramSet.destAddr   = ctrlInfo.memBase + MMC_DATA;
  paramSet.srcBIdx    = 4;
  paramSet.srcCIdx    = blkSize;
  paramSet.destBIdx   = 0;
  paramSet.destCIdx   = 0;
  paramSet.aCnt       = 0x4;
  paramSet.bCnt       = (unsigned short)blkSize/4;
  paramSet.cCnt       = (unsigned short)blks;
  paramSet.bCntReload = 0x0;
  paramSet.linkAddr   = 0xffff;
  paramSet.opt        = 0;
  // Set OPT 
  paramSet.opt |= ((DMA_CHA_MMC0_TX << DMACC_OPT_TCC_SHIFT) & DMACC_OPT_TCC);
  // Transmission completion interrupt enable 
  paramSet.opt |= (1 << DMACC_OPT_TCINTEN_SHIFT);
  // Read FIFO : DST Constant addr mode 
  paramSet.opt |= (1 << 1);
  // DST FIFO width is 32 bit 
  paramSet.opt |= (2 << 8);
  // AB-Sync mode 
  paramSet.opt |= (1 << 2);
  // configure PaRAM Set 
  DMASetPaRAM(SOC_DMA0CC_0_REGS, DMA_CHA_MMC0_TX, &paramSet);
  // Enable transfer 
  DMAEnableTransfer(SOC_DMA0CC_0_REGS, DMA_CHA_MMC0_TX, DMA_TRIG_MODE_EVENT);
}

//
//  setup a DMA transfer
//
void MMCXferSetup(mmcCtrlInfo *ctrl, unsigned char rwFlag, void *ptr,
                             unsigned int blkSize, unsigned int nBlks) {
  callbackOccured = 0;
  xferCompFlag = 0;
  if (rwFlag == 1) MMCRxDMAsetup(ptr, blkSize, nBlks);
  else MMCTxDMAsetup(ptr, blkSize, nBlks);

  ctrl->dmaEnable = 1;
  MMCBlkLenSet(ctrl->memBase, blkSize);
}

//
//  DMA channel controller completion IRQ service routine
//
void DMACompletionISR(void) {
  volatile unsigned int pendingIrqs;
  volatile unsigned int isIPR = 0;
  unsigned int indexl;
  unsigned int Cnt = 0;

  indexl = 1;
  isIPR = DMAGetIntrStatus(SOC_DMA0CC_0_REGS);
  if(isIPR) {
    while ((Cnt < DMACC_COMPL_HANDLER_RETRY_COUNT)&& (indexl != 0u)) {
      indexl = 0u;
      pendingIrqs = DMAGetIntrStatus(SOC_DMA0CC_0_REGS);
      while (pendingIrqs) {
        if(pendingIrqs & 1u) {
          // if user has not given any callback function
          // while requesting TCC, its TCC specific bit
          // in IPR register will NOT be cleared.
          // here write to ICR to clear corresponding IPR bits 
          DMAClrIntr(SOC_DMA0CC_0_REGS, indexl);
          if (cb_Fxn[indexl] != NULL) {
            (*cb_Fxn[indexl])(indexl, DMA_XFER_COMPLETE);
          }
        }
        ++indexl;
        pendingIrqs >>= 1u;
      }
      Cnt++;
    }
  }
}
//
//  DMA channel controller error IRQ service routine
//
void DMACCErrorISR(void) {
  volatile unsigned int evtqueNum = 0;  // Event Queue Num
  volatile unsigned int pendingIrqs, isIPRH = 0, isIPR = 0, Cnt = 0u, index;

  pendingIrqs = 0x0;
  index = 0x1;
  isIPR  = DMAGetIntrStatus(SOC_DMA0CC_0_REGS);
  isIPRH = DMAIntrStatusHighGet(SOC_DMA0CC_0_REGS);
  if((isIPR | isIPRH ) || (DMAQdmaGetErrIntrStatus(SOC_DMA0CC_0_REGS) != 0)
  || (DMAGetCCErrStatus(SOC_DMA0CC_0_REGS) != 0)) {
    // Loop for DMACC_ERR_HANDLER_RETRY_COUNT
    // break when no pending interrupt is found
    while ((Cnt < DMACC_ERR_HANDLER_RETRY_COUNT) && (index != 0u)) {
      index = 0u;
      if(isIPR) pendingIrqs = DMAGetErrIntrStatus(SOC_DMA0CC_0_REGS);
      else pendingIrqs = DMAErrIntrHighStatusGet(SOC_DMA0CC_0_REGS);
      while (pendingIrqs) {  // Process all pending interrupts
        if(pendingIrqs & 1u) {
          // Write to EMCR to clear the corresponding EMR bits 
          // Clear any SER
          if(isIPR) DMAClrMissEvt(SOC_DMA0CC_0_REGS, index);
          else DMAClrMissEvt(SOC_DMA0CC_0_REGS, index + 32);
        }
        ++index;
        pendingIrqs >>= 1u;
      }
      index = 0u;
      pendingIrqs = DMAQdmaGetErrIntrStatus(SOC_DMA0CC_0_REGS);
      while (pendingIrqs) {  // Process pending interrupts
        if(pendingIrqs & 1u) {
          // write to QEMCR to clear corresponding QEMR bits, clear any QSER
          DMAQdmaClrMissEvt(SOC_DMA0CC_0_REGS, index);
        }
        ++index;
        pendingIrqs >>= 1u;
      }
      index = 0u;
      pendingIrqs = DMAGetCCErrStatus(SOC_DMA0CC_0_REGS);
      if (pendingIrqs != 0u) {
        // Process all pending CC error interrupts
        // Queue threshold error for different event queues
        for (evtqueNum = 0u; evtqueNum < SOC_DMA_NUM_EVQUE; evtqueNum++) {
          if((pendingIrqs & (1u << evtqueNum)) != 0u) {
            // Clear error interrupt
            DMAClrCCErr(SOC_DMA0CC_0_REGS, (1u << evtqueNum));
          }
        }
        // Transfer completion code error 
        if ((pendingIrqs & (1 << DMACC_CCERR_TCCERR_SHIFT)) != 0u) {
          DMAClrCCErr(SOC_DMA0CC_0_REGS, (0x01u << DMACC_CCERR_TCCERR_SHIFT));
        }
        ++index;
      }
      Cnt++;
    }
  }
}

//
// Mux out MMC0 and init MMC data structures
//
static void mmc0_config(void) {
  pinmux(CONTROL_CONF_MMC0_DAT3, 0x30); // MUXMODE 0, SLOW SLEW, RX ACTIVE
  pinmux(CONTROL_CONF_MMC0_DAT2, 0x30); // MUXMODE 0, SLOW SLEW, RX ACTIVE
  pinmux(CONTROL_CONF_MMC0_DAT1, 0x30); // MUXMODE 0, SLOW SLEW, RX ACTIVE
  pinmux(CONTROL_CONF_MMC0_DAT0, 0x30); // MUXMODE 0, SLOW SLEW, RX ACTIVE
  pinmux(CONTROL_CONF_MMC0_CLK, 0x30); // MUXMODE 0, SLOW SLEW, RX ACTIVE
  pinmux(CONTROL_CONF_MMC0_CMD, 0x30); // MUXMODE 0, SLOW SLEW, RX ACTIVE
  pinmux(CONTROL_CONF_SPI0_CS1, 0x35); // MUXMODE 5, SLOW SLEW, RX ACTIVE

  ctrlInfo.memBase = SOC_MMC_0_REGS;
  ctrlInfo.intrMask = (MMC_INTR_CMDCOMP | MMC_INTR_CMDTIMEOUT |
                       MMC_INTR_DATATIMEOUT | MMC_INTR_TRNFCOMP);
  ctrlInfo.busWidth = (SD_BUS_WIDTH_1BIT | SD_BUS_WIDTH_4BIT);
  ctrlInfo.highspeed = 1;
  ctrlInfo.ocr = (SD_OCR_VDD_3P0_3P1 | SD_OCR_VDD_3P1_3P2);
  ctrlInfo.card = &sdCard;
  ctrlInfo.ipClk = MMC_IN_FREQ;
  ctrlInfo.opClk = MMC_INIT_FREQ;
  ctrlInfo.cdPinNum = MMC_CARD_DETECT_PINNUM;
  
  sdCard.ctrl = &ctrlInfo;

  callbackOccured = 0;
  xferCompFlag = 0;
  dataTimeout = 0;
  cmdCompFlag = 0;
  cmdTimeout = 0;
}

//
//  mmc0 IRQ service routine
//
void mmc0_isr(void) {
  volatile unsigned int status = 0;

  status = MMCIntrStatusGet(ctrlInfo.memBase, 0xffffffff);
  mmc0_clear_status(status);
  if (status & MMC_STAT_CMDCOMP) cmdCompFlag = 1;
  if (status & MMC_STAT_ERR) {
    errFlag = status & 0xFFFF0000;
    if (status & MMC_STAT_CMDTIMEOUT) cmdTimeout = 1;
    if (status & MMC_STAT_DATATIMEOUT) dataTimeout = 1;
  }
  if (status & MMC_STAT_TRNFCOMP) xferCompFlag = 1;
  if (status & MMC_STAT_CARDINS) {
    FATFsMount(0, &sdCard, &CmdBuf);  // card inserted
    ConsolePrintf("\n");              // print current working dir
    CmdLineProcess(&CmdBuf);
  }
  if (status & MMC_STAT_CREM) {  // card removed
    CwdBuf[0] = 0x0;             // wipe current working directory
    callbackOccured = 0;
    xferCompFlag = 0;
    dataTimeout = 0;
    cmdCompFlag = 0;
    cmdTimeout = 0;
    mmc0_init();
    mmc0_irq_enab();
    ConsolePrintf("\nPlease insert card... ");
  }
}

//
//  character collection 
//
//  buffer received characters & preprocess - crude command line editing
//
void char_collect(unsigned char KeyStroke) {

  // character is a backspace and 1 or more characters in buffer?
  if((KeyStroke == '\b') && (CmdBufIdx != 0)) {
    ConsolePrintf("\b \b");  // erase last character from cmd line buffer
    CmdBufIdx--;
    CmdBuf[CmdBufIdx] = '\0';
  } 
  // character is a newline?
  else if((KeyStroke == '\r') || (KeyStroke == '\n')) {  
    ConsolePrintf("\n");
    CmdLineProcess(&CmdBuf);  // execute command in buffer
    CmdBufIdx = 0;            // re-initialize cmd line buffer
    CmdBuf[CmdBufIdx] = '\0';
    return;
  }
  // character is an escape or Ctrl-U?
  else if((KeyStroke == 0x1b) || (KeyStroke == 0x15)) {
    while(CmdBufIdx) {    // erase all characters in cmd line buffer
      ConsolePrintf("\b \b");
      CmdBufIdx--;
    }
    CmdBuf[0] = '\0';
  }
  // printable ASCII character?  also check for buffer overflow
  else if((KeyStroke >= ' ') && (KeyStroke <= '~') && (CmdBufIdx < (CMD_BUF_SIZE - 1))) {
    CmdBuf[CmdBufIdx++] = KeyStroke;  // append character to cmd line buffer
    CmdBuf[CmdBufIdx] = '\0';
    ConsolePrintf("%c", KeyStroke);   // echo charaacter
  }
  return;  // return to collect more keystrokes
}

//
// main
//
int main(void) {
  volatile unsigned int iblink=0;

  cache_en();
  mclk_1GHz();
  gpio_init(SOC_GPIO_1_REGS, (0xf << 21));  // enab USR LEDs, pin # 21-24
  uart0_init();
  tim_init();
  rtc_init();
  irq_init();
  mmu_init();
  dma_config();
  mmc0_config();
  mmc0_init();
  mmc0_irq_enab();

  CmdBufIdx = 0;            // initialize command line buffer
  CmdBuf[CmdBufIdx] = '\0';
  FATFsMount(0, &sdCard, &CmdBuf);
  ConsolePrintf("Enter ? for help\n");
  CmdLineProcess(&CmdBuf);  // print current working dir

  while(1) {  // loop 4 ever
    //
    // application code here
    // design it as a non-blocking state-machine
    // execution traps back to this loop
    // put custom console commands in file mmc_uif.c array   CmdTable[]
    //
    
    if(iblink == 0x0) gpio_on(SOC_GPIO_1_REGS, 0x1<<21);      // blink USRLED1 
    if(iblink == 0x7ffff) gpio_off(SOC_GPIO_1_REGS, 0x1<<21); // only when not busy
    iblink = (iblink + 1) & 0x7fffff;                         // processing IRQs
    
    if(uart0_irq_old != uart0_irq_count) {  // detect console keystroke
      uart0_irq_old = uart0_irq_count & 0x7fffffff;
      if(uart0_rbuf > 0x0) {                // ASCII character received
	char_collect((char)uart0_rbuf);     // console command execution
        uart0_rbuf = 0x0;
      }
    }
  }
}

// eof
