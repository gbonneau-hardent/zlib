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

   for (uint32_t chunckIndex = 4; chunckIndex < 5; chunckIndex++)
   {
      std::shared_ptr<std::map<uint32_t, huffMethod>> compStatistic = std::shared_ptr<std::map<uint32_t, huffMethod>>(new std::map<uint32_t, huffMethod>);
      std::string statFileName = std::string("deflate_") + srcStatName + "_" + std::to_string(dataChunk[chunckIndex]) + ".csv";

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

      for (uint32_t i = 0; i < (dataChunk[chunckIndex] + 32); i++) {
         (*compStatistic)[i].staticSize = 0;
         (*compStatistic)[i].dynamicSize = 0;
      }

      uint64_t compThreshold = 0;
      uint64_t totalSizeRead = 0;
      uint64_t totalSizeReadStat = 0;
      uint64_t totalSizeCompress[2] = {};
      uint64_t totalMBytes = 0;
      uint64_t lastMBytes = 0;
      uint64_t loopCount = 0;

      compThreshold = (double)dataChunk[chunckIndex] / 1.25;

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

            for (uint32_t huffIndex = 0; huffIndex < 2; huffIndex++) {

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

               deflateInit2(&zcprDeflate, cprLevel, Z_DEFLATED, MAX_WBITS, MAX_MEM_LEVEL, huffStrategy[huffIndex]);
               ret = deflate(&zcprDeflate, Z_FINISH);
               deflateEnd(&zcprDeflate);

               std::shared_ptr<unsigned char> inflateBuffer = std::shared_ptr<unsigned char>(new unsigned char[(dataChunk[chunckIndex])]);

               z_stream zcprInflate;
               ret = Z_OK;
               memset(&zcprInflate, 0, sizeof(z_stream));

               if (zcprDeflate.total_out <= compThreshold) {
                  totalSizeCompress[huffIndex] += zcprDeflate.total_out;
                  totalSizeReadStat += dataChunk[chunckIndex];
               }

               zcprInflate.next_in = deflateBuffer.get();
               zcprInflate.next_out = inflateBuffer.get();
               zcprInflate.avail_in = zcprDeflate.total_out;
               zcprInflate.avail_out = dataChunk[chunckIndex] * 2;

//#define PUFF_INFLATE
#ifdef PUFF_INFLATE
               unsigned long puffInSize = zcprDeflate.total_out - 2;  // No dictionnary thus zlib header is 2 bytes and must be skipped. See RFC 1950
               int result = puff(inflateBuffer.get(), (unsigned long *)&(dataChunk[chunckIndex]), deflateBuffer.get()+2, &puffInSize);
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
               huffStrategy[huffIndex] == Z_FIXED ? (*compStatistic)[zcprDeflate.total_out].staticSize++ : (*compStatistic)[zcprDeflate.total_out].dynamicSize++;
            }
         }
      }

      for (uint32_t i = 1; i < (dataChunk[chunckIndex] + 16); i++) {
         std::cout << "compression size = " << i << ", static count = " << (*compStatistic)[i].staticSize << ", dynamic count = " << (*compStatistic)[i].dynamicSize << std::endl;
         statFile << std::fixed << std::setprecision(2) << i << "," << ((double)dataChunk[chunckIndex] / (double)i) << "," << (*compStatistic)[i].staticSize << "," << (*compStatistic)[i].dynamicSize << std::endl;
      }
      std::cout << "Compression ratio static Huffman = " << (double)totalSizeReadStat / (double)totalSizeCompress[0] << " dynamic Huffman = " << (double)totalSizeReadStat / (double)totalSizeCompress[1] << std::endl << std::endl;
      statFile << std::endl << "Compression ratio static Huffman = " << (double)totalSizeReadStat / (double)totalSizeCompress[0] << " dynamic Huffman = " << (double)totalSizeReadStat / (double)totalSizeCompress[1] << std::endl << std::endl;;
      statFile.close();
   }
   for (auto iterFile = corpusList.begin(); iterFile != corpusList.end(); ++iterFile) {
      std::shared_ptr<std::ifstream> srcFile = *iterFile;
      srcFile->close();
   }
}

