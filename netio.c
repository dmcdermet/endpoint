//=============================================================================
//
// This contains the definitions, structures and function prototypes defined by the
// network interface module of the Interactive Endpoint project.
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

#include "userio.h"     // for logmsg
#include "netio.h"

/*
 * Description:
 * Creates a non-blocking TCP socket for use by the system and if a port is specified, binds it
 * to that port and sets up as a server by setting it to listen for connections.
 *
 * Inputs:
 *   portno  - the server port to bind it to. If 0, it is a client socket and is not bound.
 *
 * *Returns:
 *   socket descriptor value
 */
int tcp_create_socket ( int portno )
{
    int retcode, rcv_bufsize, snd_bufsize;
    int sockfd;
    socklen_t sopt_size;
    struct sockaddr_in serv_addr;

    // create the server socket
    sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0)
    {
        logmsg(PRINT_ERROR, "socket open: %s\n", strerror(errno));
        return -1;
    }

    if (portno > 0)
    {
        // assign the addr/port to the socket
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        serv_addr.sin_port = htons(portno);
        retcode = bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
        if (retcode < 0)
        {
            logmsg(PRINT_ERROR, "socket bind: %s\n", strerror(errno));
            close(sockfd);
            return -1;
        }
    }

    // set socket to non-blocking mode
    retcode = fcntl (sockfd, F_SETFL, O_NONBLOCK);
    if (retcode < 0)
    {
        logmsg(PRINT_ERROR, "socket set to non-block: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    // get info on socket buffer sizes
    sopt_size = sizeof(rcv_bufsize);
    retcode = getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcv_bufsize, &sopt_size);
    if (retcode < 0)
    {
        logmsg(PRINT_ERROR, "socket getsockopt SO_RCVBUF: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }
    sopt_size = sizeof(snd_bufsize);
    retcode = getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &snd_bufsize, &sopt_size);
    if (retcode < 0)
    {
        logmsg(PRINT_ERROR, "socket getsockopt SO_SNDBUF: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    // set socket to listen for connections
    if (portno > 0)
    {
        retcode = listen(sockfd, 5);
        if (retcode < 0)
        {
            logmsg(PRINT_ERROR, "socket listen: %s\n", strerror(errno));
            close(sockfd);
            return -1;
        }
        logmsg(PRINT_SOCKET, "server socket listening on port: %u (rcvbuf = %u, sndbuf = %u)\n",
                portno, rcv_bufsize, snd_bufsize);
    }
    else
    {
        logmsg(PRINT_SOCKET, "client socket created: (rcvbuf = %u, sndbuf = %u)\n",
                rcv_bufsize, snd_bufsize);
    }

    return sockfd;
}

/*
 * Description:
 * Connects the specified socket to a server specified by the port and address.
 *
 * Inputs:
 *   clientsock - the socket descriptor to connect
 *   portno  - the server port to connect to.
 *   server  - the server address to connect to
 *
 * *Returns:
 *   socket descriptor value
 */
int tcp_connect_to_server ( int clientsock, int portno, struct hostent * server )
{
    int  retcode;
    struct sockaddr_in serv_addr;

    if (portno <= 0)
    {
        logmsg(PRINT_ERROR, "invalid port selection: must specify destination port for this connection\n");
        return STATE_IDLE;
    }

    // setup destination address
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    // begin the connection
    retcode = connect(clientsock,(struct sockaddr *) &serv_addr,sizeof(serv_addr));
    if (retcode < 0)
    {
        if ((errno == EINPROGRESS))
        {
            logmsg(PRINT_SOCKET, "socket connect (port %u): in progress\n", portno);
            return STATE_PENDING;
        }
        logmsg(PRINT_ERROR, "socket connect (port %u): %s", portno, strerror(errno));
        return STATE_IDLE;
    }

    logmsg(PRINT_SOCKET, "socket connect (port %u): complete\n", portno);
    return STATE_READY;
}

/*
 * Description:
 * Completes a connection request from a client by accepting it.
 *
 * Inputs:
 *   serversock - the socket descriptor to connect
 *   portno  - ptr to location to return the port of the client it connects to
 *
 * *Returns:
 *   socket descriptor for communicating with the client
 */
int tcp_accept_connection ( int serversock, int * portno )
{
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    int clientsock;

    // wait for connections
    clilen = sizeof(cli_addr);
    clientsock = accept(serversock, (struct sockaddr *) &cli_addr, &clilen);
    if (clientsock < 0)
        logmsg(PRINT_ERROR, "socket accept (port %u): %s\n", cli_addr.sin_port, strerror(errno));
    else if (portno)
        * portno = ntohs(cli_addr.sin_port); // return port of connected client
    return clientsock;
}

/*
 * Description:
 * Sends a message to the specified socket
 *
 * Inputs:
 *   sockfd  - the socket to send the message on
 *   buffer  - the message to send
 *   msglen  - length of the message
 *   msgix   - an index for the messages (incremented after each send, per connection)
 *
 * *Returns:
 *   the status of the send
 */
tSendMsgTyp tcp_send_message ( int sockfd, char * buffer, int msglen, int msgix )
{
    MessageHeaderStc header;
    struct msghdr msg_header;
    struct iovec  msg_iov[2];
    int array_cnt = 0;

    // format message header
    header.msglen  = msglen;
    header.msgix   = msgix;
    msg_iov[array_cnt].iov_base = &header;
    msg_iov[array_cnt].iov_len  = sizeof(header);
    array_cnt++;
    msg_iov[array_cnt].iov_base = buffer;
    msg_iov[array_cnt].iov_len  = msglen;
    array_cnt++;

    memset (&msg_header, 0, sizeof(msg_header));
    msg_header.msg_iov = msg_iov;       // scatter-gather array
    msg_header.msg_iovlen = array_cnt;  // # elements in msg_iov
    // msg_header.msg_control     - unused
    // msg_header.msg_controllen  - unused
    // msg_header.msg_flags       - unused

    // send message to connected server
    int n = sendmsg (sockfd, &msg_header, MSG_NOSIGNAL);  // if connection broken, don't issue signal
//    logmsg(PRINT_OTHER, "sendmsg: n %d, msglen %d, headlen %d, header { %d, %d }\n", n, msglen, (int)sizeof(header), header.msglen, header.msgix);
    if (n > 0)
    {
        return SEND_COMPLETE;
    }
    else if ((n < 0) && (errno == EWOULDBLOCK))
    {
        return SEND_BLOCKED;
    }

    return SEND_FAILURE;
}

/*
 * Description:
 * Sends a message to the specified socket
 *
 * Inputs:
 *   sockfd  - the socket to receive the message on
 *   buffer  - ptr to location to receive the message in
 *   msglen  - allocation size of the message
 *
 * *Returns:
 *   the status of the receive
 */
tRecvMsgTyp tcp_recv_message ( int sockfd, char * buffer, int msglen )
{
    MessageHeaderStc header;
    struct msghdr msg_header;
    struct iovec  msg_iov[1];
    int recv_count = 0; // current number of chars received

    // format message header
    memset (&header, 0, sizeof(header));
    msg_iov[0].iov_base = &header;
    msg_iov[0].iov_len  = sizeof(header);

    memset (&msg_header, 0, sizeof(msg_header));
    msg_header.msg_iov = msg_iov;       // scatter-gather array
    msg_header.msg_iovlen = 1;  // # elements in msg_iov
    // msg_header.msg_control     - unused
    // msg_header.msg_controllen  - unused
    // msg_header.msg_flags       - unused

    // send message to connected server
    while (true)
    {
        // keep reading until error, blocked, termination, or completed msg received
        int n = recvmsg (sockfd, &msg_header, 0);
        if (n == 0) return RECV_TERMINATED; // client connection was terminated
        else if (n < 0) // error occurred
        {
            if (errno == EWOULDBLOCK) return RECV_BLOCKED;
            return RECV_FAILURE;
        }

        // success receiving some chars of the message. check if complete.
        recv_count += n; // increment the received char count by the number received
//        logmsg(PRINT_OTHER, "recvmsg: n %d, count %d, headlen %d, header { %d, %d }\n", n, recv_count, (int)sizeof(header), header.msglen, header.msgix);
        if (recv_count >= sizeof(header))
        {
            // check if header contents are valid
            if (header.msglen > msglen)
            {
                logmsg(PRINT_ERROR, "invalid message header: len = %d, ix = %d\n", header.msglen, header.msgix);
                header.msglen = msglen;
            }

            // header portion is complete & contains full message size. see if complete.
            if (recv_count >= sizeof(header) + header.msglen) // should never be >
                break;

            // if header just filled, switch to filling message
            if (recv_count == sizeof(header))
            {
                msg_iov[0].iov_base = buffer;
                msg_iov[0].iov_len  = header.msglen;
            }
            else
            {
                // adjust position and amount to fill in message buffer
                msg_iov[0].iov_base = &buffer[recv_count-sizeof(header)];
                msg_iov[0].iov_len  -= n;
            }
        }
        else
        {
            // adjust position and amount to fill in header
            msg_iov[0].iov_base = (char*)&header + recv_count;
            msg_iov[0].iov_len  = sizeof(header) - recv_count;
        }
    }

    return RECV_COMPLETE; // or RECV_INPROCESS
}

