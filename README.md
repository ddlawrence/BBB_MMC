BBB_MMC

mixed C & Assembly BARE METAL MMC/FAT32 demo for BeagleboneBlack

Includes periperal drivers:  
  IRQ
  Co-Processor
  DMA
  Cache 
  MMU
  MMC
  Timer
  RTC
  UART
  GPIO

Utilizes ported starter ware drivers and Elm-Chan FAT32 middleware. 
User console interface on UART0 for basic file manipulation. 
User application and middleware runs in SYS mode. 
Peripheral work handled by ISRs running in IRQ mode. 
No IRQ prioritizing, masking or queuing, minimal mode switching. 
As a result, ISRs are very lean and small. 
Supports full FAT32 filesystem functionality (DOS style filenames). 
Entire executable image size < 32k bytes ! 

Use the main program, C middleware and Assembly drivers as a skeleton 
for your application and strip out/add on whatever you need freely, 
without restriction.  

Makefile & loadscript provided for GCC in Linux and Windows.  
It now uses UART and MMC booting.  XDS100V2 jtag to load programs 
should still work.  Mod the Makefile as you prefer.

Most everything is fully detangled.
The rest will happen with the next release of mother (TCP is coming!)

It is all there in concise format, so it should be easy for noobs to
understand/test/hack/mod for your next BBB bare metal project requiring
a stand-alone control system.  

Get involved     www.baremetal.tech

LEGAL NOTICE:  This is abandoned software.  It does not work whatsoever.  
