/*
    LZ4 HC - High Compression Mode of LZ4
    Copyright (C) 2011, Yann Collet.
    GPL v2 License

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along
    with this program; if not, see <http://www.gnu.org/licenses/>,
	or write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

	You can contact the author at :
	- LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
	- LZ4-HC source repository : http://code.google.com/p/lz4hc/
*/



//**************************************
// CPU Feature Detection
//**************************************
// 32 or 64 bits ?
#if (defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || defined(__amd64) || defined(__ppc64__) || defined(_WIN64) || defined(__LP64__) || defined(_LP64) )   // Detects 64 bits mode
#define LZ4_ARCH64 1
#else
#define LZ4_ARCH64 0
#endif

// Little Endian or Big Endian ? 
#if (defined(__BIG_ENDIAN__) || defined(__BIG_ENDIAN) || defined(_BIG_ENDIAN) || defined(_ARCH_PPC) || defined(__PPC__) || defined(__PPC) || defined(PPC) || defined(__powerpc__) || defined(__powerpc) || defined(powerpc) || ((defined(__BYTE_ORDER__)&&(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))) )
#define LZ4_BIG_ENDIAN 1
#else
// Little Endian assumed. PDP Endian and other very rare endian format are unsupported.
#endif

// Unaligned memory access is automatically enabled for "common" CPU, such as x86.
// For others CPU, the compiler will be more cautious, and insert extra code to ensure aligned access is respected
// If you know your target CPU supports unaligned memory access, you may want to force this option manually to improve performance
#if defined(__ARM_FEATURE_UNALIGNED)
#define LZ4_FORCE_UNALIGNED_ACCESS 1
#endif


//**************************************
// Compiler Options
//**************************************
#if __STDC_VERSION__ >= 199901L    // C99
  /* "restrict" is a known keyword */
#else
#define restrict  // Disable restrict
#endif

#ifdef _MSC_VER
#define inline __forceinline    // Visual is not C99, but supports some kind of inline
#endif

#ifdef _MSC_VER  // Visual Studio
#define bswap16(x) _byteswap_ushort(x)
#else
#define bswap16(x)  ((unsigned short int) ((((x) >> 8) & 0xffu) | (((x) & 0xffu) << 8)))
#endif


//**************************************
// Includes
//**************************************
#include <string.h>   // for memset, memcpy
#include "lz4hc.h"
#include "mmc.h"


//**************************************
// Basic Types
//**************************************
#if defined(_MSC_VER)    // Visual Studio does not support 'stdint' natively
#define BYTE	unsigned __int8
#define U16		unsigned __int16
#define U32		unsigned __int32
#define S32		__int32
#define U64		unsigned __int64
#else
#include <stdint.h>
#define BYTE	uint8_t
#define U16		uint16_t
#define U32		uint32_t
#define S32		int32_t
#define U64		uint64_t
#endif

#ifndef LZ4_FORCE_UNALIGNED_ACCESS
#pragma pack(push, 1) 
#endif

typedef struct _U16_S { U16 v; } U16_S;
typedef struct _U32_S { U32 v; } U32_S;
typedef struct _U64_S { U64 v; } U64_S;

#ifndef LZ4_FORCE_UNALIGNED_ACCESS
#pragma pack(pop) 
#endif

#define A64(x) (((U64_S *)(x))->v)
#define A32(x) (((U32_S *)(x))->v)
#define A16(x) (((U16_S *)(x))->v)


//**************************************
// Constants
//**************************************
#define ML_BITS 4
#define ML_MASK ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)
#define MINMATCH 4
#define COPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (COPYLENGTH+MINMATCH)
#define MINLENGTH (MFLIMIT+1)


//**************************************
// Architecture-specific macros
//**************************************
#if ARCH64	// 64-bit
#define STEPSIZE 8
#define LZ4_COPYSTEP(s,d)		A64(d) = A64(s); d+=8; s+=8;
#define LZ4_COPYPACKET(s,d)		LZ4_COPYSTEP(s,d)
#else		// 32-bit
#define STEPSIZE 4
#define LZ4_COPYSTEP(s,d)		A32(d) = A32(s); d+=4; s+=4;
#define LZ4_COPYPACKET(s,d)		LZ4_COPYSTEP(s,d); LZ4_COPYSTEP(s,d);
#endif

#if defined(LZ4_BIG_ENDIAN)
#define LZ4_READ_LITTLEENDIAN_16(d,s,p) { U16 v = A16(p); v = bswap16(v); d = (s) - v; }
#define LZ4_WRITE_LITTLEENDIAN_16(p,i) { U16 v = (U16)(i); v = bswap16(v); A16(p) = v; p+=2; }
#else		// Little Endian
#define LZ4_READ_LITTLEENDIAN_16(d,s,p) { d = (s) - A16(p); }
#define LZ4_WRITE_LITTLEENDIAN_16(p,v) { A16(p) = v; p+=2; }
#endif


