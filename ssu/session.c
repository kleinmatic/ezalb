/* ssu/session.c — SessionConfig::start, loopback boot, IO sessions with
 * opener/reader/writer pump threads (crates/ssu/src/session/{mod,config,
 * io_session,loopback,pipe,exec,pty,sync}.rs). Pure polling: Rust wakers are
 * no-ops in blaze, so endpoints use only the nonblocking chan ops. */
#define _GNU_SOURCE 1

#include "ssu/ssu.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"

extern char **environ;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

void ssu_global_init(void)
{
    signal(SIGPIPE, SIG_IGN); /* Rust ignores SIGPIPE process-wide */
    signal(SIGCHLD, SIG_IGN); /* Rust parity: exec children are never reaped */
}

void session_parts_destroy(session_parts *parts)
{
    if (!parts || !parts->destroy) return;
    parts->destroy(parts);
}

/* loopback (loopback.rs): one shared unbounded queue; send never blocks.
 * `initial` is parsed but never injected (dead in Rust boot — kept dead). */

static sess_status loopback_send(void *self, uint8_t b)
{
    loopback_queue_push(self, b);
    return SESS_OK;
}

static sess_status loopback_recv(void *self, uint8_t *out)
{
    return loopback_queue_pop(self, out) ? SESS_OK : SESS_WOULD_BLOCK;
}

static void loopback_destroy(session_parts *parts)
{
    if (parts->send_self) loopback_queue_unref(parts->send_self);
    if (parts->recv_self) loopback_queue_unref(parts->recv_self);
    memset(parts, 0, sizeof *parts);
}

static int loopback_boot(session_parts *out)
{
    loopback_queue *q = loopback_queue_new();
    if (!q) return -1;
    loopback_queue_ref(q); /* one ref per endpoint */
    out->send_self = q;
    out->recv_self = q;
    out->send = loopback_send;
    out->recv = loopback_recv;
    out->destroy = loopback_destroy;
    return 0;
}

/* io sessions (io_session.rs): endpoints <-> pump threads via ssu_chan.
 * send_self/recv_self are the chans themselves. */

static sess_status io_send(void *self, uint8_t b)
{
    ssu_chan_elem e = { .byte = b, .err = 0 };
    switch (ssu_chan_try_send(self, e)) {
    case 1: return SESS_OK;
    case 0: return SESS_WOULD_BLOCK;
    default: errno = SSU_ENOTCONN; return SESS_ERR;
    }
}

static sess_status io_recv(void *self, uint8_t *out)
{
    ssu_chan_elem e;
    switch (ssu_chan_try_recv(self, &e)) {
    case 1:
        if (e.err) { errno = e.err; return SESS_ERR; } /* in-band error, FIFO order */
        *out = e.byte;
        return SESS_OK;
    case 0: return SESS_WOULD_BLOCK;
    default: errno = SSU_ENOTCONN; return SESS_ERR;
    }
}

static void io_destroy(session_parts *parts)
{
    if (parts->send_self) {
        ssu_chan_close(parts->send_self);
        ssu_chan_unref(parts->send_self);
    }
    if (parts->recv_self) {
        ssu_chan_close(parts->recv_self);
        ssu_chan_unref(parts->recv_self);
    }
    if (parts->ctl_self)
        ssu_serial_unref(parts->ctl_self);
    memset(parts, 0, sizeof *parts);
}

struct pump {
    ssu_chan *chan;
    int fd;
};

static void *reader_thread(void *arg)
{
    struct pump *p = arg;
    for (;;) {
        uint8_t b;
        ssize_t n = read(p->fd, &b, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            /* Rust parity: read_exact EOF/error travels in-band, then exit */
            ssu_chan_elem e = { .byte = 0, .err = n < 0 ? errno : EPIPE };
            if (!e.err) e.err = EIO;
            if (!ssu_chan_send(p->chan, e))
                LOG_ERRORF("Failed to send error to TX: channel closed");
            break;
        }
        ssu_chan_elem e = { .byte = b, .err = 0 };
        if (!ssu_chan_send(p->chan, e)) {
            LOG_ERRORF("Failed to send byte to RX: channel closed");
            break;
        }
    }
    ssu_chan_close(p->chan);
    ssu_chan_unref(p->chan);
    close(p->fd);
    free(p);
    return NULL;
}

static int write_all(int fd, uint8_t b)
{
    for (;;) {
        ssize_t n = write(fd, &b, 1);
        if (n == 1) return 0;
        if (n < 0 && errno == EINTR) continue;
        if (n == 0) errno = EIO; /* Rust WriteZero */
        return -1;
    }
}

