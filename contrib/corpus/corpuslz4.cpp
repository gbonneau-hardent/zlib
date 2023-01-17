// corpuslz4.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <map>
#include <list>
#include <vector>
#include <iomanip>
#include <cassert>




// //////////////////////////////////////////////////////////
// smallz4cat.c
// Copyright (c) 2016-2019 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/smallz4/
//
// "MIT License":
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// This program is a shorter, more readable, albeit slower re-implementation of lz4cat ( https://github.com/Cyan4973/xxHash )

// compile: gcc smallz4cat.c -O3 -o smallz4cat -Wall -pedantic -std=c99 -s
// The static 8k binary was compiled using Clang and dietlibc (see https://www.fefe.de/dietlibc/ )

// Limitations:
// - skippable frames and legacy frames are not implemented (and most likely never will)
// - checksums are not verified (see https://create.stephan-brumme.com/xxhash/ for a simple implementation)

// Replace getByteFromIn() and sendToOut() by your own code if you need in-memory LZ4 decompression.
// Corrupted data causes a call to unlz4error().

// suppress warnings when compiled by Visual C++
//#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>  // stdin/stdout/stderr, fopen, ...
#include <stdlib.h> // exit()
#include <string.h> // memcpy

#ifndef FALSE
#define FALSE 0
#define TRUE  1
#endif

/// error handler
static void unlz4error(const char* msg)
{
   // smaller static binary than fprintf(stderr, "ERROR: %s\n", msg);
   fputs("ERROR: ", stderr);
   fputs(msg, stderr);
   fputc('\n', stderr);
   exit(1);
}


// ==================== I/O INTERFACE ====================


// read one byte from input, see getByteFromIn()  for a basic implementation
typedef unsigned char (*GET_BYTE)  (void* userPtr);
// write several bytes,      see sendBytesToOut() for a basic implementation
typedef void          (*SEND_BYTES)(const unsigned char*, unsigned int, void* userPtr);

struct LZ4DecompReader
{
   char decompBuffer[8192];
   char * compBuffer;
   unsigned int  available;
   unsigned int  decompPos;

};

/// read a single byte (with simple buffering)
static unsigned char getByteFromIn(void* userPtr) // parameter "userPtr" not needed
{
   struct LZ4DecompReader* user = (struct LZ4DecompReader*)userPtr;

   if (user->available == 0) {
         unlz4error("out of data");
   }
   user->available--;
   return *user->compBuffer++;
}

/// write a block of bytes
static void sendBytesToOut(const unsigned char* data, unsigned int numBytes, void* userPtr)
{
   struct LZ4DecompReader* user = (struct LZ4DecompReader*)userPtr;
   if (data != nullptr && numBytes > 0) {
      for (unsigned int i = 0; i < numBytes; i++) {
         if (user->decompPos == 8192) {
            unlz4error("decomp buffer overflow");
            return;
         }
         user->decompBuffer[user->decompPos++] = *data++;
      }
   }
}


// ==================== LZ4 DECOMPRESSOR ====================


