// corpuszlib.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <map>
#include <list>
#include <iomanip>
#include <cassert>


//#define _CRTDBG_MAP_ALLOC
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifdef _DEBUG
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#else
#define DBG_NEW new
#endif
#endif

extern "C" {
#include "zlib.h"
}
#include "..\puff\puff.h"

struct huffMethod
{
   int32_t staticSize;
   int32_t dynamicSize;
};

#ifdef _CRTDBG_MAP_ALLOC
int __cdecl MyAllocHook(
   int      nAllocType,
   void   * pvData,
   size_t   nSize,
   int      nBlockUse,
   long     lRequest,
   const unsigned char * szFileName,
   int      nLine
   )
{
   const char *operation[] = { "", "allocating", "re-allocating", "freeing" };
   const char *blockType[] = { "Free", "Normal", "CRT", "Ignore", "Client" };

   if ( nBlockUse == _CRT_BLOCK )   // Ignore internal C runtime library allocations
      return( TRUE );

   _ASSERT( ( nAllocType > 0 ) && ( nAllocType < 4 ) );
   _ASSERT( ( nBlockUse >= 0 ) && ( nBlockUse < 5 ) );

   if (nSize != 0) {
      fprintf(stdout, "Memory operation in %s, line %d: %s a %d-byte '%s' block (#%ld)\n", szFileName, nLine, operation[nAllocType], nSize, blockType[nBlockUse], lRequest);
   }
   if ( pvData != NULL )
      fprintf(stdout, " at %p", pvData );

   return( TRUE );         // Allow the memory operation to proceed
}
#endif


