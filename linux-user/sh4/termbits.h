/* from asm/termbits.h */

#ifndef LINUX_USER_SH4_TERMBITS_H
#define LINUX_USER_SH4_TERMBITS_H

#define TARGET_NCCS 19

typedef unsigned char   target_cc_t;
typedef unsigned int    target_speed_t;
typedef unsigned int    target_tcflag_t;

struct target_termios {
    target_tcflag_t c_iflag;
    target_tcflag_t c_oflag;
    target_tcflag_t c_cflag;
    target_tcflag_t c_lflag;
    target_cc_t c_line;
    target_cc_t c_cc[TARGET_NCCS];
};


#define TARGET_VINTR 0
#define TARGET_VQUIT 1
#define TARGET_VERASE 2
#define TARGET_VKILL 3
#define TARGET_VEOF 4
#define TARGET_VTIME 5
#define TARGET_VMIN 6
#define TARGET_VSWTC 7
#define TARGET_VSTART 8
#define TARGET_VSTOP 9
#define TARGET_VSUSP 10
#define TARGET_VEOL 11
#define TARGET_VREPRINT 12
#define TARGET_VDISCARD 13
#define TARGET_VWERASE 14
#define TARGET_VLNEXT 15
#define TARGET_VEOL2 16

#define TARGET_IGNBRK  0000001
#define TARGET_BRKINT  0000002
#define TARGET_IGNPAR  0000004
#define TARGET_PARMRK  0000010
#define TARGET_INPCK   0000020
#define TARGET_ISTRIP  0000040
#define TARGET_INLCR   0000100
#define TARGET_IGNCR   0000200
#define TARGET_ICRNL   0000400
#define TARGET_IUCLC   0001000
#define TARGET_IXON    0002000
#define TARGET_IXANY   0004000
#define TARGET_IXOFF   0010000
#define TARGET_IMAXBEL 0020000
#define TARGET_IUTF8   0040000

#define TARGET_OPOST   0000001
#define TARGET_OLCUC   0000002
#define TARGET_ONLCR   0000004
#define TARGET_OCRNL   0000010
#define TARGET_ONOCR   0000020
#define TARGET_ONLRET  0000040
#define TARGET_OFILL   0000100
#define TARGET_OFDEL   0000200
#define TARGET_NLDLY   0000400
#define TARGET_NL0     0000000
#define TARGET_NL1     0000400
#define TARGET_CRDLY   0003000
#define TARGET_CR0     0000000
#define TARGET_CR1     0001000
#define TARGET_CR2     0002000
#define TARGET_CR3     0003000
#define TARGET_TABDLY  0014000
#define TARGET_TAB0    0000000
#define TARGET_TAB1    0004000
#define TARGET_TAB2    0010000
#define TARGET_TAB3    0014000
#define TARGET_XTABS   0014000
#define TARGET_BSDLY   0020000
#define TARGET_BS0     0000000
#define TARGET_BS1     0020000
#define TARGET_VTDLY   0040000
#define TARGET_VT0     0000000
#define TARGET_VT1     0040000
#define TARGET_FFDLY   0100000
#define TARGET_FF0     0000000
#define TARGET_FF1     0100000

#define TARGET_CBAUD   0010017
#define TARGET_B0      0000000
#define TARGET_B50     0000001
#define TARGET_B75     0000002
#define TARGET_B110    0000003
#define TARGET_B134    0000004
#define TARGET_B150    0000005
#define TARGET_B200    0000006
#define TARGET_B300    0000007
#define TARGET_B600    0000010
#define TARGET_B1200   0000011
#define TARGET_B1800   0000012
#define TARGET_B2400   0000013
#define TARGET_B4800   0000014
#define TARGET_B9600   0000015
#define TARGET_B19200  0000016
#define TARGET_B38400  0000017
#define TARGET_EXTA B19200
#define TARGET_EXTB B38400
#define TARGET_CSIZE   0000060
#define TARGET_CS5     0000000
#define TARGET_CS6     0000020
#define TARGET_CS7     0000040
#define TARGET_CS8     0000060
#define TARGET_CSTOPB  0000100
#define TARGET_CREAD   0000200
#define TARGET_PARENB  0000400
#define TARGET_PARODD  0001000
#define TARGET_HUPCL   0002000
#define TARGET_CLOCAL  0004000
#define TARGET_CBAUDEX 0010000
#define TARGET_B57600 0010001
#define TARGET_B115200 0010002
#define TARGET_B230400 0010003
#define TARGET_B460800 0010004
#define TARGET_B500000 0010005
#define TARGET_B576000 0010006
#define TARGET_B921600 0010007
#define TARGET_B1000000 0010010
#define TARGET_B1152000 0010011
#define TARGET_B1500000 0010012
#define TARGET_B2000000 0010013
#define TARGET_B2500000 0010014
#define TARGET_B3000000 0010015
#define TARGET_B3500000 0010016
#define TARGET_B4000000 0010017
#define TARGET_CIBAUD    002003600000
#define TARGET_CMSPAR    010000000000
#define TARGET_CRTSCTS   020000000000

