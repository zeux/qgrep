/*
    MMC (Morphing Match Chain)
    Match Finder
    Copyright (C) Yann Collet 2010-2011

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
	- MMC homepage : http://fastcompression.blogspot.com/p/mmc-morphing-match-chain.html
	- MMC source repository : http://code.google.com/p/mmc/
*/

//************************************************************
// Includes
//************************************************************
#include "mmc.h"

// Allocator definitions
#include <stdlib.h>
#define ALLOCATOR(s) calloc(1,s)
#define REALLOCATOR(p,s) realloc(p,s)
#define FREEMEM free
#include <string.h>
#define MEM_INIT memset


//**************************************
// Basic Types
//**************************************
#if defined(_MSC_VER)
#define BYTE	unsigned __int8
#define U16		unsigned __int16
#define S16		__int16
#define U32		unsigned __int32
#define S32		__int32
#else
#include <stdint.h>
#define BYTE	uint8_t
#define U16		uint16_t
#define S16		int16_t
#define U32		uint32_t
#define S32		int32_t
#endif


//************************************************************
// Constants
//************************************************************
#define MAXD (1<<DICTIONARY_LOGSIZE)
#define MAXD_MASK ((U32)(MAXD - 1))
#define MAX_DISTANCE (MAXD - 1)

#define HASH_LOG (DICTIONARY_LOGSIZE-1)    
#define HASHTABLESIZE (1 << HASH_LOG)
#define HASH_MASK (HASHTABLESIZE - 1)

#define MAX_LEVELS_LOG (DICTIONARY_LOGSIZE-1)  
#define MAX_LEVELS (1U<<MAX_LEVELS_LOG)
#define MAX_LEVELS_MASK (MAX_LEVELS-1)

#define NBCHARACTERS 256
#define NB_INITIAL_SEGMENTS 16

#define LEVEL_DOWN ((BYTE*)1)



//************************************************************
// Local Types
//************************************************************
typedef struct 
{
	BYTE* levelUp;
	BYTE* nextTry;
}selectNextHop_t;

typedef struct  
{
	BYTE* position;
	U32   size;
} segmentInfo_t;

typedef struct 
{
	segmentInfo_t * segments;
	U16 start;
	U16 max;
} segmentTracker_t;

typedef struct 
{
	BYTE* beginBuffer;		// First byte of data buffer being searched
	BYTE* hashTable[HASHTABLESIZE];
	selectNextHop_t chainTable[MAXD];
	segmentTracker_t segments[NBCHARACTERS];
	BYTE* levelList[MAX_LEVELS];
	BYTE** trackPtr[NBCHARACTERS];
	U16 trackStep[NBCHARACTERS];
} MMC_Data_Structure;


//************************************************************
// Macro
//************************************************************
#define HASH_FUNCTION(i)	((i * 2654435761U) >> ((MINMATCH*8)-HASH_LOG))
#define HASH_VALUE(p)		HASH_FUNCTION(*(U32*)p)
#define HASH_POINTER(p)		HashTable[HASH_VALUE(p)]
#define NEXT_TRY(p)			chainTable[(size_t)(p) & MAXD_MASK].nextTry 
#define LEVEL_UP(p)			chainTable[(size_t)(p) & MAXD_MASK].levelUp
#define ADD_HASH(p)			{ NEXT_TRY(p) = HashTable[HASH_VALUE(p)]; LEVEL_UP(p)=0; HashTable[HASH_VALUE(p)] = p; }
#define LEVEL(l)			levelList[(l)&MAX_LEVELS_MASK]


//************************************************************
// Creation & Destruction
//************************************************************
void* MMC_Create (char* beginBuffer)
{
	void* mmc = ALLOCATOR(sizeof(MMC_Data_Structure));
	MMC_Init (mmc, beginBuffer);
	return mmc;
}


