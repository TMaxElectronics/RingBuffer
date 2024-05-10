#ifndef RB_INC
#define RB_INC
    
#include <stdint.h>

typedef struct{
    //buffer describing constants
    uint32_t memSize;
    uint32_t dataSize;
    uint32_t dataCount;
    uint32_t dataCountLimited;
    
    //buffer data
    void* mem;
    
    //read & write pointers. These always indicate what will be either written or read next
    volatile int32_t readIndex;
    volatile int32_t writeIndex;
} volatile RingBuffer_t;

RingBuffer_t * RingBuffer_create(uint32_t bufferSize, uint32_t dataSize);
void RingBuffer_delete(RingBuffer_t * buffer);
int32_t RingBuffer_read(RingBuffer_t * buffer, void* dst, uint32_t length);
int32_t RingBuffer_readBufferPointer(RingBuffer_t * buffer, void** dst, uint32_t length);
int32_t RingBuffer_readFromISR(RingBuffer_t * buffer, void* dst, int32_t length);
int32_t RingBuffer_peek(RingBuffer_t * buffer, void* dst, uint32_t index, uint32_t length);
int32_t RingBuffer_write(RingBuffer_t * buffer, void* src, int32_t length, uint32_t shiftOnOverflow);
int32_t RingBuffer_writeFromISR(RingBuffer_t * buffer, void* src, int32_t length, uint32_t shiftOnOverflow);
int32_t RingBuffer_writeSingleWord(RingBuffer_t * buffer, void* src);

void RingBuffer_flush(RingBuffer_t * buffer);
uint32_t RingBuffer_size(RingBuffer_t * buffer);
uint32_t RingBuffer_sizeInBytes(RingBuffer_t * buffer);
int32_t RingBuffer_getDataCount(RingBuffer_t * buffer);
int32_t RingBuffer_getSpaceCount(RingBuffer_t * buffer);
void RingBuffer_setSizeLimit(RingBuffer_t * buffer, uint32_t size);

#endif