int main()
{
#ifdef _CRTDBG_MAP_ALLOC
   _CrtSetAllocHook(MyAllocHook);
#endif

   int32_t huffStrategy[2] = { Z_FIXED, Z_DEFAULT_STRATEGY };
   uint32_t dataChunk[] = { 256,512,1024,2048,4096 };

   std::list<std::shared_ptr<std::ifstream>> corpusList;
   std::shared_ptr<unsigned char> fileBuffer = std::shared_ptr<unsigned char>(new unsigned char[4096]);
   std::shared_ptr<unsigned char> deflateBuffer = std::shared_ptr<unsigned char>(new unsigned char[4096*2]);

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
      catch(std::system_error& e) {
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

   for (uint32_t chunckIndex = 4; chunckIndex < 5; chunckIndex++) {

      std::shared_ptr<std::map<uint32_t, huffMethod>> compStatistic = std::shared_ptr<std::map<uint32_t, huffMethod>>(new std::map<uint32_t, huffMethod>);
      std::string statFileName = std::string("deflate_") + srcStatName + "_" + std::to_string(dataChunk[chunckIndex]) + ".csv";
      std::string statLatencyFileName = std::string("deflate_latency_") + srcStatName + "_" + std::to_string(dataChunk[chunckIndex]) + ".csv";
      std::string statSymbolFileName = std::string("deflate_symbol_") + srcStatName + "_" + std::to_string(dataChunk[chunckIndex]) + ".csv";

      std::shared_ptr<symbolStats> memChunk = std::shared_ptr<symbolStats>(new symbolStats());
      std::shared_ptr<symbolStats> allChunk[2][49];
      
      for (uint32_t i = 0; i < 49; i++) {
         allChunk[0][i] = std::shared_ptr<symbolStats>(new symbolStats());
         allChunk[1][i] = std::shared_ptr<symbolStats>(new symbolStats());
      }

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

      std::ofstream statLatencyFile;
      statLatencyFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);

      try {
         statLatencyFile.open(statLatencyFileName.c_str(), std::ios::out | std::ios::trunc);
      }
      catch (std::system_error& e) {
         std::cerr << "Failed to open File " << statLatencyFileName << std::endl;
         std::cerr << e.code().message() << std::endl;
         exit(-1);
      }

      std::ofstream statSymbolFile;
      statSymbolFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);

      try {
         statSymbolFile.open(statSymbolFileName.c_str(), std::ios::out | std::ios::trunc);
      }
      catch (std::system_error& e) {
         std::cerr << "Failed to open File " << statLatencyFileName << std::endl;
         std::cerr << e.code().message() << std::endl;
         exit(-1);
      }

      statLatencyFile << "Source File Name = " << srcStatName << std::endl << std::endl;
      statLatencyFile << "Memory Chunck Size = " << dataChunk[chunckIndex] << std::endl << std::endl;

      statSymbolFile << "Source File Name = " << srcStatName << std::endl << std::endl;
      statSymbolFile << "Memory Chunck Size = " << dataChunk[chunckIndex] << std::endl << std::endl;


      for (uint32_t i = 0; i < (dataChunk[chunckIndex] + 32); i++) {
         (*compStatistic)[i].staticSize = 0;
         (*compStatistic)[i].dynamicSize = 0;
      }

      uint64_t compThreshold = 0;
      uint64_t totalSizeRead = 0;
      uint64_t totalChunkCount[2] = {};
      uint64_t totalChunkCompress[2] = {};
      uint64_t totalSizeReadStat[2] = {};
      uint64_t totalSizeCompress[2] = {};
      uint64_t totalSizeCompressStat[2] = {};
      uint64_t totalMBytes = 0;
      uint64_t lastMBytes = 0;
      uint64_t loopCount = 0;
      uint64_t decodeCount = 0;
      double   threshold = 0.0;

      threshold = dataChunk[chunckIndex] == 256 ? 1.00 : 1.25;
      threshold = 1.25;
      compThreshold = (double)dataChunk[chunckIndex] / threshold;

      uint32_t firstGroup = 16;
      uint32_t lastGroup = 64;

      for (auto iterFile = corpusList.begin(); iterFile != corpusList.end(); ++iterFile) {

         std::shared_ptr<std::ifstream> srcFile = *iterFile;
         srcFile->clear();
         srcFile->seekg(0);

         while (true) {
            try {
               srcFile->read((char*)fileBuffer.get(), dataChunk[chunckIndex]);
            }
            catch (std::system_error& e) {
               if (srcFile->eof()) {
                  std::cout << std::endl;
                  break;
               }
               std::cerr << e.code().message() << std::endl;
               exit(-4);
            }
            totalSizeRead += dataChunk[chunckIndex];
            totalMBytes = totalSizeRead >> 20;
            if (lastMBytes != totalMBytes) {
               lastMBytes = totalMBytes;
               std::cout << " \r MBytes Read = " << totalMBytes;
            }
            loopCount++;

            unsigned char* buffer = fileBuffer.get();
            int cprLevel = Z_DEFAULT_COMPRESSION;

            for (uint32_t huffMode = 0; huffMode < 2; huffMode++) {

               z_stream zcprDeflate;
               int ret = Z_OK;
               memset(&zcprDeflate, 0, sizeof(z_stream));

               zcprDeflate.next_in = fileBuffer.get();
               zcprDeflate.next_out = deflateBuffer.get();
               zcprDeflate.avail_in = dataChunk[chunckIndex];
               zcprDeflate.avail_out = dataChunk[chunckIndex] * 2;

               zcprDeflate.zalloc = nullptr;
               zcprDeflate.zfree = nullptr;
               zcprDeflate.opaque = nullptr;

               deflateInit2(&zcprDeflate, cprLevel, Z_DEFLATED, MAX_WBITS, MAX_MEM_LEVEL, huffStrategy[huffMode]);
               ret = deflate(&zcprDeflate, Z_FINISH);
               deflateEnd(&zcprDeflate);

               totalSizeCompress[huffMode] += zcprDeflate.total_out;

               std::shared_ptr<unsigned char> inflateBuffer = std::shared_ptr<unsigned char>(new unsigned char[(dataChunk[chunckIndex])]);

               z_stream zcprInflate;
               ret = Z_OK;
               memset(&zcprInflate, 0, sizeof(z_stream));

               if (zcprDeflate.total_out <= compThreshold) {

                  double ratio = (((double)dataChunk[chunckIndex] / (double)zcprDeflate.total_out)) * 100.0;
                  uint64_t intRatio = ratio;
                  intRatio = intRatio / 5;
                  intRatio = intRatio * 5;
                  double compChunckSize = (double)dataChunk[chunckIndex] / ((double)intRatio/100.0);
                  uint64_t intCompressSize = compChunckSize;

                  totalChunkCompress[huffMode]++;
                  huffStrategy[huffMode] == Z_FIXED ? (*compStatistic)[intCompressSize].staticSize++ : (*compStatistic)[intCompressSize].dynamicSize++;

                  totalSizeCompressStat[huffMode] += zcprDeflate.total_out;
                  totalSizeReadStat[huffMode] += dataChunk[chunckIndex];
               }
               else {
                  totalSizeCompressStat[huffMode] += dataChunk[chunckIndex];
                  totalSizeReadStat[huffMode] += dataChunk[chunckIndex];
               }

               zcprInflate.next_in = deflateBuffer.get();
               zcprInflate.next_out = inflateBuffer.get();
               zcprInflate.avail_in = zcprDeflate.total_out;
               zcprInflate.avail_out = dataChunk[chunckIndex] * 2;

               totalChunkCount[huffMode]++;

#define PUFF_INFLATE
#ifdef PUFF_INFLATE

               for (uint32_t groupSlice = firstGroup; groupSlice <= lastGroup; groupSlice++) {

                  memChunk->thresholdBits = groupSlice;

                  unsigned long puffInSize = zcprDeflate.total_out - 2;  // No dictionnary thus zlib header is 2 bytes and must be skipped. See RFC 1950
                  int result = puff(inflateBuffer.get(), (unsigned long*)&(dataChunk[chunckIndex]), deflateBuffer.get() + 2, &puffInSize, memChunk.get());

                  if (zcprDeflate.total_out <= compThreshold) {
                     for (auto mapIter = memChunk->bitsHistogram.begin(); mapIter != memChunk->bitsHistogram.end(); mapIter++) {
                        allChunk[huffMode][groupSlice-firstGroup]->bitsHistogram[mapIter->first] += mapIter->second;
                        allChunk[huffMode][groupSlice-firstGroup]->symbolHistogram[mapIter->first] += memChunk->symbolHistogram[mapIter->first];
                     }
                     for (auto mapIter = memChunk->charHistogram.begin(); mapIter != memChunk->charHistogram.end(); mapIter++) {
                        allChunk[huffMode][groupSlice-firstGroup]->charHistogram[mapIter->first] += mapIter->second;
                     }
                  }

                  memChunk->bitsHistogram.clear();
                  memChunk->symbolHistogram.clear();
                  memChunk->charHistogram.clear();

                  memChunk->totalBits = 0;
                  memChunk->numGroupDec = 0;
                  memChunk->groupSymbols = 0;
                  memChunk->groupCharacters = 0;

                  decodeCount++;
#else 
                  inflateInit(&zcprInflate);
                  ret = inflate(&zcprInflate, Z_FINISH);
                  inflateEnd(&zcprInflate);
#endif
                  int retCmp = std::strncmp((const char*)fileBuffer.get(), (const char*)inflateBuffer.get(), dataChunk[chunckIndex]);
                  
                  if (retCmp != 0) {
                     assert(false);
                     exit(-4);
                  }
               }
            }
         }
      }

      for (uint32_t i = 1; i < (dataChunk[chunckIndex] + 16); i++) {
         if (((*compStatistic)[i].staticSize == 0) && ((*compStatistic)[i].dynamicSize == 0))
            continue;
         std::cout << "compression size = " << i << ", static count = " << (*compStatistic)[i].staticSize << ", dynamic count = " << (*compStatistic)[i].dynamicSize << std::endl;
         statFile << std::fixed << std::setprecision(2) << i << "," << ((double)dataChunk[chunckIndex] / (double)i) << "," << (*compStatistic)[i].staticSize << "," << (*compStatistic)[i].dynamicSize << std::endl;
      }
      std::cout << std::fixed << std::setprecision(1) << std::endl;
      std::cout <<  "Total chunks (size = " << dataChunk[chunckIndex] << ") processed = " << totalChunkCount[0] << ", static chunk compressed = " << totalChunkCompress[0] << " (" << (totalChunkCompress[0]*100.0) / totalChunkCount[0] << "), dynamic chunck compressed = " << totalChunkCompress[1] << " (" << (totalChunkCompress[1] * 100.0) / totalChunkCount[0] << ")" << std::endl;
      std::cout << std::fixed << std::setprecision(2);
      std::cout << "Compression ratio with threshold (" << threshold << "), static Huffman = " << (double)totalSizeReadStat[0] / (double)totalSizeCompressStat[0] << ", dynamic Huffman = " << (double)totalSizeReadStat[1] / (double)totalSizeCompressStat[1] << std::endl;
      std::cout << "Compression ratio global (all chunk) static Huffman = " << (double)totalSizeRead / (double)totalSizeCompress[0] << " dynamic Huffman = " << (double)totalSizeRead / (double)totalSizeCompress[1] << std::endl;

      statFile << std::endl << "Compression ratio global (all chunk) static Huffman = " << (double)totalSizeRead / (double)totalSizeCompress[0] << " dynamic Huffman = " << (double)totalSizeRead / (double)totalSizeCompress[1] << std::endl << std::endl;;
      statFile.close();

      // Dump latency statistic.

      for (uint32_t huffmode = 0; huffmode < 2; huffmode++) {

         statLatencyFile << (huffmode == 0 ? "Huffman Static" : "Huffman Dynamic") << std::endl;   
         statLatencyFile << std::endl;

         statSymbolFile << (huffmode == 0 ? "Huffman Static" : "Huffman Dynamic") << std::endl;
         statSymbolFile << std::endl;

         for (uint32_t i = 0; i < (lastGroup - firstGroup + 1); i++) {

            uint32_t totalSymbols = 0;
            uint32_t totalGroups = 0;
            double averageSymbols = 0.0;

            for (auto mapIter = allChunk[huffmode][i]->symbolHistogram.begin(); mapIter != allChunk[huffmode][i]->symbolHistogram.end(); mapIter++) {

               if ((i + firstGroup) == 32) {
                  statSymbolFile << std::fixed << std::setprecision(2) << mapIter->first << "," << mapIter->second << std::endl;
               }

               totalSymbols += mapIter->first * mapIter->second;
               totalGroups += mapIter->second;
            }
            averageSymbols = double(totalSymbols) / double(totalGroups);

            if ((i + firstGroup) == 32) {
               statSymbolFile << std::endl;
               statSymbolFile << std::fixed << std::setprecision(2) << "Average decoded symbols per clock (32 slices) = " << averageSymbols << std::endl;
               statSymbolFile << std::endl;
               statSymbolFile << "Number of character decoded" << std::endl << std::endl;
            }

            totalGroups = 0;
            uint32_t totalCharacters = 0;
            double averageCharacters = 0.0;
            double average32Characters = 0.0;
            double average64Characters = 0.0;
            double average128Characters = 0.0;

            for (auto mapIter = allChunk[huffmode][i]->charHistogram.begin(); mapIter != allChunk[huffmode][i]->charHistogram.end(); mapIter++) {

               if ((i + firstGroup) == 32) {
                  statSymbolFile << std::fixed << std::setprecision(2) << mapIter->first << "," << mapIter->second << std::endl;
               }

               totalCharacters += mapIter->first * mapIter->second;
               totalGroups += mapIter->second;

               if (mapIter->first == 32) {
                  average32Characters = double(totalCharacters) / double(totalGroups);
               }
               else if (mapIter->first == 64) {
                  average64Characters = double(totalCharacters) / double(totalGroups);
               }
               else if (mapIter->first == 128) {
                  average128Characters = double(totalCharacters) / double(totalGroups);
               }
            }
            averageCharacters = double(totalCharacters) / double(totalGroups);
            statLatencyFile << std::fixed << std::setprecision(2) << "Decoded slices," << firstGroup + i << ",Average decoded characters per clock (32)," << average32Characters << ",Average decoded characters per clock (64)," << average64Characters << ",Average decoded characters per clock (128)," << average128Characters << ",Average decoded characters per clock," << averageCharacters << std::endl;
         }
         statLatencyFile << std::endl;
      }
      statLatencyFile.close();
      statSymbolFile.close();

      for (uint32_t i = 0; i < 49; i++) {
         allChunk[0][i].reset();
         allChunk[1][i].reset();
      }
   }
   for (auto iterFile = corpusList.begin(); iterFile != corpusList.end(); ++iterFile) {
      std::shared_ptr<std::ifstream> srcFile = *iterFile;
      srcFile->close();
   }
}