/// decompress everything in input stream (accessed via getByte) and write to output stream (via sendBytes)
void unlz4_userPtr(GET_BYTE getByte, SEND_BYTES sendBytes, const char* dictionary, void* userPtr)
{
   // signature
   unsigned char signature1 = getByte(userPtr);
   unsigned char signature2 = getByte(userPtr);
   unsigned char signature3 = getByte(userPtr);
   unsigned char signature4 = getByte(userPtr);
   unsigned int  signature = (signature4 << 24) | (signature3 << 16) | (signature2 << 8) | signature1;
   unsigned char isModern = (signature == 0x184D2204);
   unsigned char isLegacy = (signature == 0x184C2102);
   if (!isModern && !isLegacy)
      unlz4error("invalid signature");

   unsigned char hasBlockChecksum = FALSE;
   unsigned char hasContentSize = FALSE;
   unsigned char hasContentChecksum = FALSE;
   unsigned char hasDictionaryID = FALSE;
   if (isModern)
   {
      // flags
      unsigned char flags = getByte(userPtr);
      hasBlockChecksum = flags & 16;
      hasContentSize = flags & 8;
      hasContentChecksum = flags & 4;
      hasDictionaryID = flags & 1;

      // only version 1 file format
      unsigned char version = flags >> 6;
      if (version != 1)
         unlz4error("only LZ4 file format version 1 supported");

      // ignore blocksize
      char numIgnore = 1;

      // ignore, skip 8 bytes
      if (hasContentSize)
         numIgnore += 8;
      // ignore, skip 4 bytes
      if (hasDictionaryID)
         numIgnore += 4;

      // ignore header checksum (xxhash32 of everything up this point & 0xFF)
      numIgnore++;

      // skip all those ignored bytes
      while (numIgnore--)
         getByte(userPtr);
   }

   // don't lower this value, backreferences can be 64kb far away
#define HISTORY_SIZE 64*1024
  // contains the latest decoded data
   unsigned char history[HISTORY_SIZE];
   // next free position in history[]
   unsigned int  pos = 0;

   // dictionary compression is a recently introduced feature, just move its contents to the buffer
   if (dictionary != NULL)
   {
      // open dictionary
      FILE* dict;
      fopen_s(&dict, dictionary, "rb");
      if (!dict)
         unlz4error("cannot open dictionary");

      // get dictionary's filesize
      fseek(dict, 0, SEEK_END);
      long dictSize = ftell(dict);
      // only the last 64k are relevant
      long relevant = dictSize < 65536 ? 0 : dictSize - 65536;
      fseek(dict, relevant, SEEK_SET);
      if (dictSize > 65536)
         dictSize = 65536;
      // read it and store it at the end of the buffer
      fread(history + HISTORY_SIZE - dictSize, 1, dictSize, dict);
      fclose(dict);
   }

   // parse all blocks until blockSize == 0
   while (1)
   {
      // block size
      unsigned int blockSize = getByte(userPtr);
      blockSize |= (unsigned int)getByte(userPtr) << 8;
      blockSize |= (unsigned int)getByte(userPtr) << 16;
      blockSize |= (unsigned int)getByte(userPtr) << 24;

      // highest bit set ?
      unsigned char isCompressed = isLegacy || (blockSize & 0x80000000) == 0;
      if (isModern)
         blockSize &= 0x7FFFFFFF;

      // stop after last block
      if (blockSize == 0)
         break;

      if (isCompressed)
      {
         // decompress block
         unsigned int blockOffset = 0;
         unsigned int numWritten = 0;
         while (blockOffset < blockSize)
         {
            // get a token
            unsigned char token = getByte(userPtr);
            blockOffset++;

            // determine number of literals
            unsigned int numLiterals = token >> 4;
            if (numLiterals == 15)
            {
               // number of literals length encoded in more than 1 byte
               unsigned char current;
               do
               {
                  current = getByte(userPtr);
                  numLiterals += current;
                  blockOffset++;
               } while (current == 255);
            }

            blockOffset += numLiterals;

            // copy all those literals
            if (pos + numLiterals < HISTORY_SIZE)
            {
               // fast loop
               while (numLiterals-- > 0)
                  history[pos++] = getByte(userPtr);
            }
            else
            {
               // slow loop
               while (numLiterals-- > 0)
               {
                  history[pos++] = getByte(userPtr);

                  // flush output buffer
                  if (pos == HISTORY_SIZE)
                  {
                     sendBytes(history, HISTORY_SIZE, userPtr);
                     numWritten += HISTORY_SIZE;
                     pos = 0;
                  }
               }
            }

            // last token has only literals
            if (blockOffset == blockSize)
               break;

            // match distance is encoded in two bytes (little endian)
            unsigned int delta = getByte(userPtr);
            delta |= (unsigned int)getByte(userPtr) << 8;
            // zero isn't allowed
            if (delta == 0)
               unlz4error("invalid offset");
            blockOffset += 2;

            // match length (always >= 4, therefore length is stored minus 4)
            unsigned int matchLength = 4 + (token & 0x0F);
            if (matchLength == 4 + 0x0F)
            {
               unsigned char current;
               do // match length encoded in more than 1 byte
               {
                  current = getByte(userPtr);
                  matchLength += current;
                  blockOffset++;
               } while (current == 255);
            }

            // copy match
            unsigned int referencePos = (pos >= delta) ? (pos - delta) : (HISTORY_SIZE + pos - delta);
            // start and end within the current 64k block ?
            if (pos + matchLength < HISTORY_SIZE && referencePos + matchLength < HISTORY_SIZE)
            {
               // read/write continuous block (no wrap-around at the end of history[])
               // fast copy
               if (pos >= referencePos + matchLength || referencePos >= pos + matchLength)
               {
                  // non-overlapping
                  memcpy(history + pos, history + referencePos, matchLength);
                  pos += matchLength;
               }
               else
               {
                  // overlapping, slower byte-wise copy
                  while (matchLength-- > 0)
                     history[pos++] = history[referencePos++];
               }
            }
            else
            {
               // either read or write wraps around at the end of history[]
               while (matchLength-- > 0)
               {
                  // copy single byte
                  history[pos++] = history[referencePos++];

                  // cannot write anymore ? => wrap around
                  if (pos == HISTORY_SIZE)
                  {
                     // flush output buffer
                     sendBytes(history, HISTORY_SIZE, userPtr);
                     numWritten += HISTORY_SIZE;
                     pos = 0;
                  }
                  // wrap-around of read location
                  referencePos %= HISTORY_SIZE;
               }
            }
         }

         // all legacy blocks must be completely filled - except for the last one
         if (isLegacy && numWritten + pos < 8 * 1024 * 1024)
            break;
      }
      else
      {
         // copy uncompressed data and add to history, too (if next block is compressed and some matches refer to this block)
         while (blockSize-- > 0)
         {
            // copy a byte ...
            history[pos++] = getByte(userPtr);
            // ... until buffer is full => send to output
            if (pos == HISTORY_SIZE)
            {
               sendBytes(history, HISTORY_SIZE, userPtr);
               pos = 0;
            }
         }
      }

      if (hasBlockChecksum)
      {
         // ignore checksum, skip 4 bytes
         getByte(userPtr); getByte(userPtr); getByte(userPtr); getByte(userPtr);
      }
   }

   if (hasContentChecksum)
   {
      // ignore checksum, skip 4 bytes
      getByte(userPtr); getByte(userPtr); getByte(userPtr); getByte(userPtr);
   }

   // flush output buffer
   sendBytes(history, pos, userPtr);
}

