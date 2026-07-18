// Code for ATtiny84 MCU to control logic for letter M display device
// How I will implement it is I will have a temporary buffer in software that will hold the states of all 13 LEDs.
// Whenever I want to change which LEDs are actually on I must update the internal buffer and then push this buffer to the LED driver.
// I will limit the number of LEDs that can be on at once to 6.

// Clock speed = 1 MHz

#include <stdbool.h>
#include <avr/io.h> // The preprocessor will automatically detect your target chip and load the specific definitions for the ATtiny84   <-- How??
#include <avr/interrupt.h>  // What does this library provide ??

#define SDI 0
#define CLK 1
#define LE 2
#define OE 3
#define BTNA 2
#define BTNB 7

// '''
// Atomicity: While changing an int between 0 and 1 is safe on most 8-bit, 16-bit, and 32-bit microcontrollers,
//you should use standard atomic types (volatile sig_atomic_t from <signal.h> or <stdatomic.h>) if you are
//working with larger data types. This prevents the main() loop from reading a half-written value if the
//interrupt strikes exactly during a read operation.
// '''

//  --- Interface ---
// Inputs:
// - Button A,  Pin 5 (enable pull-up),  PB2
// - Button B,  Pin 6 (enable pull-up),  PA7
// - Pot,       Pin 7,                   PA6
// Outputs:
// - SDI,       Pin 13 (active low), PA0
// - CLK,       Pin 12,              PA1
// - LE,        Pin 11,              PA2
// - OE,        Pin 10 (active low), PA3
// ***NOTE: Writing a logical one to PINxn toggles the value of PORTxn, independent on the value of DDRxn.

//  --- ISRs ---
// Button A - Enable internal pullup, Falling edge
// Button B - Enable internal pullup, Falling edge

// --- Peripherals ---
// Timer - 16bit  (internal)
// ADC (internal)
// Shift Register (external)
// Buttons (external)
// Potentiometer (external)

volatile bool BTN_A_pressed = 0;
volatile bool BTN_B_pressed = 0;
volatile bool POT_delay_flag = 0;

uint8_t mode = 0;
uint16_t LED_STATES = 0;  // 13 bit register to hold the boolean states of each of the LEDs
uint8_t mode_0_LED_gap = 13;
uint8_t mode_0_counter = 13;


// ISR(TIM0_COMPA_vect) {
//     // Empty ISR is sufficient if ADC triggering is handled in hardware
//     __asm__("nop");
// }

ISR(TIM1_COMPA_vect) {
  POT_delay_flag = true;
}


void pulse_pin(uint8_t pin) {
  switch (pin) {
    case (CLK):
      PORTA |= (1 << CLK);
      __asm__("nop");
      PORTA &= ~(1 << CLK);
      break;
    case (LE):
      PORTA |= (1 << LE);
      __asm__("nop");
      PORTA &= ~(1 << LE);
      break;
    case (OE):
      break;
    default:
      break;
  }
  return;
}


void init_GPIO() {
  // Set inputs and outputs
  DDRA |= (1 << SDI) | (1 << CLK) | (1 << LE) | (1 << OE);
  DDRA &= ~(1 << BTNB) & ~(1 << DDA6);
  DDRB &= ~(1 << BTNA);

  // Enable pull-up resistors for push buttons
  PORTA |= (1 << BTNB);
  PORTB |= (1 << BTNA);

  // Set OE initial state to LOW
  PORTA &= ~(1 << OE);
}


void init_ADC() {
  // REFS1:0 set to 0 for VCC to be used as ADC reference
  // MUX5:0 = 000110 for ADC6 on PA6
  ADMUX = (1 << MUX2) | (1 << MUX1);

  // enable, auto trigger enable, interrupt enable, prescaler = 8 (adc clock = 125KHz, 1 sample = 104us)
  ADCSRA = (1 << ADEN) | (1 << ADATE) | (1 << ADIE) | (1 << ADPS1) | (1 << ADPS0);

  // auto trigger source = Timer/Counter0 Compare Match A
  ADCSRB = (1 << ADTS1) | (1 << ADTS0);

  // disable digital input buffer on ADC pin 6 to reduce poweer consumption
  DIDR0 = (1 << ADC6D);
}


void init_timer0() {
  // Need to setup Timer/Counter0 Compare Match A for ADC auto trigger
  // Compare match should trigger every ~17ms (60Hz)
  // CTC Mode
  TCCR0A = (1 << WGM01);

  TCCR0B = (1 << CS02);

  OCR0A = 64;

  TIMSK0 = (1 << OCIE0A);
}


void init_timer1() {
  // CTC mode, prescaler 256
  TCCR1B = (1 << WGM12) | (1 << CS12);

  // Output compare 3096 is 1 second
  OCR1AH = (3905 >> 8);
  OCR1AL = (uint8_t)3905;

  TIMSK1 = (1 << OCIE1A);
}


void update_LEDs() {
  // Can we represent LED_STATES as an array of bytes to save RAM and minimize clock cycles?
  for (int8_t i = 12; i >= 0; i--) {
    if (((LED_STATES >> i) & 1)) {
      PORTA |= (1 << SDI);
    } else {
      PORTA &= ~(1 << SDI);
    }
    pulse_pin(CLK);
  }
  pulse_pin(LE);
  return;
}


int main() {

  init_GPIO();
  // init_ADC();
  // init_timer0();
  init_timer1();

  sei();

  while(1) {

    // Button A cycles through the modes
    if (mode == 0) {
        if (POT_delay_flag) {
        // Mode 0 is when the LEDs travel from left to right along the M
        // Button B: number of LEDs 'on' at once
        // Pot: speed of LEDs travelling along M

        POT_delay_flag = 0;

        if (BTN_B_pressed) {
          BTN_B_pressed = 0;
          if (mode_0_LED_gap <= 1) {
            mode_0_LED_gap = 13;
          } else {
            mode_0_LED_gap--;
          }
        }

        if (BTN_A_pressed) {
          BTN_A_pressed = 0;
          mode = 1;
        }

        // Go through LED_STATES buffer and for each bit that is set, unset it and set the next one over
        // for (int8_t i = 12; i >=; i--) {
        //   if ((LED_STATES << i) & 1) {
        //     LED_STATES &= ~(1 << i);
        //     if (i != 12) {
        //       LED_STATES |= (1 << (i+1));
        //     }
        //   }
        // }
        // Or just shift the buffer over by one
        LED_STATES = LED_STATES >> 1;
        
        // Then based on mode_0_LED_gap value, set the first LED or not
        mode_0_counter--;
        if (mode_0_counter == 0) {
          mode_0_counter = mode_0_LED_gap;
          LED_STATES |= (1 << 12);
        }

        // Delay for a certain amount of time (delay duration based on potentiometer)
        // For this delay we will use 16-bit timer 1

      }

    } else if (mode == 1) {
      // Mode 1 is the same as Mode 0 but with the LEDs going the other direction (right to left)

    } else if (mode == 2) {
      // Mode 2 has the LED chain behaving as a shift register
      // Button B: shift LEDs over by one
      // Pot: shift in 1 or 0

    } else if (mode == 3) {
      // Mode 3
    }




  }


  return 0;

}

