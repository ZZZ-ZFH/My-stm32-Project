#include "protocol.h"
#include "bsp_uart.h"
/* 计算累加和校验 */
static uint8_t calc_checksum(const uint8_t *data, uint16_t len)
{
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/**
 * @brief  封装协议帧到发送缓冲区（CMD在前，LEN在后）
 * @param  out_buf: 输出缓冲区（需 ≥ MAX_FRAME_LEN）
 * @param  cmd: 命令字
 * @param  data: 待发送数据区
 * @param  data_len: 数据区长度
 * @return 封装后的总帧长度（字节数）
 */
uint16_t Protocol_Pack(uint8_t *out_buf, uint8_t cmd, 
                       const uint8_t *data, uint16_t data_len)
{
    uint16_t idx = 0;
    
    // 1. 帧头
    out_buf[idx++] = FRAME_HEAD1;
    out_buf[idx++] = FRAME_HEAD2;
    
    // 2. 命令字（先放）
    out_buf[idx++] = cmd;
    
    // 3. 数据长度（后放）
    out_buf[idx++] = data_len;
    
    // 4. 数据区
    if (data_len > 0 && data != NULL) {
        memcpy(&out_buf[idx], data, data_len);
        idx += data_len;
    }
    
    // 5. 校验和（从帧头到数据区末尾）
    out_buf[idx++] = calc_checksum(out_buf, idx);
    
    // 6. 帧尾
    out_buf[idx++] = FRAME_TAIL1;
    out_buf[idx++] = FRAME_TAIL2;
    
    return idx;
}

/**
 * @brief  一次性解析完整协议帧（非逐字节流式解析）
 * @param  data: 输入缓冲区，包含完整的协议帧数据
 * @param  len: 输入缓冲区长度
 * @param  frame: 输出参数，解析成功时填充的帧结构体
 * @return 0 = 解析成功，非0 = 错误码
 * 
 * 协议帧格式（CMD在前，LEN在后）：
 *   HEAD(2B) | CMD(1B) | LEN(1B) | DATA(NB) | CHK(1B) | TAIL(2B)
 * 
 * 错误码定义：
 *   -1: 长度过短（小于最小帧长度）
 *   -2: 帧头错误
 *   -3: 数据长度超过最大支持值
 *   -4: 帧尾错误
 *   -5: 校验和错误
 *   -6: 实际数据长度与声明长度不匹配
 */
int8_t Protocol_Parse_Frame(const uint8_t *data, uint16_t len, ProtocolFrame *frame)
{
    /* 最小帧长度：帧头2 + 命令1 + 长度1 + 校验1 + 帧尾2 = 7字节 */
    const uint16_t MIN_FRAME_LEN = 7;
    
    /* 参数检查 */
    if (data == NULL || frame == NULL) {
        return -1;  // 空指针错误
    }
    
    if (len < MIN_FRAME_LEN) {
        return -1;  // 长度过短
    }
    
    /* 1. 检查帧头 */
    if (data[0] != FRAME_HEAD1 || data[1] != FRAME_HEAD2) {
        return -2;  // 帧头错误
    }
    
    /* 2. 提取命令字（索引2） */
    uint8_t cmd = data[2];
    
    /* 3. 提取数据长度（索引3） */
    uint8_t data_len = data[3];
    
    /* 4. 验证数据长度是否超过最大支持 */
    if (data_len > MAX_DATA_LEN) {
        return -3;  // 数据长度超限
    }
    
    /* 5. 计算完整帧应该有的长度：
         帧头2 + 命令1 + 长度1 + 数据data_len + 校验1 + 帧尾2 */
    uint16_t expected_len = 2 + 1 + 1 + data_len + 1 + 2;
    
    if (len < expected_len) {
        return -6;  // 实际数据不足，可能为半包
    }
    
    /* 6. 检查帧尾（索引：2 + 1 + 1 + data_len + 1 = 5 + data_len 和 6 + data_len） */
    if (data[5 + data_len] != FRAME_TAIL1 || data[6 + data_len] != FRAME_TAIL2) {
        return -4;  // 帧尾错误
    }
    
    /* 7. 计算校验和（从帧头到数据区末尾，排除校验和字段本身和帧尾）
        计算范围：索引0 到 索引(4 + data_len - 1) */
    uint8_t calc_sum = 0;
    for (uint16_t i = 0; i < (4 + data_len); i++) {
        calc_sum += data[i];
    }
    
    /* 8. 提取接收到的校验和（索引：4 + data_len） */
    uint8_t recv_checksum = data[4 + data_len];
    
    /* 9. 校验和验证 */
    if (calc_sum != recv_checksum) {
        return -5;  // 校验和错误
    }
    
    /* 10. 所有检查通过，填充输出结构体 */
    frame->head1    = data[0];
    frame->head2    = data[1];
    frame->cmd      = cmd;
    frame->len      = data_len;
    
    if (data_len > 0) {
        memcpy(frame->data, &data[4], data_len);  // 数据从索引4开始
    }
    
    frame->checksum = recv_checksum;
    frame->tail1    = data[5 + data_len];
    frame->tail2    = data[6 + data_len];
    
    return 0;  // 解析成功
}

void Protocol_SendDemo()
{
	//封装协议包
	uint8_t out_buf[MAX_FRAME_LEN] = {0};
	uint16_t frame_len = Protocol_Pack(out_buf, Cmd_DEVICE_LED0,ON, strlen(ON));
	//发送
	BSP_UART_SendBuffer(out_buf,frame_len);
}

void Protocol_RecDemo()
{
	uint8_t rx_buf[MAX_FRAME_LEN] = {0};
	uint16_t rx_len = 0;

	while(1)
	{
		//判断是否为一帧数据(取到一帧的同时自动清空接收缓冲)
		if(BSP_UART_GetRxFrame(rx_buf, &rx_len))
		{
			printf("rx_buf:");
			for(uint16_t i=0; i<rx_len; i++)
			{
				printf("%#x\t",rx_buf[i]);
			}
			printf("\r\n");

			ProtocolFrame frame;
			memset(&frame,0,sizeof(frame));
			//解析
			int8_t ret = Protocol_Parse_Frame(rx_buf,rx_len,&frame);
			if(ret==0)
			{
				switch(frame.cmd)
				{
					case Cmd_DEVICE_LED0:
						if(strcmp(frame.data,ON)==0)
						{
							//BSP_LED_On(BSP_LED_1);
						}
						else if(strcmp(frame.data,OFF)==0)
						{
							//BSP_LED_Off(BSP_LED_1);
						}
						break;
					case Cmd_DEVICE_LED1:
						break;
					case Cmd_DEVICE_LED2:
						break;
					case Cmd_DEVICE_LED3:
						break;
					case Cmd_DEVICE_BEEP:
						break;
				}
			}
		}
	}
}