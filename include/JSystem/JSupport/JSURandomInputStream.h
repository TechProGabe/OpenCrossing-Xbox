#ifndef JSURANDOMINPUTSTREAM_H
#define JSURANDOMINPUTSTREAM_H

#include "types.h"
#include "JSystem/JKernel/JKRFile.h"
#include "JSystem/JSupport/JSUInputStream.h"

#ifdef __cplusplus
class JSURandomInputStream : public JSUInputStream {
  public:
    virtual ~JSURandomInputStream() {
    }

    virtual int getAvailable() const {
        return this->getLength() - this->getPosition();
    }
    virtual int skip(s32 amount);
    virtual int readData(void* buf, s32 count) = 0;
    virtual int getLength() const = 0;
    virtual int getPosition() const = 0;
    virtual int seekPos(s32 offset, JSUStreamSeekFrom from) = 0;
#ifdef TARGET_XBOX
    int seekPos(s32 offset, int from) { return seekPos(offset, (JSUStreamSeekFrom)from); }
#endif

    int align(s32 alignment);
    int peek(void* buf, s32 len);
    int seek(s32 offset, JSUStreamSeekFrom from);
#ifdef TARGET_XBOX
    /* stdio SEEK_* macros are ints; clang rejects the implicit enum conversion. */
    int seek(s32 offset, int from) { return seek(offset, (JSUStreamSeekFrom)from); }
#endif
};

class JSURandomOutputStream : public JSUOutputStream {
  public:
    virtual ~JSURandomOutputStream() {
    }

    virtual int getAvailable() const;
    virtual int skip(s32 amount);
    virtual int readData(void* buf, s32 count) = 0;
    virtual int getLength() const = 0;
    virtual int getPosition() const = 0;
    virtual int seekPos(s32 offset, JSUStreamSeekFrom from) = 0;
#ifdef TARGET_XBOX
    int seekPos(s32 offset, int from) { return seekPos(offset, (JSUStreamSeekFrom)from); }
#endif

    int align(s32 alignment);
    int peek(void* buf, s32 len);
    int seek(s32 offset, JSUStreamSeekFrom from);
#ifdef TARGET_XBOX
    /* stdio SEEK_* macros are ints; clang rejects the implicit enum conversion. */
    int seek(s32 offset, int from) { return seek(offset, (JSUStreamSeekFrom)from); }
#endif
};

#endif

#endif
