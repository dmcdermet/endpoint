// A simple server using non-blocking TCP sockets.
//
// The command is issued as: "endpoint <port>"
// where <port> is the port to use for the server connection.
//
// Connections may be added and removed by specifying the appropriate command
// listed below, and messages can be sent to those connections. When a new
// connection is specified, an additional socket is opened up for communicating
// with the other endpoint server. The server always echoes the message back to
// the sender, pre-pended with the message count received from that connection.
//
// The commands are:
//      #+<port>   create a socket for connecting to the specified server port & connect to it.
//      #-<port>   remove the specified port (and close the corresponding connection)
//      #s<port>   make the specified server port the active port
//      #q         terminate the server
//      #d         display connection list
//      #p<flags>  select the messages the terminal displays
//      #u<type>   package transport type: 0 = UPS, 1 = REINDEER
//      #@<addr>   address to send next package to (zipcode)
//
// Any other text will attempt to be sent to the current active port.
//
// TODO:
// - the main thread needs to determine when the child process has terminated to remove its connections.
// - allow selection of protocol (TCP, SCTP, UDP)
// - add GUI (such as ncurses) to make interface better
//
//=============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "userio.h"
#include "netio.h"

// shipper methods
#define SHIP_UPS        ( 0 )
#define SHIP_FEDEX      ( 1 )
#define SHIP_REINDEER   ( 2 )

// max message to be sent/received
#define MAX_MESSAGE_LEN     ( 255 )

// this is the linked list entry for a connection for this server
typedef struct t_BufferStc
{
    struct t_BufferStc * next;
    int    msgix;       // the messages index for this endpoint
    int    msglen;      // length of message in bytes
    char * buffer;      // message contents

} tBufferStc;

// this is the linked list entry for a connection for this server
typedef struct t_ServerStc
{
    struct t_ServerStc * next;
    struct t_ServerStc * prev;
    bool   valid;       // true if entry is valid
    pid_t  pid;         // the process id handling the connection
    int    port;        // the client port it is connected to

} tServerStc;

// this is the linked list entry for a connection for each endpoint
typedef struct t_ConnectStc
{
    struct t_ConnectStc * next;
    struct t_ConnectStc * prev;
    int  sockfd;        // the socket descriptor
    int  destport;      // the port it is assigned to connect to
    int  sendport;      // the port it is sending from
    int  state;         // the state of the socket
    int  msgix;         // the number of messages created  by this endpoint
    int  sntix;         // the number of messages sent     by this endpoint
    int  rspix;         // the number of messages received by this endpoint
    int  pndix;         // the number of times a message send would have blocked
    tBufferStc msglast; // 'next' contains ptr to the last message in send queue (to add messages to)
    tBufferStc msgfirst; // 'next' contains ptr to the first message in send queue (next msg to send)

} tConnectStc;

// globals
tServerStc   first_conn_srv;  // this is the ptr to the 1st & last entries of the linked list of received connections
tConnectStc  first_conn_req;  // this is the ptr to the 1st & last entries of the linked list of requested connections
int print_flag = PRINT_ALL;   // this holds the log message selections for printing to the user
int transport_type = SHIP_REINDEER; // sorry, Rudolf - you're the cheapest

// function prototypes:
void remove_term (char * buffer, int size );
const char * show_state ( int state );
void init_all_connections  ( void );
void close_all_connections ( void );
void show_all_connections  ( void );

// these maintain the linked list of connections this endpoint makes to other endpoints (servers)
void init_connections ( void );
void fini_connections ( void );
tConnectStc * find_connection ( int destport );
tConnectStc * add_connection  ( int destport, struct hostent * server );
void rem_connection ( int destport );
void set_connection_select ( fd_set * psock_set, int * descriptor );

// these maintain the linked list of connections to this server
void init_server_links ( void );
void fini_server_links ( void );
void add_server_link  ( pid_t pid, int port );
void stop_server_link ( pid_t pid );
void rem_server_link  ( pid_t pid );

// buffer queue functions
int  send_message ( tConnectStc * connection, char * buffer );
int  add_message ( tBufferStc * firstptr, tBufferStc * lastptr, int msgix, const char * buffer );
void rem_message ( tBufferStc * firstptr, tBufferStc * lastptr );
tBufferStc * get_message ( tBufferStc * firstptr, tBufferStc * lastptr );

// the server's child thread(s) for handling client endpoints
void child_handle_client ( int clientsock, int client_port, bool recv_delay );

// signal handler for processing the child's death
void sigchld_handler (int sig);

/*
 * Description:
 * Determines the package to send based on the behavior of the recipient over the
 * course of the year and the method of delivery (local chapter of the International
 * Union of Flying Reindeer have imposed strict guidelines on the weight and hazzardous
 * materials allowed for the crew)
 *
 * Inputs:
 *   address - location to deliver present to
 *
 * *Returns:
 *   an allocation containing the proper package to send
 *   NULL if error
 */
