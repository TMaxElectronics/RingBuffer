#include <stdint.h>
#include <string.h>
#include "include/RingBuffer.h"
#include "FreeRTOS.h"
#include "task.h"

RingBuffer_t * RingBuffer_create(uint32_t bufferSizeWords, uint32_t dataSize){
    if(bufferSizeWords < 2 || dataSize == 0) return NULL;
    
    //allocate descriptor
    RingBuffer_t * ret = pvPortMalloc(sizeof(RingBuffer_t));
    memset(ret, 0, sizeof(RingBuffer_t));
    
    //allocate memory
    ret->mem = pvPortMalloc(bufferSizeWords * dataSize);
    memset(ret->mem, 0, bufferSizeWords * dataSize);
    
    //data sizes
    ret->memSize = bufferSizeWords * dataSize;
    ret->dataCount = bufferSizeWords;
    ret->dataCountLimited = bufferSizeWords;
    ret->dataSize = dataSize;
    
    //pointers are cleared by the memclear operation after descriptor allocation
    
    return ret;
}

void RingBuffer_delete(RingBuffer_t * buffer){
    if(buffer == NULL) return;
    
    //make sure nothing writes into this buffer while we delete it
    vTaskEnterCritical();
    buffer->memSize = 0;
    
    //then free memory
    vPortFree(buffer->mem);
    vPortFree(buffer);
    
    vTaskExitCritical();
}

void RingBuffer_setSizeLimit(RingBuffer_t * buffer, uint32_t size){
    RingBuffer_flush(buffer);
    
    if(size > buffer->dataCount) size = buffer->dataCount;
    buffer->dataCountLimited = size;
}

/* thoughts regarding thread safe reading and writing:
 * 
 * in order for a read to be valid we need to make sure that the position read has been written completely before accessing it. The solution: increase the write pointer only after every buffer write operation is done.
 * 
 * same goes for writing. We must ensure that a write doesn't happen if the position we write to might still be read at the time of writing. So we must only to increase the read pointer after the read operation has finished
 * 
 * TODO this is certainly not valid for the shiftOnOverflow addition, verify and fix potential issues
 */
#pragma GCC push_options
#pragma GCC optimize ("Os")

static int32_t RingBuffer_writeToBuffer(RingBuffer_t * buffer, void* src, int32_t length, uint32_t shiftOnOverflow){
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
        if(currIndex >= buffer->dataCountLimited) currIndex = 0;
        
        //check if we overran the buffer, this should only happen with shiftOnOverflow enabled
        //TODO fix this? Seems like this wouldn't ever trigger
        if(shiftOnOverflow && currIndex == buffer->readIndex){
            //yes, we did overrun. Since shiftOnOverflow is enabled we want to offset the read pointer to point to the now oldest valid element
            buffer->readIndex++;
            if(buffer->readIndex >= buffer->dataCountLimited) buffer->readIndex = 0;
        }
    }
    
    //and finally increase the write index
    buffer->writeIndex = currIndex;
    return length;
}

int32_t RingBuffer_writeSingleWord(RingBuffer_t * buffer, void* src){
    if(buffer == NULL || src == NULL) return -1;
    
    uint32_t currIndex = buffer->writeIndex;
    
    //Calculate the pointer from the current index. This is potentially a bit shady? Idk (TODO evaluate?). Because our datasize is dynamic we need to do an integer addition to the pointer
    void* dstPtr = (void*) ((uint32_t) buffer->mem + currIndex * buffer->dataSize);

    //copy the data
    memcpy(dstPtr, src, buffer->dataSize);

    currIndex++;
    if(currIndex >= buffer->dataCountLimited) currIndex = 0;
    
    //and finally increase the write index
    buffer->writeIndex = currIndex;
    
    return 1;
}

int32_t RingBuffer_writeFromISR(RingBuffer_t * buffer, void* src, int32_t length, uint32_t shiftOnOverflow){
    if(buffer == NULL || src == NULL) return -1;
    
    //make sure there is enough space in the buffer
    if(!shiftOnOverflow && RingBuffer_getSpaceCount(buffer) < length){
        //nope there isn't, return with an error
        //TODO maybe only write some data now?
        return -1;
    }
    
    return RingBuffer_writeToBuffer(buffer, src, length, shiftOnOverflow);
}

int32_t RingBuffer_write(RingBuffer_t * buffer, void* src, int32_t length, uint32_t shiftOnOverflow){
    if(buffer == NULL || src == NULL) return -1;
    
    //make sure there is enough space in the buffer
    if(!shiftOnOverflow && RingBuffer_getSpaceCount(buffer) < length){
        //nope there isn't, return with an error
        //TODO maybe only write some data now?
        return -1;
    }
    
    //make sure we don't get interrupted while touching the data (?° ?? ?°) WHY github oh why do you ruin lenny face :(((((((
    taskENTER_CRITICAL();
    
    int32_t ret = RingBuffer_writeToBuffer(buffer, src, length, shiftOnOverflow);
    
    //and release the status again
    taskEXIT_CRITICAL();
    
    return ret;
}

