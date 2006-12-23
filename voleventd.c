#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <alsa/asoundlib.h>
//#include <xosd.h>
#include <errno.h>
#include "voleventd.h"

static int running = -1;

void term_handler(int sig)
{
    running = -sig;
}

#if 0
void
volume_display(xosd *osd, int percent)
{
    char vol_percent[15];
    snprintf(vol_percent, 15, "Volume - %i%%", percent);
    xosd_set_timeout(osd, 5);
    xosd_display(osd, 0, XOSD_string, vol_percent);
    xosd_display(osd, 1, XOSD_slider, percent);
}
#endif

int
main(int argc, char **argv)
{
    int i, ev_fd, rd_event, err;
    char ev_dev[20];
    unsigned long bit[NBITS(EV_MAX)];
    struct input_event ev;
    
    pid_t pid, sid;
    int should_fork = 1;

    snd_mixer_t *mixer_handle;
    char card_id[] = "default";
    snd_mixer_elem_t *elem, *master_elem = NULL;
    long mixer_min, mixer_max, mixer_curr;
    int muted = 0;
    //int percent = 0;
    
    FILE *pid_fd;

    int s_fd;
    struct sockaddr_un server;

    //xosd *osd;

#ifdef SIGTERM
    signal(SIGTERM, term_handler);
#endif

    if (argc > 1) {
        if (strncmp(argv[1], "-nofork", 7) == 0)
            printf("Not forking\n");
            should_fork = 0;
    }

    if ((err = snd_mixer_open(&mixer_handle, 0)) < 0) {
        fprintf(stderr, "Couldn't open mixer device\n");
        return EXIT_FAILURE;
    }
    if ((err = snd_mixer_attach(mixer_handle, card_id)) < 0) {
        fprintf(stderr, "Couldn't attach to mixer device\n");
        snd_mixer_close(mixer_handle);
        return EXIT_FAILURE;
    }
    if ((err = snd_mixer_selem_register(mixer_handle, NULL, NULL)) < 0) {
        fprintf(stderr, "Couldn't register mixer registers\n");
        snd_mixer_close(mixer_handle);
        return EXIT_FAILURE;
    }
    if ((err = snd_mixer_load(mixer_handle)) < 0) {
        fprintf(stderr, "Couldn't load mixer\n");
        snd_mixer_close(mixer_handle);
        return EXIT_FAILURE;
    }

    /* only want to control 'Master' volume */
    for (elem = snd_mixer_first_elem(mixer_handle); elem;
            elem = snd_mixer_elem_next(elem)) {
        if (strcmp(snd_mixer_selem_get_name(elem), "Master") == 0)
            master_elem = elem;
    }
    
    snd_mixer_selem_get_playback_volume_range(master_elem, &mixer_min, &mixer_max);
    snd_mixer_selem_get_playback_volume(master_elem, 0, &mixer_curr);

    /* find keyboard event handler */
    int found_keyboard = 0;
    for (i = 0; i < 5; i++) {
        snprintf(ev_dev, 20, "/dev/input/event%d", i);
        if ((ev_fd = open(ev_dev, O_RDONLY)) >= 0) {
            ioctl(ev_fd, EVIOCGBIT(0, EV_MAX), bit);
            
            // only want to watch keyboard events
            if (test_bit(EV_KEY, bit) && test_bit(EV_REP, bit)) {
                found_keyboard = 1;
                break;
            }
        }   
    }
    if (!found_keyboard) {
        fprintf(stderr, "Couldn't find a keyboard device. "
                "Possibly permission problems\n");
        return EXIT_FAILURE;
    }

    if (should_fork) {
        pid = fork();
        if (pid < 0)
            return EXIT_FAILURE;
        else if (pid > 0)
            return EXIT_SUCCESS;
    
        // create child session id
        sid = setsid();
        if (sid < 0)
            return EXIT_FAILURE;
    
        if ((chdir("/")) < 0)
            return EXIT_FAILURE;
    
        /* potential security risk and don't need them */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    pid_fd = fopen(PID_FILE, "w");
    if (pid_fd == NULL) {
        fprintf(stderr, "Couldn't create file: %s\n", PID_FILE);
        goto cleanup;
        return EXIT_FAILURE;
    }

    if ((s_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "%s\n", strerror(errno));
        goto cleanup;
        return EXIT_FAILURE;
    } 
    
    memset(&server, 0, sizeof(struct sockaddr_un));
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, VOLEVENTD_SOCKET);

    if (bind(s_fd, (struct sockaddr *) &server,
                sizeof(struct sockaddr_un)) < 0) {
        fprintf(stderr, "%s\n", strerror(errno));
        goto cleanup;
        return EXIT_FAILURE;
    }

    if (listen(s_fd, 5) < 0) {
        fprintf(stderr, "%s\n", strerror(errno));
        goto cleanup;
        return EXIT_FAILURE;
    }

#if 0 
    // FIXME: don't want this to block
    if ((client_fd = accept(s_fd, 0, 0)) < 0) {
        fprintf(stderr, "%s\n", strerror(errno));
        goto cleanup;
        return EXIT_FAILURE;
    }
#endif


#if 0    
    /* set volume display options */
    osd = xosd_create(2);
    xosd_set_bar_length(osd, 30);
    xosd_set_pos(osd, XOSD_middle);
    xosd_set_align(osd, XOSD_center);
#endif

    running = 1;
    while (running > 0) {
        rd_event = read(ev_fd, &ev, sizeof(struct input_event));
        /* only use keypresses no key release */
        if (ev.value <= 0)
            continue;

        switch (ev.code) {
            case KEY_MUTE_TOGGLE:
                snd_mixer_selem_get_playback_switch(master_elem, 0, &muted);
                snd_mixer_selem_set_playback_switch_all(master_elem, !muted);
#if 0           
                //xosd_display(osd, 0, XOSD_string, "Volume");
                if (muted) {
                    send_message(MSG_MUTE);
                    //volume_display(osd, 0);
                }
                else {
                    percent = ((float)mixer_curr/mixer_max) * 100;
                    snprintf(buf, 12, "%s %i", MSG_UNMUTE, percent);
                    send_message(buf); 
                    //volume_display(osd, percent);
                }
#endif
                break;
            case KEY_VOL_DOWN:
                snd_mixer_selem_get_playback_volume(master_elem, 0, &mixer_curr);
                if ((mixer_curr-1) < mixer_min)
                    mixer_curr = mixer_min;
                else
                    mixer_curr--;
                snd_mixer_selem_set_playback_volume_all(master_elem, mixer_curr);
#if 0
                percent = ((float)mixer_curr/mixer_max) * 100;
                snprintf(buf, 10, "%s %i", MSG_VOL_DOWN, percent);
                send_message(buf);
                //volume_display(osd, percent);
#endif                
                break;
            case KEY_VOL_UP:
                snd_mixer_selem_get_playback_volume(master_elem, 0, &mixer_curr);
                if ((mixer_curr+1) > mixer_max)
                    mixer_curr = mixer_max;
                else
                    mixer_curr++;
                snd_mixer_selem_set_playback_volume_all(master_elem, mixer_curr);
#if 0
                percent = ((float)mixer_curr/mixer_max) * 100;
                snprintf(buf, 10, "%s %i", MSG_VOL_UP, percent);
                send_message(buf);
                //volume_display(osd, percent);
#endif                
                break;
        }
    }

    //xosd_destroy(osd);
cleanup:
    //close(client_fd);
    close(s_fd);
    snd_mixer_close(mixer_handle);
    close(ev_fd);
    unlink(VOLEVENTD_SOCKET);
    unlink(PID_FILE);

    return 0;
}