static void *writer_thread(void *arg)
{
    struct pump *p = arg;
    for (;;) {
        ssu_chan_elem e;
        if (!ssu_chan_recv(p->chan, &e)) {
            LOG_ERRORF("Failed to receive byte from TX: channel closed");
            break;
        }
        if (write_all(p->fd, e.byte) != 0) {
            /* Rust parity: write_all().unwrap() kills only the pump thread */
            LOG_ERRORF("Failed to write byte: %s", strerror(errno));
            break;
        }
    }
    ssu_chan_close(p->chan);
    ssu_chan_unref(p->chan);
    close(p->fd);
    free(p);
    return NULL;
}

static int start_pump(void *(*fn)(void *), ssu_chan *chan, int fd)
{
    struct pump *p = malloc(sizeof *p);
    if (!p) return ENOMEM;
    p->chan = chan;
    p->fd = fd;
    pthread_t t;
    int rc = pthread_create(&t, NULL, fn, p);
    if (rc != 0) { free(p); return rc; }
    pthread_detach(t);
    return 0;
}

/* concrete openers; each returns 0 or an errno */

static int open_pipe(const char *path, int *rfd, int *wfd)
{
    /* Rust parity: O_RDWR FIFO never blocks on open and never EOFs (self-writer) */
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return errno;
    int fd2 = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (fd2 < 0) {
        int e = errno;
        close(fd);
        return e;
    }
    *rfd = fd;
    *wfd = fd2;
    return 0;
}

static int open_pipes(const char *rx, const char *tx, int *rfd, int *wfd)
{
    int r = open(rx, O_RDONLY | O_CLOEXEC);
    if (r < 0) return errno;
    int w = open(tx, O_WRONLY | O_CLOEXEC); /* blocks until a reader appears */
    if (w < 0) {
        int e = errno;
        close(r);
        return e;
    }
    *rfd = r;
    *wfd = w;
    return 0;
}

/* The child talks to the emulated VT420: TERM=vt420 if that entry exists,
 * else the closest ancestor that does — vt420 ships in ncurses-term and is
 * missing on stock Ubuntu/Fedora, where curses apps would refuse to start.
 * No terminfo database at all (termcap-only BSDs): keep vt420. */
static bool terminfo_has(const char *dir, const char *name)
{
    char p[PATH_MAX];
    struct stat st;

    snprintf(p, sizeof p, "%s/%c/%s", dir, name[0], name);
    if (stat(p, &st) == 0)
        return true;
    snprintf(p, sizeof p, "%s/%02x/%s", dir, (unsigned)(unsigned char)name[0], name);
    return stat(p, &st) == 0; /* macOS/ncurses hex directories */
}

static const char *child_term(void)
{
    static const char *const names[] = { "vt420", "vt220", "vt100" };
    const char *dirs[8], *env = getenv("TERMINFO"), *home = getenv("HOME");
    char home_db[PATH_MAX];
    size_t n = 0;

    if (env && *env)
        dirs[n++] = env;
    if (home) {
        snprintf(home_db, sizeof home_db, "%s/.terminfo", home);
        dirs[n++] = home_db;
    }
    dirs[n++] = "/etc/terminfo";
    dirs[n++] = "/lib/terminfo";
    dirs[n++] = "/usr/share/terminfo";
    dirs[n++] = "/usr/local/share/terminfo";
    dirs[n++] = "/usr/share/lib/terminfo";

    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        for (size_t d = 0; d < n; d++)
            if (terminfo_has(dirs[d], names[i]))
                return names[i];
    return "vt420";
}

/* The child talks to the emulated VT420, not to whatever terminal launched
 * us: hand it TERM=vt420 and drop the host terminal's identity. Sharing
 * TERM_SESSION_ID with Apple Terminal makes two login bashes write the same
 * ~/.bash_sessions file and corrupt it. Built before fork() — setenv() in the
 * child of a threaded process is not async-signal-safe. */
static char **child_env(void)
{
    static const char *const drop[] = {
        "TERM", "TERM_PROGRAM", "TERM_PROGRAM_VERSION", "TERM_SESSION_ID",
        "ITERM_SESSION_ID", "ITERM_PROFILE", "LC_TERMINAL", "LC_TERMINAL_VERSION",
        "COLORTERM", "LINES", "COLUMNS",
    };
    size_t n = 0, k = 0;
    char **env, *term;

    while (environ[n]) n++;
    if (!(env = malloc((n + 2) * sizeof *env + 32)))
        return NULL;
    term = (char *)(env + n + 2); /* the TERM= string rides along in the block */
    snprintf(term, 32, "TERM=%s", child_term());
    for (size_t i = 0; i < n; i++) {
        bool skip = false;
        for (size_t d = 0; d < sizeof drop / sizeof drop[0] && !skip; d++) {
            size_t len = strlen(drop[d]);
            skip = strncmp(environ[i], drop[d], len) == 0 && environ[i][len] == '=';
        }
        if (!skip)
            env[k++] = environ[i];
    }
    env[k++] = term;
    env[k] = NULL;
    return env;
}