/// old interface where getByte and sendBytes use global file handles
void unlz4(GET_BYTE getByte, SEND_BYTES sendBytes, const char* dictionary)
{
   unlz4_userPtr(getByte, sendBytes, dictionary, NULL);
}


// ==================== COMMAND-LINE HANDLING ====================


#include "smallz4.h"

struct LZ4Reader
{
   char fileBuffer[8192];
   char compBuffer[8192];

   uint64_t totalSizeRead = 0;
   uint64_t totalSizeReadStat = 0;
   uint64_t totalSizeCompressStat = 0;
   uint64_t totalSizeCompress = 0;
   uint64_t totalMBytes = 0;
   uint64_t lastMBytes = 0;
   uint64_t loopCount = 0;
   uint64_t dataChunkSize = 0;
   uint64_t dataReadSize = 0;
   uint64_t dataCompressSize = 0;
   bool     dataEof = false;

   std::shared_ptr<std::ifstream> srcFile = nullptr;
};


std::string testLZ4("0123456789abkzde_0123456bczefthi01234567abcdefgh0123456789ABCDEFGHIJKLMNOP");

size_t getBytesFromInTest(void* data, size_t numBytes, void* userPtr)
{
   static bool readStatus = false;

   LZ4Reader* lz4Reader = (LZ4Reader*)userPtr;

   if (readStatus) {
      lz4Reader->dataEof = true;
      return 0;
   }

   memcpy((char*)data, testLZ4.c_str(), testLZ4.size());
   memcpy(lz4Reader->fileBuffer + lz4Reader->dataReadSize, data, testLZ4.size());

   readStatus = true;

   return testLZ4.size();
}

size_t getBytesFromIn(void* data, size_t numBytes, void* userPtr)
{
   if (data && numBytes > 0) {
      LZ4Reader* lz4Reader = (LZ4Reader*)userPtr;
      uint64_t remainToRead = lz4Reader->dataChunkSize - lz4Reader->dataReadSize;
      uint64_t numToTread = remainToRead >= numBytes ? numBytes : remainToRead;
      if (numToTread == 0) {
         return 0;
      }
      try {
         lz4Reader->srcFile->read((char*)data, numToTread);
         memcpy(lz4Reader->fileBuffer + lz4Reader->dataReadSize, data, numToTread);
      }
      catch (std::system_error& e) {
         if (lz4Reader->srcFile->eof()) {
            std::cout << std::endl;
            lz4Reader->dataEof = true;
            return 0;
         }
         std::cerr << e.code().message() << std::endl;
         exit(-4);
      }
      lz4Reader->dataReadSize += numToTread;
      lz4Reader->totalSizeRead += numToTread;
      lz4Reader->totalMBytes = lz4Reader->totalSizeRead >> 20;
      if (lz4Reader->lastMBytes != lz4Reader->totalMBytes) {
         lz4Reader->lastMBytes = lz4Reader->totalMBytes;
         std::cout << " \r MBytes Read = " << lz4Reader->totalMBytes;
      }
      lz4Reader->loopCount++;
      return numToTread;
   }
   return 0;
}


