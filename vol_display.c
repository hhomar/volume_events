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
#include <xosd.h>
#include "vol_display.h"

static int running = -1;

void term_handler(int sig)
{
    running = -sig;
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
main(int argc, char **argv)
{
    int i, fd, rd_event, err;
    char filename[20];
    unsigned long bit[NBITS(EV_MAX)];
    struct input_event ev;
    
    pid_t pid, sid;
    int should_fork = 1;

    snd_mixer_t *mixer_handle;
    char card_id[] = "default";
    snd_mixer_elem_t *elem, *master_elem = NULL;
    long mixer_min, mixer_max, mixer_curr;
    int muted = 0;
    int percent = 0;
    FILE *log_fd;

    xosd *osd;

#ifdef SIGTERM
    signal(SIGTERM, term_handler);
#endif

    if (argc > 1) {
        if (strncmp(argv[1], "-nofork", 7) == 0)
            printf("Not forking\n");
            should_fork = 0;
    }

    if ((err = snd_mixer_open(&mixer_handle, 0)) < 0)
        return EXIT_FAILURE;
    if ((err = snd_mixer_attach(mixer_handle, card_id)) < 0) {
        snd_mixer_close(mixer_handle);
        return EXIT_FAILURE;
    }
    if ((err = snd_mixer_selem_register(mixer_handle, NULL, NULL)) < 0) {
        snd_mixer_close(mixer_handle);
        return EXIT_FAILURE;
    }
    if ((err = snd_mixer_load(mixer_handle)) < 0) {
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
    for (i = 0; i < 5; i++) {
        snprintf(filename, 18, "/dev/input/event%d", i);
        if ((fd = open(filename, O_RDONLY)) >= 0) {
            ioctl(fd, EVIOCGBIT(0, EV_MAX), bit);
            
            // only want to watch keyboard events
            // FIXME: maybe want to handle having two keyboards
            if (test_bit(EV_KEY, bit) && test_bit(EV_REP, bit)) {
                break;
            }
        }   
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
    
    /* set volume display options */
    osd = xosd_create(2);
    xosd_set_bar_length(osd, 30);
    xosd_set_pos(osd, XOSD_middle);
    xosd_set_align(osd, XOSD_center);

    running = 1;
    while (running > 0) {
        rd_event = read(fd, &ev, sizeof(struct input_event));
        /* only use keypresses no key release */
        if (ev.value <= 0)
            continue;

        switch (ev.code) {
            case KEY_MUTE_TOGGLE:
                snd_mixer_selem_get_playback_switch(master_elem, 0, &muted);
                snd_mixer_selem_set_playback_switch_all(master_elem, !muted);
                
                xosd_display(osd, 0, XOSD_string, "Volume");
                if (muted) {
                    volume_display(osd, 0);
                }
                else {
                    percent = ((float)mixer_curr/mixer_max) * 100;
                    volume_display(osd, percent);
                }
                break;
            case KEY_VOL_DOWN:
                snd_mixer_selem_get_playback_volume(master_elem, 0, &mixer_curr);
                if ((mixer_curr-1) < mixer_min)
                    mixer_curr = mixer_min;
                else
                    mixer_curr--;
                snd_mixer_selem_set_playback_volume_all(master_elem, mixer_curr);
                percent = ((float)mixer_curr/mixer_max) * 100;
                volume_display(osd, percent);
                
                break;
            case KEY_VOL_UP:
                snd_mixer_selem_get_playback_volume(master_elem, 0, &mixer_curr);
                if ((mixer_curr+1) > mixer_max)
                    mixer_curr = mixer_max;
                else
                    mixer_curr++;
                snd_mixer_selem_set_playback_volume_all(master_elem, mixer_curr);
                percent = ((float)mixer_curr/mixer_max) * 100;
                volume_display(osd, percent);
                
                break;
        }
    }

    xosd_destroy(osd);
    snd_mixer_close(mixer_handle);
    close(fd);
    fclose(log_fd);

    return 0;
}
