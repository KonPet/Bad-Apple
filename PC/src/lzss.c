/*----------------------------------------------------------------------------*/
/*--  lzss.c - LZSS coding for Nintendo GBA/DS                              --*/
/*--  Copyright (C) 2011 CUE                                                --*/
/*--  Modified by KonPet                                                    --*/
/*--                                                                        --*/
/*--  This program is free software: you can redistribute it and/or modify  --*/
/*--  it under the terms of the GNU General Public License as published by  --*/
/*--  the Free Software Foundation, either version 3 of the License, or     --*/
/*--  (at your option) any later version.                                   --*/
/*--                                                                        --*/
/*--  This program is distributed in the hope that it will be useful,       --*/
/*--  but WITHOUT ANY WARRANTY; without even the implied warranty of        --*/
/*--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          --*/
/*--  GNU General Public License for more details.                          --*/
/*--                                                                        --*/
/*--  You should have received a copy of the GNU General Public License     --*/
/*--  along with this program. If not, see <http://www.gnu.org/licenses/>.  --*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "lzss.h"

/*----------------------------------------------------------------------------*/
#define CMD_CODE_10   0x10       // LZSS magic number

#define LZS_SHIFT     1          // bits to shift
#define LZS_MASK      0x80       // bits to check:
        // ((((1 << LZS_SHIFT) - 1) << (8 - LZS_SHIFT)

#define LZS_THRESHOLD 2          // max number of bytes to not encode
#define LZS_N         0x1000     // max offset (1 << 12)
#define LZS_F         0x12       // max coded ((1 << 4) + LZS_THRESHOLD)
#define LZS_NIL       LZS_N      // index for root of binary search trees

/*----------------------------------------------------------------------------*/
char *Memory(int length, int size);

char *LZS_Fast(unsigned char *raw_buffer, int raw_len, int *new_len);
void  LZS_InitTree(void);
void  LZS_InsertNode(int r);
void  LZS_DeleteNode(int p);

/*----------------------------------------------------------------------------*/
unsigned char ring[LZS_N + LZS_F - 1];
int           dad[LZS_N + 1], lson[LZS_N + 1], rson[LZS_N + 1 + 256];
int           pos_ring, len_ring, lzs_vram;

/*----------------------------------------------------------------------------*/
#define EXIT(text)  { printf(text); exit(-1); }

/*----------------------------------------------------------------------------*/
char *Memory(int length, int size) {
	char *fb;

	fb = (char *) calloc(length, size);
	if (fb == NULL) EXIT("\nMemory error\n");

	return(fb);
}

/*----------------------------------------------------------------------------*/
char *LZS_Fast(unsigned char *raw_buffer, int raw_len, int *new_len) {
	unsigned char *pak_buffer, *pak, *raw, *raw_end, *flg;
	unsigned int   pak_len, len, r, s, len_tmp, i;
	unsigned char  mask; 

	pak_len = 4 + raw_len + ((raw_len + 7) / 8); // compressed len
	pak_buffer = (unsigned char *) Memory(pak_len, sizeof(char)); // compressed data

	*(unsigned int *)pak_buffer = CMD_CODE_10 | (raw_len << 8);

	pak = pak_buffer + 4; // pointer to packed data after header
	raw = raw_buffer; // pointer to raw data
	raw_end = raw_buffer + raw_len; // pointer to end of raw data

	LZS_InitTree();

	r = s = 0;

	len = raw_len < LZS_F ? raw_len : LZS_F;
	while (r < LZS_N - len) ring[r++] = 0;

	for (i = 0; i < len; i++) ring[r + i] = *raw++;

		LZS_InsertNode(r);

	mask = 0;

	while (len) {
		if (!(mask >>= LZS_SHIFT)) {
			*(flg = pak++) = 0;
			mask = LZS_MASK;
		}

		if (len_ring > len) len_ring = len;

		if (len_ring > LZS_THRESHOLD) {
			*flg |= mask;
			pos_ring = ((r - pos_ring) & (LZS_N - 1)) - 1;
			*pak++ = ((len_ring - LZS_THRESHOLD - 1) << 4) | (pos_ring >> 8);
			*pak++ = pos_ring & 0xFF;
		} else {
			len_ring = 1;
			*pak++ = ring[r];
		}

		len_tmp = len_ring;
		for (i = 0; i < len_tmp; i++) {
			if (raw == raw_end) break;
			LZS_DeleteNode(s);
			ring[s] = *raw++;
			if (s < LZS_F - 1) ring[s + LZS_N] = ring[s];
			s = (s + 1) & (LZS_N - 1);
			r = (r + 1) & (LZS_N - 1);
			LZS_InsertNode(r);
		}
		while (i++ < len_tmp) {
			LZS_DeleteNode(s);
			s = (s + 1) & (LZS_N - 1);
			r = (r + 1) & (LZS_N - 1);
			if (--len) LZS_InsertNode(r);
		}
	}

	*new_len = pak - pak_buffer;

	return(pak_buffer);
}

/*----------------------------------------------------------------------------*/
void LZS_InitTree(void) {
	int i;

	for (i = LZS_N + 1; i <= LZS_N + 256; i++)
		rson[i] = LZS_NIL;

	for (i = 0; i < LZS_N; i++)
		dad[i] = LZS_NIL;
}

/*----------------------------------------------------------------------------*/
void LZS_InsertNode(int r) {
	unsigned char *key;
	int            i, p, cmp, prev;

	prev = (r - 1) & (LZS_N - 1);

	cmp = 1;
	len_ring = 0;

	key = &ring[r];
	p = LZS_N + 1 + key[0];

	rson[r] = lson[r] = LZS_NIL;

	for ( ; ; ) {
		if (cmp >= 0) {
			if (rson[p] != LZS_NIL) p = rson[p];
			else                  { rson[p] = r; dad[r] = p; return; }
		} else {
			if (lson[p] != LZS_NIL) p = lson[p];
			else                  { lson[p] = r; dad[r] = p; return; }
		}

		for (i = 1; i < LZS_F; i++)
			if ((cmp = key[i] - ring[p + i])) break;

		if (i > len_ring) {
			if (!lzs_vram || (p != prev)) {
				pos_ring = p;
				if ((len_ring = i) == LZS_F) break;
			}
		}
	}

	dad[r] = dad[p]; lson[r] = lson[p]; rson[r] = rson[p];

	dad[lson[p]] = r; dad[rson[p]] = r;

	if (rson[dad[p]] == p) rson[dad[p]] = r;
	else                   lson[dad[p]] = r;

	dad[p] = LZS_NIL;
}

/*----------------------------------------------------------------------------*/
void LZS_DeleteNode(int p) {
	int q;

	if (dad[p] == LZS_NIL) return;

	if (rson[p] == LZS_NIL) {
		q = lson[p];
	} else if (lson[p] == LZS_NIL) {
		q = rson[p];
	} else {
		q = lson[p];
		if (rson[q] != LZS_NIL) {
			do {
				q = rson[q];
			} while (rson[q] != LZS_NIL);

			rson[dad[q]] = lson[q]; dad[lson[q]] = dad[q];
			lson[q]      = lson[p]; dad[lson[p]] = q;
		}

		rson[q] = rson[p]; dad[rson[p]] = q;
	}

	dad[q] = dad[p];

	if (rson[dad[p]] == p) rson[dad[p]] = q;
	else                   lson[dad[p]] = q;

	dad[p] = LZS_NIL;
}

/*----------------------------------------------------------------------------*/
/*--  EOF                                           Copyright (C) 2011 CUE  --*/
/*----------------------------------------------------------------------------*/
