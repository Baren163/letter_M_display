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
#define MIN_LED_GAP 1

// '''
// Atomicity: While changing an int between 0 and 1 is safe on most 8-bit, 16-bit, and 32-bit microcontrollers,
//you should use standard atomic types (volatile sig_atomic_t from <signal.h> or <stdatomic.h>) if you are
//working with larger data types. This prevents the main() loop from reading a half-written value if the
//interrupt strikes exactly during a read operation.
// '''

//  --- Interface ---
// Inputs:
// - Button A,  Pin 5 (enable pull-up),  PB2, PCINT10
// - Button B,  Pin 6 (enable pull-up),  PA7, PCINT7
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
volatile uint16_t ADC_val = 0;

uint8_t mode = 0;
uint16_t LED_STATES = 0;  // 13 bit register to hold the boolean states of each of the LEDs
uint8_t mode_0_LED_gap = 6;
uint8_t mode_0_counter = 6;


ISR(TIM0_COMPA_vect) {
  // Empty ISR is sufficient if ADC triggering is handled in hardware
}

ISR(TIM1_OVF_vect) {
  // For this timer I will look into using Fast PWM mode instead of CTC because CTC does not have double buffering
  POT_delay_flag = true;
}

ISR(ADC_vect) {
  // When ADC conversion completes (every ~17ms), read ADC value, shift it right by 3 to lose 3 bits (3 decimals) of accuracy,
  //compare with previous ADC value, if different ...


  // Read ADC value
  ADC_val = ADC;

  // // Map it to new OCR1A value
  // // Mapping (linear for now): 0 to 3.3v (0 to 1023) -> 200 to 4000
  // uint16_t output_comp_val = ((float)ADC_val * 3.714) + 200;

  // // Update OCR1A
  // OCR1AH = (output_comp_val >> 8);
  // OCR1AL = (uint8_t)output_comp_val;

  // Map 0–1023 to 200–4000
  uint16_t top = 200U + (uint16_t)(((uint32_t)ADC_val * 3800U) / 1023U);

  OCR1A = top;
}

ISR(PCINT0_vect) {
  // Pin change 0 interrupts (PCI0) will trigger if any enabled PCINT7..0 pin toggles (BTNB)

  // Only detect falling edge (button press)
  if (PINA & (1 << PINA7)) {
    // Button was just released so do nothing
  } else {
    BTN_B_pressed = true;
  }
}

ISR(PCINT1_vect) {
  // Pin change 1 interrupts (PCI1) will trigger if any enabled PCINT11..8 pin toggles (BTNA)

  // Only detect falling edge (button press)
  if (PINB & (1 << PINB2)) {
    // Button was just released so do nothing
  } else {
    BTN_A_pressed = true;
  }
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

  // Enable GPIO pin interrupts
  GIMSK = (1 << PCIE1) | (1 << PCIE0);
  PCMSK1 = (1 << PCINT10);
  PCMSK0 = (1 << PCINT7);
}


void init_ADC() {
  // REFS1:0 set to 0 for VCC to be used as ADC reference (3.3v)
  // MUX5:0 = 000110 for ADC6 on PA6
  ADMUX = (1 << MUX2) | (1 << MUX1);

  // enable, auto trigger enable, interrupt enable, prescaler = 8 (adc clock = 125KHz, 1 sample = 104us)
  ADCSRA = (1 << ADEN) | (1 << ADATE) | (1 << ADIE) | (1 << ADPS1) | (1 << ADPS0) | (1 << ADSC);

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

  TIMSK0 = (1 << OCIE0A); // you do not need to enable the Timer0 interrupt merely to generate an ADC hardware trigger, The ADC can detect the compare-match flag without executing a Timer0 ISR. Remove that line and remove the empty ISR
}