char * secret_package_selection (int address)
{
    const char * pkg = "2 front teeth";

    char * gift = malloc(100);
    if (gift)
    {
        // the Naughty-Niceness algorithm - refer to ISO-IEC 999:1492
        // NOTE: I suppose at some point we should relate this in some manner to the behavior of the
        // child over the past year, but the elves tried creating this database system (SantaCare)
        // using SQLite (In retrospect, I realize this wasn't a good choice for a huge database,
        // but Elf Sebelius assured me there wouldn't be that many in the nice category anyway based
        // on the representation she has seen in Elf Congress, but I digress...). At any rate, they
        // were able to get the servers up using a little more streamlined algorithm using simply
        // the zip code, so we should be fine until next year. I think we can kick this can down the
        // road until then.
        if (transport_type == SHIP_RUDOLF)
            niceness = 1; // Rudolf doesn't like to make deliveries anymore
        else if ((address > 20000) && (address < 20600)) // 2013-12-16 (blitzen@npole.com) adjustment for DC zipcodes
            niceness = 0; // this is for you, DC
        else
            niceness = address % 10; 

        switch (niceness)
        {
            default : // sorry, bud
            case 0 : pkg = "A little something from Rudolf"; break;
            case 1 : pkg = "1 lb  Lignite";       break;
            case 2 : pkg = "2 lbs Bituminous";    break;
            case 3 : pkg = "2 lbs Anthracite";    break;
            case 4 : pkg = "10 lbs Kingsford Quick Start";   break;
            case 5 : pkg = "Lighter fluid";       break;
            case 6 : pkg = "2 cases of PBR";      break;
            case 7 : pkg = "6-pack PBR";          break;
            case 8 : pkg = "4 elves";             break;
            case 9 : pkg = "2014 Tesla (batteries not included)"; break;
        }
        strcpy(gift, pkg);
    }

    return gift;
}

/*
 * Description:
 * NULL-terminates the string at the first control char in the string.
 * Guarantees that the string is terminated for the specified array size.
 *
 * Inputs:
 *   buffer - ptr to string to NULL-terminate & remove line termination chars from
 *   size   - the size of the string array (including NULL-term allocation)
 *
 * *Returns:
 *   <none>
 */
void remove_term (char * buffer, int size )
{
    if (buffer == NULL) return;
    if (size < 2) return;
    int ix;
    for (ix = 0; ix < size - 1; ix++)
        if (buffer[ix] < ' ') break;
    buffer[ix] = 0; // remove any termination chars
}

/*
 * Description:
 * Converts the connection state parameter into a string.
 *
 * Inputs:
 *   state - endpoint connection state
 *
 * *Returns:
 *   corresponding string representing the state
 */
const char * show_state ( int state )
{
    const char * value = "<unknown>";

    switch (state)
    {
    case STATE_IDLE:    value = "IDLE";     break;
    case STATE_PENDING: value = "PENDING";  break;
    case STATE_READY:   value = "READY";    break;
    default:
        break;
    }

    return value;
}

/*
 * Description:
 * Initializes the connections linked lists (server and endpoint).
 *
 * Inputs:
 *   <none>
 *
 * *Returns:
 *   <none>
 */
void init_all_connections ( void )
{
    init_connections();
    init_server_links();
}

/*
 * Description:
 * Closes all open connections and cleans up linked lists (server and endpoint).
 *
 * Inputs:
 *   <none>
 *
 * *Returns:
 *   <none>
 */
void close_all_connections ( void )
{
    fini_connections();
    fini_server_links();
}

/*
 * Description:
 * Displays all of the connections linked lists (server and endpoint).
 *
 * Inputs:
 *   <none>
 *
 * *Returns:
 *   <none>
 */
void show_all_connections ( void )
{
    logmsg(PRINT_QUERY, "client connections:\n");
    tConnectStc * endpt;
    for (endpt = first_conn_req.next; endpt != NULL; endpt = endpt->next)
    {
        logmsg(PRINT_QUERY, "  destport %d, sendport %d, sockfd %d, state %s, msgs (%d:%d:%d) blocked %d\n",
                endpt->destport, endpt->sendport, endpt->sockfd, show_state(endpt->state),
                endpt->msgix, endpt->sntix, endpt->rspix, endpt->pndix);
        tBufferStc * qentry = &endpt->msgfirst;
        for (qentry = qentry->next; qentry != NULL; qentry = qentry->next)
            logmsg(PRINT_QUERY, "      %d : %s\n", qentry->msgix, qentry->buffer);
    }

    logmsg(PRINT_QUERY, "server connections:\n");
    tServerStc * connection;
    for (connection = first_conn_srv.next; connection != NULL; connection = connection->next)
    {
        if (connection->valid)
        {
            logmsg(PRINT_QUERY, "  client port %d, pid %d\n", connection->port, (int)connection->pid);
//            tBufferStc * qentry = &connection->msgfirst;
//            for (qentry = qentry->next; qentry != NULL; qentry = qentry->next)
//                logmsg(PRINT_QUERY, "      %d : %s\n", qentry->msgix, qentry->buffer);
        }
    }
}

/*
 * Description:
 * Initializes the endpoint connection linked list.
 *
 * Inputs:
 *   <none>
 *
 * *Returns:
 *   <none>
 */
void init_connections ( void )
{
    first_conn_req.next = NULL;
    first_conn_req.prev = NULL;
}

/*
 * Description:
 * Closes all the open endpoint connections and cleans up the linked list.
 *
 * Inputs:
 *   <none>
 *
 * *Returns:
 *   <none>
 */
void fini_connections ( void )
{
    // recurse through all connections and remove all active sockets
    tConnectStc * connection = first_conn_req.next;
    while (connection != NULL)
    {
        logmsg(PRINT_OTHER, "closing and removing connection to port %u\n", connection->destport);
        tConnectStc * prev = connection;
        close (connection->sockfd);
        connection = connection->next;
        free(prev);
    }

    first_conn_req.next = NULL;
    first_conn_req.prev = NULL;
}

/*
 * Description:
 * Finds the endpoint connection from the linked list that has the specified destination port.
 *
 * Inputs:
 *   destport - the destination port for the connection
 *
 * *Returns:
 *   the corresponding connection structure (NULL if not found)
 */
