//
//  Multi Media Card - User Interface
//  
//  functions originally written by TI dude
//  they perform command line processing to provide a user 
//  console for file and directory manipulation/maintenance
//
#include "string.h"
#include "soc_AM335x.h"
#include "beaglebone.h"
#include "mmu.h"
#include "mmc_hwif.h"
#include "mmc_api.h"
#include "mmc_uif.h"
#include "ff.h"
#include "bbb_main.h"

// Fat devices registered 
#ifndef fatDevice
typedef struct _fatDevice {
    void *dev;               // Pointer to underlying device/controller 
    FATFS *fs;               // File system pointer 
    unsigned int initDone;  // state 
}fatDevice;
#endif
extern fatDevice fat_devices[2];

// size must hold the longest full path name, file name, and trailing null
#define PATH_BUF_SIZE   512

// size of the buffers that hold temporary data
#define DATA_BUF_SIZE   64 * (2 * 512)

// help message for cat.  UART xmodem file transfer not ported yet - TODO
#define HELP_RD_FL "  : Show contents of a text file : cat <FILENAME> \n"
#define HELP_WR_FL "       Write to a file : cat <INPUTFILE> > <OUTPUTFILE> \n"
#define HELP_RD_UART "       Read from UART : cat dev.UART \n"
#define HELP_WR_UART "       Write from UART : cat dev.UART > <OUTPUTFILE>"
#define HELP_CAT HELP_RD_FL HELP_WR_FL HELP_RD_UART HELP_WR_UART

// Current FAT fs state.
static FATFS FatFsObj  __attribute__ ((aligned (SOC_CACHELINE_SIZE)));
static DIR DirObject;
static FILINFO FileInfoObject;
static FIL FileObject  __attribute__ ((aligned (SOC_CACHELINE_SIZE)));
static FIL fileObjectWrite  __attribute__ ((aligned (SOC_CACHELINE_SIZE)));

// structure to map numerical FRESULT code and name-string
// FRESULT codes are returned from FatFs driver routines in ff.c
typedef struct {
    FRESULT fresult;
    char *ResultString;
}
tFresultString;
// tabulate result codes
#define FRESULT_ENTRY(f)        { (f), (#f) }
// table to map numerical FRESULT code and name-string
tFresultString FResultStrings[] = {
    FRESULT_ENTRY(FR_OK),
    FRESULT_ENTRY(FR_NOT_READY),
    FRESULT_ENTRY(FR_NO_FILE),
    FRESULT_ENTRY(FR_NO_PATH),
    FRESULT_ENTRY(FR_INVALID_NAME),
    FRESULT_ENTRY(FR_INVALID_DRIVE),
    FRESULT_ENTRY(FR_DENIED),
    FRESULT_ENTRY(FR_EXIST),
    FRESULT_ENTRY(FR_RW_ERROR),
    FRESULT_ENTRY(FR_WRITE_PROTECTED),
    FRESULT_ENTRY(FR_NOT_ENABLED),
    FRESULT_ENTRY(FR_NO_FILESYSTEM),
    FRESULT_ENTRY(FR_INVALID_OBJECT),
    FRESULT_ENTRY(FR_MKFS_ABORTED)
};

// number of result codes.
#define NUM_FRESULT_CODES (sizeof(FResultStrings) / sizeof(tFresultString))

// holds full path to the current working directory.  Initially set to "/"
// static char CwdBuf[PATH_BUF_SIZE] = "/";
char CwdBuf[PATH_BUF_SIZE] = "/";

// temporary data buffer used for write file paths
static char WrBufPath[PATH_BUF_SIZE] = "/";

// temporary data buffer for manipulating file paths, or reading data from card
static char TmpBufPath[PATH_BUF_SIZE] __attribute__ ((aligned (SOC_CACHELINE_SIZE)));

// temporary data buffer used for reading / writing data to memory card
static char DataBufRW[DATA_BUF_SIZE] __attribute__ ((aligned (SOC_CACHELINE_SIZE)));

