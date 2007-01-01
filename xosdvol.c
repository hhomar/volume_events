#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <xosd.h>
#include "voleventd.h"

// could work out the index dynamically but it seems pointless
int
parse_percent(char *msg, int index)
{
    char sp[5];
    int i, j, p;
            
    i = 0; j = index;
    while ((sp[i++] = msg[j++]));
    //printf("sp: |%s|\n", sp);
    p = atoi(sp);
    //printf("p: %i\n", p);
    
    return p;
}

void
volume_display(xosd *osd, int percent)
{
    char vol_percent[15];
    snprintf(vol_percent, 15, "Volume - %i%%", percent);
    xosd_set_timeout(osd, 5);
    xosd_display(osd, 0, XOSD_string, vol_percent);
    xosd_display(osd, 1, XOSD_slider, percent);
}

int
main(void)
{
    int s_fd;
    struct sockaddr_un server;
    char msg[20];
    xosd *osd;
    
    if ((s_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Couldn't create socket: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    memset(&server, 0, sizeof(struct sockaddr_un));
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, VOLEVENTD_SOCKET);

    if (connect(s_fd, (struct sockaddr *) &server,
                sizeof(struct sockaddr_un)) < 0) {
        fprintf(stderr, "Couldn't connect to server: %s\n", strerror(errno));

        return EXIT_FAILURE;
    }
    memset(&msg, 0, sizeof(msg)); 
    
    /* set volume display options */
    osd = xosd_create(2);
    xosd_set_bar_length(osd, 30);
    xosd_set_pos(osd, XOSD_middle);
    xosd_set_align(osd, XOSD_center);
 
    while (read(s_fd, msg, 20)) {
        if (strncmp(msg, MSG_MUTE, 4) == 0) {
            volume_display(osd, 0);
        }
        else if (strncmp(msg, MSG_UNMUTE, 6) == 0) {
            volume_display(osd, parse_percent(msg, 7));
        }
        else if (strncmp(msg, MSG_VOL_UP, 6) == 0) {
            volume_display(osd, parse_percent(msg, 7));
        }
        else if (strncmp(msg, MSG_VOL_DOWN, 8) == 0) {
            volume_display(osd, parse_percent(msg, 9));
        }
        memset(msg, 0, 20);
    }
    
    close(s_fd); 

    return 0;
}