void sendBytesToOut(const void* data, size_t numBytes, void* userPtr)
{
   LZ4Reader* lz4Reader = (LZ4Reader*)userPtr;
   if (data && numBytes > 0)
   {
      char* compData = (char*)data;

      for (unsigned int i = 0; i < numBytes; i++) {
         if (lz4Reader->dataCompressSize == 8192) {
            std::cerr << "Compression buffer overflow" << std::endl;
            exit(-6);
         }
         lz4Reader->totalSizeCompress++;
         lz4Reader->compBuffer[lz4Reader->dataCompressSize++] = *compData++;
      }
   }
}


int main()
{
   LZ4Reader lz4Reader;
   LZ4DecompReader lz4DecompReader;
   std::vector<unsigned char> dictionary;
   bool useLegacy = false;
   unsigned short searchWindow = 65535;

   uint32_t dataChunk[] = { 256,512,1024,2048,4096 };

   std::list<std::shared_ptr<std::ifstream>> corpusList;
   std::shared_ptr<unsigned char> fileBuffer = std::shared_ptr<unsigned char>(new unsigned char[4096]);
   std::shared_ptr<unsigned char> deflateBuffer = std::shared_ptr<unsigned char>(new unsigned char[4096 * 2]);

   //std::string listFileName("C:\\Users\\gbonneau\\git\\zlib\\data\\calgary_corpus.txt");
   //std::string listFileName("C:\\Users\\gbonneau\\git\\zlib\\data\\enwik9.txt");
   std::string listFileName("C:\\Users\\gbonneau\\git\\zlib\\data\\silicia_corpus.txt");
   //std::string listFileName("C:\\Users\\gbonneau\\git\\zlib\\data\\canterbury_corpus.txt");


   std::string srcPathFileName;
   std::ifstream listFile;

   listFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

   try {
      listFile.open(listFileName.c_str());
   }
   catch (std::system_error& e) {
      std::cerr << e.code().message() << std::endl;
      exit(-1);
   }

   while (true) {
      srcPathFileName.clear();
      try {
         std::getline(listFile, srcPathFileName);
      }
      catch (std::system_error& e) {
         if (listFile.eof()) {
            break;
         }
         std::cerr << e.code().message() << std::endl;
         exit(-5);
      }
      std::shared_ptr<std::ifstream> srcFile = std::make_shared<std::ifstream>();
      srcFile->exceptions(std::ifstream::failbit | std::ifstream::badbit | std::ifstream::eofbit);
      corpusList.push_back(srcFile);

      try {
         srcFile->open(srcPathFileName.c_str(), std::ios::in | std::ios::binary);
      }
      catch (std::system_error& e) {
         std::cerr << e.code().message() << std::endl;
         exit(-1);
      }
   }
   listFile.close();

   std::string srcStatName = listFileName.substr(listFileName.find_last_of("/\\") + 1);

   for (uint32_t chunckIndex = 0; chunckIndex < 1; chunckIndex++)
   {
      std::shared_ptr<std::map<uint32_t, uint32_t>> compStatistic = std::shared_ptr<std::map<uint32_t, uint32_t>>(new std::map<uint32_t, uint32_t>);
      std::string statFileName = std::string("lz4_") + srcStatName + "_" + std::to_string(dataChunk[chunckIndex]) + ".csv";

      std::ofstream statFile;
      statFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);

      try {
         statFile.open(statFileName.c_str(), std::ios::out | std::ios::trunc);
      }
      catch (std::system_error& e) {
         std::cerr << "Failed to open File " << statFileName << std::endl;
         std::cerr << e.code().message() << std::endl;
         exit(-1);
      }

      statFile << "Source File Name = " << srcPathFileName << std::endl << std::endl;
      statFile << "Memory Chunck Size = " << dataChunk[chunckIndex] << std::endl << std::endl;

      for (uint32_t i = 0; i < (dataChunk[chunckIndex] + 64); i++) {
         (*compStatistic)[i] = 0;
      }

      uint64_t compThreshold = (double)dataChunk[chunckIndex] / 1.00;

      lz4Reader.totalSizeRead = 0;
      lz4Reader.totalSizeReadStat = 0;
      lz4Reader.totalSizeCompress = 0;
      lz4Reader.totalSizeCompressStat = 0;
      lz4Reader.dataCompressSize = 0;
      lz4Reader.dataReadSize = 0;
      lz4Reader.dataCompressSize = 0;
      lz4Reader.totalMBytes = 0;
      lz4Reader.lastMBytes = 0;
      lz4Reader.loopCount = 0;
      lz4Reader.dataEof = false;
      lz4Reader.dataChunkSize = dataChunk[chunckIndex];

      for (auto iterFile = corpusList.begin(); iterFile != corpusList.end(); ++iterFile) {

         lz4Reader.srcFile = *iterFile;
         lz4Reader.srcFile->clear();
         lz4Reader.srcFile->seekg(0);

         lz4Reader.dataEof = false;
         lz4Reader.dataCompressSize = 0;
         lz4Reader.dataReadSize = 0;

         while (true) {

            smallz4::lz4(getBytesFromIn, sendBytesToOut, searchWindow, dictionary, useLegacy, &lz4Reader);
            //smallz4::lz4(getBytesFromInTest, sendBytesToOut, searchChainLength, dictionary, useLegacy, &lz4Reader);

            if (lz4Reader.dataEof) {
               break;
            }

            if (lz4Reader.dataCompressSize <= compThreshold) {
               lz4Reader.totalSizeReadStat += lz4Reader.dataReadSize;
               lz4Reader.totalSizeCompressStat += lz4Reader.dataCompressSize;

               //double ratio = (((double)dataChunk[chunckIndex] / (double)lz4Reader.dataCompressSize)) * 100.0;
               //uint64_t intRatio = ratio;
               //intRatio = intRatio / 5;
               //intRatio = intRatio * 5;
               //double compChunckSize = 4096.0 / ((double)intRatio/100.0);
               //uint64_t intCompressSize = compChunckSize;

               //(*compStatistic)[intCompressSize]++;
               (*compStatistic)[lz4Reader.dataCompressSize]++;
            }
            lz4DecompReader.available = lz4Reader.dataCompressSize;
            lz4DecompReader.compBuffer = lz4Reader.compBuffer;
            lz4DecompReader.decompPos = 0;

            unlz4_userPtr(getByteFromIn, sendBytesToOut, nullptr, &lz4DecompReader);

            int retCmp = std::strncmp(lz4Reader.fileBuffer, lz4DecompReader.decompBuffer, dataChunk[chunckIndex]);
            
            if (retCmp != 0) {
               assert(false);
               exit(-4);
            }
            //(*compStatistic)[lz4Reader.dataCompressSize]++;
            lz4Reader.dataCompressSize = 0;
            lz4Reader.dataReadSize = 0;
   
         }
      }

      for (uint32_t i = 1; i < (dataChunk[chunckIndex] + 64); i++) {
         if ((*compStatistic)[i]) {
            std::cout << "compression size = " << i << ", static count = " << (*compStatistic)[i] << std::endl;
            statFile << std::fixed << std::setprecision(2) << i << "," << ((double)dataChunk[chunckIndex] / (double)i) << "," << (*compStatistic)[i] << std::endl;
         }
      }
      std::cout << "Compression ratio (>1.00) = " << (double)lz4Reader.totalSizeReadStat / (double)lz4Reader.totalSizeCompressStat << std::endl << std::endl;
      std::cout << "Compression ratio = " << (double)lz4Reader.totalSizeRead / (double)lz4Reader.totalSizeCompress << std::endl << std::endl;
      statFile << std::endl << "Compression ratio = " << (double)lz4Reader.totalSizeReadStat / (double)lz4Reader.totalSizeCompressStat << std::endl << std::endl;
      statFile.close();
   }
   for (auto iterFile = corpusList.begin(); iterFile != corpusList.end(); ++iterFile) {
      std::shared_ptr<std::ifstream> srcFile = *iterFile;
      srcFile->close();
   }
}
