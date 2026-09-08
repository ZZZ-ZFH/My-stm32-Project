#ifndef __RING_BUFFER_H
#define __RING_BUFFER_H

#include <stdint.h>

/* SPSC无锁环形缓冲(单生产者-单消费者):
 * - 生产者(中断): 仅调用 RingBuffer_Write, 只修改 head
 * - 消费者(任务): 仅调用 RingBuffer_Read/Clear, 只修改 tail
 * - 读写位置单向修改, 单核Cortex-M4上无需关中断/互斥
 * - 满时新数据丢弃(返回实际写入数), 不回卷覆盖未消费数据 */
typedef struct {
    uint8_t          *mem;    /* 存储区(调用方提供, 须保持有效) */
    uint16_t          size;   /* 容量(字节) */
    volatile uint16_t head;   /* 写位置(仅生产者修改) */
    volatile uint16_t tail;   /* 读位置(仅消费者修改) */
} RingBuffer;

void     RingBuffer_Init(RingBuffer *rb, uint8_t *mem, uint16_t size);
uint16_t RingBuffer_Write(RingBuffer *rb, const uint8_t *data, uint16_t len);
uint16_t RingBuffer_Read(RingBuffer *rb, uint8_t *buf, uint16_t len);
uint16_t RingBuffer_Count(const RingBuffer *rb);
void     RingBuffer_Clear(RingBuffer *rb);

#endif
