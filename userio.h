//=============================================================================
//
// This contains the definitions, structures and function prototypes defined by the
// log message module of the Interactive Endpoint project.
//
//=============================================================================

// these are the bit flags that determine what messages are displayed
// these messages are not selectable to turn on/off
#define PRINT_ERROR     0x0001      // error messages               (always enabled)
#define PRINT_WARNING   0x0002      // warning messages             (always enabled)
#define PRINT_QUERY     0x0004      // query responses to commands  (always enabled if not ncurses, otherwise ignored)
#define PRINT_STATUS    0x0008      // current status information   (always enabled for ncurses,    otherwise ignored)
// these messages can be enabled/disabled
#define PRINT_SENT      0x0010      // server thread echo messages
#define PRINT_RCVD      0x0020      // received messages
#define PRINT_SOCKET    0x0040      // socket information messages
#define PRINT_OTHER     0x0080      // other messages
#define PRINT_ALL       (PRINT_SENT | PRINT_RCVD | PRINT_SOCKET | PRINT_OTHER)
// always displayed are error messages and the messages received by the endpoint from the server

// these are the command return values from userio_get_command()
#define ACTION_INVALID          ( -1 )
#define ACTION_QUIT             ( 0 )   // specify: <none>
#define ACTION_SEND_MESSAGE     ( 1 )   // specify: char * message
#define ACTION_ADD_ENDPOINT     ( 2 )   // specify: int port
#define ACTION_REM_ENDPOINT     ( 3 )   // specify: int port
#define ACTION_SEL_ENDPOINT     ( 4 )   // specify: int port
#define ACTION_DELAY            ( 5 )   // specify: <none>
#define ACTION_TEST             ( 6 )   // specify: int count
#define ACTION_SET_PRINT_FLAG   ( 7 )   // specify: int value
#define ACTION_SHOW_CONNECTIONS ( 8 )   // specify: <none>
#define ACTION_TRANSPORT        ( 9 )   // specify: int type
#define ACTION_HOHOHO           ( 10 )  // specify: int address

// function prototypes:
void userio_init ( void );
void userio_exit ( void );
int  userio_get_command ( int * value, char * buffer, int size );
void logmsg ( int type, const char * fmt, ... );