//**************************************
// Macros
//**************************************
#define LZ4_WILDCOPY(s,d,e)		do { LZ4_COPYPACKET(s,d) } while (d<e);
#define LZ4_BLINDCOPY(s,d,l)	{ BYTE* e=d+l; LZ4_WILDCOPY(s,d,e); d=e; }



//****************************
// Compression CODE
//****************************
int LZ4_compressHCCtx(void* ctx,
				 const char* source, 
				 char* dest,
				 int isize)
{	
	const BYTE* ip = (const BYTE*) source;
	const BYTE* anchor = ip;
	const BYTE* const iend = ip + isize;
	const BYTE* const mflimit = iend - MFLIMIT;
	const BYTE* const matchlimit = (iend - LASTLITERALS);
	const BYTE* ref;

	BYTE* op = (BYTE*) dest;
	BYTE* token;
	
	const BYTE	*srcip, *savedip=0, *srcref, *savedref=0;
	int		srcml, savedml=0;

	int	len, length, stackedmatches=0;
	int	ml;


	// Main Loop
	while (ip < mflimit-1)
	{
		ml = MMC_InsertAndFindBestMatch (ctx, (char*)ip, matchlimit-ip, (char**)(&ref));
		if (!ml) { ip++; continue; }

		srcip = ip;
		srcref = ref;
		srcml = ml;

		// Lazy Match
		do
		{
			BYTE* rtmp;
			int mltmp = MMC_InsertAndFindBestMatch(ctx, (char*)ip+1, matchlimit-ip-1, (char**)&rtmp);
			if (mltmp > ml) 
			{
				ip++; 
				ml = mltmp;
				ref = rtmp;
				continue;
			}
			mltmp = MMC_InsertAndFindBestMatch(ctx, (char*)ip+2, matchlimit-ip-2, (char**)&rtmp);
			if (mltmp > ml+1) 
			{
				ip+=2; 
				ml = mltmp;
				ref = rtmp;
				continue;
			}
			break;
		}
		while (1);

		if ((ip-srcip)>=MINMATCH) 
		{
			stackedmatches = 1;
			savedip = ip;
			savedref = ref;
			savedml = ml;
			ml = srcml<(ip-srcip)?srcml:(ip-srcip);
			ip = srcip;
			ref = srcref;
		}

_matchEncode:
		// Encode Literal length
		length = ip - anchor;
		token = op++;
		if (length>=(int)RUN_MASK) { *token=(RUN_MASK<<ML_BITS); len = length-RUN_MASK; for(; len > 254 ; len-=255) *op++ = 255; *op++ = (BYTE)len; } 
		else *token = (length<<ML_BITS);

		// Copy Literals
		LZ4_BLINDCOPY(anchor, op, length);

		// Encode Offset
		LZ4_WRITE_LITTLEENDIAN_16(op,ip-ref);

		// Encode MatchLength
		len = (int)(ml-MINMATCH);
		if (len>=(int)ML_MASK) { *token+=ML_MASK; len-=ML_MASK; for(; len > 509 ; len-=510) { *op++ = 255; *op++ = 255; } if (len > 254) { len-=255; *op++ = 255; } *op++ = (BYTE)len; } 
		else *token += len;	

		// stacked matches
		if (stackedmatches)
		{
			stackedmatches=0;
			anchor = ip+ml;
			ip = savedip;
			ref = savedref;
			ml = savedml;
			goto _matchEncode;
		}

		// Prepare next loop
		MMC_InsertMany (ctx, (char*)ip+3, ml-3);
		ip += ml;
		anchor = ip; 
	}

	// Encode Last Literals
	{
		int lastRun = iend - anchor;
		if (lastRun>=(int)RUN_MASK) { *op++=(RUN_MASK<<ML_BITS); lastRun-=RUN_MASK; for(; lastRun > 254 ; lastRun-=255) *op++ = 255; *op++ = (BYTE) lastRun; } 
		else *op++ = (lastRun<<ML_BITS);
		memcpy(op, anchor, iend - anchor);
		op += iend-anchor;
	} 

	// End
	return (int) (((char*)op)-dest);
}


int LZ4_compressHC(const char* source, 
				 char* dest,
				 int isize)
{
	void* ctx = MMC_Create((char*)source);
	int result = LZ4_compressHCCtx(ctx, source, dest, isize);
	MMC_Free (&ctx);

	return result;
}




