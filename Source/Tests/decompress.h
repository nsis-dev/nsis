#include "../zstd/lib/zstd.h"

class IDecompressor {
public:

  virtual ~IDecompressor() {};

  virtual void init() = 0;
  virtual void setNextIn(void *buffer, int size) = 0;
  virtual void setNextOut(void *buffer, int size) = 0;
  virtual int getAvailOut() = 0;
  virtual int decompress() = 0;

};

class lzmaDecompressor : public IDecompressor {
public:

  lzmaDecompressor();
  virtual ~lzmaDecompressor();

  virtual void init();
  virtual void setNextIn(void *buffer, int size);
  virtual void setNextOut(void *buffer, int size);
  virtual int getAvailOut();
  virtual int decompress();

private:

  void *vs;

};

class bzip2Decompressor : public IDecompressor {
public:

  bzip2Decompressor();
  virtual ~bzip2Decompressor();

  virtual void init();
  virtual void setNextIn(void *buffer, int size);
  virtual void setNextOut(void *buffer, int size);
  virtual int getAvailOut();
  virtual int decompress();

private:

  void *vs;

};

class zlibDecompressor : public IDecompressor {
public:

  zlibDecompressor();
  virtual ~zlibDecompressor();

  virtual void init();
  virtual void setNextIn(void *buffer, int size);
  virtual void setNextOut(void *buffer, int size);
  virtual int getAvailOut();
  virtual int decompress();

private:

  void *vs;

};

class zstdDecompressor : public IDecompressor {
public:

  zstdDecompressor();
  virtual ~zstdDecompressor();

  virtual void init();
  virtual void setNextIn(void *buffer, int size);
  virtual void setNextOut(void *buffer, int size);
  virtual int getAvailOut();
  virtual int decompress();

private:

  ZSTD_DStream *ctx;
  ZSTD_outBuffer output;
  ZSTD_inBuffer input;
  
};