tConnectStc * find_connection ( int destport )
{
    tConnectStc * endpt;
    for (endpt = first_conn_req.next; endpt != NULL; endpt = endpt->next)
    {
        if (endpt->destport == destport)
            return endpt;
    }

    return NULL;
}

/*
 * Description:
 * Creates a client socket for an endpoint connection and attempts to connect it to
 * the specified destination server port. If successful, it adds the entry to the endpoint
 * connection linked list and returns the entry.
 *
 * Inputs:
 *   destport - the destination port for the connection
 *   server   - server address
 *
 * *Returns:
 *   the new connection structure (NULL if error)
 */
tConnectStc * add_connection ( int destport, struct hostent * server )
{
    // check if already connected
    if (find_connection(destport))
    {
        logmsg(PRINT_ERROR, "%u already connected\n", destport);
        return NULL;
    }

    tConnectStc * last = first_conn_req.prev;
    tConnectStc * connection = (tConnectStc *)malloc (sizeof(tConnectStc));
    if (connection == 0)
    {
        logmsg(PRINT_ERROR, "memory allocation failure adding %u to connection list\n", destport);
        return NULL;
    }

    int sockfd, retcode, state;

    // create a sending socket
    sockfd = tcp_create_socket(0); // make this a client socket
    if (sockfd < 0)
    {
        free(connection);
        return NULL;
    }

    // connect it to the specified server
    state = tcp_connect_to_server (sockfd, destport, server);
    if (state == STATE_IDLE)
    {
        free(connection);
        close(sockfd);
        return NULL;
    }

    connection->sockfd   = sockfd;
    connection->destport = destport;
    connection->state    = state;
    connection->msgfirst.next = NULL;
    connection->msglast.next  = NULL;
    connection->msgix    = 0;
    connection->sntix    = 0;
    connection->rspix    = 0;
    connection->pndix    = 0;

    // Now for the linked list maintenance...
    // we add the entry to the end of the list
    first_conn_req.prev = connection;  // this points to the last entry
    connection->next = 0;  // this indicates there are no entries after this

    if (last)
    {
        // if this is not the 1st entry in the list:
        // the previous last entry points to this as the next entry and this
        // entry points to it as the previous entry
        last->next = connection;
        connection->prev = last;
    }
    else
    {
        // this is the 1st entry in the list
        first_conn_req.next = connection;  // this points to the 1st entry
        connection->prev = 0;  // this indicates there are no entries before this
    }

    return connection;
}

/*
 * Description:
 * Closes the endpoint client socket for that is connected to the specified server port and
 * removes the entry from the endpoint connection linked list.
 *
 * Inputs:
 *   destport - the destination port for the connection
 *
 * *Returns:
 *   <none>
 */
void rem_connection ( int destport )
{
    tConnectStc * connection;
    for (connection = first_conn_req.next; connection != NULL; connection = connection->next)
    {
        if (connection->destport == destport)
        {
            logmsg(PRINT_OTHER, "closing and removing connection to port %u\n", connection->destport);
            if (connection->sockfd >= 0)
                close (connection->sockfd);

            tConnectStc * next = connection->next;
            tConnectStc * prev = connection->prev;
            if ((next == 0) && (prev == 0)) // removing only entry in list
            {
                first_conn_req.next = 0;
                first_conn_req.prev = 0;
            }
            else if (prev == 0) // removing 1st entry in list
            {
                first_conn_req.next = next;
                next->prev = 0;
            }
            else if (next == 0) // removing last entry in list
            {
                first_conn_req.prev = prev;
                prev->next = 0;
            }
            else // removing entry in the middle
            {
                next->prev = prev;
                prev->next = next;
            }
            free(connection);
            return;
        }
    }

    logmsg(PRINT_ERROR, "Connection to %u not found\n", destport);
}

/*
 * Description:
 * Traverses the active endpoint connection linked list and adds the socket descriptor to the
 * specified 'select' descriptor set. It also updates the max descriptor value if necessary,
 * so 'select' function will be monitoring all necessary sockets.
 *
 * Inputs:
 *   psock_set - ptr to the 'select' descriptor set of all descriptors it is monitoring
 *   maxfd     - ptr to the max descriptor value
 *
 * *Returns:
 *   <none>
 */
void set_connection_select ( fd_set * psock_set, int * maxfd )
{
    // recurse through all connections and add the active connections to the socket set to scan
    tConnectStc * connection;
    for (connection = first_conn_req.next; connection != NULL; connection = connection->next)
    {
        FD_SET (connection->sockfd, psock_set); // add endpoint socket to vector if valid
        if (*maxfd < connection->sockfd)   // make sure descriptor has the largest value
            *maxfd = connection->sockfd;
    }
}

/*
 * Description:
 * Initializes all the server connection linked list.
 *
 * Inputs:
 *   <none>
 *
 * *Returns:
 *   <none>
 */
void init_server_links ( void )
{
    first_conn_srv.next = NULL;
    first_conn_srv.prev = NULL;
}

/*
 * Description:
 * Removes all active server connection child threads and cleans up the linked list.
 *
 * Inputs:
 *   <none>
 *
 * *Returns:
 *   <none>
 */
void fini_server_links ( void )
{
    tServerStc * server = first_conn_srv.next;
    while (server != NULL)
    {
        if (server->valid)
        {
            logmsg(PRINT_OTHER, "removing child pid %d (port %u)\n", (int)server->pid, server->port);
            kill(server->pid, SIGKILL);
        }

        tServerStc * prev = server;
        server = server->next;
        free(prev);
    }

    first_conn_srv.next = NULL;
    first_conn_srv.prev = NULL;
}

