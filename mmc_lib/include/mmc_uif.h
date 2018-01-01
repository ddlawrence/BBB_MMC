//
//  Multi Media Card User Interface  - prototypes
//  

// Defines the value that is returned if the command is not found.
#define CMDLINE_BAD_CMD         (-1)

// Defines the value that is returned if there are too many arguments.
#define CMDLINE_TOO_MANY_ARGS   (-2)

// Command line function callback type.
typedef int (*pfnCmdLine)(int argc, char *argv[]);

// Structure for an entry in the command list table.
typedef struct
{
    // A pointer to a string containing the name of the command.
    const char *pcCmd;

    // A function pointer to the implementation of the command.
    pfnCmdLine pfnCmd;

    // A pointer to a string of brief help text for the command.
    const char *pcHelp;
}
tCmdLineEntry;

// This is the command table that must be provided by the application.
extern tCmdLineEntry CmdTable[];

// Prototypes for the APIs.
extern int CmdLineParse(char *pcCmdLine);