// max number of arguments that can be parsed
#define CMDLINE_MAX_ARGS        8

extern int xmodemReceive(unsigned char *dest, int destsz);

//
// return a string representation of a FatTserror code 
//
const char * StringFromFresult(FRESULT fresult) {
    unsigned int uIdx;

    // Enter a loop to search the error code table for a matching error code. 
    for(uIdx = 0; uIdx < NUM_FRESULT_CODES; uIdx++) {
        // If a match is found, then return the string name of the error code.
        if(FResultStrings[uIdx].fresult == fresult) {
            return(FResultStrings[uIdx].ResultString);
        }
    }
    return("UNKNOWN ERROR CODE");  // no matching code found
}

// This function implements the "rm" command.  It takes an argument that
// specifies the file or working directory to be deleted.  Path separators must
// use a forward slash "/".  The argument to cd can be one of the following:
// * a fully specified path of file or directory ("/my/path/to/my-dir-file")
// * a single file or directory name that is in current directory ("my-dir-file")
// It does not understand relative paths ("../this/path")
//
// Note: A directory can be removed only if it is empty.
int Cmd_rm(int argc, char *argv[]) {
    FRESULT fresult;

    // If the first character is /, then this is a fully specified path, and it
    // should just be used as-is.
    if(argv[1][0] == '/') {
        // Make sure the new path is not bigger than the cwd buffer.
        if(strlen(argv[1]) + 1 > sizeof(CwdBuf)) {
   ConsolePrintf("Resulting path name too long\n");
            return(0);
        }

        // Test to make sure that when the new additional path is added on to
        // the current path, there is room in the buffer for the full new path.
        // It needs to include a new separator, and a trailing null character.
        else if(strlen(CwdBuf) >= strlen(argv[1])) {
            // If not already at the root level, then append a /
            if((!strncmp(CwdBuf, argv[1], strlen(argv[1]))) &&
               ((CwdBuf[strlen(argv[1])] == '/') ||
                (CwdBuf[strlen(argv[1])] == '\0'))) {
       ConsolePrintf("Cannot delete parent directory \n");
                return(0);
            }

            // If the new path name (in argv[1])  is not too long, then copy it
            // into the temporary buffer so it can be checked.
            else {
                strncpy(TmpBufPath, argv[1], sizeof(TmpBufPath) - 1);
            }
        }
        // If the new path name (in argv[1])  is not too long, then copy it
        // into the temporary buffer so it can be checked.
        else {
            strncpy(TmpBufPath, argv[1], sizeof(TmpBufPath) - 1);
        }
    }
    // Otherwise this is just a normal path name from the current directory,
    // and it needs to be appended to the current path.
    else {
        // Copy the current working path into a temporary buffer so it can be
        // manipulated.
        strcpy(TmpBufPath, CwdBuf);
        // Test to make sure that when the new additional path is added on to
        // the current path, there is room in the buffer for the full new path.
        // It needs to include a new separator, and a trailing null character.
        if(strlen(TmpBufPath) + strlen(argv[1]) + 1 + 1 > sizeof(CwdBuf)) {
   ConsolePrintf("Resulting path name is too long\n");
            return(0);
        }
        // The new path is okay, so add the separator and then append the new
        // directory to the path.
        else {
            // If not already at the root level, then append a /
            if(strcmp(TmpBufPath, "/")) {
                strcat(TmpBufPath, "/");
            }
            // Append the new directory to the path.
            strcat(TmpBufPath, argv[1]);
        }
    }
    // Remove file or directory path is in TmpBufPath.
    fresult = f_unlink(TmpBufPath);

    // Check status.  Inform user and return.
    if(fresult != FR_OK) {
         ConsolePrintf("rm: %s\n", CwdBuf);
        return(fresult);
    }
    // Return success
    return(0);
}
//
// "ls" command   open current directory and displays its contents
//
int Cmd_ls(int argc, char *argv[]) {

    unsigned long int i, j, ulTotalSize, ulFileCount, ulDirCount;
    FRESULT fresult;

    // Open the current directory for access
    fresult = f_opendir(&DirObject, CwdBuf);

    // Check for error and return if there is a problem.
    if(fresult != FR_OK) return(fresult);
    ulTotalSize = 0;
    ulFileCount = 0;
    ulDirCount = 0;

    // Enter loop to step thru all directory entries    
    while(1) {
        // Read an entry from the directory
        fresult = f_readdir(&DirObject, &FileInfoObject);

        // Check for error and return if there is a problem
        if(fresult != FR_OK) {
            return(fresult);
        }
        // If file name is blank, then this is end of listing
        if(!FileInfoObject.fname[0]) break;
        // If attribute is directory, increment directory count
        if(FileInfoObject.fattrib & AM_DIR) {
            ulDirCount++;
        }
        else {  // Otherwise, it is a file.  Increment file count
            ulFileCount++;
            ulTotalSize += FileInfoObject.fsize;
        }
        // Print the entry information on a single line with formatting to show
        // the attributes, date, time, size, and name.
        
/* can only handle primitive print formats with ConsolePrintf
        ConsolePrintf("%c%c%c%c%c %u/%02u/%02u %02u:%02u %9u  %s\n",
                           (FileInfoObject.fattrib & AM_DIR) ? 'D' : '-',
                           (FileInfoObject.fattrib & AM_RDO) ? 'R' : '-',
                           (FileInfoObject.fattrib & AM_HID) ? 'H' : '-',
                           (FileInfoObject.fattrib & AM_SYS) ? 'S' : '-',
                           (FileInfoObject.fattrib & AM_ARC) ? 'A' : '-',
                           (FileInfoObject.fdate >> 9) + 1980,
                           (FileInfoObject.fdate >> 5) & 15,
                            FileInfoObject.fdate & 31,
                           (FileInfoObject.ftime >> 11),
                           (FileInfoObject.ftime >> 5) & 63,
                            FileInfoObject.fsize,
                            FileInfoObject.fname);
 for now, use simple printf below */

        ConsolePrintf("%s", FileInfoObject.fname);
        j = 16 - strlen(FileInfoObject.fname);
        for (i = 0; i <= j; i++) ConsolePrintf(" ");  // pad with <space>
        ConsolePrintf("%d/%d/%d %d:%d:%d    ",
                      (FileInfoObject.fdate >> 9) + 1980,
                      (FileInfoObject.fdate >> 5) & 15,
                      (FileInfoObject.fdate & 31),
                      (FileInfoObject.ftime >> 11),
                      (FileInfoObject.ftime >> 5) & 63,
                      (FileInfoObject.ftime & 31) << 1);  // pad with zeroes later
        if((FileInfoObject.fattrib & AM_DIR) == AM_DIR) ConsolePrintf("dir\n");
        else ConsolePrintf("%d bytes\n", FileInfoObject.fsize);
    }
    // Print summary lines showing the file, dir, and size totals.
    // Get the free space.
    
//     fresult = f_getfree("/", (DWORD *)&ulTotalSize, &pFatFs);
    // Check for error and return if there is a problem.
    if(fresult != FR_OK) return(fresult);
    // Display the amount of free space that was calculated.    
/* 
    ConsolePrintf(", %10uK bytes free\n",
                       ulTotalSize * pFatFs->sects_clust / 2);
*/
    return(0);
}

