/*****************************************************************************
* Product: "Fly 'n' Shoot" game example, cooperative QV kernel
* Last Updated for Version: 5.5.1
* Date of the Last Update:  2015-10-05
*
*                    Q u a n t u m     L e a P s
*                    ---------------------------
*                    innovating embedded systems
*
* Copyright (C) Quantum Leaps, LLC. All rights reserved.
*
* This program is open source software: you can redistribute it and/or
* modify it under the terms of the GNU General Public License as published
* by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Alternatively, this program may be distributed and modified under the
* terms of Quantum Leaps commercial licenses, which expressly supersede
* the GNU General Public License and are specifically designed for
* licensees interested in retaining the proprietary status of their code.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
* Contact information:
* http://www.state-machine.com
* mailto:info@state-machine.com
*****************************************************************************/
#include "qpn.h"   /* QP-nano */
#include "game.h"  /* Game application */
#include "bsp.h"   /* Board Support Package */

#include "LM3S811.h"        /* the device specific header (TI) */
#include "display96x16x1.h" /* the OLED display driver (TI) */

Q_DEFINE_THIS_FILE

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! CAUTION !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
* Assign a priority to EVERY ISR explicitly by calling NVIC_SetPriority().
* DO NOT LEAVE THE ISR PRIORITIES AT THE DEFAULT VALUE!
*/
enum KernelUnawareISRs {  /* see NOTE00 */
    /* ... */
    MAX_KERNEL_UNAWARE_CMSIS_PRI  /* keep always last */
};
/* "kernel-unaware" interrupts can't overlap "kernel-aware" interrupts */
Q_ASSERT_COMPILE(MAX_KERNEL_UNAWARE_CMSIS_PRI <= QF_AWARE_ISR_CMSIS_PRI);

enum KernelAwareISRs {
    ADCSEQ3_PRIO = QF_AWARE_ISR_CMSIS_PRI, /* see NOTE00 */
    GPIOPORTA_PRIO,
    SYSTICK_PRIO,
    /* ... */
    MAX_KERNEL_AWARE_CMSIS_PRI /* keep always last */
};
/* "kernel-aware" interrupts should not overlap the PendSV priority */
Q_ASSERT_COMPILE(MAX_KERNEL_AWARE_CMSIS_PRI <= (0xFF >>(8-__NVIC_PRIO_BITS)));

/* ISRs defined in this BSP ------------------------------------------------*/
void SysTick_Handler(void);
void GPIOPortA_IRQHandler(void);

/* Local-scope objects -----------------------------------------------------*/
/* LEDs available on the board */
#define USER_LED  (1U << 5)

/* Push-Button wired externally to DIP8 (P0.6) */
#define USER_BTN  (1U << 4)

#define ADC_TRIGGER_TIMER       0x00000005U
#define ADC_CTL_IE              0x00000040U
#define ADC_CTL_END             0x00000020U
#define ADC_CTL_CH0             0x00000000U
#define ADC_SSFSTAT0_EMPTY      0x00000100U
#define UART_FR_TXFE            0x00000080U

/* prototypes of ISRs defined in the BSP....................................*/
void SysTick_Handler(void);
void ADCSeq3_IRQHandler(void);
void assert_failed(char const *file, int line);

