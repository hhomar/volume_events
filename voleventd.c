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
#include <errno.h>
#include "voleventd.h"

int send_message(int client, char *msg);

static int running = -1;

void term_handler(int sig)
{
    running = -sig;
}

int
main(int argc, char **argv)
{
    int i, ev_fd, nfds, ev_event, err;
    struct pollfd *fds;
    char ev_dev[20];
    unsigned long bit[NBITS(EV_MAX)];
    struct input_event ev;
    
    pid_t pid, sid;
    int should_fork = 1;

    snd_mixer_t *mixer_handle;
    char card_id[] = "default";
    snd_mixer_elem_t *elem, *master_elem = NULL;
    long mixer_min, mixer_max, mixer_curr;
    int muted;
    int percent;
    
    FILE *pid_fd;

    int s_fd, c_fd = -1, sock_flags;
    struct sockaddr_un server;
    
    char msg[20];
    
#ifdef SIGTERM
    signal(SIGTERM, term_handler);
#endif
    signal(SIGPIPE, SIG_IGN);

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

    fds = malloc(2 * sizeof(struct pollfd));
    fds[0].fd = ev_fd;
    fds[0].events = POLLIN;
    nfds = 1;

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
        fprintf(stderr, "Couldn't create a socket: %s\n", strerror(errno));
        goto cleanup;
        return EXIT_FAILURE;
    } 
    
    fds[1].fd = s_fd;
    fds[1].events = POLLIN;
    nfds++;
    
    memset(&server, 0, sizeof(struct sockaddr_un));
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, VOLEVENTD_SOCKET);

    if (bind(s_fd, (struct sockaddr *) &server,
                sizeof(struct sockaddr_un)) < 0) {
        fprintf(stderr, "Bind failed: %s\n", strerror(errno));
        goto cleanup;
        return EXIT_FAILURE;
    }

    if (listen(s_fd, 5) < 0) {
        fprintf(stderr, "Listen failed: %s\n", strerror(errno));
        goto cleanup;
        return EXIT_FAILURE;
    }
   
    /* non-blocking */ 
    sock_flags = fcntl(s_fd, F_GETFL, 0);
    fcntl(s_fd, F_SETFL, sock_flags | O_NONBLOCK);

    running = 1;
    while (running > 0) {
        ev_event = poll(fds, nfds, 200);
       
        if (fds[i].revents & POLLIN) {
            ev_event = read(fds[0].fd, &ev, sizeof(struct input_event));
            if (ev_event != sizeof(struct input_event))
                continue;

            /* only use keypresses, no key release */
            if (ev.value <= 0)
                continue;

            switch (ev.code) {
                case KEY_MUTE_TOGGLE:
                    snd_mixer_selem_get_playback_switch(master_elem, 0, &muted);
                    snd_mixer_selem_set_playback_switch_all(master_elem, !muted);

                    if (muted) {
                        strcpy(msg, MSG_MUTE);
                    }
                    else {
                        percent = ((float)mixer_curr/mixer_max) * 100;
                        snprintf(msg, 12, "%s %i", MSG_UNMUTE, percent);
                    }
                    
                    if (send_message(c_fd, msg) == -1) {
                        close(c_fd);
                        c_fd = -1;
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
                    snprintf(msg, 14, "%s %i", MSG_VOL_DOWN, percent);
                    if (send_message(c_fd, msg) == -1) {
                        close(c_fd);
                        c_fd = -1;
                    }
                    break;
                case KEY_VOL_UP:
                    snd_mixer_selem_get_playback_volume(master_elem, 0, &mixer_curr);
                    if ((mixer_curr+1) > mixer_max)
                        mixer_curr = mixer_max;
                    else
                        mixer_curr++;
                    snd_mixer_selem_set_playback_volume_all(master_elem, mixer_curr);

                    percent = ((float)mixer_curr/mixer_max) * 100;
                    snprintf(msg, 12, "%s %i", MSG_VOL_UP, percent);
                    if (send_message(c_fd, msg) == -1) {
                        close(c_fd);
                        c_fd = -1;
                    }
                    break;
            }
        }
        if (fds[1].revents & POLLIN) {
            if ((c_fd = accept(fds[1].fd, 0, 0)) < 0) {
                fprintf(stderr, "Client accept failed: %s\n", strerror(errno));
                goto cleanup;
                return EXIT_FAILURE;
            }
        }
    }

cleanup:
    for (i = 0; i < nfds; i++) {
        close(fds[i].fd);
    }
    free(fds);
    snd_mixer_close(mixer_handle);
    close(ev_fd);
    unlink(VOLEVENTD_SOCKET);
    unlink(PID_FILE);

    return 0;
}

int
send_message(int client, char *msg)
{
    int ret;
    if (client < 0)
        return 0;
    ret = write(client, msg, strlen(msg));
    return ret;
}
