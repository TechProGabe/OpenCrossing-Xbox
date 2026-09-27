#ifndef JSUIOSBASE_H
#define JSUIOSBASE_H

#include "types.h"
#include "JSystem/JSupport/JSUStreamEnum.h"

#ifdef __cplusplus
class JSUIosBase {
  public:
    inline JSUIosBase() : mState(GOOD) {
    }

    virtual ~JSUIosBase() {
    }

    bool isGood() {
        return !this->mState;
    }
    void clrState(EIoState ioState) {
        this->mState &= ~ioState;
    }
    void setState(EIoState ioState) {
        this->mState |= ioState;
    }
#ifdef TARGET_XBOX
    /* Callers pass stdio's EOF macro (-1); GCC -fpermissive accepted the
       int->enum conversion, clang has no -fpermissive. Same bits. */
    void setState(int ioState) {
        this->mState |= ioState;
    }
    void clrState(int ioState) {
        this->mState &= ~ioState;
    }
#endif

    u8 mState;
};
#endif

#endif
