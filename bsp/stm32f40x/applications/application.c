/*
 * File      : application.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2006, RT-Thread Development Team
 *
 * The license and distribution terms for this file may be
 * found in the file LICENSE in this distribution or at
 * http://www.rt-thread.org/license/LICENSE
 *
 * Change Logs:
 * Date           Author       Notes
 * 2009-01-05     Bernard      the first version
 * 2014-04-27     Bernard      make code cleanup. 
 */

#include <board.h>
#include <rtthread.h>

#ifdef RT_USING_LWIP
#include <lwip/sys.h>
#include <lwip/api.h>
#include <netif/ethernetif.h>
#include "stm32f4xx_eth.h"
#include "enc28j60.h"
#endif

#ifdef RT_USING_GDB
#include <gdb_stub.h>
#endif

extern void wslay_server();

static void Exit0_Init()
{
  	EXTI_InitTypeDef EXTI_InitStructure;
  	NVIC_InitTypeDef NVIC_InitStructure;	
	GPIO_InitTypeDef GPIO_InitStructure;
    
    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure); 

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);    //
  	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0; 
  	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
  	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  	GPIO_Init(GPIOB, &GPIO_InitStructure);                   //	

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource0);         //
    EXTI_InitStructure.EXTI_Line = EXTI_Line0;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;    
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling; 
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
	
	EXTI_ClearITPendingBit(EXTI_Line0);
}

void rt_init_thread_entry(void* parameter)
{
    Exit0_Init();
    /* GDB STUB */
#ifdef RT_USING_GDB
    gdb_set_device("uart6");
    gdb_start();
#endif

    /* LwIP Initialization */
#ifdef RT_USING_LWIP
    {
        extern void lwip_sys_init(void);

        /* register ethernetif device */
        eth_system_device_init();

        enc28j60_attach("spi20");
        //rt_hw_stm32_eth_init();

        /* init lwip system */
        lwip_sys_init();
        rt_kprintf("TCP/IP initialized!\n");
        
        wslay_server();
        //netio_init();
    }
#endif
}

void rt_led_thread_entry(void* parameter)
{
    rt_pin_mode(8, PIN_MODE_OUTPUT);
    rt_pin_mode(9, PIN_MODE_OUTPUT);
    rt_pin_mode(10, PIN_MODE_OUTPUT);
    rt_pin_mode(11, PIN_MODE_OUTPUT);
    while(1) {
        rt_pin_write(8, PIN_HIGH);
        rt_thread_delay(RT_TICK_PER_SECOND/2);
        rt_pin_write(9, PIN_HIGH);
        rt_thread_delay(RT_TICK_PER_SECOND/2);
        rt_pin_write(10, PIN_HIGH);
        rt_thread_delay(RT_TICK_PER_SECOND/2);
        rt_pin_write(11, PIN_HIGH);
        rt_thread_delay(RT_TICK_PER_SECOND/2);
        rt_pin_write(8, PIN_LOW);
        rt_pin_write(9, PIN_LOW);
        rt_pin_write(10, PIN_LOW);
        rt_pin_write(11, PIN_LOW);
        rt_thread_delay(RT_TICK_PER_SECOND/2);
    }
}

int rt_application_init()
{
    rt_thread_t tid;

    tid = rt_thread_create("init",
        rt_init_thread_entry, RT_NULL,
        8192, RT_THREAD_PRIORITY_MAX/3, 20);

    if (tid != RT_NULL)
        rt_thread_startup(tid);
    
    tid = rt_thread_create("led",
        rt_led_thread_entry, RT_NULL,
        256, RT_THREAD_PRIORITY_MAX/3, 20);
    
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    return 0;
}

/*@}*/