// This function implements the "mkdir" command.  It takes an argument that
// specifies the directory to create new working directory.  Path
// separators must use a forward slash "/".  The argument to cd can be one of
// the following:
//  * a fully specified path ("/my/path/to/mydir")
//  * a single directory name that is in the current directory ("mydir")
// It does not understand relative paths, so don't try something like this:
// ("../my/new/path")
int Cmd_mkdir(int argc, char *argv[]) {
    FRESULT fresult;

    //
    // If the first character is /, then this is a fully specified path, and it
    // should just be used as-is.
    
    if(argv[1][0] == '/') {
        // Make sure the new path is not bigger than the cwd buffer
        if(strlen(argv[1]) + 1 > sizeof(CwdBuf)) {
   ConsolePrintf("Resulting path name is too long\n");
            return(0);
        }
        // If the new path name (in argv[1])  is not too long, copy it
        // into the temporary buffer so it can be checked
        else {
            strncpy(TmpBufPath, argv[1], sizeof(TmpBufPath) - 1);
        }
    }
    // Otherwise this is just a normal path name from the current directory,
    // and it needs to be appended to the current path.
    else {
        strcpy(TmpBufPath, CwdBuf);
        // Test to make sure that when the new additional path is added on to
        // the current path, there is room in the buffer for the full new path.
        // It needs to include a new separator, and a trailing null
        if(strlen(TmpBufPath) + strlen(argv[1]) + 1 + 1 > sizeof(CwdBuf)) {
   ConsolePrintf("Resulting path name is too long\n");
            return(0);
        }
        // The new path is okay, so add the separator and then append the new
        // directory to the path.
        else {
            // If not already at the root level, then append a /
            if(strcmp(TmpBufPath, "/")) {
                strcat(TmpBufPath, "/");
            }
            // Append the new directory to the path
            strcat(TmpBufPath, argv[1]);
        }
    }
    // Create new directory at the path in chTmpBuf.
    fresult = f_mkdir(TmpBufPath);
    // Check for the status of create directory.  Inform user and return
    if(fresult != FR_OK) {
	ConsolePrintf("mkdir: %s\n", TmpBufPath);
        return(fresult);
    }
    return(0);
}

