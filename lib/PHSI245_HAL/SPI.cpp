#include "HAL.h"
#include <Arduino.h>
#include <ch32x035.h>

void spi_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    // SPI1 PartialRemap2: SCK=PA11, MISO=PA9, MOSI=PA10, CS=PA12
    GPIO_PinRemapConfig(GPIO_PartialRemap2_SPI1, ENABLE);

    // SCK: PA11
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // MISO: PA9 (input with pull-up — card DO floats before SPI mode)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // MOSI: PA10
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // CS: PA12 (manual control, not hardware NSS)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // CS idle high
    GPIO_SetBits(GPIOA, GPIO_Pin_12);

    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256; // ~187 kHz (safe for SD init)
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SD_SPI_PORT, &SPI_InitStructure);

    SPI_Cmd(SD_SPI_PORT, ENABLE);
}

// Reconfigure the SPI baud-rate prescaler (e.g. raise the clock after the SD
// card finishes its init handshake). SPI_Init clears SPE, so re-enable it.
void spi_set_prescaler(uint16_t prescaler)
{
    SPI_InitTypeDef SPI_InitStructure = {0};
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = prescaler;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SD_SPI_PORT, &SPI_InitStructure);
    SPI_Cmd(SD_SPI_PORT, ENABLE);
}

uint8_t spi_transfer(uint8_t data)
{
    while (SPI_I2S_GetFlagStatus(SD_SPI_PORT, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SD_SPI_PORT, data);
    while (SPI_I2S_GetFlagStatus(SD_SPI_PORT, SPI_I2S_FLAG_RXNE) == RESET);
    return SPI_I2S_ReceiveData(SD_SPI_PORT);
}

void spi_cs_low(void)
{
    GPIO_ResetBits(GPIOA, GPIO_Pin_12);
}

void spi_cs_high(void)
{
    GPIO_SetBits(GPIOA, GPIO_Pin_12);
}
