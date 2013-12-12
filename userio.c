//=============================================================================
//
// This is the user interface module of the Interactive Endpoint project.
//
//=============================================================================

// if GUI interface desired, uncomment the following and add -lncurses to end of g++ command.
// (e.g.: g++ -o endpoint endpoint.c -lncurses)
#include <ncurses.h>

#include <stdio.h>
#include <stdarg.h>  // for vprintf functions
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "userio.h"

#ifdef NCURSES_BOOL
WINDOW * win_input  = NULL;  // this holds the window structure for the user command input and the command responses (PRINT_QUERY)
WINDOW * win_msgs   = NULL;  // this holds the window structure for displaying messages sent & received on sockets (PRINT_RCVD, _ECHO)
WINDOW * win_error  = NULL;  // this holds the window structure for displaying error and informational messages (PRINT_ERROR, _SOCKET, _OTHER)
WINDOW * win_status = NULL;  // this holds the window structure for displaying communication status (PRINT_STATUS)
#endif

/*
 * Description:
 * Handles the outputting of all messages. Categorizes them to allow selective enabling.
 *
 * Inputs:
 *   category - the message category
 *   fmt      - the printf format arguments
 *
 * *Returns:
 *   <none>
 */
void logmsg ( int category, const char * fmt, ... )
{
    va_list args;
    char disp_array[121], * dptr;
    dptr = disp_array;

#ifdef NCURSES_BOOL
    // always print all messages
    WINDOW * window;

    // prepend a prefix to the message dependent on the message type and get the window to display msg in
    switch (category)
    {
        default :
            break;
        case PRINT_STATUS  : window = win_status; break;
        case PRINT_QUERY   : window = win_input;  break;
        case PRINT_ERROR   : window = win_error;  sprintf (dptr, "ERROR : ");  break;
        case PRINT_WARNING : window = win_error;  sprintf (dptr, "WARN  : ");  break;
        case PRINT_SOCKET  : window = win_error;  sprintf (dptr, "SOCK  : ");  break;
        case PRINT_OTHER   : window = win_error;  sprintf (dptr, "INFO  : ");  break;
        case PRINT_RCVD    : window = win_msgs;   sprintf (dptr, "< ");  break;
        case PRINT_SENT    : window = win_msgs;   sprintf (dptr, "> ");  break;
    }

    if (window)
    {
        dptr += strlen(dptr);

        // now add the log message
        va_start(args, fmt);
        vsprintf(dptr, fmt, args);
        va_end(args);

        // display message in selected window
        wprintw (window, "%s", disp_array);
        wrefresh(window);
    }
#else
    // always print errors, warnings and command response messages
    int allowed = print_flag | PRINT_ERROR | PRINT_WARNING | PRINT_QUERY;

    if (category & allowed)
    {
        // prepend a prefix to the message dependent on the message type
        switch (category)
        {
            default :
                break;
            case PRINT_STATUS  : break;
            case PRINT_QUERY   : break;
            case PRINT_ERROR   : sprintf (dptr, " ! ERROR : ");  break;
            case PRINT_WARNING : sprintf (dptr, " ! WARN  : ");  break;
            case PRINT_SOCKET  : sprintf (dptr, " ! ");  break;
            case PRINT_OTHER   : sprintf (dptr, " ! ");  break;
            case PRINT_RCVD    : sprintf (dptr, " < ");  break;
            case PRINT_SENT    : sprintf (dptr, " > ");  break;
        }

        dptr += strlen(dptr);

        // now add the log message and output to the terminal
        va_start(args, fmt);
        vsprintf(dptr, fmt, args);
        va_end(args);
        printf ("%s", disp_array);
    }
#endif
}

int userio_get_command ( int * value, char * buffer, int size )
{
    int command = ACTION_INVALID;

    if ((value == 0) || (buffer == 0) || (size == 0))
        return ACTION_INVALID;

    // read the user input
#ifdef NCURSES_BOOL
    getstr(buffer);
    wprintw (win_input, "%s", buffer);
#else
    fgets(buffer, size - 1, stdin);
#endif

    // check if we received a command
    if (buffer[0] == '#')
    {
        char invalid_char = 0;
        char * flag = &buffer[2];

        switch (buffer[1])
        {
        case 'q':   command = ACTION_QUIT;              break;
        case '+':   command = ACTION_ADD_ENDPOINT;      *value = atoi(&buffer[2]);      break;
        case '-':   command = ACTION_REM_ENDPOINT;      *value = atoi(&buffer[2]);      break;
        case 's':   command = ACTION_SEL_ENDPOINT;      *value = atoi(&buffer[2]);      break;
        case 'z':   command = ACTION_DELAY;             break;
        case 't':   command = ACTION_TEST;              *value = atoi(&buffer[2]);      break;

#ifndef NCURSES_BOOL // these are only used if gui not running
        case 'p':   command = ACTION_SET_PRINT_FLAG;    *value = 0;
            while (!invalid_char && *flag > ' ')
            {
                if (*flag == '0') *value = 0;
                else if (*flag == 'a') *value = PRINT_ALL;
                else if (*flag == 's') *value |= PRINT_SENT;
                else if (*flag == 'r') *value |= PRINT_RCVD;
                else if (*flag == 'c') *value |= PRINT_SOCKET;
                else if (*flag == 'o') *value |= PRINT_OTHER;
                else invalid_char = *flag;
                flag++;
            }
            if (invalid_char)
            {
                logmsg(PRINT_QUERY, "invalid print flag: %c. must be { 0, a, s, r, c, o }\n", invalid_char);
                *value = PRINT_ALL;
            }
            else
            {
                logmsg(PRINT_QUERY, "print_flag = 0x%2.2X\n", *value);
            }
            break;

        case 'd':
            command = ACTION_SHOW_CONNECTIONS;
            break;
#endif

        default:
            logmsg(PRINT_ERROR, "Invalid command\n");
            break;
        }
    }
    else
    {
        command = ACTION_SHOW_CONNECTIONS;
    }

    return command;
}

