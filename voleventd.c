#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <sys/time.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <alsa/asoundlib.h>
#include <errno.h>
#include "voleventd.h"

struct master_mixer {
    int muted;
    long volume, max, min;
    int client; /* FIXME: this wouldn't work with multiple clients */
};

int send_message(int client, int event, struct master_mixer *mm);
int mute_toogle(snd_mixer_elem_t *elem, int status);
int mixer_elem_event(snd_mixer_elem_t *elem, unsigned int mask);

static int running = -1;

void term_handler(int sig)
{
    running = -sig;
}

int
main(int argc, char **argv)
{
    int i, ev_fd, nfds, poll_event, err;
    struct pollfd *fds;
    char ev_dev[20];
    unsigned long bit[NBITS(EV_MAX)];
    struct input_event ev;
    
    pid_t pid, sid;
    int should_fork = 1;

    char card_id[] = "default";
    snd_mixer_elem_t *elem, *master_elem = NULL;
    snd_mixer_t *mixer_handle;
    
    FILE *pid_fd;

    int s_fd, c_fd = -1, sock_flags;
    struct sockaddr_un server;
    
#ifdef SIGTERM
    signal(SIGTERM, term_handler);
#endif
    signal(SIGPIPE, SIG_IGN);

    if (argc > 1) {
        if (strncmp(argv[1], "-nofork", 7) == 0)
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
    
    struct master_mixer *mm = malloc(sizeof(struct master_mixer));
    mm->client = -1;
    snd_mixer_selem_get_playback_volume_range(master_elem, &mm->min, &mm->max);
    snd_mixer_selem_get_playback_volume(master_elem, 0, &mm->volume);
    snd_mixer_selem_get_playback_switch(master_elem, 0, &mm->muted);
    
    snd_mixer_elem_set_callback(master_elem, mixer_elem_event);
    snd_mixer_elem_set_callback_private(master_elem, mm);

    fds = malloc(3 * sizeof(struct pollfd));
    nfds = snd_mixer_poll_descriptors(mixer_handle, fds, 1);

    /* find keyboard event handler */
    int found_keyboard = 0;
    for (i = 0; i < 10; i++) {
        snprintf(ev_dev, 20, "/dev/input/event%d", i);
        if ((ev_fd = open(ev_dev, O_RDONLY)) >= 0) {
            ioctl(ev_fd, EVIOCGBIT(0, EV_MAX), bit);

	   // only want to watch keyboard events
            if (test_bit(EV_KEY, bit) && test_bit(EV_REP, bit)) {
		/* FIXME: this is a bogus check, it checks to see if the
		 * device has an LED bit set.
		 * Normal keyboards would have this set.
		 * Multimedia keys in another event stream won't have
		 * this bit set.
		 * There needs to be a proper check to handle both
		 * cases. */
		if (test_bit(EV_LED, bit)) {	    
		   found_keyboard = 1;
		   if (!should_fork)
		     printf("Found keyboard at \%s\n", ev_dev);
	           break;
		}
	       
            }
        }   
    }

    if (!found_keyboard) {
        fprintf(stderr, "Couldn't find a keyboard device. "
                "Possibly permission problems\n");
        return EXIT_FAILURE;
    }

    fds[1].fd = ev_fd;
    fds[1].events = POLLIN;
    nfds++;

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
    }
    fprintf(pid_fd, "%d\n", getpid());
    fclose(pid_fd);

    if ((s_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Couldn't create a socket: %s\n", strerror(errno));
        goto cleanup;
    } 
    
    fds[2].fd = s_fd;
    fds[2].events = POLLIN;
    nfds++;
    
    memset(&server, 0, sizeof(struct sockaddr_un));
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, VOLEVENTD_SOCKET);

    if (bind(s_fd, (struct sockaddr *) &server,
                sizeof(struct sockaddr_un)) < 0) {
        fprintf(stderr, "Bind failed: %s\n", strerror(errno));
        goto cleanup;
    }

    if (listen(s_fd, 5) < 0) {
        fprintf(stderr, "Listen failed: %s\n", strerror(errno));
        goto cleanup;
    }
   
    /* non-blocking */ 
    sock_flags = fcntl(s_fd, F_GETFL, 0);
    fcntl(s_fd, F_SETFL, sock_flags | O_NONBLOCK);
    
    struct group *grp = getgrnam("audio");
    if (!grp) {
	fprintf(stderr, "Couldn't find group 'audio'\n");
	goto cleanup;
    }
    chown(VOLEVENTD_SOCKET, -1, grp->gr_gid);
    chmod(VOLEVENTD_SOCKET, SOCKET_PERMISSIONS);

    running = 1;
    while (running > 0) {
        poll_event = poll(fds, nfds, 200);

        if (fds[0].revents & POLLIN) {
	   snd_mixer_handle_events(mixer_handle);
        }
        if (fds[1].revents & POLLIN) {
            poll_event = read(fds[1].fd, &ev, sizeof(struct input_event));
            if (poll_event != sizeof(struct input_event))
                continue;

            /* only check for keypresses. Don't care about key release */
            if (ev.value <= 0)
                continue;

            switch (ev.code) {
                case KEY_MUTE_TOGGLE:
                    mm->muted = mute_toogle(master_elem, mm->muted);
                    
                    c_fd = send_message(c_fd, KEY_MUTE_TOGGLE, mm);
                                        
                    break;
                case KEY_VOL_DOWN:
                    snd_mixer_selem_get_playback_volume(master_elem, 0, &mm->volume);
                    if ((mm->volume-1) < mm->min)
                        mm->volume = mm->min;
                    else
                        mm->volume--;
                    snd_mixer_selem_set_playback_volume_all(master_elem, mm->volume);

                    c_fd = send_message(c_fd, KEY_VOL_DOWN, mm);

                    break;
                case KEY_VOL_UP:
                    snd_mixer_selem_get_playback_volume(master_elem, 0, &mm->volume);
                    if ((mm->volume+1) > mm->max)
                        mm->volume = mm->max;
                    else
                        mm->volume++;
                    snd_mixer_selem_set_playback_volume_all(master_elem, mm->volume);
                    
                    c_fd = send_message(c_fd, KEY_VOL_UP, mm);

                    break;
            }
        }
        if (fds[2].revents & POLLIN) {
            mm->client = c_fd = accept(fds[2].fd, 0, 0);
            if (c_fd < 0) {
                fprintf(stderr, "Client accept failed: %s\n", strerror(errno));
                goto cleanup;
            }
        }
    }

cleanup:

    for (i = 0; i < nfds; i++) {
        close(fds[i].fd);
    }
    
    free(fds);
    free(mm);
    free(mixer_handle);
    close(ev_fd);
    unlink(VOLEVENTD_SOCKET);
    unlink(PID_FILE);

    return 0;
}

