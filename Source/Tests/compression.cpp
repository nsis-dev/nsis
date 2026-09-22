#include <cppunit/extensions/HelperMacros.h>
#include "../Platform.h"
#include "../growbuf.h"

#include <stdlib.h>
#include <time.h>

#include "decompress.h"

#include "../cbzip2.h"
#include "../clzma.h"
#include "../czlib.h"
#include "../czstd.h"

class CompressionTest : public CppUnit::TestFixture {

public:
  void randData(IGrowBuf &buf, int kb) {
    srand(time(0));

#define IsBug1156(r) ( ((r) & 0x80) == 0x80 )
    for (int i = 0; i < kb; i++) {
      int r;
      do 
        r = rand();
      while (IsBug1156(r)); // Temporary workaround for https://sf.net/p/nsis/bugs/1156/#zlibCompressionTest loops endlessly
      for (size_t j = 0; j < 1024/sizeof(int); j++) {
        buf.add(&r, sizeof(int));
      }
    }
  }

  // compressor must be initialized!
  void compress(ICompressor &compressor, IGrowBuf& in, IGrowBuf& out) {
    compressor.SetNextIn((char *) in.get(), in.getlen());

    int ret;

    do {
      char outbuf[1024];
      compressor.SetNextOut(outbuf, sizeof(outbuf));

      ret = compressor.Compress(C_FINISH);

      CPPUNIT_ASSERT_MESSAGE( compressor.GetErrStr(ret) , ret >= 0 );

      out.add(outbuf, sizeof(outbuf) - compressor.GetAvailOut());
    } while (ret == 0);
  }

  typedef void (*decompressInitPtr)(void *);
  typedef int (*decompressWorkPtr)(void *);

  void decompress(IDecompressor& decompressor, IGrowBuf& in, IGrowBuf& out) {
    decompressor.init();
    decompressor.setNextIn(in.get(), in.getlen());

    int ret;

    do {
      char outbuf[1024];
      decompressor.setNextOut(outbuf, sizeof(outbuf));

      ret = decompressor.decompress();

      CPPUNIT_ASSERT( ret >= 0 );

      out.add(outbuf, sizeof(outbuf) - decompressor.getAvailOut());

    } while (ret == 0);

  }

  // compressor must be initialized!
  void testCompressDecompress(int size_kb, ICompressor &compressor, IDecompressor& decompressor) {
    GrowBuf data;
    GrowBuf compressed;
    GrowBuf decompressed;

    randData(data, size_kb);

    compress(compressor, data, compressed);
    decompress(decompressor, compressed, decompressed);

    CPPUNIT_ASSERT_MESSAGE( "decompressed data is smaller", data.getlen() <= decompressed.getlen() );
    CPPUNIT_ASSERT_MESSAGE( "decompressed data is larger", data.getlen() >= decompressed.getlen() );
    CPPUNIT_ASSERT_EQUAL_MESSAGE( "decompressed data is different", 0, memcmp(data.get(), decompressed.get(), data.getlen()) );
  }

  void testCompressDecompress(ICompressor &compressor, IDecompressor& decompressor) {
    CPPUNIT_ASSERT_EQUAL( C_OK, compressor.Init(9, 1 << 23, 1024) );
    testCompressDecompress(1, compressor, decompressor);

    CPPUNIT_ASSERT_EQUAL( C_OK, compressor.Init(9, 1 << 23, 1024*1024) );
    testCompressDecompress(1024, compressor, decompressor);

#ifndef NSIS_TESTS_FASTCOMPRESSIONONLY
    CPPUNIT_ASSERT_EQUAL( C_OK, compressor.Init(9, 1 << 23, 8*1024*1024) );
    testCompressDecompress(8*1024, compressor, decompressor);

    CPPUNIT_ASSERT_EQUAL( C_OK, compressor.Init(9, 1 << 23, 32*1024*1024) );
    testCompressDecompress(32*1024, compressor, decompressor);
#endif
  }

};

class bzip2CompressionTest : public CompressionTest {

  CPPUNIT_TEST_SUITE( bzip2CompressionTest );
  CPPUNIT_TEST( test );
  CPPUNIT_TEST_SUITE_END();

public:

  void test() {
    CBzip2 compressor;
    bzip2Decompressor decompressor;

    testCompressDecompress(compressor, decompressor);
  }

};

class lzmaCompressionTest : public CompressionTest {

  CPPUNIT_TEST_SUITE( lzmaCompressionTest );
  CPPUNIT_TEST( test );
  CPPUNIT_TEST_SUITE_END();

public:

  void test() {
    CLZMA compressor;
    lzmaDecompressor decompressor;

    testCompressDecompress(compressor, decompressor);
  }

};

class zlibCompressionTest : public CompressionTest {

  CPPUNIT_TEST_SUITE( zlibCompressionTest );
  CPPUNIT_TEST( test );
  CPPUNIT_TEST_SUITE_END();

public:

  void test() {
    CZlib compressor;
    zlibDecompressor decompressor;

    testCompressDecompress(compressor, decompressor);
  }

};

class zstdCompressionTest : public CompressionTest {

  CPPUNIT_TEST_SUITE( zstdCompressionTest );
  CPPUNIT_TEST( test );
  CPPUNIT_TEST_SUITE_END();

public:

  void test() {
    CZstd compressor;
    zstdDecompressor decompressor;

    testCompressDecompress(compressor, decompressor);
  }

};

CPPUNIT_TEST_SUITE_REGISTRATION( bzip2CompressionTest );
CPPUNIT_TEST_SUITE_REGISTRATION( lzmaCompressionTest );
CPPUNIT_TEST_SUITE_REGISTRATION( zlibCompressionTest );
CPPUNIT_TEST_SUITE_REGISTRATION( zstdCompressionTest );
