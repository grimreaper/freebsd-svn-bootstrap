/*
 * This header provides constants for the STM32H7 RCC IP
 */

#ifndef _DT_BINDINGS_MFD_STM32H7_RCC_H
#define _DT_BINDINGS_MFD_STM32H7_RCC_H

/* AHB3 */
#define STM32H7_RCC_AHB3_MDMA		0
#define STM32H7_RCC_AHB3_DMA2D		4
#define STM32H7_RCC_AHB3_JPGDEC		5
#define STM32H7_RCC_AHB3_FMC		12
#define STM32H7_RCC_AHB3_QUADSPI	14
#define STM32H7_RCC_AHB3_SDMMC1		16
#define STM32H7_RCC_AHB3_CPU		31

#define STM32H7_AHB3_RESET(bit) (STM32H7_RCC_AHB3_##bit + (0x7C * 8))

/* AHB1 */
#define STM32H7_RCC_AHB1_DMA1		0
#define STM32H7_RCC_AHB1_DMA2		1
#define STM32H7_RCC_AHB1_ADC12		5
#define STM32H7_RCC_AHB1_ART		14
#define STM32H7_RCC_AHB1_ETH1MAC	15
#define STM32H7_RCC_AHB1_USB1OTG	25
#define STM32H7_RCC_AHB1_USB2OTG	27

#define STM32H7_AHB1_RESET(bit) (STM32H7_RCC_AHB1_##bit + (0x80 * 8))

/* AHB2 */
#define STM32H7_RCC_AHB2_CAMITF		0
#define STM32H7_RCC_AHB2_CRYPT		4
#define STM32H7_RCC_AHB2_HASH		5
#define STM32H7_RCC_AHB2_RNG		6
#define STM32H7_RCC_AHB2_SDMMC2		9

#define STM32H7_AHB2_RESET(bit) (STM32H7_RCC_AHB2_##bit + (0x84 * 8))

/* AHB4 */
#define STM32H7_RCC_AHB4_GPIOA		0
#define STM32H7_RCC_AHB4_GPIOB		1
#define STM32H7_RCC_AHB4_GPIOC		2
#define STM32H7_RCC_AHB4_GPIOD		3
#define STM32H7_RCC_AHB4_GPIOE		4
#define STM32H7_RCC_AHB4_GPIOF		5
#define STM32H7_RCC_AHB4_GPIOG		6
#define STM32H7_RCC_AHB4_GPIOH		7
#define STM32H7_RCC_AHB4_GPIOI		8
#define STM32H7_RCC_AHB4_GPIOJ		9
#define STM32H7_RCC_AHB4_GPIOK		10
#define STM32H7_RCC_AHB4_CRC		19
#define STM32H7_RCC_AHB4_BDMA		21
#define STM32H7_RCC_AHB4_ADC3		24
#define STM32H7_RCC_AHB4_HSEM		25

#define STM32H7_AHB4_RESET(bit) (STM32H7_RCC_AHB4_##bit + (0x88 * 8))

/* APB3 */
#define STM32H7_RCC_APB3_LTDC		3
#define STM32H7_RCC_APB3_DSI		4

#define STM32H7_APB3_RESET(bit) (STM32H7_RCC_APB3_##bit + (0x8C * 8))

/* APB1L */
#define STM32H7_RCC_APB1L_TIM2		0
#define STM32H7_RCC_APB1L_TIM3		1
#define STM32H7_RCC_APB1L_TIM4		2
#define STM32H7_RCC_APB1L_TIM5		3
#define STM32H7_RCC_APB1L_TIM6		4
#define STM32H7_RCC_APB1L_TIM7		5
#define STM32H7_RCC_APB1L_TIM12		6
#define STM32H7_RCC_APB1L_TIM13		7
#define STM32H7_RCC_APB1L_TIM14		8
#define STM32H7_RCC_APB1L_LPTIM1	9
#define STM32H7_RCC_APB1L_SPI2		14
#define STM32H7_RCC_APB1L_SPI3		15
#define STM32H7_RCC_APB1L_SPDIF_RX	16
#define STM32H7_RCC_APB1L_USART2	17
#define STM32H7_RCC_APB1L_USART3	18
#define STM32H7_RCC_APB1L_UART4		19
#define STM32H7_RCC_APB1L_UART5		20
#define STM32H7_RCC_APB1L_I2C1		21
#define STM32H7_RCC_APB1L_I2C2		22
#define STM32H7_RCC_APB1L_I2C3		23
#define STM32H7_RCC_APB1L_HDMICEC	27
#define STM32H7_RCC_APB1L_DAC12		29
#define STM32H7_RCC_APB1L_USART7	30
#define STM32H7_RCC_APB1L_USART8	31

#define STM32H7_APB1L_RESET(bit) (STM32H7_RCC_APB1L_##bit + (0x90 * 8))

/* APB1H */
#define STM32H7_RCC_APB1H_CRS		1
#define STM32H7_RCC_APB1H_SWP		2
#define STM32H7_RCC_APB1H_OPAMP		4
#define STM32H7_RCC_APB1H_MDIOS		5
#define STM32H7_RCC_APB1H_FDCAN		8

#define STM32H7_APB1H_RESET(bit) (STM32H7_RCC_APB1H_##bit + (0x94 * 8))

/* APB2 */
#define STM32H7_RCC_APB2_TIM1		0
#define STM32H7_RCC_APB2_TIM8		1
#define STM32H7_RCC_APB2_USART1		4
#define STM32H7_RCC_APB2_USART6		5
#define STM32H7_RCC_APB2_SPI1		12
#define STM32H7_RCC_APB2_SPI4		13
#define STM32H7_RCC_APB2_TIM15		16
#define STM32H7_RCC_APB2_TIM16		17
#define STM32H7_RCC_APB2_TIM17		18
#define STM32H7_RCC_APB2_SPI5		20
#define STM32H7_RCC_APB2_SAI1		22
#define STM32H7_RCC_APB2_SAI2		23
#define STM32H7_RCC_APB2_SAI3		24
#define STM32H7_RCC_APB2_DFSDM1		28
#define STM32H7_RCC_APB2_HRTIM		29

#define STM32H7_APB2_RESET(bit) (STM32H7_RCC_APB2_##bit + (0x98 * 8))

/* APB4 */
#define STM32H7_RCC_APB4_SYSCFG		1
#define STM32H7_RCC_APB4_LPUART1	3
#define STM32H7_RCC_APB4_SPI6		5
#define STM32H7_RCC_APB4_I2C4		7
#define STM32H7_RCC_APB4_LPTIM2		9
#define STM32H7_RCC_APB4_LPTIM3		10
#define STM32H7_RCC_APB4_LPTIM4		11
#define STM32H7_RCC_APB4_LPTIM5		12
#define STM32H7_RCC_APB4_COMP12		14
#define STM32H7_RCC_APB4_VREF		15
#define STM32H7_RCC_APB4_SAI4		21
#define STM32H7_RCC_APB4_TMPSENS	26

#define STM32H7_APB4_RESET(bit) (STM32H7_RCC_APB4_##bit + (0x9C * 8))

#endif /* _DT_BINDINGS_MFD_STM32H7_RCC_H */