#define TARGET_ISIG     0000001
#define TARGET_ICANON   0000002
#define TARGET_XCASE    0000004
#define TARGET_ECHO     0000010
#define TARGET_ECHOE    0000020
#define TARGET_ECHOK    0000040
#define TARGET_ECHONL   0000100
#define TARGET_NOFLSH   0000200
#define TARGET_TOSTOP   0000400
#define TARGET_ECHOCTL  0001000
#define TARGET_ECHOPRT  0002000
#define TARGET_ECHOKE   0004000
#define TARGET_FLUSHO   0010000
#define TARGET_PENDIN   0040000
#define TARGET_IEXTEN   0100000
#define TARGET_EXTPROC  0200000


#define TARGET_TCOOFF          0
#define TARGET_TCOON           1
#define TARGET_TCIOFF          2
#define TARGET_TCION           3

#define TARGET_TCIFLUSH    0
#define TARGET_TCOFLUSH    1
#define TARGET_TCIOFLUSH   2

#define TARGET_TCSANOW          0
#define TARGET_TCSADRAIN        1
#define TARGET_TARGET_TCSAFLUSH 2

#define TARGET_FIOCLEX         TARGET_IO('f', 1)
#define TARGET_FIONCLEX        TARGET_IO('f', 2)
#define TARGET_FIOASYNC        TARGET_IOW('f', 125, int)
#define TARGET_FIONBIO         TARGET_IOW('f', 126, int)
#define TARGET_FIONREAD        TARGET_IOR('f', 127, int)
#define TARGET_TIOCINQ         TARGET_FIONREAD
#define TARGET_FIOQSIZE        TARGET_IOR('f', 128, loff_t)
#define TARGET_TCGETS          0x5401
#define TARGET_TCSETS          0x5402
#define TARGET_TCSETSW         0x5403
#define TARGET_TCSETSF         0x5404
#define TARGET_TCGETA          TARGET_IOR('t', 23, struct termio)
#define TARGET_TIOCSWINSZ      TARGET_IOW('t', 103, struct winsize)
#define TARGET_TIOCGWINSZ      TARGET_IOR('t', 104, struct winsize)
#define TARGET_TIOCSTART       TARGET_IO('t', 110)
#define TARGET_TIOCSTOP        TARGET_IO('t', 111)
#define TARGET_TIOCOUTQ        TARGET_IOR('t', 115, int)

#define TARGET_TIOCSPGRP       TARGET_IOW('t', 118, int)
#define TARGET_TIOCGPGRP       TARGET_IOR('t', 119, int)

#define TARGET_TCSETA          TARGET_IOW('t', 24, struct termio)
#define TARGET_TCSETAW         TARGET_IOW('t', 25, struct termio)
#define TARGET_TCSETAF         TARGET_IOW('t', 28, struct termio)
#define TARGET_TCSBRK          TARGET_IO('t', 29)
#define TARGET_TCXONC          TARGET_IO('t', 30)
#define TARGET_TCFLSH          TARGET_IO('t', 31)

#define TARGET_TIOCSWINSZ      TARGET_IOW('t', 103, struct winsize)
#define TARGET_TIOCGWINSZ      TARGET_IOR('t', 104, struct winsize)
#define TARGET_TIOCSTART       TARGET_IO('t', 110)
#define TARGET_TIOCSTOP        TARGET_IO('t', 111)
#define TARGET_TIOCOUTQ        TARGET_IOR('t', 115, int)