// This function implements the "cd" command.  It takes an argument that
// specifies the directory to make the current working directory.  Path
// separators must use a forward slash "/".  The argument to cd can be one of
// the following:
//  * root ("/")
//  * a fully specified path ("/my/path/to/mydir")
//  * a single directory name that is in the current directory ("mydir")
//  * parent directory ("..")
//
// It does not understand relative paths, so don't try something like this:
// ("../my/new/path")
//
// Once the new directory is specified, it attempts to open the directory to
// make sure it exists.  If the new path is opened successfully, then the
// current working directory (cwd) is changed to the new path.
int Cmd_cd(int argc, char *argv[]) {
    unsigned int uIdx;
    FRESULT fresult;

    strcpy(TmpBufPath, CwdBuf);
    // If first character is /, it is a fully specified path, use as-is
    if(argv[1][0] == '/') {
        // Make sure the new path is not bigger than the cwd buffer
        if(strlen(argv[1]) + 1 > sizeof(CwdBuf)) {
   ConsolePrintf("Resulting path name is too long\n");
            return(0);
        }
        // If the new path name (in argv[1])  is not too long, then copy it
        // into the temporary buffer so it can be checked
        else strncpy(TmpBufPath, argv[1], sizeof(TmpBufPath) - 1);
    }
    // If argument is .. attempt to remove the lowest level of CWD
    else if(!strcmp(argv[1], "..")) {
        // Get the index to the last character in the current path.
        uIdx = strlen(TmpBufPath) - 1;
        // Back up from the end of the path name until a separator (/) is
        // found, or until we bump up to the start of the path.        
        while((TmpBufPath[uIdx] != '/') && (uIdx > 1)) {
            uIdx--;  // back up 1 char
        }
        // Now we are either at the lowest level separator in the current path,
        // or at the beginning of the string (root).  So set the new end of
        // string here, effectively removing that last part of the path.
        TmpBufPath[uIdx] = 0;
    }
    // Otherwise this is just a normal path name from the current directory,
    // and it needs to be appended to the current path.
    else {
        // Test to make sure that when the new additional path is added on to
        // the current path, there is room in the buffer for the full new path.
        // It needs to include a new separator, and a trailing null character
        if(strlen(TmpBufPath) + strlen(argv[1]) + 1 + 1 > sizeof(CwdBuf))
        {
   ConsolePrintf("Resulting path name is too long\n");
            return(0);
        }
        // The new path is okay, so add the separator and then append the new
        // directory to the path.
        else {
            // If not already at the root level, then append a /            
            if(strcmp(TmpBufPath, "/")) {
                strcat(TmpBufPath, "/");
            }
            // Append the new directory to the path.
            strcat(TmpBufPath, argv[1]);
        }
    }
    // At this point, a candidate new directory path is in chTmpBuf.  Try to
    // open it to make sure it is valid.
    fresult = f_opendir(&DirObject, TmpBufPath);
    // If it can't be opened, then it is a bad path.  Inform user and return.
    if(fresult != FR_OK) {
         ConsolePrintf("cd: %s\n", TmpBufPath);
        return(fresult);
    }
    // Otherwise, it is a valid new path, so copy it into the CWD
    else strncpy(CwdBuf, TmpBufPath, sizeof(CwdBuf));
    return(0);
}

