/*
 * This is D3DES (V5.09) by Richard Outerbridge with the double and
 * triple-length support removed for use in VNC.
 *
 * These changes are:
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef QCRYPTO_DESRFB_H
#define QCRYPTO_DESRFB_H

/* d3des.h -
 *
 * Headers and defines for d3des.c
 * Graven Imagery, 1992.
 *
 * Copyright (c) 1988,1989,1990,1991,1992 by Richard Outerbridge
 * (GEnie : OUTER; CIS : [71755,204])
 */

#define EN0 0
#define DE1 1

void deskey(unsigned char *, int);

void usekey(unsigned long *);

void des(unsigned char *, unsigned char *);

#endif