int MMC_Init (void* MMC_Data, char* beginBuffer)
{
	MMC_Data_Structure * MMC = (MMC_Data_Structure *) MMC_Data;

	MMC->beginBuffer = (BYTE*)beginBuffer;
	MEM_INIT(MMC->hashTable, 0, sizeof(MMC->hashTable));
	MEM_INIT(MMC->chainTable, 0, sizeof(MMC->chainTable));
	// Init RLE detector
	{ 
		int c; 
		for (c=0; c<NBCHARACTERS; c++) 
		{ 
			MMC->segments[c].segments = (segmentInfo_t *) REALLOCATOR(MMC->segments[c].segments, NB_INITIAL_SEGMENTS * sizeof(segmentInfo_t));
			MMC->segments[c].max = NB_INITIAL_SEGMENTS;
			MMC->segments[c].start = 0;
			MMC->segments[c].segments[0].size = -1; 
			MMC->segments[c].segments[0].position = (BYTE*)beginBuffer - (MAX_DISTANCE+1); 
		} 
	}

	return 1;
}


int MMC_Free (void** MMC_Data)
{
	MMC_Data_Structure * MMC = * (MMC_Data_Structure **) MMC_Data;
	// RLE dynamic structure releasing
	{ 
		int c; 
		for (c=0; c<NBCHARACTERS; c++) FREEMEM(MMC->segments[c].segments);
	}
	FREEMEM(*MMC_Data);
	*MMC_Data = NULL;
	return (1);
}


