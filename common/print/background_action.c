/* SPDX-License-Identifier: MPL-2.0 */
#include "background_action.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int open_action_file(const char *path, int access_mode)
{
    int fd = open(path, O_CREAT | O_NOFOLLOW | O_NONBLOCK | access_mode, 0600);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_uid != geteuid() || st.st_nlink != 1) {
        close(fd);
        return -1;
    }
    return fd;
}

int deneb_background_action_running(const char *lock_path)
{
    int fd = open_action_file(lock_path, O_RDWR);
    if (fd < 0)
        return 1; /* Unknown ownership fails closed. */
    int busy = flock(fd, LOCK_EX | LOCK_NB) != 0;
    close(fd);
    return busy;
}

int deneb_background_action_start(const char *lock_path, const char *command,
                                   const char *log_path, pid_t *pid)
{
    if (!lock_path || !command || !log_path || !pid)
        return -1;
    int fd = open_action_file(lock_path, O_RDWR);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return -1;
    }
    int log_fd = open_action_file(log_path, O_WRONLY);
    if (log_fd < 0 || ftruncate(log_fd, 0) != 0) {
        if (log_fd >= 0)
            close(log_fd);
        close(fd);
        return -1;
    }
    pid_t child = fork();
    if (child == 0) {
        if (dup2(log_fd, STDOUT_FILENO) < 0 || dup2(log_fd, STDERR_FILENO) < 0)
            _exit(126);
        close(log_fd);
        /* fd intentionally remains inherited to own the operation lock. */
        execl("/bin/sh", "sh", "-c", command, (char *)0);
        _exit(127);
    }
    close(log_fd);
    close(fd);
    if (child < 0)
        return -1;
    *pid = child;
    return 0;
}

int deneb_background_action_poll(pid_t *pid)
{
    int status;
    if (!pid || *pid <= 0)
        return -1;
    pid_t rc = waitpid(*pid, &status, WNOHANG);
    if (rc == 0 || (rc < 0 && errno == EINTR))
        return 0;
    *pid = 0;
    return rc > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 1 : -1;
}
