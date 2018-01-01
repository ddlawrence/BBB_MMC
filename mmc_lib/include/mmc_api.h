//
//  Multi Media Card Application Program Interface  -  prototypes
// 
#define BIT(x) (1 << x)

// SD Card information structure
#define MMC_CARD_SD		       0x0
#define MMC_CARD_MMC		   0x1

struct _mmcCtrlInfo;

// Structure for Card information 
typedef struct _mmcCardInfo {
    struct _mmcCtrlInfo *ctrl;
	unsigned int cardType;
	unsigned int rca;
	unsigned int raw_scr[2];
	unsigned int raw_csd[4];
	unsigned int raw_cid[4];
	unsigned int ocr;
	unsigned char sd_ver;
	unsigned char busWidth;
	unsigned char tranSpeed;
	unsigned char highCap;
	unsigned int blkLen;
	unsigned int nBlks;
	unsigned int size;

}mmcCardInfo;

// Structure for command 
typedef struct _mmcCmd {
	unsigned int idx;
	unsigned int flags;
	unsigned int arg;
	signed char *data;
	unsigned int nblks;
	unsigned int rsp[4];
}mmcCmd;

// Structure for controller information 
typedef struct _mmcCtrlInfo {
	unsigned int memBase;
	unsigned int ipClk;
	unsigned int opClk;
    unsigned int intrMask;
	unsigned int dmaEnable;
	unsigned int busWidth;
	unsigned int highspeed;
	unsigned int ocr;
    unsigned int cdPinNum;
    unsigned int wpPinNum;
	mmcCardInfo *card;
}mmcCtrlInfo;

// SD Commands enumeration 
#define SD_CMD(x)   (x)

// Command/Response flags for notifying some information to controller 
#define SD_CMDRSP_NONE			BIT(0)
#define SD_CMDRSP_STOP			BIT(1)
#define SD_CMDRSP_FS			BIT(2)
#define SD_CMDRSP_ABORT			BIT(3)
#define SD_CMDRSP_BUSY			BIT(4)
#define SD_CMDRSP_136BITS		BIT(5)
#define SD_CMDRSP_DATA			BIT(6)
#define SD_CMDRSP_READ			BIT(7)
#define SD_CMDRSP_WRITE			BIT(8)

// SD voltage enumeration as per VHS field of the interface command 
#define SD_VOLT_2P7_3P6                 (0x000100u)

// SD OCR register definitions 
// High capacity 
#define SD_OCR_HIGH_CAPACITY    	BIT(30)
// Voltage 
#define SD_OCR_VDD_2P7_2P8		BIT(15)
#define SD_OCR_VDD_2P8_2P9		BIT(16)
#define SD_OCR_VDD_2P9_3P0		BIT(17)
#define SD_OCR_VDD_3P0_3P1		BIT(18)
#define SD_OCR_VDD_3P1_3P2		BIT(19)
#define SD_OCR_VDD_3P2_3P3		BIT(20)
#define SD_OCR_VDD_3P3_3P4		BIT(21)
#define SD_OCR_VDD_3P4_3P5		BIT(22)
#define SD_OCR_VDD_3P5_3P6		BIT(23)
// This is for convenience only. Sets all the VDD fields 
#define SD_OCR_VDD_WILDCARD		(0x1FF << 15)

// SD CSD register definitions 
#define SD_TRANSPEED_25MBPS		(0x32u)
#define SD_TRANSPEED_50MBPS		(0x5Au)

#define SD_CARD_CSD_VERSION(crd) (((crd)->raw_csd[3] & 0xC0000000) >> 30)

#define SD_CSD0_DEV_SIZE(csd3, csd2, csd1, csd0) (((csd2 & 0x000003FF) << 2) | ((csd1 & 0xC0000000) >> 30))
#define SD_CSD0_MULT(csd3, csd2, csd1, csd0) ((csd1 & 0x00038000) >> 15)
#define SD_CSD0_RDBLKLEN(csd3, csd2, csd1, csd0) ((csd2 & 0x000F0000) >> 16)
#define SD_CSD0_TRANSPEED(csd3, csd2, csd1, csd0) ((csd3 & 0x000000FF) >> 0)

