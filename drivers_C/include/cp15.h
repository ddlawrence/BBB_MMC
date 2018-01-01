//
// cp15 coprocessor routines  -  prototypes
//

// Macros which can be passed to CP15ControlFeatureDisable/Enable routines 
// as 'features'. Any, or an OR combination of the below macros can be
// passed to disable/enable the corresponding feature
#define CP15_CONTROL_TEXREMAP                  (0x10000000) 
#define CP15_CONTROL_ACCESSFLAG                (0x20000000)
#define CP15_CONTROL_ALIGN_CHCK                (0x00000002)
#define CP15_CONTROL_MMU                       (0x00000001)

// prototypes
extern void CP15AuxControlFeatureEnable(unsigned int enFlag);
extern void CP15AuxControlFeatureDisable(unsigned int disFlag);
extern void CP15DCacheCleanBuff(unsigned int bufPtr, unsigned int size);
extern void CP15DCacheCleanFlushBuff(unsigned int bufPtr, unsigned int size);
extern void CP15DCacheFlushBuff(unsigned int bufPtr, unsigned int size);
extern void CP15ICacheFlushBuff(unsigned int bufPtr, unsigned int size);
extern void CP15ICacheDisable(void);
extern void CP15DCacheDisable(void);
extern void CP15ICacheEnable(void);
extern void CP15DCacheEnable(void);
extern void CP15DCacheCleanFlush(void);
extern void CP15DCacheClean(void);
extern void CP15DCacheFlush(void);
extern void CP15ICacheFlush(void);
extern void CP15Ttb0Set(unsigned int ttb);
extern void CP15TlbInvalidate(void);
extern void CP15MMUDisable(void);
extern void CP15MMUEnable(void);
extern void CP15VectorBaseAddrSet(unsigned int addr);
extern void CP15BranchPredictorInvalidate(void);
extern void CP15BranchPredictionEnable(void);
extern void CP15BranchPredictionDisable(void);
extern void CP15DomainAccessClientSet(void);
extern void CP15ControlFeatureDisable(unsigned int features);
extern void CP15ControlFeatureEnable(unsigned int features);
extern void CP15TtbCtlTtb0Config(void);
extern unsigned int CP15MainIdPrimPartNumGet(void);

// eof
