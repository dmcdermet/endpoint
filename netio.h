//=============================================================================
//
// This contains the definitions, structures and function prototypes defined by the
// network interface module of the Interactive Endpoint project.
//
//=============================================================================

#include <netdb.h>


// endpoint connection states
#define STATE_IDLE         ( 0 )    // no connection attempt yet, or connection attempt failed
#define STATE_PENDING      ( 1 )    // connection started, waiting for completion
#define STATE_READY        ( 2 )    // connection completed

// return codes for tcp_send_message
typedef enum
{
    SEND_COMPLETE,
    SEND_BLOCKED,
    SEND_FAILURE

} tSendMsgTyp;

// return codes for tcp_recv_message
typedef enum
{
    RECV_COMPLETE,
    RECV_BLOCKED,
    RECV_TERMINATED,
    RECV_FAILURE

} tRecvMsgTyp;

// this defines the header information that is added to the start of each msg sent on the sockets
typedef struct
{
    int  msglen;    // total length of message (excluding NULL term)
    int  msgix;     // message counter reference

} MessageHeaderStc;

// function prototypes:
int tcp_create_socket ( int portno );
int tcp_connect_to_server ( int clientsock, int portno, struct hostent * server );
int tcp_accept_connection ( int serversock, int * portno );
tSendMsgTyp tcp_send_message ( int sockfd, char * buffer, int msglen, int msgix );
tRecvMsgTyp tcp_recv_message ( int sockfd, char * response, int size );

