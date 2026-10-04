//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/* $Header: /CounterStrike/SHA.CPP 1     3/03/97 10:25a Joe_bostic $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : SHA.CPP                                                      *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : 07/03/96                                                     *
 *                                                                                             *
 *                  Last Update : July 3, 1996 [JLB]                                           *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   SHAEngine::Result -- Fetch the current digest.                                            *
 *   SHAEngine::Hash -- Process an arbitrarily long data block.                                *
 *   SHAEngine::Process_Partial -- Helper routine to process any partially accumulated data blo*
 *   SHAEngine::Process_Block -- Process a full data block into the hash accumulator.          *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include <stdlib.h>
#include <string.h>
//#include	<iostream.h>
#include "sha.h"
#include "endianness.h"
#include "memrev.h"
#include "rotates.h"
#include <algorithm>

/***********************************************************************************************
 * SHAEngine::Process_Partial -- Helper routine to process any partially accumulated data bloc *
 *                                                                                             *
 *    This routine will see if there is a partial block already accumulated in the holding     *
 *    buffer. If so, then the data is fetched from the source such that a full buffer is       *
 *    accumulated and then processed. If there is insufficient data to fill the buffer, then   *
 *    it accumulates what data it can and then returns so that this routine can be called      *
 *    again later.                                                                             *
 *                                                                                             *
 * INPUT:   data  -- Reference to a pointer to the data. This pointer will be modified if      *
 *                   this routine consumes any of the data in the buffer.                      *
 *                                                                                             *
 *          length-- Reference to the length of the data available. If this routine consumes   *
 *                   any of the data, then this length value will be modified.                 *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/03/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
void SHAEngine::Process_Partial(void const*& data, int32_t& length)
{
    if (length == 0 || data == NULL)
        return;

    /*
    **	If there is no partial buffer and the source is greater than
    **	a source block size, then partial processing is unnecessary.
    **	Bail out in this case.
    */
    if (PartialCount == 0 && length >= SRC_BLOCK_SIZE)
        return;

    /*
    **	Attach as many bytes as possible from the source data into
    **	the staging buffer.
    */
    int add_count = std::min((int)length, (int)SRC_BLOCK_SIZE - PartialCount);
    memcpy(&Partial[PartialCount], data, add_count);
    data = ((char const*&)data) + add_count;
    PartialCount += add_count;
    length -= add_count;

    /*
    **	If a full staging buffer has been accumulated, then process
    **	the staging buffer and then bail.
    */
    if (PartialCount == SRC_BLOCK_SIZE) {
        Process_Block(&Partial[0], Acc);
        Length += (int32_t)SRC_BLOCK_SIZE;
        PartialCount = 0;
    }
}

/***********************************************************************************************
 * SHAEngine::Hash -- Process an arbitrarily long data block.                                  *
 *                                                                                             *
 *    This is the main access routine to the SHA engine. It will take the arbitrarily long     *
 *    data block and process it. The hash value is accumulated with any previous calls to      *
 *    this routine.                                                                            *
 *                                                                                             *
 * INPUT:   data     -- Pointer to the data block to process.                                  *
 *                                                                                             *
 *          length   -- The number of bytes to process.                                        *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/03/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
void SHAEngine::Hash(void const* data, int32_t length)
{
    IsCached = false;

    /*
    **	Check for and handle any smaller-than-512bit blocks. This can
    **	result in all of the source data submitted to this routine to be
    **	consumed at this point.
    */
    Process_Partial(data, length);

    /*
    **	If there is no more source data to process, then bail. Speed reasons.
    */
    if (length == 0)
        return;

    /*
    **	First process all the whole blocks available in the source data.
    */
    int32_t blocks = (length / SRC_BLOCK_SIZE);
    int32_t const* source = (int32_t const*)data;
    for (int bcount = 0; bcount < blocks; bcount++) {
        Process_Block(source, Acc);
        Length += (int32_t)SRC_BLOCK_SIZE;
        source += SRC_BLOCK_SIZE / sizeof(int32_t);
        length -= (int32_t)SRC_BLOCK_SIZE;
    }

    /*
    **	Process any remainder bytes. This data is stored in the source
    **	accumulator buffer for future processing.
    */
    data = source;
    Process_Partial(data, length);
}