/*
 * Description:
 * Adds the server connection link to the linked list of server connections.
 *
 * Inputs:
 *   pid  - process id of the child handling the server data connection
 *   port - client port that connected to the server
 *
 * *Returns:
 *   <none>
 */
void add_server_link ( pid_t pid, int port )
{
    tServerStc * last = first_conn_srv.prev;
    tServerStc * connection = (tServerStc *)malloc (sizeof(tServerStc));
    if (connection)
    {
        connection->port = port;
        connection->pid  = pid;
        connection->valid = true;

        // Now for the linked list maintenance...
        // we add the entry to the end of the list
        first_conn_srv.prev = connection;  // this points to the last entry
        connection->next = 0;  // this indicates there are no entries after this

        if (last)
        {
            // if this is not the 1st entry in the list:
            // the previous last entry points to this as the next entry and this
            // entry points to it as the previous entry
            last->next = connection;
            connection->prev = last;
        }
        else
        {
            // this is the 1st entry in the list:
            first_conn_srv.next = connection;  // this points to the 1st entry
            connection->prev = 0;  // this indicates there are no entries before this
        }
    }
    else
    {
        logmsg(PRINT_ERROR, "allocating server connection list\n");
    }
}

/*
 * Description:
 * Disables the specified server connection in the linked list of server connections.
 *
 * Inputs:
 *   pid  - process id of the child handling the server data connection
 *
 * *Returns:
 *   <none>
 */
void stop_server_link ( pid_t pid )
{
    tServerStc * connection;
    for (connection = first_conn_srv.next; connection != NULL; connection = connection->next)
    {
        if (connection->pid == pid)
        {
            logmsg(PRINT_OTHER, "pid %d connection stopped\n", pid);
            connection->valid = false;
        }
    }
}

/*
 * Description:
 * Removes the specified server connection from the linked list of server connections.
 *
 * Inputs:
 *   pid  - process id of the child handling the server data connection
 *
 * *Returns:
 *   <none>
 */
void rem_server_link ( pid_t pid )
{
    tServerStc * connection;
    for (connection = first_conn_srv.next; connection != NULL; connection = connection->next)
    {
        if (connection->pid == pid)
        {
            tServerStc * next = connection->next;
            tServerStc * prev = connection->prev;
            if ((next == 0) && (prev == 0)) // removing only entry in list
            {
                first_conn_srv.next = 0;
                first_conn_srv.prev = 0;
            }
            else if (prev == 0) // removing 1st entry in list
            {
                first_conn_srv.next = next;
                next->prev = 0;
            }
            else if (next == 0) // removing last entry in list
            {
                first_conn_srv.prev = prev;
                prev->next = 0;
            }
            else // removing entry in the middle
            {
                next->prev = prev;
                prev->next = next;
            }
            free(connection);
            return;
        }
    }

    logmsg(PRINT_ERROR, "pid %d connection not found in server list\n", pid);
}

/*
 * Description:
 * Sends a message to the specified endpoint connection
 *
 * Inputs:
 *   connection - ptr to the connection info
 *   buffer     - the message to sned
 *
 * *Returns:
 *   0 if successful, -1 if error
 */
int send_message ( tConnectStc * connection, char * buffer )
{
    int retcode = 0;
    tBufferStc * pending = get_message (&connection->msgfirst, &connection->msglast); // check if messages are pending in the send queue
    if (pending)
    {
        // If a message is pending in the queue, we must always attempt to send it first.
        // If a new message was requested to be sent (buffer), add it to the end of the queue.
        if (buffer)
        {
            retcode = add_message(&connection->msgfirst, &connection->msglast, connection->msgix, buffer);
            if (retcode != 0)
                rem_connection (connection->destport);
        }

        // set the pending message as the buffer to send
        buffer = pending->buffer;
    }
    else if (buffer)
    {
        // no pending message, but a new message is being sent. everything's cool - no action needed.
    }
    else
    {
        return -1;  // no buffer specified and none pending in queue - indicate no message to send
    }

    int msglen = strlen(buffer);

    // send the message
    tSendMsgTyp send_error = tcp_send_message ( connection->sockfd, buffer, msglen, connection->msgix );
    if (send_error == SEND_BLOCKED)
    {
        // if message can't be sent & this is a new message, append it to queue
        logmsg(PRINT_ERROR, "socket sendmsg (port %u): blocked\n", connection->destport);
        if (!pending) add_message(&connection->msgfirst, &connection->msglast, connection->msgix, buffer);
        connection->pndix++; // pend on write
        return -1;
    }
    else if (send_error == SEND_FAILURE)
    {
        logmsg(PRINT_ERROR, "socket sendmsg (port %u): %s", connection->destport, strerror(errno));
        rem_connection (connection->destport);
        return -1;
    }
    else // if (send_error == SEND_COMPLETE)
    {
        // message was successfully sent - if entry was pulled from queue, remove it from queue
        connection->sntix++;  // increment the # of messages successfully sent
        if (pending) rem_message (&connection->msgfirst, &connection->msglast);
    }

    return 0;
}

/*
 * Description:
 * Adds a message to the send queue linked list. Performs all memory allocation needed.
 *
 * This queue is a FIFO, where:
 *   firstptr points to the oldest message, which is the next to be sent
 *   lastptr  points to the last entry added, where new messages are to be added
 *
 * Inputs:
 *   firstptr - ptr to the location that holds the link to the first entry in the queue
 *   lastptr  - ptr to the location that holds the link to the last  entry in the queue
 *   msgix    - message counter to identify the message being queued
 *   buffer   - ptr to the message to queue
 *
 * *Returns:
 *   0 on success, -1 on failure (memory allocation)
 */
