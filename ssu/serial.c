/* ssu/serial.c — host serial port session (no Rust counterpart).
 *
 * Opens a real tty (/dev/cu.* on macOS, /dev/ttyUSB*|/dev/ttyS* on Linux,
 * /dev/cuaU*|/dev/cuau* on the BSDs) in raw mode and keeps a control fd so
 * the line can be retuned while running: the VT420 firmware programs baud
 * and framing into the DUART's CSR/MR registers whenever Set-Up changes,
 * and machine/duart.c publishes the decoded settings through here. */
#define _GNU_SOURCE 1

#include "ssu/ssu.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* macOS: arbitrary (non-table) rates go through IOSSIOSPEED after tcsetattr.
 * Declaring the ioctl here avoids a dependency on <IOKit/serial/ioss.h>. */
#if defined(__APPLE__) && !defined(IOSSIOSPEED)
#define IOSSIOSPEED _IOW('T', 2, speed_t)
#endif

/* On the BSDs and macOS speed_t is the literal rate, so anything missing
 * from the table still works; glibc/musl need a Bxxx constant. */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__) || defined(__DragonFly__)
#define SPEED_T_IS_RATE 1
#endif

#define SPEED(rate, code) { (rate), (code) },

static const struct { uint32_t rate; speed_t code; } SPEEDS[] = {
    SPEED(50, B50) SPEED(75, B75) SPEED(110, B110) SPEED(134, B134)
    SPEED(150, B150) SPEED(200, B200) SPEED(300, B300) SPEED(600, B600)
    SPEED(1200, B1200) SPEED(1800, B1800) SPEED(2400, B2400)
    SPEED(4800, B4800) SPEED(9600, B9600) SPEED(19200, B19200)
    SPEED(38400, B38400)
#ifdef B7200
    SPEED(7200, B7200)
#endif
#ifdef B14400
    SPEED(14400, B14400)
#endif
#ifdef B28800
    SPEED(28800, B28800)
#endif
#ifdef B57600
    SPEED(57600, B57600)
#endif
#ifdef B76800
    SPEED(76800, B76800)
#endif
#ifdef B115200
    SPEED(115200, B115200)
#endif
#ifdef B230400
    SPEED(230400, B230400)
#endif
#ifdef B460800
    SPEED(460800, B460800)
#endif
#ifdef B500000
    SPEED(500000, B500000)
#endif
#ifdef B921600
    SPEED(921600, B921600)
#endif
#ifdef B1000000
    SPEED(1000000, B1000000)
#endif
#ifdef B1500000
    SPEED(1500000, B1500000)
#endif
#ifdef B2000000
    SPEED(2000000, B2000000)
#endif
#ifdef B3000000
    SPEED(3000000, B3000000)
#endif
#ifdef B4000000
    SPEED(4000000, B4000000)
#endif
};

#undef SPEED

/* What the port runs at until the firmware programs the DUART. The VT420's
 * factory NVR says the same, so in practice nothing changes at handover. */
static const line_params BOOT_LINE = {
    .baud = 9600, .data_bits = 8, .stop_bits = 1, .parity = 'N'
};

struct ssu_serial {
    pthread_mutex_t mu;
    int         fd; /* control fd; -1 until open succeeds */
    int         refs;
    line_params cur;
};

/* Sets the rate in *t, or reports it needs the platform escape hatch. */
static int set_speed(struct termios *t, uint32_t baud, bool *needs_ioctl)
{
    *needs_ioctl = false;
    for (size_t i = 0; i < sizeof SPEEDS / sizeof *SPEEDS; i++) {
        if (SPEEDS[i].rate != baud)
            continue;
        if (cfsetispeed(t, SPEEDS[i].code) != 0 || cfsetospeed(t, SPEEDS[i].code) != 0)
            return errno;
        return 0;
    }
#ifdef SPEED_T_IS_RATE
    if (cfsetispeed(t, (speed_t)baud) != 0 || cfsetospeed(t, (speed_t)baud) != 0)
        return errno;
#ifdef __APPLE__
    *needs_ioctl = true; /* the driver may still reject it; IOSSIOSPEED won't */
#endif
    return 0;
#else
    return EINVAL;
#endif
}

/* Applies p to an already-open tty. No host-side flow control: the terminal
 * runs XON/XOFF itself in firmware, and those bytes belong on the wire. */