/* ISRs used in the application ==========================================*/
void SysTick_Handler(void) {
    QF_tickXISR(0U); /* process time events for rate 0 */

    /* post TIME_TICK events to all interested active objects... */
    QACTIVE_POST_ISR((QMActive *)&AO_Tunnel,  TIME_TICK_SIG, 0);
    QACTIVE_POST_ISR((QMActive *)&AO_Ship,    TIME_TICK_SIG, 0);
    QACTIVE_POST_ISR((QMActive *)&AO_Missile, TIME_TICK_SIG, 0);
}
/*..........................................................................*/
void ADCSeq3_IRQHandler(void) {
    static uint32_t adcLPS = 0U; /* Low-Pass-Filtered ADC reading */
    static uint32_t wheel  = 0U; /* the last wheel position */

    /* state variables for button debouncing, see below */
    static struct ButtonsDebouncing {
        uint32_t depressed;
        uint32_t previous;
    } buttons = { ~0U, ~0U };
    uint32_t current;

    uint32_t tmp;

    ADC->ISC = (1U << 3); /* clear the ADCSeq3 interrupt */

    /* the ADC Sequence 3 FIFO must have a sample */
    Q_ASSERT((ADC->SSFSTAT3 & ADC_SSFSTAT0_EMPTY) == 0);

    /* 1st order low-pass filter: time constant ~= 2^n samples
     * TF = (1/2^n)/(z-((2^n - 1)/2^n)),
     * eg, n=3, y(k+1) = y(k) - y(k)/8 + x(k)/8 => y += (x - y)/8
     */
    tmp = ADC->SSFIFO3; /* read the data from the ADC */
    adcLPS += (((int)tmp - (int)adcLPS + 4) >> 3);

    /* compute the next position of the wheel */
    tmp = (((1U << 10) - adcLPS)*(BSP_SCREEN_HEIGHT - 2U)) >> 10;
    if (tmp != wheel) { /* did the wheel position change? */
        QACTIVE_POST_ISR((QMActive *)&AO_Ship, PLAYER_SHIP_MOVE_SIG,
                         ((tmp << 8) | GAME_SHIP_X));
        wheel = tmp; /* save the last position of the wheel */
    }

    /* Perform the debouncing of buttons. The algorithm for debouncing
    * adapted from the book "Embedded Systems Dictionary" by Jack Ganssle
    * and Michael Barr, page 71.
    */
    current = ~GPIOC->DATA; /* read the port with the User Button */
    tmp = buttons.depressed; /* save the debounced depressed buttons */
    buttons.depressed |= (buttons.previous & current); /* set depressed */
    buttons.depressed &= (buttons.previous | current); /* clear released */
    buttons.previous   = current; /* update the history */
    tmp ^= buttons.depressed;     /* changed debounced depressed */
    if ((tmp & USER_BTN) != 0U) { /* debounced USER_BTN state changed? */
        if ((buttons.depressed & USER_BTN) != 0U) { /* is BTN depressed? */
            QACTIVE_POST_ISR((QMActive *)&AO_Ship,   PLAYER_TRIGGER_SIG, 0U);
            QACTIVE_POST_ISR((QMActive *)&AO_Tunnel, PLAYER_TRIGGER_SIG, 0U);
        }
        else { /* the button is released */
        }
    }
}
/*..........................................................................*/
void GPIOPortA_IRQHandler(void) {
    /* for testing... */
    QACTIVE_POST_ISR((QMActive *)&AO_Tunnel, TAKE_OFF_SIG, 0U);
}

/* BSP functions ===========================================================*/
void BSP_init(void) {
    /* NOTE: SystemInit() already called from startup_TM4C123GH6PM.s
    *  but SystemCoreClock needs to be updated
    */
    SystemCoreClockUpdate();

    /* enable clock to the peripherals used by the application */
    SYSCTL->RCGC0 |= (1U << 16);              /* enable clock to ADC        */
    SYSCTL->RCGC1 |= (1U << 16) | (1U << 17); /* enable clock to TIMER0 & 1 */
    SYSCTL->RCGC2 |= (1U <<  0) | (1U <<  2); /* enable clock to GPIOA & C  */
    __NOP();                                  /* wait after enabling clocks */
    __NOP();
    __NOP();

    /* Configure the ADC Sequence 3 to sample the potentiometer when the
    * timer expires. Set the sequence priority to 0 (highest).
    */
    ADC->EMUX   = (ADC->EMUX   & ~(0xFU << (3*4)))
                  | (ADC_TRIGGER_TIMER << (3*4));
    ADC->SSPRI  = (ADC->SSPRI  & ~(0xFU << (3*4)))
                  | (0 << (3*4));
    /* set ADC Sequence 3 step to 0 */
    ADC->SSMUX3 = (ADC->SSMUX3 & ~(0xFU << (0*4)))
                  | ((ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END) << (0*4));
    ADC->SSCTL3 = (ADC->SSCTL3 & ~(0xFU << (0*4)))
                  | (((ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END) >> 4) <<(0*4));
    ADC->ACTSS |= (1U << 3);

    /* configure TIMER1 to trigger the ADC to sample the potentiometer. */
    TIMER1->CTL  &= ~((1U << 0) | (1U << 16));
    TIMER1->CFG   = 0x00U;
    TIMER1->TAMR  = 0x02U;
    TIMER1->TAILR = SystemCoreClock / 120U;
    TIMER1->CTL  |= 0x02U;
    TIMER1->CTL  |= 0x20U;

    /* configure the User LED... */
    GPIOC->DIR |= USER_LED; /* set direction: output */
    GPIOC->DEN |= USER_LED; /* digital enable */
    GPIOC->DATA_Bits[USER_LED] = 0U; /* turn the User LED off */

    /* configure the User Button... */
    GPIOC->DIR &= ~USER_BTN; /*  set direction: input */
    GPIOC->DEN |= USER_BTN;  /* digital enable */

    Display96x16x1Init(1); /* initialize the OLED display */
}
/*..........................................................................*/
void BSP_drawBitmap(uint8_t const *bitmap) {
    Display96x16x1ImageDraw(bitmap, 0U, 0U,
                            BSP_SCREEN_WIDTH, (BSP_SCREEN_HEIGHT >> 3));
}
/*..........................................................................*/
void BSP_drawBitmapXY(uint8_t const *bitmap, uint8_t x, uint8_t y) {
    Display96x16x1ImageDraw(bitmap, x, y,
                            BSP_SCREEN_WIDTH, (BSP_SCREEN_HEIGHT >> 3));
}
/*..........................................................................*/
void BSP_drawNString(uint8_t x, uint8_t y, char const *str) {
    Display96x16x1StringDraw(str, x, y);
}
/*..........................................................................*/
void BSP_updateScore(uint16_t score) {
    /* no room on the OLED display of the EV-LM3S811 board for the score */
}
/*..........................................................................*/
void BSP_displayOn(void) {
    Display96x16x1DisplayOn();
}
/*..........................................................................*/
void BSP_displayOff(void) {
    Display96x16x1DisplayOff();
}