void init_timer1() {
  // Fast PWM mode uses double buffering for the OCR1A value and since we have no need for a PWM waveform we can use the
  //OCR1A value as the TOP of the timer

  // 3906 = 1s
  OCR1AH = (600 >> 8);
  OCR1AL = (uint8_t)600;

  // Fast PWM mode with OCR1A as TOP, instead of using Output Compare 1A interrupt I will use TOV1 interrupt, prescaler 256
  TCCR1A = (1 << WGM11) | (1 << WGM10);
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS12);
  TIMSK1 = (1 << TOIE1);
}


void update_LEDs() {
  // // Can we represent LED_STATES as an array of bytes to save RAM and minimize clock cycles?
  // for (int8_t i = 12; i >= 0; i--) {
  //   if (((LED_STATES >> i) & 1)) {
  //     PORTA |= (1 << SDI);
  //   } else {
  //     PORTA &= ~(1 << SDI);
  //   }
  //   pulse_pin(CLK);
  // }
  // pulse_pin(LE);
  // return;

  uint16_t temp_states = LED_STATES;

  // Loop exactly 13 times (from bit index 12 down to 0)
  for (uint8_t i = 0; i < 13; i++) {
    // Always check the highest bit of interest (Bit 12)
    if (temp_states & (1 << 12)) {
      PORTA |= (1 << SDI);
    } else {
      PORTA &= ~(1 << SDI);
    }
    
    pulse_pin(CLK);
    
    // Shift left so the next bit moves into the Bit 12 position
    temp_states <<= 1; 
  }
  
  pulse_pin(LE);
}


int main() {

  init_GPIO();
  init_ADC();
  init_timer0();
  init_timer1();

  sei();

  while(1) {

    // Button A cycles through the modes
    switch (mode) {
      case (0):
        if (POT_delay_flag) {
          // Mode 0 is when the LEDs travel from left to right along the M
          // Button B: number of LEDs 'on' at once
          // Pot: speed of LEDs travelling along M

          POT_delay_flag = 0;

          if (BTN_B_pressed) {
            BTN_B_pressed = 0;
            if (mode_0_LED_gap <= MIN_LED_GAP) {
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
            LED_STATES |= (1 << 13);
          }

          update_LEDs();

          // Delay for a certain amount of time (delay duration based on potentiometer)
          // For this delay we will use 16-bit timer 1
        }
        break;

      case (1):
        // Mode 1 is the same as Mode 0 but with the LEDs going the other direction (right to left)
        if (POT_delay_flag) {
          // Mode 0 is when the LEDs travel from left to right along the M
          // Button B: number of LEDs 'on' at once
          // Pot: speed of LEDs travelling along M

          POT_delay_flag = 0;

          if (BTN_B_pressed) {
            BTN_B_pressed = 0;
            if (mode_0_LED_gap <= MIN_LED_GAP) {
              mode_0_LED_gap = 13;
            } else {
              mode_0_LED_gap--;
            }
          }

          if (BTN_A_pressed) {
            BTN_A_pressed = 0;
            mode = 2;
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
          LED_STATES = LED_STATES << 1;
          
          // Then based on mode_0_LED_gap value, set the first LED or not
          mode_0_counter--;
          if (mode_0_counter == 0) {
            mode_0_counter = mode_0_LED_gap;
            LED_STATES |= (1);
          }

          update_LEDs();

          // Delay for a certain amount of time (delay duration based on potentiometer)
          // For this delay we will use 16-bit timer 1
        }
        break;

      case (2):
        // Mode 2 has manual LED sending with BTN_B and POT controls the speed it moves along
        if (POT_delay_flag) {

          POT_delay_flag = 0;

          if (BTN_B_pressed) {
            BTN_B_pressed = 0;
            LED_STATES |= (1 << 13);
          }

          if (BTN_A_pressed) {
            BTN_A_pressed = 0;
            mode = 0;
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


          update_LEDs();

          // Delay for a certain amount of time (delay duration based on potentiometer)
          // For this delay we will use 16-bit timer 1
        }
        break;

      case (3):
        // Mode 3 has the LED chain behaving as a shift register
        // Button B: shift LEDs over by one
        // Pot: shift in 1 or 0

      default:
        break;

    }




  }


  return 0;

}