static int apply(int fd, const line_params *p)
{
    struct termios t;
    bool needs_ioctl;
    int err;

    if (tcgetattr(fd, &t) != 0)
        return errno;
    cfmakeraw(&t);
    t.c_cflag |= CLOCAL | CREAD; /* ignore modem lines; a hangup must not EOF us */
    t.c_cflag &= (tcflag_t)~CSIZE;
    switch (p->data_bits) {
    case 5:  t.c_cflag |= CS5; break;
    case 6:  t.c_cflag |= CS6; break;
    case 7:  t.c_cflag |= CS7; break;
    default: t.c_cflag |= CS8; break;
    }
    if (p->stop_bits >= 2)
        t.c_cflag |= CSTOPB;
    else
        t.c_cflag &= (tcflag_t)~CSTOPB;
    t.c_cflag &= (tcflag_t)~(PARENB | PARODD);
    if (p->parity == 'E' || p->parity == 'O')
        t.c_cflag |= PARENB;
    if (p->parity == 'O')
        t.c_cflag |= PARODD;

    t.c_iflag &= (tcflag_t)~(IXON | IXOFF | IXANY);
#ifdef CRTSCTS
    t.c_cflag &= (tcflag_t)~CRTSCTS;
#endif

    t.c_cc[VMIN] = 1; /* pump threads read one byte at a time, blocking */
    t.c_cc[VTIME] = 0;

    if ((err = set_speed(&t, p->baud, &needs_ioctl)) != 0)
        return err;
    if (tcsetattr(fd, TCSANOW, &t) != 0) {
#ifdef __APPLE__
        if (!needs_ioctl)
            return errno;
#else
        return errno;
#endif
    }
#if defined(__APPLE__) && defined(IOSSIOSPEED)
    if (needs_ioctl) {
        speed_t s = (speed_t)p->baud;
        if (ioctl(fd, IOSSIOSPEED, &s) != 0)
            return errno;
    }
#endif
    return 0;
}

ssu_serial *ssu_serial_new(void)
{
    ssu_serial *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    pthread_mutex_init(&s->mu, NULL);
    s->fd = -1;
    s->refs = 1;
    return s;
}

void ssu_serial_ref(ssu_serial *s)
{
    pthread_mutex_lock(&s->mu);
    s->refs++;
    pthread_mutex_unlock(&s->mu);
}

void ssu_serial_unref(ssu_serial *s)
{
    pthread_mutex_lock(&s->mu);
    bool last = --s->refs == 0;
    int fd = -1;
    if (last) {
        fd = s->fd; /* the control fd outlives the pump threads' dups */
        s->fd = -1;
    }
    pthread_mutex_unlock(&s->mu);
    if (!last)
        return;
    if (fd >= 0)
        close(fd);
    pthread_mutex_destroy(&s->mu);
    free(s);
}

int ssu_serial_open(ssu_serial *s, const char *path, int *rfd, int *wfd)
{
    /* O_NDELAY so a /dev/tty* without carrier does not block forever, and
     * O_NOCTTY so the port never becomes our controlling terminal. The flag
     * belongs to the description, so clearing it covers all three fds. */
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    int r = -1, w = -1, err;

    if (fd < 0)
        return errno;
    if (!isatty(fd)) {
        close(fd);
        return ENOTTY;
    }
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK) != 0)
        goto fail;
    if ((err = apply(fd, &BOOT_LINE)) != 0) {
        close(fd);
        return err;
    }
    tcflush(fd, TCIOFLUSH);

    if ((r = fcntl(fd, F_DUPFD_CLOEXEC, 0)) < 0)
        goto fail;
    if ((w = fcntl(fd, F_DUPFD_CLOEXEC, 0)) < 0)
        goto fail;

    pthread_mutex_lock(&s->mu);
    s->fd = fd;
    s->cur = BOOT_LINE;
    pthread_mutex_unlock(&s->mu);
    *rfd = r;
    *wfd = w;
    return 0;
fail:
    err = errno;
    if (r >= 0)
        close(r);
    close(fd);
    return err;
}

int ssu_serial_set_line(void *self, const line_params *p)
{
    ssu_serial *s = self;
    int err = 0;

    pthread_mutex_lock(&s->mu);
    if (s->fd < 0 || line_params_eq(&s->cur, p)) {
        pthread_mutex_unlock(&s->mu);
        return 0;
    }
    if ((err = apply(s->fd, p)) == 0)
        s->cur = *p;
    pthread_mutex_unlock(&s->mu);

    if (err)
        LOG_ERRORF("Serial port: cannot set %u %u%c%u: %s", p->baud,
                   p->data_bits, p->parity, p->stop_bits, strerror(err));
    else
        LOG_INFOF("Serial port set to %u %u%c%u", p->baud, p->data_bits,
                  p->parity, p->stop_bits);
    return err;
}