/***********************************************************************************************
 * SHAEngine::Result -- Fetch the current digest.                                              *
 *                                                                                             *
 *    This routine will return the digest as it currently stands.                              *
 *                                                                                             *
 * INPUT:   pointer  -- Pointer to the buffer that will hold the digest -- 20 bytes.           *
 *                                                                                             *
 * OUTPUT:  Returns with the number of bytes copied into the buffer. This will always be       *
 *          20.                                                                                *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/03/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
int SHAEngine::Result(void* result) const
{
    /*
    **	If the final hash result has already been calculated for the
    **	current data state, then immediately return with the precalculated
    **	value.
    */
    if (IsCached) {
        memcpy(result, &FinalResult, sizeof(FinalResult));
    }

    int32_t length = Length + PartialCount;
    int partialcount = PartialCount;
    char partial[SRC_BLOCK_SIZE];
    memcpy(partial, Partial, sizeof(Partial));

    /*
    **	Cap the end of the source data stream with a 1 bit.
    */
    partial[partialcount] = (char)0x80;

    /*
    **	Determine if there is insufficient room to append the
    **	data length number to the hash source. If not, then
    **	fill out the rest of the accumulator and flush it to
    **	the hash so that there will be room for the final
    **	count value.
    */
    SHADigest acc = Acc;
    if ((SRC_BLOCK_SIZE - partialcount) < 9) {
        if (partialcount + 1 < SRC_BLOCK_SIZE) {
            memset(&partial[partialcount + 1], '\0', SRC_BLOCK_SIZE - (partialcount + 1));
        }
        Process_Block(&partial[0], acc);
        partialcount = 0;
    } else {
        partialcount++;
    }

    /*
    **	Put the length of the source data as a 64 bit integer in the
    **	last 8 bytes of the pseudo-source data.
    */
    memset(&partial[partialcount], '\0', SRC_BLOCK_SIZE - partialcount);
    int32_t bitlength = htobe32((length * 8));
    memcpy(&partial[SRC_BLOCK_SIZE - 4], &bitlength, sizeof(bitlength)); // partial may be unaligned.
    Process_Block(&partial[0], acc);

    memcpy((char*)&FinalResult, &acc, sizeof(acc));
    for (int index = 0; index < sizeof(FinalResult) / sizeof(int32_t); index++) {
        //	for (int index = 0; index < SRC_BLOCK_SIZE/sizeof(int32_t); index++) {
        (int32_t&)FinalResult.Long[index] = htobe32(FinalResult.Long[index]);
    }
    (bool&)IsCached = true;
    memcpy(result, &FinalResult, sizeof(FinalResult));
    return (sizeof(FinalResult));
}

/***********************************************************************************************
 * SHAEngine::Process_Block -- Process a full data block into the hash accumulator.            *
 *                                                                                             *
 *    This helper routine is called when a full block of data is available for processing      *
 *    into the hash.                                                                           *
 *                                                                                             *
 * INPUT:   source   -- Pointer to the block of data to process.                               *
 *                                                                                             *
 *          acc      -- Reference to the hash accumulator that this hash step will be          *
 *                      accumulated into.                                                      *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/03/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
void SHAEngine::Process_Block(void const* source, SHADigest& acc) const
{
    /*
    ** Standard SHA-1, unrolled, with the state in locals and a rolling 16-word
    ** schedule: several times faster than going round one loop 80 times choosing the
    ** function and constant each time, which mattered on a StrongARM, where checking
    ** the digests of the mixfiles cached at start-up (14 MB in Red Alert) took 4 s.
    */
    uint32_t w[16];
    unsigned char const* bytes = static_cast<unsigned char const*>(source); // may be unaligned
    for (int index = 0; index < 16; index++) {
        w[index] = uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
        bytes += 4;
    }

#define SHA_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define SHA_W(t)                                                                                              \
    (w[(t)&15] = SHA_ROL(w[((t) + 13) & 15] ^ w[((t) + 8) & 15] ^ w[((t) + 2) & 15] ^ w[(t)&15], 1))
#define R0(a, b, c, d, e, t)                                                                                  \
    e += (d ^ (b & (c ^ d))) + w[t] + 0x5a827999u + SHA_ROL(a, 5);                                            \
    b = SHA_ROL(b, 30);
#define R1(a, b, c, d, e, t)                                                                                  \
    e += (d ^ (b & (c ^ d))) + SHA_W(t) + 0x5a827999u + SHA_ROL(a, 5);                                        \
    b = SHA_ROL(b, 30);
#define R2(a, b, c, d, e, t)                                                                                  \
    e += (b ^ c ^ d) + SHA_W(t) + 0x6ed9eba1u + SHA_ROL(a, 5);                                                \
    b = SHA_ROL(b, 30);
#define R3(a, b, c, d, e, t)                                                                                  \
    e += (((b | c) & d) | (b & c)) + SHA_W(t) + 0x8f1bbcdcu + SHA_ROL(a, 5);                                  \
    b = SHA_ROL(b, 30);