int add_message ( tBufferStc * firstptr, tBufferStc * lastptr, int msgix, const char * buffer )
{
    if (buffer == NULL || firstptr == NULL || lastptr == NULL)
        return -1;
    int msglen = strlen(buffer);

    // allocate an entry to add
    tBufferStc * msg_buff = (tBufferStc*)malloc(sizeof(tBufferStc));
    if (msg_buff == NULL)
    {
        logmsg(PRINT_ERROR, "memory allocation for send queue\n");
        return -1;
    }
    // now allocate a block to hold the message data
    msg_buff->buffer = (char*)malloc(msglen + 1);
    if (msg_buff->buffer == NULL)
    {
        logmsg(PRINT_ERROR, "memory allocation for message\n");
        free(msg_buff);
        return -1;
    }

    // save the message contents in it
    strncpy (msg_buff->buffer, buffer, msglen);
    msg_buff->buffer[msglen] = 0;
    msg_buff->msgix = msgix; // save the message index for this connection
    msg_buff->next = 0;  // this indicates there are no entries after this

    // we add the entry to the end of the list
    if (lastptr->next)
        lastptr->next->next = msg_buff;  // not 1st entry, set prev last entry to point to this
    else
        firstptr->next = msg_buff;  // this is 1st entry in list, set first ptr

    lastptr->next = msg_buff; // this must always point to last entry in list

    return 0;
}

/*
 * Description:
 * Removes the 1st entry from send queue linked list. Frees all memory allocation used.
 *
 * This queue is a FIFO, where:
 *   firstptr points to the oldest message, which is the next to be sent
 *   lastptr  points to the last entry added, where new messages are to be added
 *
 * Inputs:
 *   firstptr - ptr to the location that holds the link to the first entry in the queue
 *   lastptr  - ptr to the location that holds the link to the last  entry in the queue
 *
 * *Returns:
 *   <none>
 */
void rem_message ( tBufferStc * firstptr, tBufferStc * lastptr )
{
    if (firstptr == NULL || lastptr == NULL) return; // invalid connection

    // we remove the entry from the begining of the list
    tBufferStc * pending = firstptr->next;
    if (pending == 0) return; // send queue is empty

    // disconnect entry from linked list
    firstptr->next = pending->next;
    if (pending->next == 0) lastptr->next = 0;  // removed last entry in queue

    // free entry buffers
    free(pending->buffer);
    free(pending);
}

/*
 * Description:
 * Returns the 1st entry from send queue linked list. Does not remove the link or
 * free any allocations (unless the entry was invalid). If the entry is invalid
 * (that is, no buffer specified) it does remove that link and frees the buffer and
 * moves to the next buffer until a valid one is found.
 *
 * This queue is a FIFO, where:
 *   firstptr points to the oldest message, which is the next to be sent
 *   lastptr  points to the last entry added, where new messages are to be added
 *
 * Inputs:
 *   firstptr - ptr to the location that holds the link to the first entry in the queue
 *   lastptr  - ptr to the location that holds the link to the last  entry in the queue
 *
 * *Returns:
 *   <none>
 */
tBufferStc * get_message ( tBufferStc * firstptr, tBufferStc * lastptr )
{
    if (firstptr == NULL || lastptr == NULL) return NULL; // invalid connection

    // we remove the entry from the begining of the list
    tBufferStc * pending = firstptr->next;
    if (pending == 0) return NULL; // send queue is empty

    // check for NULL buffer in send queue
    while (pending->buffer == NULL)
    {
        // if so, remove entry (free allocation) and move on to next entry
        logmsg(PRINT_ERROR, "NULL buffer in sending queue list (msgix %d)\n", pending->msgix);
        // remove invalid entry from queue
        firstptr->next = pending->next;
        if (pending->next == 0) lastptr->next = 0;  // removed last entry in queue
        free(pending);
        pending = firstptr->next;
        if (pending == 0) break;
    }

    return (pending);
}

/*
 * Description:
 * This is the child thread created by the server for handling incoming connections.
 * It waits for messages and echoes them back to the client that sent them.
 *
 * Inputs:
 *   clientsock  - the socket this thread will communicate to the client with
 *   client_port - the port of the client this thread is monitoring
 *   recv_delay  - true if the read process is to be slowed down
 *
 * *Returns:
 *   the new connection structure (NULL if error)
 */
