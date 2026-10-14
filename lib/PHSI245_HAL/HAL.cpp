#include "HAL.h"
#include <Arduino.h>
#include <ch32x035.h>

uint16_t TouchState (0);

void initTouchButtons()
{
    GPIO_InitTypeDef GPIO_InitStructure={0};
	ADC_InitTypeDef ADC_InitStructure={0};
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE );
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE );

    const unsigned char cap_pins[6] = {PIN_BTN_UP, PIN_BTN_LEFT, PIN_BTN_DOWN, PIN_BTN_RIGHT, PIN_BTN_A, PIN_BTN_B};
	for (int i = 0; i < 6; ++ i)
	{
		GPIO_InitStructure.GPIO_Pin = (1<<cap_pins[i]);
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
		GPIO_Init(GPIOA, &GPIO_InitStructure);
	}
    ADC_CLKConfig(ADC1, ADC_CLK_Div6);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);
	ADC_Cmd(ADC1, ENABLE);
	TKey1->CTLR1 |= (1<<24);     // Enable TouchKey

}

uint16_t Touch_Key_Adc(uint8_t ch)
{
  ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_11Cycles );
  TKey1->IDATAR1 =0x80;  //Charging Time
  TKey1->RDATAR =0x8;   //Discharging Time
  while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC ));
  return (uint16_t) TKey1->RDATAR;
}

int IsTouched(unsigned long int channel)
{
    uint16_t val = Touch_Key_Adc(channel);
    if ( !(TouchState & (1 << channel)) && val < TOUCH_THRESHOLD )
        {
            TouchState |= (1 << channel);
            return 1;
        }
    if ( (TouchState & (1 << channel)) && val > (TOUCH_THRESHOLD + TOUCH_DEBOUNCE) )
        {
            TouchState &= ~(1 << channel);
            return 0;
        }
    return (TouchState & (1 << channel));
}