/* QF callbacks ============================================================*/
void QF_onStartup(void) {
    /* set up the SysTick timer to fire at BSP_TICKS_PER_SEC rate */
    SysTick_Config(SystemCoreClock / BSP_TICKS_PER_SEC);

    /* assing all priority bits for preemption-prio. and none to sub-prio. */
    NVIC_SetPriorityGrouping(0U);

    /* set priorities of ALL ISRs used in the system, see NOTE00
    *
    * !!!!!!!!!!!!!!!!!!!!!!!!!!!! CAUTION !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    * Assign a priority to EVERY ISR explicitly by calling NVIC_SetPriority().
    * DO NOT LEAVE THE ISR PRIORITIES AT THE DEFAULT VALUE!
    */
    NVIC_SetPriority(ADCSeq3_IRQn,   ADCSEQ3_PRIO);
    NVIC_SetPriority(SysTick_IRQn,   SYSTICK_PRIO);
    /* ... */

    /* enable IRQs... */
    NVIC_EnableIRQ(ADCSeq3_IRQn);
    NVIC_EnableIRQ(GPIOPortA_IRQn);

    ADC->ISC = (1U << 3);
    ADC->IM |= (1U << 3);

    TIMER1->CTL |= ((1U << 0) | (1U << 16)); /* enable TIMER1 */
}
/*..........................................................................*/
void QF_stop(void) {
}
/*..........................................................................*/
void QV_onIdle(void) {  /* called with interrupts disabled, see NOTE01 */

    /* toggle the User LED on and then off, see NOTE02 */
    GPIOC->DATA_Bits[USER_LED] = 0xFFU; /* turn the User LED on  */
    GPIOC->DATA_Bits[USER_LED] = 0x00U; /* turn the User LED off */

#ifdef NDEBUG
    /* Put the CPU and peripherals to the low-power mode.
    * you might need to customize the clock management for your application,
    * see the datasheet for your particular Cortex-M3 MCU.
    */
    QV_CPU_SLEEP();  /* atomically go to sleep and enable interrupts */
#else
    QF_INT_ENABLE(); /* just enable interrupts */
#endif
}

/*..........................................................................*/
void Q_onAssert(char const Q_ROM * const Q_ROM_VAR module, int loc) {
    /*
    * NOTE: add here your application-specific error handling
    */
    (void)module;
    (void)loc;

    NVIC_SystemReset();
}

/*****************************************************************************
* NOTE00:
* The QF_AWARE_ISR_CMSIS_PRI constant from the QF port specifies the highest
* ISR priority that is disabled by the QF framework. The value is suitable
* for the NVIC_SetPriority() CMSIS function.
*
* Only ISRs prioritized at or below the QF_AWARE_ISR_CMSIS_PRI level (i.e.,
* with the numerical values of priorities equal or higher than
* QF_AWARE_ISR_CMSIS_PRI) are allowed to call the QK_ISR_ENTRY/QK_ISR_ENTRY
* macros or any other QF/QK  services. These ISRs are "QF-aware".
*
* Conversely, any ISRs prioritized above the QF_AWARE_ISR_CMSIS_PRI priority
* level (i.e., with the numerical values of priorities less than
* QF_AWARE_ISR_CMSIS_PRI) are never disabled and are not aware of the kernel.
* Such "QF-unaware" ISRs cannot call any QF/QK services. In particular they
* can NOT call the macros QK_ISR_ENTRY/QK_ISR_ENTRY. The only mechanism
* by which a "QF-unaware" ISR can communicate with the QF framework is by
* triggering a "QF-aware" ISR, which can post/publish events.
*
* NOTE01:
* The QV_onIdle() callback is called with interrupts disabled, because the
* determination of the idle condition might change by any interrupt posting
* an event. QV_onIdle() must internally enable interrupts, ideally
* atomically with putting the CPU to the power-saving mode.
*
* NOTE02:
* The User LED is used to visualize the idle loop activity. The brightness
* of the LED is proportional to the frequency of invcations of the idle loop.
* Please note that the LED is toggled with interrupts locked, so no interrupt
* execution time contributes to the brightness of the User LED.
*/