int
send_message(int client, int event, struct master_mixer *mm)
{
    char msg[20];
    int percent;
    
    if (client < 0)
        return -1;

    switch (event) {
        case KEY_VOL_DOWN:
            percent = ((float)mm->volume/mm->max) * 100;
            snprintf(msg, 16, "%s %i %i", MSG_VOL_DOWN, percent, mm->muted);

            break;
        case KEY_VOL_UP:
            percent = ((float)mm->volume/mm->max) * 100;
            snprintf(msg, 14, "%s %i %i", MSG_VOL_UP, percent, mm->muted);

            break;
        case KEY_MUTE_TOGGLE:
            if (mm->muted) {
                percent = ((float)mm->volume/mm->max) * 100;
                snprintf(msg, 14, "%s %i %i", MSG_UNMUTE, percent, mm->muted);
            }
            else {
                percent = ((float)mm->volume/mm->max) * 100;
                snprintf(msg, 12, "%s %i %i", MSG_MUTE, percent, mm->muted);
            }

            break;
    }

    if (write(client, msg, strlen(msg)) == -1) {
        close(client);
        mm->client = -1;
        return -1;
    }
    
    return client;
}

int
mute_toogle(snd_mixer_elem_t *elem, int status)
{
    status = !status;
    snd_mixer_selem_set_playback_switch_all(elem, status);
    return status;
}

int
mixer_elem_event(snd_mixer_elem_t *elem, unsigned int mask)
{
    int s;
    long v;

    if ((strcmp(snd_mixer_selem_get_name(elem), "Master") != 0) &&
        (mask & SND_CTL_EVENT_MASK_VALUE))
        return 0;

    struct master_mixer *mm = snd_mixer_elem_get_callback_private(elem);
    snd_mixer_selem_get_playback_switch(elem, 0, &s);
    snd_mixer_selem_get_playback_volume(elem, 0, &v);

    if (s != mm->muted) {
        mm->muted = s;
        send_message(mm->client, KEY_MUTE_TOGGLE, mm);
    }
    else if (v < mm->volume) {
        mm->volume = v;
        send_message(mm->client, KEY_VOL_DOWN, mm);
    }
    else if (v > mm->volume) {
        mm->volume = v;
        send_message(mm->client, KEY_VOL_UP, mm);
    }

    return 0;
}
