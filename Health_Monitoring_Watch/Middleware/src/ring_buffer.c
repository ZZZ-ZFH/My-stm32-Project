/**
 * @file    ring_buffer.c
 * @brief   SPSC无锁环形缓冲: 中断生产/任务消费场景的字节FIFO
 *          (uart1/dx24接收缓冲共用, 消除中断写与任务读的竞态)
 */
#include "ring_buffer.h"

/* 初始化: 绑定存储区并复位读写位置(重复调用可作清空) */
void RingBuffer_Init(RingBuffer *rb, uint8_t *mem, uint16_t size)
{
    rb->mem  = mem;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
}

/* 生产者写入(中断或任务): FIFO满则丢弃, 返回实际写入字节数 */
uint16_t RingBuffer_Write(RingBuffer *rb, const uint8_t *data, uint16_t len)
{
    uint16_t written = 0;

    while (written < len)
    {
        uint16_t next = (uint16_t)((rb->head + 1) % rb->size);

        if (next == rb->tail)   /* 满: 剩余字节丢弃 */
        {
            break;
        }

        rb->mem[rb->head] = data[written++];
        rb->head = next;        /* 数据先落位, 再公布新head */
    }

    return written;
}

/* 消费者读取(任务): 返回实际读出字节数 */
uint16_t RingBuffer_Read(RingBuffer *rb, uint8_t *buf, uint16_t len)
{
    uint16_t read_cnt = 0;

    while ((read_cnt < len) && (rb->tail != rb->head))
    {
        buf[read_cnt++] = rb->mem[rb->tail];
        rb->tail = (uint16_t)((rb->tail + 1) % rb->size);
    }

    return read_cnt;
}

/* 查询已缓存字节数(消费者视角, 数值只增不减直到被读走) */
uint16_t RingBuffer_Count(const RingBuffer *rb)
{
    uint16_t head = rb->head;
    uint16_t tail = rb->tail;

    return (uint16_t)((head + rb->size - tail) % rb->size);
}

/* 清空(仅消费者调用, 生产者不受影响) */
void RingBuffer_Clear(RingBuffer *rb)
{
    rb->tail = rb->head;
}
