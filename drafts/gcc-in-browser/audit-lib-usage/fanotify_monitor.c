#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/fanotify.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

void print_path(int fd) {
    char proc_path[PATH_MAX];
    char actual_path[PATH_MAX];
    ssize_t len;

    snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);

    len = readlink(proc_path, actual_path, sizeof(actual_path) - 1);
    if (len < 0) {
        perror("readlink");
        printf("File path: [unknown]\n");
        return;
    }
    actual_path[len] = '\0';
    printf("%s", actual_path);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory-to-monitor>\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    int fan_fd = fanotify_init(FAN_CLASS_NOTIF | FAN_REPORT_PIDFD, O_RDONLY | O_LARGEFILE);
    if (fan_fd < 0) {
        perror("fanotify_init");
        return 1;
    }

    if (fanotify_mark(fan_fd, FAN_MARK_ADD | FAN_MARK_MOUNT,
                      FAN_OPEN | FAN_CLOSE_WRITE | FAN_EVENT_ON_CHILD, AT_FDCWD, path) < 0) {
        perror("fanotify_mark");
        return 1;
    }

    printf("Monitoring file access in: %s\n", path);
    fflush(stdout);

    struct pollfd fds[1];
    fds[0].fd = fan_fd;
    fds[0].events = POLLIN;

    while (1) {
        int poll_num = poll(fds, 1, -1);
        if (poll_num < 0) {
            perror("poll");
            break;
        }

        if (poll_num > 0 && (fds[0].revents & POLLIN)) {
            char buf[65536];
            ssize_t len = read(fan_fd, buf, sizeof(buf));
            if (len < 0) {
                perror("read");
                break;
            }

            struct fanotify_event_metadata *metadata = (struct fanotify_event_metadata *)buf;
            while (FAN_EVENT_OK(metadata, len)) {
                if (metadata->vers != FANOTIFY_METADATA_VERSION) {
                    fprintf(stderr, "Mismatch of fanotify metadata version.\n");
                    exit(EXIT_FAILURE);
                }

                if (metadata->fd >= 0) {
                    print_path(metadata->fd);
                    printf("        ");
                    if (metadata->mask & FAN_OPEN) printf("OPEN ");
                    if (metadata->mask & FAN_CLOSE_WRITE) printf("CLOSE_WRITE ");
                    printf("PID=%d FD=%d\n", metadata->pid, metadata->fd);
                    close(metadata->fd);
                    fflush(stdout);
                }

                metadata = FAN_EVENT_NEXT(metadata, len);
            }
        }
    }

    close(fan_fd);
    return 0;
}