static void child_exec(const char *cmd, char **env)
{
    char *argv[] = { (char *)"sh", (char *)"-c", (char *)cmd, NULL };

    execve("/bin/sh", argv, env ? env : environ);
    _exit(127);
}

static int open_exec(const char *cmd, int *rfd, int *wfd)
{
    int in[2], out[2];
    char **env = child_env();
    if (pipe(in) != 0) {
        free(env);
        return errno;
    }
    if (pipe(out) != 0) {
        int e = errno;
        close(in[0]);
        close(in[1]);
        free(env);
        return e;
    }
    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close(in[0]);
        close(in[1]);
        close(out[0]);
        close(out[1]);
        free(env);
        return e;
    }
    if (pid == 0) {
        signal(SIGPIPE, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        dup2(in[0], 0);
        dup2(out[1], 1);
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) {
            dup2(nul, 2);
            if (nul > 2) close(nul);
        }
        if (in[0] > 2) close(in[0]);
        if (in[1] > 2) close(in[1]);
        if (out[0] > 2) close(out[0]);
        if (out[1] > 2) close(out[1]);
        child_exec(cmd, env);
    }
    close(in[0]);
    close(out[1]);
    free(env);
    fcntl(in[1], F_SETFD, FD_CLOEXEC);
    fcntl(out[0], F_SETFD, FD_CLOEXEC);
    *rfd = out[0];
    *wfd = in[1];
    return 0;
}

static pthread_mutex_t ptsname_mu = PTHREAD_MUTEX_INITIALIZER;

static int open_exec_pty(const char *cmd, uint16_t rows, uint16_t cols,
                         int *rfd, int *wfd)
{
    char **env = child_env();
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        free(env);
        return errno;
    }
    fcntl(master, F_SETFD, FD_CLOEXEC);
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        int e = errno;
        close(master);
        free(env);
        return e;
    }

    char spath[128];
    pthread_mutex_lock(&ptsname_mu);
    const char *sn = ptsname(master);
    if (sn) snprintf(spath, sizeof spath, "%s", sn);
    pthread_mutex_unlock(&ptsname_mu);
    if (!sn) {
        close(master);
        free(env);
        return EIO;
    }

    int slave = open(spath, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0) {
        int e = errno;
        close(master);
        free(env);
        return e;
    }

    struct winsize ws = { .ws_row = rows, .ws_col = cols, .ws_xpixel = 0, .ws_ypixel = 0 };
    if (ioctl(master, TIOCSWINSZ, &ws) != 0) { /* resize before spawn (Rust parity) */
        int e = errno;
        close(slave);
        close(master);
        free(env);
        return e;
    }

    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close(slave);
        close(master);
        free(env);
        return e;
    }
    if (pid == 0) {
        signal(SIGPIPE, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        if (slave > 2) close(slave);
        close(master);
        child_exec(cmd, env);
    }
    close(slave);
    free(env);
    int rd = fcntl(master, F_DUPFD_CLOEXEC, 0); /* pty.try_clone: reader fd */
    if (rd < 0) {
        int e = errno;
        close(master); /* child leaks, as in Rust */
        return e;
    }
    *rfd = rd;
    *wfd = master;
    return 0;
}

struct io_boot {
    session_config_kind kind;
    char *a, *b; /* pipe: path / pipes: rx,tx / exec: command / serial: path */
    uint16_t rows, cols;
    ssu_serial *serial;              /* serial only; opener-held ref */
    int pre_rfd, pre_wfd;            /* serial only: opened before boot returns */
    ssu_chan *send_chan, *recv_chan; /* opener-held refs */
};

static void io_boot_free(struct io_boot *bt)
{
    free(bt->a);
    free(bt->b);
    if (bt->pre_rfd >= 0) close(bt->pre_rfd);
    if (bt->pre_wfd >= 0) close(bt->pre_wfd);
    if (bt->serial) ssu_serial_unref(bt->serial);
    if (bt->send_chan) ssu_chan_unref(bt->send_chan);
    if (bt->recv_chan) ssu_chan_unref(bt->recv_chan);
    free(bt);
}

