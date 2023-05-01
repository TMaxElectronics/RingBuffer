#include <stdint.h>
#include <string.h>
#include "RingBuffer.h"
#include "FreeRTOS.h"
#include "task.h"

RingBuffer_t * RingBuffer_create(uint32_t bufferSize, uint32_t dataSize){
    if(bufferSize < 2 || dataSize == 0) return NULL;
    
    //allocate descriptor
    RingBuffer_t * ret = pvPortMalloc(sizeof(RingBuffer_t));
    memset(ret, 0, sizeof(RingBuffer_t));
    
    //allocate memory
    ret->mem = pvPortMalloc(bufferSize * dataSize);
    memset(ret->mem, 0, bufferSize * dataSize);
    
    //data sizes
    ret->memSize = bufferSize * dataSize;
    ret->dataCount = bufferSize;
    ret->dataSize = dataSize;
    
    //pointers are cleared by the memclear operation after descriptor allocation
    
    return ret;
}

/* thoughts regarding thread safe reading and writing:
 * 
 * in order for a read to be valid we need to make sure that the position read has been written completely before accessing it. The solution: increase the write pointer only after every buffer write operation is done.
 * 
 * same goes for writing. We must ensure that a write doesn't happen if the position we write to might still be read at the time of writing. So we must only to increase the read pointer after the read operation has finished
 */


int32_t RingBuffer_write(RingBuffer_t * buffer, void* src, uint32_t length){
    if(buffer == NULL || src == NULL) return -1;
    
    //make sure there is enough space in the buffer
    if(RingBuffer_getSpaceCount(buffer) < length){
        //nope there isn't, return with an error
        //TODO maybe only write some data now?
        return -1;
    }
    
    //make sure we don't get interrupted while touching the data (?° ?? ?°)
    vTaskEnterCritical();
    
    uint32_t currIndex = buffer->writeIndex;
    uint32_t currItem = 0;
    
    while(currItem < length){
        //Calculate the pointer from the current index. This is potentially a bit shady? Idk (TODO evaluate?). Because our datasize is dynamic we need to do an integer addition to the pointer
        void* dstPtr = (void*) ((uint32_t) buffer->mem + currIndex * buffer->dataSize);
        void* srcPtr = (void*) ((uint32_t) src + currItem * buffer->dataSize);

        //copy the data
        memcpy(dstPtr, srcPtr, buffer->dataSize);
        
        //go to the next entry
        currItem++;
        currIndex++;
        if(currIndex >= buffer->dataCount) currIndex = 0;
    }
    
    //and finally increase the write index
    buffer->writeIndex = currIndex;
    
    //and release the status again
    vTaskExitCritical();
    
    return length;
}

int32_t RingBuffer_read(RingBuffer_t * buffer, void* dst, uint32_t length){
    if(buffer == NULL || dst == NULL) return -1;
   
    //make sure there is enough data to read
    if(RingBuffer_getDataCount(buffer) < length){
        //return with an error
        //TODO also check if we might want to read only the available data here
        return -1;
    }
    
    //make sure we don't get interrupted while touching the data (?° ?? ?°)
    vTaskEnterCritical();
    
    uint32_t currIndex = buffer->readIndex;
    uint32_t currItem = 0;
    
    while(currItem < length){
        //Calculate the pointer from the current index. This is potentially a bit shady? Idk (TODO evaluate?). Because our datasize is dynamic we need to do an integer addition to the pointer
        void* dstPtr = (void*) ((uint32_t) dst + currItem * buffer->dataSize);
        void* srcPtr = (void*) ((uint32_t) buffer->mem + currIndex * buffer->dataSize);

        //copy the data
        memcpy(dstPtr, srcPtr, buffer->dataSize);
        
        //go to the next entry
        currItem++;
        currIndex++;
        if(currIndex >= buffer->dataCount) currIndex = 0;
    }
    
    //and finally update the read index
    buffer->readIndex = currIndex;
    
    //and release the status again
    vTaskExitCritical();
    
    return length;
}

int32_t RingBuffer_readFromISR(RingBuffer_t * buffer, void* dst, uint32_t length){
    if(buffer == NULL || dst == NULL) return -1;
   
    //make sure there is enough data to read
    if(RingBuffer_getDataCount(buffer) < length){
        //return with an error
        //TODO also check if we might want to read only the available data here
        return -1;
    }
    //make sure we don't get interrupted while touching the data (?° ?? ?°)
    UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    
    uint32_t currIndex = buffer->readIndex;
    uint32_t currItem = 0;
    
    while(currItem < length){
        //Calculate the pointer from the current index. This is potentially a bit shady? Idk (TODO evaluate?). Because our datasize is dynamic we need to do an integer addition to the pointer
        void* dstPtr = (void*) ((uint32_t) dst + currItem * buffer->dataSize);
        void* srcPtr = (void*) ((uint32_t) buffer->mem + currIndex * buffer->dataSize);

        //copy the data
        memcpy(dstPtr, srcPtr, buffer->dataSize);
        
        //go to the next entry
        currItem++;
        currIndex++;
        if(currIndex >= buffer->dataCount) currIndex = 0;
    }
    
    //and finally update the read index
    buffer->readIndex = currIndex;
    
    //and release the status again
    taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
    
    return length;
}

void RingBuffer_flush(RingBuffer_t * buffer){
    buffer->readIndex = buffer->writeIndex;
}

uint32_t RingBuffer_size(RingBuffer_t * buffer){
    return buffer->dataCount;
}

uint32_t RingBuffer_sizeInBytes(RingBuffer_t * buffer){
    return buffer->memSize;
}

uint32_t RingBuffer_getDataCount(RingBuffer_t * buffer){
    uint32_t dataCount = buffer->writeIndex - buffer->readIndex;
    if(dataCount < 0) dataCount += buffer->dataCount;
    return dataCount;
}

uint32_t RingBuffer_getSpaceCount(RingBuffer_t * buffer){
    uint32_t dataCount = buffer->readIndex - buffer->writeIndex - 1;
    if(dataCount < 0) dataCount += buffer->dataCount;
    return dataCount;
}