//*********************************************************************
// Basic Search operations (Greedy / Lazy / Flexible parsing)
//*********************************************************************
int MMC_InsertAndFindBestMatch (void* MMC_Data, char* inputPointer, int maxLength, char** matchpos)
{
	MMC_Data_Structure * MMC = (MMC_Data_Structure *) MMC_Data;
	segmentTracker_t * const Segments = MMC->segments;
	selectNextHop_t * const chainTable = MMC->chainTable;
	BYTE** const HashTable = MMC->hashTable;
	BYTE** const levelList = MMC->levelList;
	BYTE*** const trackPtr = MMC->trackPtr;
	U16*   trackStep = MMC->trackStep;
	const BYTE* const iend = (BYTE*)inputPointer + maxLength;
	BYTE* ip = (BYTE*)inputPointer;
	BYTE*  ref;
	BYTE** gateway;
	BYTE*  currentP;
	U16	stepNb=0;
	U32 currentLevel, maxLevel;
	U32 ml, mlt, nbChars, sequence;

	ml = mlt = nbChars = 0;
	sequence = *(U32*)ip;

	// stream match finder
	if ((U16)sequence==(U16)(sequence>>16))
	if ((BYTE)sequence == (BYTE)(sequence>>8))
	{
		BYTE c = (BYTE)sequence;
		U32 index = Segments[c].start;
		BYTE* endSegment = ip+MINMATCH;

		while ((*endSegment==c) && (endSegment<iend)) endSegment++; nbChars = endSegment-ip;

		while (Segments[c].segments[index].size < nbChars) index--;

		if ((Segments[c].segments[index].position - nbChars) <= (ip - MAX_DISTANCE))      // no large enough previous serie within range
		{
			// no "previous" segment within range
			NEXT_TRY(ip) = LEVEL_UP(ip) = 0;    
			if (nbChars==MINMATCH) MMC_Insert1(MMC, (char*)ip);
			if ((ip>MMC->beginBuffer) && (*(ip-1)==c))         // obvious RLE solution
			{
				*matchpos=(char*)ip-1;
				return nbChars;
			}
			return 0;
		}

		ref = NEXT_TRY(ip)= Segments[c].segments[index].position - nbChars;
		currentLevel = maxLevel = ml = nbChars;
		LEVEL(currentLevel) = ip;
		gateway = 0; // work around due to erasing
		LEVEL_UP(ip) = 0;
		if (*(ip-1)==c) *matchpos = (char*)ip-1; else *matchpos = (char*)ref;     // "basis" to be improved upon
		if (nbChars==MINMATCH) 
		{
			MMC_Insert1(MMC, (char*)ip);
			gateway = &LEVEL_UP(ip);
		}
		goto _FindBetterMatch;
	}

	// MMC match finder
	ref=HashTable[HASH_FUNCTION(sequence)];
	ADD_HASH(ip);
	if (!ref) return 0;
	gateway = &LEVEL_UP(ip);
	currentLevel = maxLevel = MINMATCH-1;
	LEVEL(MINMATCH-1) = ip;

	// Collision detection & avoidance
	while ((ref) && ((ip-ref) < MAX_DISTANCE))
	{
		if (*(U32*)ref != sequence)
		{
			LEVEL(MINMATCH-1) = ref; 
			ref = NEXT_TRY(ref);
			continue;
		}

		mlt = MINMATCH;
		while ((mlt<(U32)maxLength) && (*(ip+mlt)) == *(ref+mlt)) mlt++;

		if (mlt>ml)
		{
			ml = mlt; 
			*matchpos = (char*)ref; 
		}

		// Continue level mlt chain
		if (mlt<=maxLevel)
		{
			NEXT_TRY(LEVEL(mlt)) = ref; LEVEL(mlt) = ref;	// Completing chain at Level mlt
		}

		// New level creation
		else 
		{
			if (gateway)			// Only guaranteed the first time (gateway is ip)
			{
				maxLevel++;
				*gateway = ref;
				LEVEL(maxLevel)=ref;						// First element of level maxLevel
				if (mlt>maxLevel) gateway=&(LEVEL_UP(ref)); else gateway=0;
			}

			// Special case : no gateway, but mlt>maxLevel
			else
			{
				gateway = &(LEVEL_UP(ref));
				NEXT_TRY(LEVEL(maxLevel)) = ref; LEVEL(maxLevel) = ref;		// Completing chain at Level maxLevel
			}
		}

		currentP = ref;
		NEXT_TRY(LEVEL(MINMATCH-1)) = NEXT_TRY(ref);		// Extraction from base level
		if (LEVEL_UP(ref))
		{
			ref=LEVEL_UP(ref);
			NEXT_TRY(currentP) = LEVEL_UP(currentP) = 0;	// Clean, because extracted
			currentLevel++;
			NEXT_TRY(LEVEL(MINMATCH)) = ref;
			break;
		}
		ref=NEXT_TRY(ref);
		NEXT_TRY(currentP) = 0;								// initialisation, due to promotion; note that LEVEL_UP(ref)=0;
	}

	if (ml==0) return 0;  // no match found


	// looking for better length of match
_FindBetterMatch:
	while ((ref) && ((ip-ref) < MAX_DISTANCE))
	{
		// Reset rolling counter for Secondary Promotions
		if (!stepNb)
		{
			U32 i;
			for (i=0; i<NBCHARACTERS; i++) trackStep[i]=0;
			stepNb=1;
		}

		// Match Count
		mlt = currentLevel;
		while ((mlt<(U32)maxLength) && (*(ip+mlt)) == *(ref+mlt)) mlt++;

		// First case : No improvement => continue on current chain
		if (mlt==currentLevel)
		{
			BYTE c = *(ref+currentLevel);
			if (trackStep[c] == stepNb)								// this wrong character was already met before
			{
				BYTE* next = NEXT_TRY(ref);
				*trackPtr[c] = ref;									// linking
				NEXT_TRY(LEVEL(currentLevel)) = NEXT_TRY(ref);		// extraction
				if (LEVEL_UP(ref))
				{
					NEXT_TRY(ref) = LEVEL_UP(ref);					// Promotion
					LEVEL_UP(ref) = 0;
					trackStep[c] = 0;								// Shutdown chain (avoid overwriting when multiple unfinished chains)
				}
				else
				{
					NEXT_TRY(ref) = LEVEL_DOWN;						// Promotion, but link back to previous level for now
					trackPtr[c] = &(NEXT_TRY(ref));					// saving for next link
				}

				if (next==LEVEL_DOWN)
				{
					NEXT_TRY(LEVEL(currentLevel)) = 0;				// Erase the LEVEL_DOWN
					currentLevel--; stepNb++;
					next = NEXT_TRY(LEVEL(currentLevel));	
					while (next > ref) { LEVEL(currentLevel) = next; next = NEXT_TRY(next); }
				}
				ref = next;					

				continue;
			}

			// first time we see this character
			if (LEVEL_UP(ref)==0)   // don't interfere if a serie has already started... 
				// Note : to "catch up" the serie, it would be necessary to scan it, up to its last element
				// this effort would be useless if the chain is complete
				// Alternatively : we could keep that gateway in memory, and scan the chain on finding that it is not complete.
				// But would it be worth it ??
			{
				trackStep[c] = stepNb;
				trackPtr[c] = &(LEVEL_UP(ref)); 
			}

_continue_same_level:
			LEVEL(currentLevel) = ref; 
			ref = NEXT_TRY(ref);												
			if (ref == LEVEL_DOWN)
			{
				BYTE* currentP = LEVEL(currentLevel);
				BYTE* next = NEXT_TRY(LEVEL(currentLevel-1));
				NEXT_TRY(currentP) = 0;							// Erase the LEVEL_DOWN
				while (next>currentP) { LEVEL(currentLevel-1) = next; next = NEXT_TRY(next);}
				ref = next;
				currentLevel--; stepNb++;
			}
			continue;
		}

		// Now, mlt > currentLevel
		if (mlt>ml)
		{
			ml = mlt; 
			*matchpos = (char*)ref; 
		}

		// placing into corresponding chain
		if (mlt<=maxLevel)
		{
			NEXT_TRY(LEVEL(mlt)) = ref; LEVEL(mlt) = ref;		// Completing chain at Level mlt
_check_mmc_levelup:
			currentP = ref;
			NEXT_TRY(LEVEL(currentLevel)) = NEXT_TRY(ref);		// Extraction from base level
			if (LEVEL_UP(ref)) 
			{ 
				ref = LEVEL_UP(ref);							// LevelUp
				NEXT_TRY(currentP) = LEVEL_UP(currentP) = 0;	// Clean, because extracted
				currentLevel++; stepNb++;
				NEXT_TRY(LEVEL(currentLevel)) = ref;			// We don't know yet ref's level, but just in case it would be only ==currentLevel...
			} 
			else 
			{ 
				ref = NEXT_TRY(ref);
				NEXT_TRY(currentP) = 0;							// promotion to level mlt; note that LEVEL_UP(ref)=0;
				if (ref == LEVEL_DOWN)
				{
					BYTE* next = NEXT_TRY(LEVEL(currentLevel-1));
					NEXT_TRY(LEVEL(currentLevel)) = 0;			// Erase the LEVEL_DOWN (which has been transfered)
					while (next>currentP) { LEVEL(currentLevel-1) = next; next = NEXT_TRY(next); }
					ref = next;
					currentLevel--; stepNb++;
				}
			}
			continue;
		}

		// MaxLevel increase
		if (gateway)
		{
			*gateway = ref;
			maxLevel++;
			LEVEL(maxLevel) = ref;								// First element of level max 
			if (mlt>maxLevel) gateway=&(LEVEL_UP(ref)); else gateway=0;
			goto _check_mmc_levelup;
		}

		// Special case : mlt>maxLevel==currentLevel, no Level_up nor gateway
		if ((maxLevel==currentLevel) && (!(LEVEL_UP(ref))))
		{
			gateway = &(LEVEL_UP(ref));							// note : *gateway = 0
			goto _continue_same_level;
		}

		// Special case : mlt>maxLevel==currentLevel, Level_up available, but no gateway
		if (maxLevel==currentLevel)
		{
			LEVEL(currentLevel) = ref;
			ref = LEVEL_UP(ref);
			maxLevel++;
			currentLevel++; stepNb++;
			continue;
		}

		// Special case : mlt>maxLevel, but no gateway; Note that we don't know about level_up yet
		{
			gateway = &(LEVEL_UP(ref));
			NEXT_TRY(LEVEL(maxLevel)) = ref; LEVEL(maxLevel) = ref;		// Completing chain of maxLevel
			goto _check_mmc_levelup;
		}

	}

	if (gateway) *gateway=ip-MAX_DISTANCE-1;    // early end trick
	stepNb++;

	// prevent match beyond buffer
	if ((ip+ml)>iend) ml = iend-ip;

	return ml;
}