static void *opener_thread(void *arg)
{
    struct io_boot *bt = arg;
    int rfd = -1, wfd = -1, err = 0;

    if (bt->pre_rfd >= 0) { /* serial: already open, boot would have failed */
        rfd = bt->pre_rfd;
        wfd = bt->pre_wfd;
        bt->pre_rfd = bt->pre_wfd = -1;
    } else switch (bt->kind) {
    case SESSION_CFG_PIPE:  err = open_pipe(bt->a, &rfd, &wfd); break;
    case SESSION_CFG_PIPES: err = open_pipes(bt->a, bt->b, &rfd, &wfd); break;
    case SESSION_CFG_EXEC:  err = open_exec(bt->a, &rfd, &wfd); break;
    default:                err = open_exec_pty(bt->a, bt->rows, bt->cols, &rfd, &wfd); break;
    }

    if (err == 0) {
        err = start_pump(reader_thread, bt->recv_chan, rfd);
        if (err == 0) {
            bt->recv_chan = NULL; /* ref + fd now owned by the pump */
            rfd = -1;
            err = start_pump(writer_thread, bt->send_chan, wfd);
            if (err == 0) {
                bt->send_chan = NULL;
                wfd = -1;
            }
        }
    }

    if (err) {
        /* Rust parity: failure is only logged; endpoints observe NotConnected */
        LOG_ERRORF("Failed to start IO session: %s", strerror(err));
        if (rfd >= 0) close(rfd);
        if (wfd >= 0) close(wfd);
    }
    if (bt->send_chan) ssu_chan_close(bt->send_chan);
    if (bt->recv_chan) ssu_chan_close(bt->recv_chan);
    io_boot_free(bt);
    return NULL;
}

static int boot_io(const session_config *cfg, session_parts *out)
{
    struct io_boot *bt = calloc(1, sizeof *bt);
    if (!bt) return -1;
    bt->kind = cfg->kind;
    bt->pre_rfd = bt->pre_wfd = -1;

    int bad = 0;
    switch (cfg->kind) {
    case SESSION_CFG_PIPE:
        bad = !(bt->a = strdup(cfg->u.pipe.path));
        break;
    case SESSION_CFG_PIPES:
        bad = !(bt->a = strdup(cfg->u.pipes.rx_path)) ||
              !(bt->b = strdup(cfg->u.pipes.tx_path));
        break;
    case SESSION_CFG_EXEC:
        bad = !(bt->a = strdup(cfg->u.exec.command));
        break;
    case SESSION_CFG_EXEC_PTY:
        bad = !(bt->a = strdup(cfg->u.exec_pty.cmd));
        bt->rows = cfg->u.exec_pty.rows;
        bt->cols = cfg->u.exec_pty.cols;
        break;
    /* Unlike the other kinds, a serial port is opened before boot returns:
     * a wrong device path is a typo the user needs to see, not a terminal
     * that silently never talks to anything. */
    case SESSION_CFG_SERIAL: {
        bad = !(bt->a = strdup(cfg->u.serial.path)) ||
              !(bt->serial = ssu_serial_new());
        int e = bad ? ENOMEM
                    : ssu_serial_open(bt->serial, bt->a, &bt->pre_rfd, &bt->pre_wfd);
        if (e) {
            if (!bad)
                LOG_ERRORF("Cannot open serial port \"%s\": %s", bt->a, strerror(e));
            io_boot_free(bt);
            return -1;
        }
        break;
    }
    default:
        bad = 1;
        break;
    }
    bt->send_chan = ssu_chan_new();
    bt->recv_chan = ssu_chan_new();
    if (bad || !bt->send_chan || !bt->recv_chan) {
        io_boot_free(bt);
        return -1;
    }

    ssu_chan_ref(bt->send_chan); /* endpoint refs; bt keeps the opener refs */
    ssu_chan_ref(bt->recv_chan);
    out->send_self = bt->send_chan;
    out->recv_self = bt->recv_chan;
    out->send = io_send;
    out->recv = io_recv;
    out->destroy = io_destroy;
    if (bt->serial) {
        ssu_serial_ref(bt->serial); /* endpoint ref; bt keeps the opener ref */
        out->ctl_self = bt->serial;
        out->set_line = ssu_serial_set_line;
        /* A real peer needs to see the terminal's XON/XOFF on the wire. */
        out->no_flow_gate = true;
    }

    pthread_t t;
    int rc = pthread_create(&t, NULL, opener_thread, bt);
    if (rc != 0) {
        /* Rust parity: boot stays Ok; endpoints observe NotConnected */
        LOG_ERRORF("Failed to start IO session: %s", strerror(rc));
        ssu_chan_close(bt->send_chan);
        ssu_chan_close(bt->recv_chan);
        io_boot_free(bt);
        return 0;
    }
    pthread_detach(t);
    return 0;
}

int session_config_start(const session_config *cfg, session_parts *out)
{
    memset(out, 0, sizeof *out);
    if (cfg->kind == SESSION_CFG_LOOPBACK) return loopback_boot(out);
    return boot_io(cfg, out);
}