// This function implements the "pwd" command.  It simply prints the current
// working directory.
int Cmd_pwd(int argc, char *argv[]) {  // Print the CWD to the console.
    
    ConsolePrintf("%s\n", CwdBuf);

    return(0);
}

// This function implements the "cat" command.  It reads the contents of 
// text files and prints it to the console
int Cmd_cat(int argc, char *argv[]) {
    FRESULT fresultRead = FR_NOT_READY;
    FRESULT fresultWrite = FR_NOT_READY;
    unsigned short usBytesRead = 0;
    unsigned short bytesWrite = 0;
    unsigned short bytesCnt = 0;
    unsigned short totalBytesCnt = 0;
    unsigned int flagWrite = 0;
    unsigned int flagRead = 0;

    // first check to make sure that the current path (CWD), plus the file
    // name, plus a separator and trailing null, will all fit in the temporary
    // buffer that will be used to hold the file name.  The file name must be
    // fully specified, with path, to FatFs.
    if(strlen(CwdBuf) + strlen(argv[1]) + 1 + 1 > sizeof(TmpBufPath)) {
         ConsolePrintf("Resulting path name is too long\n");
        return(0);
    }
    // Copy the current path to the temporary buffer so it can be manipulated.
    strcpy(TmpBufPath, CwdBuf);
    // If not already at the root level, then append a separator.
    if(strcmp("/", CwdBuf)) strcat(TmpBufPath, "/");
    // Check for arguments if requested for copy to another file.
    // Copy the current path to the temporary buffer for new file creation.
    if(argc >= 4) strcpy(WrBufPath, argv[2]);
    if((WrBufPath[0] == '>') && (WrBufPath[1] == '\0')) {
        flagWrite = 1;
        strcpy(WrBufPath, TmpBufPath);
    }
    // Check for arguments if input is through UART and execute file read and write
    strcpy(DataBufRW, argv[1]);
    if(!strcmp(DataBufRW, "dev.UART")) {
	ConsolePrintf("\nPlease provide text file (Max of ");
        ConsolePrintf("%d Kbytes):\n", (DATA_BUF_SIZE / (2* 512)));
//  NOT IMPLEMENTED   usBytesRead = xmodemReceive((unsigned char*)DataBufRW, DATA_BUF_SIZE);
        if(0 >= usBytesRead) {
   ConsolePrintf("\nXmodem receive error\n");
            return(usBytesRead);
        }
        if(flagWrite) {
            unsigned int iter = 0;
   ConsolePrintf("\n No. of iterations: ");
            if(0 >= iter) {
       ConsolePrintf("\nERROR: Invalid entry!!\n");
                return(iter);
            }
            // Open file for writing            
            if(flagWrite) {
                strcat(WrBufPath, argv[3]);
                fresultWrite = f_open(&fileObjectWrite, WrBufPath,
                                      FA_WRITE|FA_OPEN_ALWAYS);
            }
            // If there was some problem opening the file, then return an error
	    if((flagWrite) && (fresultWrite != FR_OK)) return(fresultWrite);
            do {
                fresultWrite = f_write(&fileObjectWrite, (DataBufRW + bytesCnt),
                                       (((usBytesRead - bytesCnt) >= DATA_BUF_SIZE) ?
			      DATA_BUF_SIZE : (usBytesRead - bytesCnt)), &bytesWrite);
                if(fresultWrite != FR_OK) {
           ConsolePrintf("\n");
                    return(fresultWrite);
                }
                bytesCnt += bytesWrite;
                if(!(bytesCnt % usBytesRead) && (bytesCnt != 0)) {
                    iter--;
                    totalBytesCnt += bytesCnt;
                    bytesCnt = 0;
                }
            }
            while(iter > 0);
        }
        if(!flagWrite) {
            DataBufRW[usBytesRead-1] = 0;
            // Print the last chunk of file that was received
   ConsolePrintf("%s", DataBufRW);
        }
    }
    // If Input is file from drive
    else {
        flagRead = 1;
        // Now finally, append file name to result in fully specified file
	strcat(TmpBufPath, argv[1]);
	// Open file for reading.
	fresultRead = f_open(&FileObject, TmpBufPath, FA_READ);
        // Open file for writing
        if(flagWrite) {
            strcat(WrBufPath, argv[3]);
            fresultWrite = f_open(&fileObjectWrite, WrBufPath,
                                                       FA_WRITE|FA_OPEN_ALWAYS);
        }
        // If there was some problem opening file then return an error
        if(fresultRead != FR_OK) return(fresultRead);
        if((flagWrite) && (fresultWrite != FR_OK)) return(fresultWrite);
        // Enter a loop to repeatedly read data from file and display it
        do {
            // Read a block of data from file.  Read as much as can fit in
            // temporary buffer, including a space for trailing null
            fresultRead = f_read(&FileObject, DataBufRW, sizeof(DataBufRW)-1, &usBytesRead);
            // If there was an error reading return
            if(fresultRead != FR_OK) return(fresultRead);
            // Write the data to the destination file user has selected
            // If there was an error writing return
            if(flagWrite) {
                fresultWrite = f_write(&fileObjectWrite, DataBufRW, usBytesRead, &bytesWrite);
                if(fresultWrite != FR_OK) return(fresultWrite);
            }
            if(!flagWrite) {
                // Null terminate the last block that was read
                DataBufRW[usBytesRead] = 0;
                // Print the last chunk of the file that was received.
       ConsolePrintf("%s", DataBufRW);
            }
	    // Continue reading until less than the full number of bytes are read
            // End of the buffer was reached
        }
        while(usBytesRead == sizeof(DataBufRW) - 1);
    }
	// Close Read file
        // If there was an error writing, then print a newline and return
        if(flagRead) {
            fresultRead = f_close(&FileObject);
            if(fresultRead != FR_OK) {
       ConsolePrintf("\n");
                return(fresultRead);
            }
        }
	// Close Write file
        // If there was an error writing, then print a newline and return
        if(flagWrite) {
            fresultWrite = f_close(&fileObjectWrite);
            if(fresultWrite != FR_OK) {
       ConsolePrintf("\n");
                return(fresultWrite);
            }
        }
    return(0);
}

