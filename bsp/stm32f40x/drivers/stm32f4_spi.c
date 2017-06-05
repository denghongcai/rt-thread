#include "stm32f4_spi.h"

//#define SPI_USE_DMA

struct stm32_spi_bus
{
    struct rt_spi_bus parent;
    
    SPI_TypeDef * SPI;
    
#ifdef SPI_USE_DMA
    DMA_Stream_TypeDef * DMA_Stream_TX;
    uint32_t DMA_Channel_TX;

    DMA_Stream_TypeDef * DMA_Stream_RX;
    uint32_t DMA_Channel_RX;

    uint32_t DMA_Channel_TX_FLAG_TC;
    uint32_t DMA_Channel_RX_FLAG_TC;
#endif /* #ifdef SPI_USE_DMA */    
};

struct stm32_spi_cs
{
    GPIO_TypeDef * GPIOx;
    uint16_t GPIO_Pin;
};

rt_inline uint16_t get_spi_BaudRatePrescaler(rt_uint32_t max_hz)
{
    uint16_t SPI_BaudRatePrescaler;

    /* STM32F10x SPI MAX 18Mhz */
    if(max_hz >= SystemCoreClock/2 && SystemCoreClock/2 <= 18000000)
    {
        SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    }
    else if(max_hz >= SystemCoreClock/4)
    {
        SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    }
    else if(max_hz >= SystemCoreClock/8)
    {
        SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
    }
    else if(max_hz >= SystemCoreClock/16)
    {
        SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
    }
    else if(max_hz >= SystemCoreClock/32)
    {
        SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32;
    }
    else if(max_hz >= SystemCoreClock/64)
    {
        SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_64;
    }
    else if(max_hz >= SystemCoreClock/128)
    {
        SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_128;
    }
    else
    {
        /* min prescaler 256 */
        SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;
    }

    return SPI_BaudRatePrescaler;
}


static rt_err_t configure(struct rt_spi_device* device, struct rt_spi_configuration* configuration)
{
    struct stm32_spi_bus * stm32_spi_bus = (struct stm32_spi_bus *)device->bus;
    SPI_InitTypeDef SPI_InitStructure;

    SPI_StructInit(&SPI_InitStructure);

    /* data_width */
    if(configuration->data_width <= 8)
    {
        SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    }
    else if(configuration->data_width <= 16)
    {
        SPI_InitStructure.SPI_DataSize = SPI_DataSize_16b;
    }
    else
    {
        return RT_EIO;
    }
    /* baudrate */
    SPI_InitStructure.SPI_BaudRatePrescaler = get_spi_BaudRatePrescaler(configuration->max_hz);
    /* CPOL */
    if(configuration->mode & RT_SPI_CPOL)
    {
        SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    }
    else
    {
        SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    }
    /* CPHA */
    if(configuration->mode & RT_SPI_CPHA)
    {
        SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    }
    else
    {
        SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    }
    /* MSB or LSB */
    if(configuration->mode & RT_SPI_MSB)
    {
        SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    }
    else
    {
        SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_LSB;
    }
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_NSS  = SPI_NSS_Soft;

    /* init SPI */
    SPI_I2S_DeInit(stm32_spi_bus->SPI);
    SPI_Init(stm32_spi_bus->SPI, &SPI_InitStructure);
    /* Enable SPI_MASTER */
    SPI_Cmd(stm32_spi_bus->SPI, ENABLE);
    SPI_CalculateCRC(stm32_spi_bus->SPI, DISABLE);

    return RT_EOK;
};