__inline static U32 MMC_Insert (void* MMC_Data, BYTE* ip, U32 max)
{
	MMC_Data_Structure * MMC = (MMC_Data_Structure *) MMC_Data;
	segmentTracker_t * Segments = MMC->segments;
	selectNextHop_t * chainTable = MMC->chainTable;
	BYTE** HashTable = MMC->hashTable;
	BYTE* iend = ip+max;
	BYTE* beginBuffer = MMC->beginBuffer;

	// Stream updater
	if ((*(U16*)ip == *(U16*)(ip+2)) && (*ip == *(ip+1)))
	{
		BYTE c=*ip;
		U32 nbForwardChars, nbPreviousChars, segmentSize, n=MINMATCH;
		BYTE* endSegment = ip+MINMATCH;
		BYTE* baseStreamP = ip;

		iend += MINMATCH;
		while ((*endSegment==c) && (endSegment<iend)) endSegment++; 
		if (endSegment == iend) return (iend-ip);			// skip the whole forward segment; we'll start again later
		nbForwardChars = endSegment-ip;
		while ((baseStreamP>beginBuffer) && (baseStreamP[-1]==c)) baseStreamP--; 
		nbPreviousChars = ip-baseStreamP;
		segmentSize = nbForwardChars + nbPreviousChars;
		if (segmentSize > MAX_DISTANCE-1) segmentSize = MAX_DISTANCE-1;

		while (Segments[c].segments[Segments[c].start].size <= segmentSize)
		{
			if (Segments[c].segments[Segments[c].start].position <= (ip-MAX_DISTANCE)) break;
			for ( ; n<=Segments[c].segments[Segments[c].start].size ; n++) 
			{
				NEXT_TRY(endSegment-n) = Segments[c].segments[Segments[c].start].position - n;
				LEVEL_UP(endSegment-n) = 0;
			}
			Segments[c].start--;
		}
	
		if (Segments[c].segments[Segments[c].start].position <= (ip-MAX_DISTANCE)) Segments[c].start = 0;   // no large enough serie within range

		for ( ; n<=segmentSize ; n++) 
		{
			NEXT_TRY(endSegment-n) = Segments[c].segments[Segments[c].start].position - n;
			LEVEL_UP(endSegment-n) = 0;
		}

		// overflow protection : new segment smaller than previous, but too many segments in memory
		if (Segments[c].start > Segments[c].max-2) 
		{
			int beginning=0;
			U32 i;

			Segments[c].max *= 2;
			Segments[c].segments = (segmentInfo_t *) REALLOCATOR (Segments[c].segments, (Segments[c].max)*sizeof(segmentInfo_t));
			while (Segments[c].segments[beginning].position < (ip-MAX_DISTANCE)) beginning++;
			i = beginning;
			while (i<=Segments[c].start) { Segments[c].segments[i - (beginning-1)] = Segments[c].segments[i]; i++; }
			Segments[c].start -= (beginning-1);
			
		}
		Segments[c].start++;
		Segments[c].segments[Segments[c].start].position = endSegment;
		Segments[c].segments[Segments[c].start].size = segmentSize;

		return (endSegment-ip-(MINMATCH-1));
	}

	//Normal update
	ADD_HASH(ip); 
	return 1; 
}


int MMC_Insert1 (void* MMC_Data, char* inputPointer)
{
	MMC_Insert (MMC_Data, (BYTE*)inputPointer, 1);
	return 1;
}

int MMC_InsertMany (void* MMC_Data, char* inputPointer, int length)
{
	int done=0;
	while  (done<length) done += MMC_Insert (MMC_Data, (BYTE*)(inputPointer+done), length-done);
	return length;
}


