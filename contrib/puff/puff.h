/* puff.h
  Copyright (C) 2002-2013 Mark Adler, all rights reserved
  version 2.3, 21 Jan 2013

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the author be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Mark Adler    madler@alumni.caltech.edu
 */


/*
 * See puff.c for purpose and usage.
 */
#ifndef NIL
#  define NIL ((unsigned char *)0)      /* for no output option */
#endif

#include <map>

typedef struct symbolStats
{
   void updateStat(unsigned int len, unsigned int characters);

   unsigned int groupSymbols = 0;
   unsigned int totalBits = 0;
   unsigned int bitSymbol = 0;
   unsigned int groupCharacters = 0;
   unsigned int numChunk = 0;
   unsigned int numGroupDec = 0;
   unsigned int thresholdBits = 32;

   std::map<unsigned int, unsigned int> symbolHistogram = {};
   std::map<unsigned int, unsigned int> bitsHistogram = {};
   std::map<unsigned int, unsigned int> charHistogram = {};

} symbolStats;

int puff(unsigned char *dest,            /* pointer to destination pointer */
         unsigned long *destlen,         /* amount of output space */
         const unsigned char *source,    /* pointer to source data pointer */
         unsigned long *sourcelen,       /* amount of input available */
         struct symbolStats* stats = nullptr);