#define TARGET_TIOCSPGRP       TARGET_IOW('t', 118, int)
#define TARGET_TIOCGPGRP       TARGET_IOR('t', 119, int)
#define TARGET_TIOCEXCL        TARGET_IO('T', 12)
#define TARGET_TIOCNXCL        TARGET_IO('T', 13)
#define TARGET_TIOCSCTTY       TARGET_IO('T', 14)

#define TARGET_TIOCSTI         TARGET_IOW('T', 18, char)
#define TARGET_TIOCMGET        TARGET_IOR('T', 21, unsigned int)
#define TARGET_TIOCMBIS        TARGET_IOW('T', 22, unsigned int)
#define TARGET_TIOCMBIC        TARGET_IOW('T', 23, unsigned int)
#define TARGET_TIOCMSET        TARGET_IOW('T', 24, unsigned int)
#define TARGET_TIOCM_LE       0x001
#define TARGET_TIOCM_DTR      0x002
#define TARGET_TIOCM_RTS      0x004
#define TARGET_TIOCM_ST       0x008
#define TARGET_TIOCM_SR       0x010
#define TARGET_TIOCM_CTS      0x020
#define TARGET_TIOCM_CAR      0x040
#define TARGET_TIOCM_RNG      0x080
#define TARGET_TIOCM_DSR      0x100
#define TARGET_TIOCM_CD       TARGET_TIOCM_CAR
#define TARGET_TIOCM_RI       TARGET_TIOCM_RNG

#define TARGET_TIOCGSOFTCAR    TARGET_IOR('T', 25, unsigned int)
#define TARGET_TIOCSSOFTCAR    TARGET_IOW('T', 26, unsigned int)
#define TARGET_TIOCLINUX       TARGET_IOW('T', 28, char)
#define TARGET_TIOCCONS        TARGET_IO('T', 29)
#define TARGET_TIOCGSERIAL     TARGET_IOR('T', 30, int)
#define TARGET_TIOCSSERIAL     TARGET_IOW('T', 31, int)
#define TARGET_TIOCPKT         TARGET_IOW('T', 32, int)
#define TARGET_TIOCPKT_DATA            0
#define TARGET_TIOCPKT_FLUSHREAD       1
#define TARGET_TIOCPKT_FLUSHWRITE      2
#define TARGET_TIOCPKT_STOP            4
#define TARGET_TIOCPKT_START           8
#define TARGET_TIOCPKT_NOSTOP         16
#define TARGET_TIOCPKT_DOSTOP         32


#define TARGET_TIOCNOTTY       TARGET_IO('T', 34)
#define TARGET_TIOCSETD        TARGET_IOW('T', 35, int)
#define TARGET_TIOCGETD        TARGET_IOR('T', 36, int)
#define TARGET_TCSBRKP         TARGET_IOW('T', 37, int)
#define TARGET_TIOCSBRK        TARGET_IO('T', 39) /* 0x5427 */ /* BSD compatibility */
#define TARGET_TIOCCBRK        TARGET_IO('T', 40) /* 0x5428 */ /* BSD compatibility */
#define TARGET_TIOCGSID        TARGET_IOR('T', 41, pid_t)
#define TARGET_TIOCGPTN        TARGET_IOR('T',0x30, unsigned int)
#define TARGET_TIOCSPTLCK      TARGET_IOW('T',0x31, int)
#define TARGET_TIOCGPTPEER     TARGET_IO('T', 0x41)


#define TARGET_TIOCSERCONFIG   TARGET_IO('T', 83)
#define TARGET_TIOCSERGWILD    TARGET_IOR('T', 84,  int)
#define TARGET_TIOCSERSWILD    TARGET_IOW('T', 85,  int)
#define TARGET_TIOCGLCKTRMIOS  0x5456
#define TARGET_TIOCSLCKTRMIOS  0x5457
#define TARGET_TIOCSERGSTRUCT  TARGET_IOR('T', 88, int)
#define TARGET_TIOCSERGETLSR   TARGET_IOR('T', 89, unsigned int)
# define TARGET_TIOCSER_TEMT   0x01
#define TARGET_TIOCSERGETMULTI TARGET_IOR('T', 90, int)
#define TARGET_TIOCSERSETMULTI TARGET_IOW('T', 91, int)

#define TARGET_TIOCMIWAIT      TARGET_IO('T', 92)
#define TARGET_TIOCGICOUNT     TARGET_IOR('T', 93, int)

#endif
