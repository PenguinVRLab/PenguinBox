/*
 * Common network MII address and register definitions.
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * Allwinner EMAC register definitions from Linux kernel are:
 *   Copyright 2012 Stefan Roese <sr@denx.de>
 *   Copyright 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 *   Copyright 1997 Sten Wang
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#ifndef MII_H
#define MII_H

#define MII_BMCR            0
#define MII_BMSR            1
#define MII_PHYID1          2
#define MII_PHYID2          3
#define MII_ANAR            4
#define MII_ANLPAR          5
#define MII_ANER            6
#define MII_ANNP            7
#define MII_ANLPRNP         8
#define MII_CTRL1000        9
#define MII_STAT1000        10
#define MII_MDDACR          13
#define MII_MDDAADR         14
#define MII_EXTSTAT         15
#define MII_NSR             16
#define MII_LBREMR          17
#define MII_REC             18
#define MII_SNRDR           19
#define MII_TEST            25

#define MII_BMCR_RESET      (1 << 15)
#define MII_BMCR_LOOPBACK   (1 << 14)
#define MII_BMCR_SPEED100   (1 << 13)
#define MII_BMCR_SPEED      MII_BMCR_SPEED100
#define MII_BMCR_AUTOEN     (1 << 12)
#define MII_BMCR_PDOWN      (1 << 11)
#define MII_BMCR_ISOLATE    (1 << 10)
#define MII_BMCR_ANRESTART  (1 << 9)
#define MII_BMCR_FD         (1 << 8)
#define MII_BMCR_CTST       (1 << 7)
#define MII_BMCR_SPEED1000  (1 << 6)

#define MII_BMSR_100T4      (1 << 15)
#define MII_BMSR_100TX_FD   (1 << 14)
#define MII_BMSR_100TX_HD   (1 << 13)
#define MII_BMSR_10T_FD     (1 << 12)
#define MII_BMSR_10T_HD     (1 << 11)
#define MII_BMSR_100T2_FD   (1 << 10)
#define MII_BMSR_100T2_HD   (1 << 9)
#define MII_BMSR_EXTSTAT    (1 << 8)
#define MII_BMSR_MFPS       (1 << 6)
#define MII_BMSR_AN_COMP    (1 << 5)
#define MII_BMSR_RFAULT     (1 << 4)
#define MII_BMSR_AUTONEG    (1 << 3)
#define MII_BMSR_LINK_ST    (1 << 2)
#define MII_BMSR_JABBER     (1 << 1)
#define MII_BMSR_EXTCAP     (1 << 0)

#define MII_ANAR_RFAULT     (1 << 13)
#define MII_ANAR_PAUSE_ASYM (1 << 11)
#define MII_ANAR_PAUSE      (1 << 10)
#define MII_ANAR_TXFD       (1 << 8)
#define MII_ANAR_TX         (1 << 7)
#define MII_ANAR_10FD       (1 << 6)
#define MII_ANAR_10         (1 << 5)
#define MII_ANAR_CSMACD     (1 << 0)
#define MII_ANAR_SELECT     (0x001f)

#define MII_ANLPAR_ACK      (1 << 14)
#define MII_ANLPAR_PAUSEASY (1 << 11)
#define MII_ANLPAR_PAUSE    (1 << 10)
#define MII_ANLPAR_T4       (1 << 9)
#define MII_ANLPAR_TXFD     (1 << 8)
#define MII_ANLPAR_TX       (1 << 7)
#define MII_ANLPAR_10FD     (1 << 6)
#define MII_ANLPAR_10       (1 << 5)
#define MII_ANLPAR_CSMACD   (1 << 0)

#define MII_ANER_NP         (1 << 2)
#define MII_ANER_NWAY       (1 << 0)

#define MII_ANNP_MP         (1 << 13)

#define MII_CTRL1000_MASTER (1 << 11)
#define MII_CTRL1000_PORT   (1 << 10)
#define MII_CTRL1000_FULL   (1 << 9)
#define MII_CTRL1000_HALF   (1 << 8)

#define MII_STAT1000_LOK    (1 << 13)
#define MII_STAT1000_ROK    (1 << 12)
#define MII_STAT1000_FULL   (1 << 11)
#define MII_STAT1000_HALF   (1 << 10)

#define MII_EXTSTAT_1000T_FD (1 << 13)
#define MII_EXTSTAT_1000T_HD (1 << 12)

#define RTL8201CP_PHYID1    0x0000
#define RTL8201CP_PHYID2    0x8201

#define SMSCLAN9118_PHYID1  0x0007
#define SMSCLAN9118_PHYID2  0xc0d1

#define RTL8211E_PHYID1     0x001c
#define RTL8211E_PHYID2     0xc915

#define DP83840_PHYID1      0x2000
#define DP83840_PHYID2      0x5c01

#define DP83848_PHYID1      0x2000
#define DP83848_PHYID2      0x5c90

#endif
