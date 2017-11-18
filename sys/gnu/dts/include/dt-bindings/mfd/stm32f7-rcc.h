/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants for the STM32F7 RCC IP
 */

#ifndef _DT_BINDINGS_MFD_STM32F7_RCC_H
#define _DT_BINDINGS_MFD_STM32F7_RCC_H

/* AHB1 */
#define STM32F7_RCC_AHB1_GPIOA		0
#define STM32F7_RCC_AHB1_GPIOB		1
#define STM32F7_RCC_AHB1_GPIOC		2
#define STM32F7_RCC_AHB1_GPIOD		3
#define STM32F7_RCC_AHB1_GPIOE		4
#define STM32F7_RCC_AHB1_GPIOF		5
#define STM32F7_RCC_AHB1_GPIOG		6
#define STM32F7_RCC_AHB1_GPIOH		7
#define STM32F7_RCC_AHB1_GPIOI		8
#define STM32F7_RCC_AHB1_GPIOJ		9
#define STM32F7_RCC_AHB1_GPIOK		10
#define STM32F7_RCC_AHB1_CRC		12
#define STM32F7_RCC_AHB1_BKPSRAM	18
#define STM32F7_RCC_AHB1_DTCMRAM	20
#define STM32F7_RCC_AHB1_DMA1		21
#define STM32F7_RCC_AHB1_DMA2		22
#define STM32F7_RCC_AHB1_DMA2D		23
#define STM32F7_RCC_AHB1_ETHMAC		25
#define STM32F7_RCC_AHB1_ETHMACTX	26
#define STM32F7_RCC_AHB1_ETHMACRX	27
#define STM32FF_RCC_AHB1_ETHMACPTP	28
#define STM32F7_RCC_AHB1_OTGHS		29
#define STM32F7_RCC_AHB1_OTGHSULPI	30

#define STM32F7_AHB1_RESET(bit) (STM32F7_RCC_AHB1_##bit + (0x10 * 8))
#define STM32F7_AHB1_CLOCK(bit) (STM32F7_RCC_AHB1_##bit)


/* AHB2 */
#define STM32F7_RCC_AHB2_DCMI		0
#define STM32F7_RCC_AHB2_CRYP		4
#define STM32F7_RCC_AHB2_HASH		5
#define STM32F7_RCC_AHB2_RNG		6
#define STM32F7_RCC_AHB2_OTGFS		7

#define STM32F7_AHB2_RESET(bit)	(STM32F7_RCC_AHB2_##bit + (0x14 * 8))
#define STM32F7_AHB2_CLOCK(bit)	(STM32F7_RCC_AHB2_##bit + 0x20)

/* AHB3 */
#define STM32F7_RCC_AHB3_FMC		0
#define STM32F7_RCC_AHB3_QSPI		1

#define STM32F7_AHB3_RESET(bit)	(STM32F7_RCC_AHB3_##bit + (0x18 * 8))
#define STM32F7_AHB3_CLOCK(bit)	(STM32F7_RCC_AHB3_##bit + 0x40)

/* APB1 */
#define STM32F7_RCC_APB1_TIM2		0
#define STM32F7_RCC_APB1_TIM3		1
#define STM32F7_RCC_APB1_TIM4		2
#define STM32F7_RCC_APB1_TIM5		3
#define STM32F7_RCC_APB1_TIM6		4
#define STM32F7_RCC_APB1_TIM7		5
#define STM32F7_RCC_APB1_TIM12		6
#define STM32F7_RCC_APB1_TIM13		7
#define STM32F7_RCC_APB1_TIM14		8
#define STM32F7_RCC_APB1_LPTIM1		9
#define STM32F7_RCC_APB1_WWDG		11
#define STM32F7_RCC_APB1_SPI2		14
#define STM32F7_RCC_APB1_SPI3		15
#define STM32F7_RCC_APB1_SPDIFRX	16
#define STM32F7_RCC_APB1_UART2		17
#define STM32F7_RCC_APB1_UART3		18
#define STM32F7_RCC_APB1_UART4		19
#define STM32F7_RCC_APB1_UART5		20
#define STM32F7_RCC_APB1_I2C1		21
#define STM32F7_RCC_APB1_I2C2		22
#define STM32F7_RCC_APB1_I2C3		23
#define STM32F7_RCC_APB1_I2C4		24
#define STM32F7_RCC_APB1_CAN1		25
#define STM32F7_RCC_APB1_CAN2		26
#define STM32F7_RCC_APB1_CEC		27
#define STM32F7_RCC_APB1_PWR		28
#define STM32F7_RCC_APB1_DAC		29
#define STM32F7_RCC_APB1_UART7		30
#define STM32F7_RCC_APB1_UART8		31

#define STM32F7_APB1_RESET(bit)	(STM32F7_RCC_APB1_##bit + (0x20 * 8))
#define STM32F7_APB1_CLOCK(bit)	(STM32F7_RCC_APB1_##bit + 0x80)

/* APB2 */
#define STM32F7_RCC_APB2_TIM1		0
#define STM32F7_RCC_APB2_TIM8		1
#define STM32F7_RCC_APB2_USART1		4
#define STM32F7_RCC_APB2_USART6		5
#define STM32F7_RCC_APB2_ADC1		8
#define STM32F7_RCC_APB2_ADC2		9
#define STM32F7_RCC_APB2_ADC3		10
#define STM32F7_RCC_APB2_SDMMC1		11
#define STM32F7_RCC_APB2_SPI1		12
#define STM32F7_RCC_APB2_SPI4		13
#define STM32F7_RCC_APB2_SYSCFG		14
#define STM32F7_RCC_APB2_TIM9		16
#define STM32F7_RCC_APB2_TIM10		17
#define STM32F7_RCC_APB2_TIM11		18
#define STM32F7_RCC_APB2_SPI5		20
#define STM32F7_RCC_APB2_SPI6		21
#define STM32F7_RCC_APB2_SAI1		22
#define STM32F7_RCC_APB2_SAI2		23
#define STM32F7_RCC_APB2_LTDC		26

#define STM32F7_APB2_RESET(bit)	(STM32F7_RCC_APB2_##bit + (0x24 * 8))
#define STM32F7_APB2_CLOCK(bit)	(STM32F7_RCC_APB2_##bit + 0xA0)

#endif /* _DT_BINDINGS_MFD_STM32F7_RCC_H */