static rt_uint32_t xfer(struct rt_spi_device* device, struct rt_spi_message* message)
{
    struct stm32_spi_bus * stm32_spi_bus = (struct stm32_spi_bus *)device->bus;
    struct rt_spi_configuration * config = &device->config;
    SPI_TypeDef * SPI = stm32_spi_bus->SPI;
    struct stm32_spi_cs * stm32_spi_cs = device->parent.user_data;
    rt_uint32_t size = message->length;

    /* take CS */
    if(message->cs_take)
    {
        GPIO_ResetBits(stm32_spi_cs->GPIOx, stm32_spi_cs->GPIO_Pin);
    }

#ifdef SPI_USE_DMA
    if(message->length > 32)
    {
        if(config->data_width <= 8)
        {
            DMA_Configuration(stm32_spi_bus, message->send_buf, message->recv_buf, message->length);
            SPI_I2S_DMACmd(SPI, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, ENABLE);
            while (DMA_GetFlagStatus(stm32_spi_bus->DMA_Channel_RX_FLAG_TC) == RESET
                    || DMA_GetFlagStatus(stm32_spi_bus->DMA_Channel_TX_FLAG_TC) == RESET);
            SPI_I2S_DMACmd(SPI, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, DISABLE);
        }
//        rt_memcpy(buffer,_spi_flash_buffer,DMA_BUFFER_SIZE);
//        buffer += DMA_BUFFER_SIZE;
    }
    else
#endif
    {
        if(config->data_width <= 8)
        {
            const rt_uint8_t * send_ptr = message->send_buf;
            rt_uint8_t * recv_ptr = message->recv_buf;

            while(size--)
            {
                rt_uint8_t data = 0xFF;

                if(send_ptr != RT_NULL)
                {
                    data = *send_ptr++;
                }

                //Wait until the transmit buffer is empty
                while (SPI_I2S_GetFlagStatus(SPI, SPI_I2S_FLAG_TXE) == RESET);
                // Send the byte
                SPI_I2S_SendData(SPI, data);

                //Wait until a data is received
                while (SPI_I2S_GetFlagStatus(SPI, SPI_I2S_FLAG_RXNE) == RESET);
                // Get the received data
                data = SPI_I2S_ReceiveData(SPI);

                if(recv_ptr != RT_NULL)
                {
                    *recv_ptr++ = data;
                }
            }
        }
        else if(config->data_width <= 16)
        {
            const rt_uint16_t * send_ptr = message->send_buf;
            rt_uint16_t * recv_ptr = message->recv_buf;

            while(size--)
            {
                rt_uint16_t data = 0xFF;

                if(send_ptr != RT_NULL)
                {
                    data = *send_ptr++;
                }

                //Wait until the transmit buffer is empty
                while (SPI_I2S_GetFlagStatus(SPI, SPI_I2S_FLAG_TXE) == RESET);
                // Send the byte
                SPI_I2S_SendData(SPI, data);

                //Wait until a data is received
                while (SPI_I2S_GetFlagStatus(SPI, SPI_I2S_FLAG_RXNE) == RESET);
                // Get the received data
                data = SPI_I2S_ReceiveData(SPI);

                if(recv_ptr != RT_NULL)
                {
                    *recv_ptr++ = data;
                }
            }
        }
    }

    /* release CS */
    if(message->cs_release)
    {
        GPIO_SetBits(stm32_spi_cs->GPIOx, stm32_spi_cs->GPIO_Pin);
    }

    return message->length;
};

static struct rt_spi_ops stm32_spi_ops =
{
    configure,
    xfer
};

rt_err_t stm32_spi_register(SPI_TypeDef * SPI,
                            struct stm32_spi_bus * stm32_spi,
                            const char * spi_bus_name)
{
    if(SPI == SPI1)
    {
        stm32_spi->SPI = SPI1;
            RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);//84MHZ
            
#ifdef SPI_USE_DMA
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
        /* DMA2_Stream0 DMA_Channel_3 : SPI1_RX ; DMA2_Stream2 DMA_Channel_3 : SPI1_RX */
        stm32_spi->DMA_Stream_RX = DMA2_Stream0;
        stm32_spi->DMA_Channel_RX = DMA_Channel_3;
        stm32_spi->DMA_Channel_RX_FLAG_TC = DMA_FLAG_TCIF0;
        /* DMA2_Stream3 DMA_Channel_3 : SPI1_TX ; DMA2_Stream5 DMA_Channel_3 : SPI1_TX */   
        stm32_spi->DMA_Stream_TX = DMA2_Stream3;
        stm32_spi->DMA_Channel_TX = DMA_Channel_3;
        stm32_spi->DMA_Channel_TX_FLAG_TC = DMA_FLAG_TCIF3;
#endif        
    }
    else if(SPI == SPI2)
    {
      stm32_spi->SPI = SPI2;
            RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);//42MHZ
            
#ifdef SPI_USE_DMA
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
        /* DMA1_Stream3 DMA_Channel_0 : SPI2_RX */
        stm32_spi->DMA_Stream_RX = DMA1_Stream3;
        stm32_spi->DMA_Channel_RX = DMA_Channel_0;
        stm32_spi->DMA_Channel_RX_FLAG_TC = DMA_FLAG_TCIF3;
        /* DMA1_Stream4 DMA_Channel_0 : SPI2_TX */
        stm32_spi->DMA_Stream_TX = DMA1_Stream4;
        stm32_spi->DMA_Channel_TX = DMA_Channel_0;
        stm32_spi->DMA_Channel_TX_FLAG_TC = DMA_FLAG_TCIF4;