#define R4(a, b, c, d, e, t)                                                                                  \
    e += (b ^ c ^ d) + SHA_W(t) + 0xca62c1d6u + SHA_ROL(a, 5);                                                \
    b = SHA_ROL(b, 30);

    uint32_t a = uint32_t(acc.Long[0]);
    uint32_t b = uint32_t(acc.Long[1]);
    uint32_t c = uint32_t(acc.Long[2]);
    uint32_t d = uint32_t(acc.Long[3]);
    uint32_t e = uint32_t(acc.Long[4]);

    R0(a, b, c, d, e, 0);
    R0(e, a, b, c, d, 1);
    R0(d, e, a, b, c, 2);
    R0(c, d, e, a, b, 3);
    R0(b, c, d, e, a, 4);
    R0(a, b, c, d, e, 5);
    R0(e, a, b, c, d, 6);
    R0(d, e, a, b, c, 7);
    R0(c, d, e, a, b, 8);
    R0(b, c, d, e, a, 9);
    R0(a, b, c, d, e, 10);
    R0(e, a, b, c, d, 11);
    R0(d, e, a, b, c, 12);
    R0(c, d, e, a, b, 13);
    R0(b, c, d, e, a, 14);
    R0(a, b, c, d, e, 15);
    R1(e, a, b, c, d, 16);
    R1(d, e, a, b, c, 17);
    R1(c, d, e, a, b, 18);
    R1(b, c, d, e, a, 19);
    R2(a, b, c, d, e, 20);
    R2(e, a, b, c, d, 21);
    R2(d, e, a, b, c, 22);
    R2(c, d, e, a, b, 23);
    R2(b, c, d, e, a, 24);
    R2(a, b, c, d, e, 25);
    R2(e, a, b, c, d, 26);
    R2(d, e, a, b, c, 27);
    R2(c, d, e, a, b, 28);
    R2(b, c, d, e, a, 29);
    R2(a, b, c, d, e, 30);
    R2(e, a, b, c, d, 31);
    R2(d, e, a, b, c, 32);
    R2(c, d, e, a, b, 33);
    R2(b, c, d, e, a, 34);
    R2(a, b, c, d, e, 35);
    R2(e, a, b, c, d, 36);
    R2(d, e, a, b, c, 37);
    R2(c, d, e, a, b, 38);
    R2(b, c, d, e, a, 39);
    R3(a, b, c, d, e, 40);
    R3(e, a, b, c, d, 41);
    R3(d, e, a, b, c, 42);
    R3(c, d, e, a, b, 43);
    R3(b, c, d, e, a, 44);
    R3(a, b, c, d, e, 45);
    R3(e, a, b, c, d, 46);
    R3(d, e, a, b, c, 47);
    R3(c, d, e, a, b, 48);
    R3(b, c, d, e, a, 49);
    R3(a, b, c, d, e, 50);
    R3(e, a, b, c, d, 51);
    R3(d, e, a, b, c, 52);
    R3(c, d, e, a, b, 53);
    R3(b, c, d, e, a, 54);
    R3(a, b, c, d, e, 55);
    R3(e, a, b, c, d, 56);
    R3(d, e, a, b, c, 57);
    R3(c, d, e, a, b, 58);
    R3(b, c, d, e, a, 59);
    R4(a, b, c, d, e, 60);
    R4(e, a, b, c, d, 61);
    R4(d, e, a, b, c, 62);
    R4(c, d, e, a, b, 63);
    R4(b, c, d, e, a, 64);
    R4(a, b, c, d, e, 65);
    R4(e, a, b, c, d, 66);
    R4(d, e, a, b, c, 67);
    R4(c, d, e, a, b, 68);
    R4(b, c, d, e, a, 69);
    R4(a, b, c, d, e, 70);
    R4(e, a, b, c, d, 71);
    R4(d, e, a, b, c, 72);
    R4(c, d, e, a, b, 73);
    R4(b, c, d, e, a, 74);
    R4(a, b, c, d, e, 75);
    R4(e, a, b, c, d, 76);
    R4(d, e, a, b, c, 77);
    R4(c, d, e, a, b, 78);
    R4(b, c, d, e, a, 79);

#undef R0
#undef R1
#undef R2
#undef R3
#undef R4
#undef SHA_W
#undef SHA_ROL

    acc.Long[0] = int32_t(uint32_t(acc.Long[0]) + a);
    acc.Long[1] = int32_t(uint32_t(acc.Long[1]) + b);
    acc.Long[2] = int32_t(uint32_t(acc.Long[2]) + c);
    acc.Long[3] = int32_t(uint32_t(acc.Long[3]) + d);
    acc.Long[4] = int32_t(uint32_t(acc.Long[4]) + e);
}