#ifdef NCURSES_BOOL
// some ncurses functions:
typedef struct
{
    chtype ls, rs, ts, bs;      // chars to represent each side (l=left, r=right, t=top, b=bottom
    chtype tl, tr, bl, br;      // chars to represent each corner

} WIN_BORDER;

typedef struct
{
    int startx;
    int starty;
    int height;
    int width;
    WIN_BORDER border;

} WIN;

void init_win_params ( WIN *p_win )
{
    p_win->height = 3;
    p_win->width = 10;
    p_win->starty = (LINES - p_win->height)/2;
    p_win->startx = (COLS - p_win->width)/2;
    p_win->border.ls = '|';
    p_win->border.rs = '|';
    p_win->border.ts = '-';
    p_win->border.bs = '-';
    p_win->border.tl = '+';
    p_win->border.tr = '+';
    p_win->border.bl = '+';
    p_win->border.br = '+';
}

void create_box ( WIN *p_win, bool flag )
{
    int i, j;
    int x, y, w, h;
    x = p_win->startx;
    y = p_win->starty;
    w = p_win->width;
    h = p_win->height;
    if (flag == TRUE)
    {
        mvaddch(y,     x,     p_win->border.tl);
        mvaddch(y,     x + w, p_win->border.tr);
        mvaddch(y + h, x,     p_win->border.bl);
        mvaddch(y + h, x + w, p_win->border.br);
        mvhline(y,     x + 1, p_win->border.ts, w - 1);
        mvhline(y + h, x + 1, p_win->border.bs, w - 1);
        mvvline(y + 1, x,     p_win->border.ls, h - 1);
        mvvline(y + 1, x + w, p_win->border.rs, h - 1);
    }
    else
    {
        for(j = y; j <= y + h; ++j)
            for(i = x; i <= x + w; ++i)
                mvaddch(j, i, ' ');
    }

    refresh();
}
#endif

void userio_init ( void )
{
#ifdef NCURSES_BOOL
//    WIN  win;
    initscr();      // start ncurses
    cbreak();       // allow control chars to act. (use raw() to prevent use of signals from keyboard)
    noecho();       // echo keyboard input (use noecho() to allow better presentation of key input)
    keypad(stdscr, true);  // enable ability to get function and arrow keys from user input
    // setup windows for GUI: newwin params are: line height, column width, start line(y), start column(x)
    const int w_inp = 20,  h_inp = 10;              // input   window is  20 chars wide and 10 lines in height
    const int w_msg = 100, h_msg = 40;              // message window is 100 chars wide and 40 lines in height
    // these are derrived from the above
    const int w_sta = w_inp, h_sta = h_msg;         // status window is width of input   and height of message
    const int w_err = w_msg, h_err = h_inp;         // error  window is width of message and height of command
    const int l_sta = 1        , c_sta = 1;         // status  window is top left
    const int l_inp = h_msg + 1, c_inp = 1;         // input   window is bottom left
    const int l_msg = 1        , c_msg = w_inp + 1; // message window is top right
    const int l_err = h_msg + 1, c_err = w_inp + 1; // error   window is bottom right
    win_status = newwin(h_sta, w_sta, l_sta, c_sta);    box (win_status, 0, 0);   wrefresh(win_status);
    win_msgs   = newwin(h_msg, w_msg, l_msg, c_msg);    box (win_msgs  , 0, 0);   wrefresh(win_msgs);
    win_input  = newwin(h_inp, w_inp, l_inp, c_inp);    box (win_input , 0, 0);   wrefresh(win_input);
    win_error  = newwin(h_err, w_err, l_err, c_err);    box (win_error , 0, 0);   wrefresh(win_error);
//    init_win_params (&win);  // init window parameters
//    create_box(&win, true);  // display the window borders
#endif
}

void userio_exit ( void )
{
#ifdef NCURSES_BOOL
    endwin();      // exit ncurses
#endif
}