void child_handle_client ( int clientsock, int client_port, bool recv_delay )
{
    int retcode, send_count, recv_count;
    pid_t procid = getpid();
    tBufferStc firstmsg, lastmsg;
    char buffer[MAX_MESSAGE_LEN + 1];

    firstmsg.next = NULL;
    lastmsg.next = NULL;
    send_count = 0;
    recv_count = 0;
    bzero(buffer, sizeof(buffer));

    bool running = true;
    while (running)
    {
        // zero the socket descriptor vector and set for server sockets
        // (NOTE: this must be reset every time select() is called)
        fd_set  read_set, write_set;
        FD_ZERO (&read_set);
        FD_ZERO (&write_set);
        FD_SET (clientsock, &read_set);     // add server socket to read vector
        int max_descriptor = clientsock;

        // set the timeout for events and wait
        struct timeval  sel_timeout;
        sel_timeout.tv_sec = 1;
        sel_timeout.tv_usec = 0;
        retcode = select (max_descriptor+1, &read_set, &write_set, NULL, &sel_timeout);
        if (retcode < 0)
        {
//            if (errno == EINTR) continue;
            logmsg(PRINT_ERROR, "select: %s\n", strerror(errno));
            running = false;
            break;
        }
        if (retcode == 0)  // ignore timeout condition
            continue;

        if (FD_ISSET (clientsock, &read_set))
        {
            // read response from server
            bzero(buffer, sizeof(buffer));
            tRecvMsgTyp recv_error = tcp_recv_message (clientsock, buffer, sizeof(buffer));
            if (recv_error == RECV_TERMINATED)
            {
                logmsg(PRINT_SOCKET, "socket recvmsg (port %u) pid %d terminated connection\n", client_port, (int)procid);
                running = false;
                break;
            }
            else if (recv_error == RECV_BLOCKED)
            {
                logmsg(PRINT_ERROR, "socket recvmsg (port %u): blocked\n", client_port);
            }
            else if (recv_error == RECV_FAILURE)
            {
                logmsg(PRINT_ERROR, "socket recvmsg (port %u): %s\n", client_port, strerror(errno));
                running = false;
                break;
            }
            else // if (recv_error == RECV_COMPLETE)
            {
                // success - echo response back to the client
                recv_count++;
                remove_term (buffer, sizeof(buffer)); // remove any terminator chars
                logmsg(PRINT_SENT, "pid %d [port %u msg %u] : %.30s\n", (int)procid, client_port, recv_count, buffer);
                int msglen = strlen(buffer);

                // allocate a block to hold the received message
                char * response = (char*)malloc(msglen + 1);
                if (response == NULL)
                {
                    logmsg(PRINT_ERROR, "memory allocation for message\n");
                    running = false;
                    break;
                }

                // place response in send queue
                // allocate a message queue entry to add
                tBufferStc * qentry = (tBufferStc*)malloc(sizeof(tBufferStc));
                if (qentry == NULL)
                {
                    logmsg(PRINT_ERROR, "memory allocation for send queue\n");
                    free(response);
                    running = false;
                    break;
                }

                // save the message contents in it
                memcpy (response, buffer, msglen + 1);
                bzero(buffer, sizeof(buffer)); // reset the receive buffer
                qentry->buffer = response;
                qentry->msglen = msglen;
                qentry->msgix  = recv_count; // save the message index for this connection
                qentry->next   = 0;       // this indicates there are no entries after this

                // we add the entry to the end of the list
                if (lastmsg.next) lastmsg.next->next = qentry;  // not 1st entry, set last entry to point to this
                else              firstmsg.next      = qentry;  // adding 1st entry to list, set first ptr
                lastmsg.next = qentry; // this must always point to new last entry
            }

            // if we are trying to slow down the response of the server, let's insert a short delay here
            if (recv_delay) sleep(1);

        } // end: if (FD_ISSET (clientsock, &read_set))

        // NOTE: always attempt to send, since we may not get notified when we first add an entry to the queue.
        //if (FD_ISSET (clientsock, &write_set))
        {
            // attempt to send messages from queue
            tBufferStc * pending = firstmsg.next;
            while (pending)
            {
                tBufferStc * next = pending->next;
                if (pending->buffer == NULL)
                {
                    //rem_message (&firstmsg, &lastmsg); // invalid NULL buffer
                    firstmsg.next = next;
                    if (next == 0) lastmsg.next = 0;  // removed last entry in queue
                    free(pending);
                    pending = next;
                    continue;
                }

                // send the message
                tSendMsgTyp send_error = tcp_send_message ( clientsock, pending->buffer, strlen(pending->buffer), send_count+1 );
                if (send_error == SEND_BLOCKED)
                {
                    logmsg(PRINT_ERROR, "socket sendmsg (port %u): blocked\n", client_port);
                }
                else if (send_error == SEND_FAILURE)
                {
                    logmsg(PRINT_ERROR, "socket sendmsg (port %u): %s\n", client_port, strerror(errno));
                    running = false;
                    break;
                }
                else // if (send_error == SEND_COMPLETE)
                {
                    // message was successfully sent - remove it from queue
                    firstmsg.next = pending->next;
                    if (pending->next == 0) lastmsg.next = 0;  // removed last entry in queue
                    free(pending->buffer);
                    free(pending);
                    send_count++;
                }

                pending = next;
            }
        } // end: if (FD_ISSET (clientsock, &write_set))
    }

    close(clientsock);
    logmsg(PRINT_OTHER, "pid %d terminating\n", (int)procid);
}

/*
 * Description:
 * This is signal handler function for handling the death of a child process.
 *
 * Inputs:
 *   sig  - the signal received
 *
 * *Returns:
 *   <none>
 */
void sigchld_handler (int sig)
{
    pid_t pid;

    while (true)
    {
        pid = waitpid((pid_t)-1, NULL, WNOHANG); // this will return immediately

        if (pid == 0)
        {
            break;  // no more children to process, so break
        }
        else if (pid == -1)
        {
            // handle errors
            if (errno == EINTR)  continue;  // continue on interruption
            if (errno == ECHILD) break;     // exit without complaints if no children
            logmsg(PRINT_ERROR, "waitpid: %s\n", strerror(errno)); // exit & report error on anything else
            break;
        }
        else
        {
            // success: mark server list entry invalid
            stop_server_link (pid);
            logmsg(PRINT_OTHER, "pid %d zombie removed\n", (int)pid);
        }
    }
}