#define SD_CARD0_DEV_SIZE(crd) SD_CSD0_DEV_SIZE((crd)->raw_csd[3], (crd)->raw_csd[2], (crd)->raw_csd[1], (crd)->raw_csd[0])
#define SD_CARD0_MULT(crd) SD_CSD0_MULT((crd)->raw_csd[3], (crd)->raw_csd[2], (crd)->raw_csd[1], (crd)->raw_csd[0])
#define SD_CARD0_RDBLKLEN(crd) SD_CSD0_RDBLKLEN((crd)->raw_csd[3], (crd)->raw_csd[2], (crd)->raw_csd[1], (crd)->raw_csd[0])
#define SD_CARD0_TRANSPEED(crd) SD_CSD0_TRANSPEED((crd)->raw_csd[3], (crd)->raw_csd[2], (crd)->raw_csd[1], (crd)->raw_csd[0])
#define SD_CARD0_NUMBLK(crd) ((SD_CARD0_DEV_SIZE((crd)) + 1) * (1 << (SD_CARD0_MULT((crd)) + 2)))
#define SD_CARD0_SIZE(crd) ((SD_CARD0_NUMBLK((crd))) * (1 << (SD_CARD0_RDBLKLEN(crd))))

#define SD_CSD1_DEV_SIZE(csd3, csd2, csd1, csd0) (((csd2 & 0x0000003F) << 16) | ((csd1 & 0xFFFF0000) >> 16))
#define SD_CSD1_RDBLKLEN(csd3, csd2, csd1, csd0) ((csd2 & 0x000F0000) >> 16)
#define SD_CSD1_TRANSPEED(csd3, csd2, csd1, csd0) ((csd3 & 0x000000FF) >> 0)

#define SD_CARD1_DEV_SIZE(crd) SD_CSD1_DEV_SIZE((crd)->raw_csd[3], (crd)->raw_csd[2], (crd)->raw_csd[1], (crd)->raw_csd[0])
#define SD_CARD1_RDBLKLEN(crd) SD_CSD1_RDBLKLEN((crd)->raw_csd[3], (crd)->raw_csd[2], (crd)->raw_csd[1], (crd)->raw_csd[0])
#define SD_CARD1_TRANSPEED(crd) SD_CSD1_TRANSPEED((crd)->raw_csd[3], (crd)->raw_csd[2], (crd)->raw_csd[1], (crd)->raw_csd[0])
#define SD_CARD1_SIZE(crd) ((SD_CARD1_DEV_SIZE((crd)) + 1) * (512 * 1024))

// Check RCA/status 
#define SD_RCA_ADDR(rca)             ((rca & 0xFFFF0000) >> 16)
#define SD_RCA_STAT(rca)             (rca & 0x0xFFFF)

// Check pattern that can be used for card response validation 
#define SD_CHECK_PATTERN   0xAA

// SD SCR related macros 
#define SD_VERSION_1P0		0
#define SD_VERSION_1P1		1
#define SD_VERSION_2P0		2
#define SD_BUS_WIDTH_1BIT	1
#define SD_BUS_WIDTH_4BIT	4

// Helper macros 
// Note card registers are big endian 
#define SD_CARD_VERSION(sdcard)		((sdcard)->raw_scr[0] & 0xF)
#define SD_CARD_BUSWIDTH(sdcard)	(((sdcard)->raw_scr[0] & 0xF00) >> 8)
#define GET_SD_CARD_BUSWIDTH(sdcard)  ((((sdcard.busWidth) & 0x0F) == 0x01) ? \
                                      0x1 : ((((sdcard).busWidth & 0x04) == \
                                      0x04) ? 0x04 : 0xFF))
#define GET_SD_CARD_FRE(sdcard)	      (((sdcard.tranSpeed) == 0x5A) ? 50 : \
                                      (((sdcard.tranSpeed) == 0x32) ? 25 : 0))

// Cacheline size 
#ifndef SOC_CACHELINE_SIZE
#define SOC_CACHELINE_SIZE         128
#endif

// CM6 Swith mode arguments for High Speed 
#define SD_SWITCH_MODE        0x80FFFFFF
#define SD_CMD6_GRP1_SEL      0xFFFFFFF0
#define SD_CMD6_GRP1_HS       0x1


// Function prototypes
extern unsigned int MMCReadCmdSend(mmcCtrlInfo *ctrl, void *ptr, unsigned int block,
				                     unsigned int blks);
extern unsigned int MMCWriteCmdSend(mmcCtrlInfo *ctrl, void *ptr, unsigned int block,
				                       unsigned int blks);
extern unsigned int MMCAppCmdSend(mmcCtrlInfo *ctrl, mmcCmd *c);
extern unsigned int MMCTranSpeedSet(mmcCtrlInfo *ctrl);
extern unsigned int MMCTranSpeedSet(mmcCtrlInfo *ctrl);
extern unsigned int MMCBusWidthSet(mmcCtrlInfo *ctrl);
extern unsigned int MMCStopCmdSend(mmcCtrlInfo *ctrl);
extern unsigned int MMCCardReset(mmcCtrlInfo *ctrl);
extern unsigned int MMCCardInit(mmcCtrlInfo *ctrl);