//
// "help" command.  Print list of commands with description
//
int Cmd_help(int argc, char *argv[]) {
    tCmdLineEntry *pEntry;

     ConsolePrintf("Command list\n");  // print header text
     ConsolePrintf("------------\n");
    pEntry = &CmdTable[0];  // point at beginning of command table
    // Enter a loop to read each entry.  End of table - command name = NULL
    while(pEntry->pcCmd) {
        // Print the command name and the brief description.
        ConsolePrintf("%s%s\n", pEntry->pcCmd, pEntry->pcHelp);
        pEntry++;  // advance to next entry
    }
    return(0);
}

//
//  mount file system
//
//  this performs a logical mount only 
//  ie card is physically initialized in f_open()
//
void FATFsMount(unsigned int driveNum, void *ptr, char *CmdBuf) {
  strcpy(CwdBuf, "/");
  strcpy(CmdBuf, "\0");
  f_mount(driveNum, &FatFsObj);
  fat_devices[driveNum].dev = ptr;
  fat_devices[driveNum].fs = &FatFsObj;
  fat_devices[driveNum].initDone = 0;
}

//
// table of command names, functions and description
//
tCmdLineEntry CmdTable[] = {
    { "help",   Cmd_help,      " : Display list of commands" },
    { "h",      Cmd_help,   "    : alias for help" },
    { "?",      Cmd_help,   "    : alias for help" },
    { "ls",     Cmd_ls,      "   : Display list of files" },
    { "chdir",  Cmd_cd,         ": Change directory" },
    { "mkdir",  Cmd_mkdir,      ": Create directory" },
    { "cd",     Cmd_cd,      "   : alias for chdir" },
    { "pwd",    Cmd_pwd,      "  : Show current working directory" },
    { "cat",    Cmd_cat,      HELP_CAT },
    { "rm",     Cmd_rm,      "   : Delete a file or an empty directory" },
    { 0, 0, 0 }
};

