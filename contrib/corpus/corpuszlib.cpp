// corpuszlib.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <map>
#include <cassert>


//#define _CRTDBG_MAP_ALLOC
//#include <stdlib.h>
//#include <crtdbg.h>
//
//#ifdef _DEBUG
//#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
//#else
//#define DBG_NEW new
//#endif

extern "C" {
#include "zlib.h"
}

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

   //_CrtSetAllocHook(MyAllocHook);

   std::shared_ptr<unsigned char> fileBuffer = std::shared_ptr<unsigned char>(new unsigned char[256]);
   std::shared_ptr<unsigned char> deflateBuffer = std::shared_ptr<unsigned char>(new unsigned char[512]);
   std::shared_ptr<std::map<uint32_t, huffMethod>> compStatistic = std::shared_ptr<std::map<uint32_t, huffMethod>>(new std::map<uint32_t, huffMethod>);

   uint32_t chunckIndex = 4;
   uint32_t dataChunk[] = { 256,512,1024,2048,4096 };

   for (uint32_t i = 0; i < (dataChunk[chunckIndex] + 32); i++) {
      (*compStatistic)[i].staticSize = 0;
      (*compStatistic)[i].dynamicSize = 0;
   }

   std::string listFileName("C:\\Users\\gbonneau\\git\\zlib\\data\\listFile.txt");
   std::string srcPathFileName;

   std::ifstream listFile;
   std::ifstream srcFile;

   listFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

   try {
      listFile.open(listFileName.c_str());
   }
   catch (std::system_error& e) {
      std::cerr << e.code().message() << std::endl;
      exit(-1);
   }
   
   std::getline(listFile, srcPathFileName);
   if (srcPathFileName.empty()) {
      std::cout << "Filename from list file is empty" << std::endl;
      exit(-2);
   }

   srcFile.open(srcPathFileName.c_str(), std::ios::in | std::ios::binary);
   if (srcFile.fail()) {
      std::cout << "Failed to open File " << listFileName << std::endl;
      exit(-1);
   }
   std::string srcFileName = srcPathFileName.substr(srcPathFileName.find_last_of("/\\") + 1);
   std::string statFileName = std::string("deflate_") + srcFileName + "_" + std::to_string(dataChunk[chunckIndex]) + ".csv";

   std::ofstream statFile;
   statFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);

   try {
      statFile.open(statFileName.c_str(), std::ios::out | std::ios::trunc);
   }
   catch (std::system_error& e) {
      std::cerr << "Failed to open File " << "deflateStat.csv" << std::endl;
      std::cerr << e.code().message() << std::endl;
      exit(-1);
   }

   statFile << "Source File Name = " << srcPathFileName << std::endl << std::endl;

   uint64_t totalSizeRead = 0;
   uint64_t totalSizeCompress[2] = {};
   uint64_t totalMBytes = 0;
   int32_t huffStrategy[2] = { Z_FIXED, Z_DEFAULT_STRATEGY };
   int32_t huffIndex = 0;

   statFile << "Memory Chunck Size = " << dataChunk[chunckIndex] << std::endl << std::endl;
   uint64_t loopCount = 0;

   while (true) {
      srcFile.read((char*)fileBuffer.get(), dataChunk[chunckIndex]);
      if (!srcFile) {
         std::cout << "EOF reached " << srcFile.gcount() << " could be read" << std::endl;
         break;
      }
      totalSizeRead += dataChunk[chunckIndex];
      totalMBytes = totalSizeRead >> 20;
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
         zcprDeflate.avail_out = dataChunk[chunckIndex]*2;

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

         totalSizeCompress[huffIndex] += zcprDeflate.total_out;

         zcprInflate.next_in = deflateBuffer.get();
         zcprInflate.next_out = inflateBuffer.get();
         zcprInflate.avail_in = zcprDeflate.total_out;
         zcprInflate.avail_out = dataChunk[chunckIndex]*2;

         inflateInit(&zcprInflate);
         ret = inflate(&zcprInflate, Z_FINISH);
         inflateEnd(&zcprInflate);

         int retCmp = std::strncmp((const char*)fileBuffer.get(), (const char*)inflateBuffer.get(), dataChunk[chunckIndex]);

         if (retCmp != 0) {
            assert(false);
            exit(-4);
         }
         huffStrategy[huffIndex] == Z_FIXED ? (*compStatistic)[zcprDeflate.total_out].staticSize++ : (*compStatistic)[zcprDeflate.total_out].dynamicSize++;
      }
   }

   for (uint32_t i = 0; i < (dataChunk[chunckIndex] + 16); i++) {
      std::cout << "compression size = " << i << ", static count = " << (*compStatistic)[i].staticSize << ", dynamic count = " << (*compStatistic)[i].dynamicSize << std::endl;
      statFile << i << "," << (*compStatistic)[i].staticSize << "," << (*compStatistic)[i].dynamicSize << std::endl;
   }
   std::cout << "Compression ratio static Huffman = " << (double)totalSizeRead / (double)totalSizeCompress[0] << " dynamic Huffman = " << (double)totalSizeRead / (double)totalSizeCompress[1] << std::endl;

   listFile.close();
   srcFile.close();
   statFile.close();
}