int32_t RingBuffer_read(RingBuffer_t * buffer, void* dst, uint32_t length){
    if(buffer == NULL || dst == NULL || buffer->memSize == 0) return -1;
   
    //make sure there is enough data to read
    if(RingBuffer_getDataCount(buffer) < length){
        //return with an error
        //TODO also check if we might want to only read the available data here
        return -1;
    }
    
    //make sure we don't get interrupted while touching the data (?° ?? ?°)
    taskENTER_CRITICAL();
    
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
        if(currIndex >= buffer->dataCountLimited) currIndex = 0;
    }
    
    //and finally update the read index
    buffer->readIndex = currIndex;
    
    //and release the status again
    taskEXIT_CRITICAL();
    
    return length;
}

//gets the pointer to the next elements in a buffer
//WARNING: this function is not write over safe => if the buffer overflows while using the pointer the data is going to be invalid
int32_t RingBuffer_readBufferPointer(RingBuffer_t * buffer, void** dst, uint32_t length){
    if(buffer == NULL || dst == NULL || buffer->memSize == 0) return -1;
   
    //make sure there is enough data to read
    if(RingBuffer_getDataCount(buffer) < length){
        //return with an error
        //TODO also check if we might want to only read the available data here
        return -1;
    }
    
    //check if we would cross the end of the buffer
    if(buffer->readIndex + length > buffer->dataCountLimited) return -1;
    
    //make sure we don't get interrupted while touching the data (?° ?? ?°)
    vTaskEnterCritical();
    
    //get pointer to current data segment
    *dst = (void*) ((uint32_t) buffer->mem + buffer->readIndex * buffer->dataSize);
    
    //and then update the read index
    //WARNING this is the step after which the write routing sees the memory pointed to by the buffer returned to be free.
    //        IT WILL OVERWRITE IT IF IT RUNS ACROSS IT
    buffer->readIndex += length;
    if(buffer->readIndex == buffer->dataCountLimited) buffer->readIndex = 0;
    
    //and release the status again
    vTaskExitCritical();
    
    return length;
}

//have a look at the buffer at an indexed offset from readIndex
int32_t RingBuffer_peek(RingBuffer_t * buffer, void* dst, uint32_t index, uint32_t length){
    if(buffer == NULL || dst == NULL || buffer->memSize == 0) return -1;
    
    //check if there is enough data to even get to the index
    int32_t currDataCount = RingBuffer_getDataCount(buffer);
    if(index+1 > currDataCount) return -1;
    
    //limit read length to available data
    if(length > currDataCount - index) length = currDataCount - index;
    
    //make sure we don't get interrupted while touching the data
    vTaskEnterCritical();
    
    //calculate new offset
    uint32_t currIndex = buffer->readIndex + index;
    if(currIndex >= buffer->dataCountLimited){ 
        currIndex -= buffer->dataCountLimited;
    }
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
        if(currIndex >= buffer->dataCountLimited) currIndex = 0;
    }
    
    //DON'T update any indices... after all we are only peeking at the data
    
    //and release the status again
    vTaskExitCritical();
    
    return length;
}

int32_t RingBuffer_readFromISR(RingBuffer_t * buffer, void* dst, int32_t length){
    if(buffer == NULL || dst == NULL || buffer->memSize == 0) return -1;
   
    //make sure there is enough data to read
    if(RingBuffer_getDataCount(buffer) < length){
        //return with an error
        //TODO also check if we might want to read only the available data here
        return -1;
    }
    
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
        if(currIndex >= buffer->dataCountLimited) currIndex = 0;
    }
    
    //and finally update the read index
    buffer->readIndex = currIndex;
    
    return length;
}

void RingBuffer_startIndexedPeek(RingBuffer_t * buffer){
    //remember the read pointer we are at now so we can access the buffer by index from this point on
    buffer->startIndex = buffer->readIndex;
}

void* RingBuffer_indexedPeek(RingBuffer_t * buffer, uint32_t index){
    //user requests access to data at a specified index
    
    //buffer index to access
    int32_t targetIndex = index + buffer->startIndex;
    while(targetIndex >= buffer->dataCountLimited) targetIndex -= buffer->dataCountLimited; //access looped around to first buffer item
    
    //Check if the index is currently part of valid data
    if(buffer->writeIndex > buffer->readIndex){
        //write index hasn't yet looped around => check if we are inbetween the two values. If not return NULL as no valid data could be read
        if(targetIndex < buffer->readIndex || targetIndex >= buffer->writeIndex) return NULL;
    }else{
        //write index has yet looped around => check opposite of the other case. If not return NULL as no valid data could be read
        if(targetIndex >= buffer->writeIndex && !(targetIndex > buffer->readIndex)) return NULL;
    }
    
    //data should be valid => return pointer to it
    return (void*) ((uint32_t) buffer->mem + targetIndex * buffer->dataSize);
}

void RingBuffer_flush(RingBuffer_t * buffer){
    buffer->readIndex = buffer->writeIndex = 0;
}

uint32_t RingBuffer_size(RingBuffer_t * buffer){
    return buffer->dataCountLimited;
}

uint32_t RingBuffer_sizeInBytes(RingBuffer_t * buffer){
    return buffer->memSize;
}

int32_t RingBuffer_getDataCount(RingBuffer_t * buffer){
    int32_t dataCount = buffer->writeIndex - buffer->readIndex;
    if(dataCount < 0) dataCount += buffer->dataCountLimited;
    return dataCount;
}

int32_t RingBuffer_getSpaceCount(RingBuffer_t * buffer){
    int32_t dataCount = buffer->readIndex - buffer->writeIndex - 1;
    if(dataCount < 0) dataCount += buffer->dataCountLimited;
    return dataCount;
}

#pragma GCC pop_options