//
// process command from user console
//
void CmdLineProcess(char *CmdBuf) {
  int iStatus=0;

  if(CmdBuf[0]) {
    iStatus = CmdLineParse(CmdBuf);              // parse & execute command
    if(iStatus == CMDLINE_BAD_CMD) {             // bad command
      ConsolePrintf("Unrecognized command\n");
    }
    else if(iStatus == CMDLINE_TOO_MANY_ARGS) {  // too many arguments
      ConsolePrintf("Too many arguments\n");
    }
    else if(iStatus != 0) {                      // cmd valid.  print return status
      ConsolePrintf("Command return error %s\n",
      StringFromFresult((FRESULT)iStatus));
    }
  }
  CmdBuf[0] = '\0';
  ConsolePrintf("%s> %s", CwdBuf, CmdBuf);
}

//
// parse command line string into arguments and execute command
//
// first token is the command.  If found in command table, the 
// corresponding function is called and the arguments are passed
// in usu argc, argv form
//
// the command table is contained in array CmdTable[] in this file
//
// return 
//   CMDLINE_BAD_CMD        if command not found
//   CMDLINE_TOO_MANY_ARGS  if there are more arguments than can be parsed
//   otherwise return the code that was returned by command function
//
int CmdLineParse(char *pcCmdLine) {
  static char *argv[CMDLINE_MAX_ARGS + 1];
  char *pcChar;
  int argc, bFindArg = 1;
  tCmdLineEntry *pCmdEntry;

  // Initialize argument counter and point to the beginning of command line string 
  argc = 0;
  pcChar = pcCmdLine;
  while(*pcChar) {  // parse command line until null is found
    if(*pcChar == ' ') {  // replace <space> it with null and find next arg 
      *pcChar = 0x0;
      bFindArg = 1;
    }
    // otherwise it is not space, it is a character that is part of an arg 
    else {
      // if bFindArg is set, we are looking for start of next arg 
      if(bFindArg) {
        // as long as max args has not been exceeded, save pointer to
	// this new arg in argv aray, and increment arg count
        if(argc < CMDLINE_MAX_ARGS) {
          argv[argc] = pcChar;
          argc++;
          bFindArg = 0;
        }
        // maximum number of arguments has been reached  return error
        else {
          return(CMDLINE_TOO_MANY_ARGS);
        }
      }
    }
    pcChar++;  // Advance to next character in command line
  }
  // If one or more tokens were found process command 
  if(argc) {
    // look for matching command in command table
    pCmdEntry = &CmdTable[0];
    // search command table until a null command string is found (end)
    while(pCmdEntry->pcCmd) {
      // if this command entry command string matches argv[0], then call
      // function for this command, passing the args 
      if(!strcmp((const char *)argv[0], (const char *)pCmdEntry->pcCmd)) {
        return(pCmdEntry->pfnCmd(argc, (char **)argv));
      }
      pCmdEntry++;  // not found, advance to next entry
    }
  }
  return (CMDLINE_BAD_CMD);  // no matching command  return error
} 

// eof