int main(int argc, char *argv[])
{
    int  serversock, clientsock;
    int  portno, destport, setport, retcode;
    int  recv_delay, testcount;
    tConnectStc * current_endpt;
    unsigned int  child_count = 0;
    pid_t  process_id;
    struct hostent *server;

    // initialize any user interface setup
    userio_init();

    if (argc < 2)
    {
        fprintf(stderr," ! ERROR, no port provided\n");
        exit(1);
    }

    portno = atoi(argv[1]);
    recv_delay = 0;
    process_id = 0;
    destport = -1;
    serversock = -1;
    clientsock = -1;
    testcount = 0;
    current_endpt = NULL;
    init_all_connections();

    server = gethostbyname("localhost");
    if (server == NULL)
    {
        fprintf(stderr," ! ERROR, no such host\n");
        exit(1);
    }

    // create the server socket for accepting incoming connections
    serversock = tcp_create_socket (portno);
    if (serversock < 0)
        exit(1);

    // setup handler for SIGCHLD signal to handle the death of a child
    //(the children processes handle the server responses for each server connection)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);

    bool running = true;
    while (running)
    {
        // zero the socket descriptor vector and set for server sockets
        // (NOTE: this must be reset every time select() is called)
        fd_set  read_set, write_set;
        FD_ZERO (&read_set);
        FD_ZERO (&write_set);
        FD_SET (STDIN_FILENO, &read_set);   // add keyboard to read vector
        FD_SET (serversock, &read_set);     // add server socket to read vector
        int max_descriptor = serversock;
        set_connection_select (&read_set, &max_descriptor);  // add active endpoints to read vector
        set_connection_select (&write_set, &max_descriptor); // add active endpoints to write vector

        // set the timeout for events and wait
        struct timeval  sel_timeout;
        sel_timeout.tv_sec = 1;
        sel_timeout.tv_usec = 0;
        retcode = select (max_descriptor+1, &read_set, &write_set, NULL, &sel_timeout);
        if (retcode < 0)
        {
            if (errno == EINTR)
            {
                logmsg(PRINT_OTHER, "select [main] interrupted, restarting\n");
                continue;
            }
            logmsg(PRINT_ERROR, "select [main]: %s\n", strerror(errno));
            exit(1);
        }
        else if (retcode == 0)
        {
            // timeout
        }
        else
        {
            if (FD_ISSET (STDIN_FILENO, &read_set))
            {
                //=====================================================================
                // THIS SECTION HANDLES THE KEYBOARD INPUT FROM THE USER, WHICH IS USED
                // TO HANDLE COMMANDS (SUCH AS OPENING AND CLOSING CONNECTIONS TO OTHER
                // ENDPOINTS AND TERMINATING) AS WELL AS SPECIFYING MESSAGES TO SEND TO
                // THE ENDPOINTS IT IS CONNECTED TO.
                //=====================================================================

                // read the keyboard input
                testcount = 0; // any keyboard input automatically stops the message test mode
                int value = 0;
                char buffer[MAX_MESSAGE_LEN + 1];
                bzero(buffer, sizeof(buffer));
                int command = userio_get_command (&value, buffer, sizeof(buffer));
                switch (command)
                {
                    case ACTION_QUIT :
                        logmsg(PRINT_QUERY, "endpoint exiting...\n");
                        running = false;
                        break;
                    case ACTION_SEND_MESSAGE :
                        // check if we have a server connection yet
                        if (current_endpt == NULL || current_endpt->state == STATE_IDLE)
                        {
                            logmsg(PRINT_ERROR, "No active connection specified. Either create or select a connection to use\n");
                        }
                        else
                        {
                            // attempt to send the message
                            current_endpt->msgix++; // increment the # of messages produced
                            send_message (current_endpt, buffer);
                        }
                        break;
                    case ACTION_ADD_ENDPOINT :
                        current_endpt = add_connection (value, server);
                        // if successful, new connection becomes active socket
                        break;
                    case ACTION_REM_ENDPOINT :
                        rem_connection (value);
                        // if current endpoint is the one we deleted, set selection to NULL
                        if (current_endpt != NULL && current_endpt->destport == value)
                            current_endpt = NULL;
                        break;
                    case ACTION_SEL_ENDPOINT :
                        current_endpt = find_connection (value);
                        if (current_endpt == NULL)
                            logmsg(PRINT_ERROR, "connection to port %u not found\n", value);
                        break;
                    case ACTION_DELAY :
                        recv_delay = true;
                        break;
                    case ACTION_TEST :
                        if (current_endpt == NULL || current_endpt->state == STATE_IDLE)
                            logmsg(PRINT_ERROR, "No active connection specified. Either create or select a connection to use\n");
                        else
                        {
                            testcount = value;
                            if (testcount > 99999) testcount = 99999;
                            if (testcount < 0)     testcount = 0;
                        }
                        break;
                    case ACTION_SET_PRINT_FLAG :
                        print_flag = value;
                        break;
                    case ACTION_SHOW_CONNECTIONS :
                        show_all_connections ();
                        break;
                    case ACTION_TRANSPORT :
                        transport_type = value ? SHIP_REINDEER : SHIP_UPS;
                        break;
                    case ACTION_HOHOHO :
                        char * package = secret_package_selection (value);
                        send_message (current_endpt, package);
                        break;
                    default :
                    case ACTION_INVALID :
                        logmsg(PRINT_ERROR, "Unknown command received: %d\n", command);
                        break;
                }
            } // end: if (FD_ISSET (STDIN_FILENO, &read_set))

            // check if message test is running
            if (testcount)
            {
                char tempbuf[101];
                sprintf(tempbuf, "%5.5d: This is a test message to determine if the send process gets blocked. 01234567890123456789...", testcount);
                current_endpt->msgix++; // increment the # of messages produced
                send_message (current_endpt, tempbuf);
                testcount--;
            }

            if (running)
            {
                if (FD_ISSET (serversock, &read_set))
                {
                    //=====================================================================
                    // THIS SECTION HANDLES THE SERVER LISTEN SOCKET, WHICH:
                    // - RECEIVES CONNECTION REQUESTS FROM NEW CLIENTS (MAIN THREAD)
                    // - RECEIVES MESSAGES FROM THE EXTERNAL ENDPOINT CLIENTS (CHILD THREAD)
                    //
                    // NOTE THAT THERE IS ONE CHILD THREAD CREATED FOR EACH ENDPOINT CONNECTION.
                    //=====================================================================

                    // wait for connections
                    int client_port;
                    clientsock = tcp_accept_connection (serversock, &client_port);
                    if (clientsock < 0) exit(1);
                    if ((process_id = fork()) < 0)
                    {
                        logmsg(PRINT_ERROR, "fork: %s\n", strerror(errno));
                        exit(1);
                    }

                    // the child process (it handles the data socket)...
                    else if (process_id == 0)
                    {
                        close (serversock); // close parent socket
                        child_handle_client (clientsock, client_port, recv_delay); // child handles data on client socket
                        exit (0); // terminate the child process
                    }

                    // the parent process (it handles the connection socket)...
                    logmsg(PRINT_OTHER, "spawned child process pid: %d to handle port %u (recv delay = %d)\n",
                            (int)process_id, client_port, recv_delay);
                    add_server_link (process_id, client_port);
                    close (clientsock); // close the child socket
                } // end: if (FD_ISSET (serversock, &read_set))

                //=====================================================================
                // THIS SECTION HANDLES EACH OF THE ENDPOINT SOCKETS
                // (HOWEVER MANY ACTIVE CONNECTIONS THERE ARE)
                //=====================================================================
                tConnectStc * connection;
                for (connection = first_conn_req.next; connection != NULL; connection = connection->next)
                {
                    if (FD_ISSET (connection->sockfd, &write_set))
                    {
                        //=====================================================================
                        // THIS SECTION HANDLES THE ENDPOINT WRITE EVENTS, WHICH:
                        // - HANDLE COMPLETION OF THE CONNECTION TO ANOTHER ENDPOINT (STARTED
                        //   WHEN THE CONNECTION COMMAND WAS INITIATED BY THE KEYBOARD INPUT).
                        // - HANDLE SENDING NEXT QUEUED MESSAGE THAT WAS PREVIOUSLY BLOCKED.
                        //=====================================================================
                        if (connection->state == STATE_PENDING)
                        {
                            // determine if connection to server has completed successfully
                            int sock_error;
                            socklen_t sopt_size;
                            sopt_size = sizeof(sock_error);
                            retcode = getsockopt(connection->sockfd, SOL_SOCKET, SO_ERROR, &sock_error, &sopt_size);
                            if (retcode < 0)
                            {
                                logmsg(PRINT_SOCKET, "socket getsockopt failed (port %u): %s\n", connection->destport, strerror(errno));
                                rem_connection (connection->destport);
                                continue; // exit processing of this connection
                            }
                            else if (sock_error != 0)
                            {
                                logmsg(PRINT_SOCKET, "socket getsockopt connect failure (port %u): %s\n", connection->destport, strerror(sock_error));
                                rem_connection (connection->destport);
                                continue; // exit processing of this connection
                            }
                            else
                            {
                                // get the assigned port for the endpoint
                                struct sockaddr_in my_addr;
                                socklen_t addr_size = sizeof(my_addr);
                                getsockname(connection->sockfd, (struct sockaddr*)&my_addr, &addr_size);
                                connection->sendport = ntohs(my_addr.sin_port);
                                connection->state = STATE_READY;
                                logmsg(PRINT_SOCKET, "socket getsockopt connect complete (port %u) - sending on port: %u\n", connection->destport, connection->sendport);
                            }
                        }

                        // if messages are pending in the queue, send them now
                        while (! send_message (connection, 0)) { } // terminates when queue is empty or send fails
                    } // end: if (FD_ISSET (endsock, &write_set))

                    if (FD_ISSET (connection->sockfd, &read_set))
                    {
                        //=====================================================================
                        // THIS SECTION HANDLES THE ENDPOINT READ EVENTS, WHICH:
                        // - RECEIVES MESSAGES FROM THE EXTERNAL ENDPOINT'S SERVER, WHICH ARE
                        //   THE RESPONSES TO THE MESSAGES SENT TO IT FROM THIS ENDPOINT.
                        //=====================================================================
                        if (connection->state == STATE_READY)
                        {
                            char response[MAX_MESSAGE_LEN + 1];
                            bzero(response, sizeof(response));
                            while (true)
                            {
                                // read response from server
                                tRecvMsgTyp recv_error = tcp_recv_message (connection->sockfd, response, sizeof(response));
                                if (recv_error == RECV_COMPLETE)
                                {
                                    remove_term (response, sizeof(response));
                                    logmsg(PRINT_RCVD, "%.30s\n",response);
                                    connection->rspix++; // increment the # of messages received
                                }
                                else if (recv_error == RECV_BLOCKED)
                                {
                                    break;
                                }
                                else if (recv_error == RECV_TERMINATED)
                                {
                                    logmsg(PRINT_SOCKET, "socket recvmsg (port %u) terminated connection\n", connection->destport);
                                    rem_connection (connection->destport);
                                    break;
                                }
                                else // if (recv_error == RECV_FAILURE)
                                {
                                    logmsg(PRINT_ERROR, "socket recvmsg (port %u): %s\n", connection->destport, strerror(errno));
                                    rem_connection (connection->destport);
                                    break;
                                }
                            }
                        }
                    }  // end: if (FD_ISSET (endsock, &read_set))

                } // end: for (connection =...

            } // end: if(running)
        }
    }

    signal(SIGCHLD, SIG_IGN); // Silently (and portably) reap children
    close(serversock);
    close(clientsock);
    close_all_connections();
    userio_exit();
    return 0;
}

