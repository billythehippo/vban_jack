#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

//#define STATICSIZE
#define maxRingBufSize 32768

typedef struct ringBuffer8_t{
uint16_t readPtr;
uint16_t writePtr;
uint16_t size;
uint16_t actualData;
#ifdef STATICSIZE
uint8_t buffer[maxRingBufSize];
#else
uint8_t* buffer;
#endif
} ringBuffer8_t;

static inline void ringBuffer8_tInit(ringBuffer8_t* ringbuffer, uint16_t size)
{
    ringbuffer->size = size;
    ringbuffer->readPtr = 0;
    ringbuffer->writePtr = 0;
    ringbuffer->actualData = 0;
#ifdef STATICSIZE
    memset((uint8_t*)&(ringbuffer->buffer), 0, maxRingBufSize);
#else
    ringbuffer->buffer = (uint8_t*)malloc(sizeof(uint8_t)*size);
#endif
}

#ifndef STATICSIZE
static inline void ringBuffer8_tFree(ringBuffer8_t *ringbuffer)
{
    free(ringbuffer);
}
#endif

static inline uint16_t ringBuffer8_tReadSpaceAvailable(ringBuffer8_t* ringbuffer)
{
    return (ringbuffer->writePtr>ringbuffer->readPtr ? ringbuffer->writePtr - ringbuffer->readPtr : ringbuffer->size + ringbuffer->writePtr - ringbuffer->readPtr);
}

static inline uint16_t ringBuffer8_tWriteSpaceAvailable(ringBuffer8_t* ringbuffer)
{
    return (ringbuffer->readPtr>ringbuffer->writePtr ? ringbuffer->readPtr - ringbuffer->writePtr : ringbuffer->size + ringbuffer->readPtr - ringbuffer->writePtr);
}

static inline uint8_t ringBufferReadByte8_t(ringBuffer8_t *ringBuffer)//, uint16_t addr)
{
    uint8_t result;
    if (ringBuffer->readPtr<ringBuffer->size) result = ringBuffer->buffer[ringBuffer->readPtr];//addr];
    if (ringBuffer->readPtr>=ringBuffer->size-1) ringBuffer->readPtr = 0;
    else ringBuffer->readPtr++;
    if (ringBuffer->actualData>0) ringBuffer->actualData--;
    return result;
}

static inline void ringBufferWriteByte8_t(ringBuffer8_t *ringBuffer, uint8_t data)// uint16_t addr, uint8_t data)
{
    ringBuffer->buffer[ringBuffer->writePtr] = data;//addr] = data;
    if (ringBuffer->writePtr>=ringBuffer->size-1) ringBuffer->writePtr = 0;
    else ringBuffer->writePtr++;
    if (ringBuffer->actualData<ringBuffer->size) ringBuffer->actualData++;
    else
    {
        if (ringBuffer->readPtr>=ringBuffer->size-1) ringBuffer->readPtr = 0;
        else ringBuffer->readPtr++;                
    }
}

static inline uint8_t ringBufferReadArray8_t(ringBuffer8_t *ringBuffer, uint8_t* buf, uint16_t len)
{
    if (len>ringBuffer->actualData) return 0x80;
    else
    {
        uint16_t ind = 0;
        while(ind!=len)
        {
            if (ringBuffer->readPtr<ringBuffer->size) buf[ind] = ringBuffer->buffer[ringBuffer->readPtr];
            ind++;
            if (ringBuffer->readPtr>=ringBuffer->size-1) ringBuffer->readPtr = 0;
            else ringBuffer->readPtr++;
            if (ringBuffer->actualData>0) ringBuffer->actualData--;
        }
    return 0;
    }
}

static inline uint8_t ringBufferWriteArray8_t(ringBuffer8_t *ringBuffer, uint8_t* buf, uint16_t len)
{
    if (len>ringBuffer->size) return 0x80;
    else
    {
        uint16_t ind = 0;
        while(ind!=len)
        {
            ringBuffer->buffer[ringBuffer->writePtr] = buf[ind];
            ind++;
            if (ringBuffer->writePtr>=ringBuffer->size-1) ringBuffer->writePtr = 0;
            else ringBuffer->writePtr++;
            if (ringBuffer->actualData<ringBuffer->size) ringBuffer->actualData++;
            else
            {
                if (ringBuffer->readPtr>=ringBuffer->size-1) ringBuffer->readPtr = 0;
                else ringBuffer->readPtr++;                
            }
        }
    return 0;
    }
}

#endif