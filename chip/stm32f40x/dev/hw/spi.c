#include <stdint.h>
#include <stddef.h>
#include <dev/registers.h>
#include <kernel/semaphore.h>
#include <kernel/fault.h>

#include <dev/hw/spi.h>

#define SPI_READ    (uint8_t) (1 << 7)

uint8_t spinowrite(uint8_t addr, uint8_t data, void(*cs_high)(void), void(*cs_low)(void)) __attribute__((section(".kernel")));
uint8_t spinoread(uint8_t addr, void(*cs_high)(void), void(*cs_low)(void)) __attribute__((section(".kernel")));
uint8_t spi1_write(uint8_t addr, uint8_t data, void(*cs_high)(void), void(*cs_low)(void)) __attribute__((section(".kernel")));
uint8_t spi1_read(uint8_t addr, void(*cs_high)(void), void(*cs_low)(void)) __attribute__((section(".kernel")));

spi_dev spi1 = {
    .curr_addr = 0,
    .addr_ctr = 0,
    .read = &spinoread,
    .write = &spinowrite
};

struct semaphore spi1_semaphore = {
    .lock = 0,
    .held_by = NULL,
    .waiting = NULL
};

uint8_t spinowrite(uint8_t addr, uint8_t data, void(*cs_high)(void), void(*cs_low)(void)) {
    panic_print("Attempted write on uninitialized spi device.\r\n");
    /* Execution will never reach here */
    return -1;
}

uint8_t spinoread(uint8_t addr, void(*cs_high)(void), void(*cs_low)(void)) {
    panic_print("Attempted read on uninitialized spi device.\r\n");
    /* Execution will never reach here */
    return -1;
}

void init_spi(void) {
    *RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;     /* Enable SPI1 Clock */
    *RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;    /* Enable GPIOA Clock */

    /* Set PA5, PA6, and PA7 to alternative function SPI1
     * See stm32f4_ref.pdf pg 141 and stm32f407.pdf pg 51 */

    /* Sets PA5, PA6, and PA7 to alternative function mode */
    *GPIOA_MODER &= ~(GPIO_MODER_M(5) | GPIO_MODER_M(6) | GPIO_MODER_M(7));
    *GPIOA_MODER |= (GPIO_MODER_ALT << GPIO_MODER_PIN(5)) | (GPIO_MODER_ALT << GPIO_MODER_PIN(6)) | (GPIO_MODER_ALT << GPIO_MODER_PIN(7));

    /* Sets PA5, PA6, and PA7 to SPI1-2 mode */
    *GPIOA_AFRL &= ~(GPIO_AFRL_M(5) | GPIO_AFRL_M(6) | GPIO_AFRL_M(7));
    *GPIOA_AFRL |= (GPIO_AF_SPI12 << GPIO_AFRL_PIN(5)) | (GPIO_AF_SPI12 << GPIO_AFRL_PIN(6)) | (GPIO_AF_SPI12 << GPIO_AFRL_PIN(7));

    /* Sets pin output to push/pull */
    *GPIOA_OTYPER &= ~(GPIO_OTYPER_M(5) | GPIO_OTYPER_M(6) | GPIO_OTYPER_M(7));
    *GPIOA_OTYPER |= (GPIO_OTYPER_PP << GPIO_OTYPER_PIN(5)) | (GPIO_OTYPER_PP << GPIO_OTYPER_PIN(6)) | (GPIO_OTYPER_PP << GPIO_OTYPER_PIN(7));

    /* No pull-up, no pull-down */
    *GPIOA_PUPDR &= ~(GPIO_PUPDR_M(5) | GPIO_PUPDR_M(6) | GPIO_PUPDR_M(7));
    *GPIOA_PUPDR |= (GPIO_PUPDR_NONE << GPIO_PUPDR_PIN(5)) | (GPIO_PUPDR_NONE << GPIO_PUPDR_PIN(6)) | (GPIO_PUPDR_NONE << GPIO_PUPDR_PIN(7));

    /* Speed to 50Mhz */
    *GPIOA_OSPEEDR &= ~(GPIO_OSPEEDR_M(5) | GPIO_OSPEEDR_M(6) | GPIO_OSPEEDR_M(7));
    *GPIOA_OSPEEDR |= (GPIO_OSPEEDR_50M << GPIO_OSPEEDR_PIN(5)) | (GPIO_OSPEEDR_50M << GPIO_OSPEEDR_PIN(6)) | (GPIO_OSPEEDR_50M << GPIO_OSPEEDR_PIN(7));

    /* Baud = fPCLK/8, Clock high on idle, Capture on rising edge, 16-bit data format */
    //*SPI1_CR1 |= SPI_CR1_BR_256 | SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_DFF | SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_LSBFIRST;
    *SPI1_CR1 |= SPI_CR1_BR_4 | SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;

    *SPI1_CR1 |= SPI_CR1_SPE;

    /* Sets PE3 to output mode */
    *GPIOE_MODER &= ~(GPIO_MODER_M(3));
    *GPIOE_MODER |= (GPIO_MODER_OUT << GPIO_MODER_PIN(3));

    /* Sets pin output to push/pull */
    *GPIOE_OTYPER &= ~(GPIO_OTYPER_M(3));
    *GPIOE_OTYPER |= (GPIO_OTYPER_PP << GPIO_OTYPER_PIN(3));

    /* No pull-up, no pull-down */
    *GPIOE_PUPDR &= ~(GPIO_PUPDR_M(3));
    *GPIOE_PUPDR |= (GPIO_PUPDR_NONE << GPIO_PUPDR_PIN(3));

    /* Speed to 50Mhz */
    *GPIOE_OSPEEDR &= ~(GPIO_OSPEEDR_M(3));
    *GPIOE_OSPEEDR |= (GPIO_OSPEEDR_100M << GPIO_OSPEEDR_PIN(3));

    init_semaphore(&spi1_semaphore);

    /* Set up abstract device for later use in device drivers */
    spi1.write = &spi1_write;
    spi1.read = &spi1_read;
}

uint8_t spi1_write(uint8_t addr, uint8_t data, void(*cs_high)(void), void(*cs_low)(void)) {
    /* Data MUST be read after each TX */
    volatile uint8_t read;

    /* Clear overrun by reading old data */
    if (*SPI1_SR & SPI_SR_OVR) {
        read = *SPI1_DR;
        read = *SPI1_SR;
    }

    cs_low();

    while (!(*SPI1_SR & SPI_SR_TXNE));
    *SPI1_DR = addr;

    while (!(*SPI1_SR & SPI_SR_RXNE));

    read = *SPI1_DR;

    while (!(*SPI1_SR & SPI_SR_TXNE));
    *SPI1_DR = data;

    while (!(*SPI1_SR & SPI_SR_RXNE));

    cs_high();

    read = *SPI1_DR;

    return read;
}

uint8_t spi1_read(uint8_t addr, void(*cs_high)(void), void(*cs_low)(void)) {

    /* Data MUST be read after each TX */
    volatile uint8_t read;

    /* Clear overrun by reading old data */
    if (*SPI1_SR & SPI_SR_OVR) {
        read = *SPI1_DR;
        read = *SPI1_SR;
    }

    cs_low();

    while (!(*SPI1_SR & SPI_SR_TXNE));

    *SPI1_DR = (addr | SPI_READ);

    while (!(*SPI1_SR & SPI_SR_RXNE));
    read = *SPI1_DR;

    while (!(*SPI1_SR & SPI_SR_TXNE));
    *SPI1_DR = 0x00;

    while (!(*SPI1_SR & SPI_SR_RXNE));

    cs_high();

    read = *SPI1_DR;

    return read;
}