#endif       
    }
    else if(SPI == SPI3)
    {
        stm32_spi->SPI = SPI3;
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI3, ENABLE);//42MHZ
            
#ifdef SPI_USE_DMA
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
        /* DMA1_Stream2 DMA_Channel_0 : SPI3_RX ; DMA1_Stream0 DMA_Channel_0 : SPI3_RX */
        stm32_spi->DMA_Stream_RX = DMA1_Stream2;
        stm32_spi->DMA_Channel_RX = DMA_Channel_0;
        stm32_spi->DMA_Channel_RX_FLAG_TC = DMA_FLAG_TCIF2;
        /* DMA1_Stream5 DMA_Channel_0 : SPI3_TX ; DMA1_Stream7 DMA_Channel_0 : SPI3_TX */
        stm32_spi->DMA_Stream_TX = DMA1_Stream5;
        stm32_spi->DMA_Channel_TX = DMA_Channel_0;
        stm32_spi->DMA_Channel_TX_FLAG_TC = DMA_FLAG_TCIF5;
#endif        
    }
    else
    {
        return RT_ENOSYS;
    }

    return rt_spi_bus_register(&stm32_spi->parent, spi_bus_name, &stm32_spi_ops);
}

int stm32_hw_spi2_init(void)
{
    /* register SPI bus */
    static struct stm32_spi_bus stm32_spi;             //it must be add static
    
    /* SPI1 configure */
    {
        GPIO_InitTypeDef GPIO_InitStructure;

        /* Enable GPIO Periph clock */
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
        
        GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_SPI1);
        GPIO_PinAFConfig(GPIOB, GPIO_PinSource14, GPIO_AF_SPI1);
        GPIO_PinAFConfig(GPIOB, GPIO_PinSource15, GPIO_AF_SPI1);

        GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
        GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL; 

        /* Configure SPI2 pins */
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
        GPIO_Init(GPIOB, &GPIO_InitStructure);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;
        GPIO_Init(GPIOB, &GPIO_InitStructure);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
        GPIO_Init(GPIOB, &GPIO_InitStructure);
    } /* SPI2 configuration */

    /* register SPI2 to stm32_spi_bus */
    stm32_spi_register(SPI2, &stm32_spi, "spi2");
    
    /* attach spi20 */
    {
        static struct rt_spi_device rt_spi_device_20;    //it must be add static
        static struct stm32_spi_cs  stm32_spi_cs_20;     //it must be add static
        
        stm32_spi_cs_20.GPIOx    = GPIOB;
        stm32_spi_cs_20.GPIO_Pin = GPIO_Pin_12;
        
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

        GPIO_InitTypeDef GPIO_InitStructure;
        GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
        GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
        GPIO_InitStructure.GPIO_Pin   = stm32_spi_cs_20.GPIO_Pin;
        GPIO_Init(stm32_spi_cs_20.GPIOx, &GPIO_InitStructure);
        GPIO_SetBits(stm32_spi_cs_20.GPIOx, stm32_spi_cs_20.GPIO_Pin);
        
        rt_spi_bus_attach_device(&rt_spi_device_20, "spi20", "spi2", (void*)&stm32_spi_cs_20);//set spi_device->bus
//        /* config spi */
//        {
//            struct rt_spi_configuration cfg;
//            cfg.data_width = 8;
//            cfg.mode = RT_SPI_MODE_3 | RT_SPI_MSB; /* SPI Compatible Modes 3 and SPI_FirstBit_MSB in lis302dl datasheet */
//            
//            //APB2=168M/2=84M, SPI1 = 84/2,4,8,16,32 = 42M, 21M, 10.5M, 5.25M, 2.625M ...
//            cfg.max_hz = 2625000; /* SPI_BaudRatePrescaler_16=84000000/16=5.25MHz. The max_hz of lis302dl is 10MHz in datasheet */ 
//            rt_spi_configure(&rt_spi_device_20, &cfg);
//        } /* config spi */    
    } /* attach spi20 */    
    
    return 0;
}
INIT_BOARD_EXPORT(stm32_hw_spi2_init);//rt_hw_spi1_init will be called in rt_components_board_init()
