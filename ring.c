/*
 * tuerklingel by prom
 *
 * public domain
 * all rights reversed
 *
 * WARNING: this wont work (and probably wont
 * even compile) with the avr-libc distributed
 * in ubuntu. this version is totally outdated.
 * use a more current one.
 *
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include <avril/types.h>
#include <avril/pin.h>
#include <avril/tick.h>
#include <avril/urxtx.h>


/* number of times ring must be pressed */
#define PRESSES 5

/* number of ticks per second (must correspond to system timer) */
#define SECOND 100

/* debouncing delay */
#define DEBOUNCE 0.025*SECOND

/* timeout for the "opening process" */
#define TIMEOUT 15*SECOND

/* duration of led blink on ring */
#define BLINKTIME 0.3*SECOND

/* how long should the door be opened? */
#define OPENTIME 3*SECOND

/* time to wait before activating strobe */
#define STROBEDELAY 2*SECOND

/* locked by default? */
#define LOCKED_BY_DEFAULT FALSE

/* strobe enabled by default? */
#define STROBE_BY_DEFAULT TRUE


/* ports for our devices */

PIN_FOR_INPUT(ring, D, PORTD2)
#define getring       get_ring

PIN_FOR_OUTPUT(door, C, PORTC0)
#define doorengage    door1
#define doordisengage door0

PIN_FOR_OUTPUT(led, C, PORTC2)
#define ledon  led1
#define ledoff led0

PIN_FOR_OUTPUT(strobo, C, PORTC1)
#define stroboon  strobo1
#define strobooff strobo0


/* initialize hardware */
void
init(void)
{
  led_init();
  ledon();

  strobo_init();
  strobooff();

  door_init();
  doordisengage();

  ring_init();

  /* timer initialization
   *
   * CTC mode
   * clocked from system clock
   * prescaler /1024
   * compare value 10
   *
   * this gives about 100 Hz at 1 MHz system clock
   *
   * we also enable the compare match interrupt
   * 
   */
  TCNT2 =  0;
  OCR2  = 10;
  TCCR2 = _BV(WGM21)|_BV(CS22)|_BV(CS21)|_BV(CS20);
  TIMSK = _BV(OCIE2);

  /* initialize usart */
  uinit(9600);

  /* enable interrupts */
  sei();
}

/* handle timer interrupt */

ISR(TIMER2_COMP_vect)
{
  tick();
}

/* door opening procedure */

void
opendoor(void)
{
  utx('O');
  doorengage();
  waitticks(OPENTIME);
  doordisengage();
}

/* event types */
enum {
  /* ring button pressed */
  EVENT_RING,
  /* opening sequence timeout reached */
  EVENT_TIMEOUT,
  /* strobe should be activated */
  EVENT_STROBE,
  /* command to open the door */
  EVENT_CMD_OPEN,
  /* command to lock the facility */
  EVENT_CMD_LOCK,
  /* command to unlock the facility*/
  EVENT_CMD_UNLOCK,
  /* command to disable strobo */
  EVENT_CMD_STROBO_DISABLE,
  /* command to enable strobo */
  EVENT_CMD_STROBO_ENABLE
};

/* time of first keypress in current sequence, else 0 */
tick_t firstpress;

/* time of last keypress */
tick_t lastpress;

/* times the ring button has been pressed */
uint8_t presses;

/* lockdown - if true, ring will be ignored */
bool_t lockdown;

/* strobo - if true, strobo will be used */
bool_t strobo;

/* wait for an event */
int
waitevent()
{
  while(1) {
    /* ring button pressed? */
    if(ring_get()) {
      waitticks(DEBOUNCE);
      if(ring_get()) {
	waitticks(DEBOUNCE);
	if(ring_get()) {
	  while(ring_get());
	  return EVENT_RING;
	}
      }
    }

    /* sequence timeout or pending strobe activation? */
    if(firstpress) {
      tick_t time = gettick();
      if((time - firstpress) >= TIMEOUT) {
    	return EVENT_TIMEOUT;
      }
      if((time - lastpress) >= STROBEDELAY) {
        return EVENT_STROBE;
      }
    }

    /* command input? */
    char input;
    if(urx(&input)) {
      switch(input) {
      case 'O':
	return EVENT_CMD_OPEN;
      case 'L':
	return EVENT_CMD_LOCK;
      case 'U':
	return EVENT_CMD_UNLOCK;
      case 'S':
	return EVENT_CMD_STROBO_ENABLE;
      case 's':
	return EVENT_CMD_STROBO_DISABLE;
      }
    }
  }
}

/* ... the usual ... */
int
main(void)
{
  presses    = 0;
  firstpress = 0;
  lastpress  = 0;
  lockdown   = LOCKED_BY_DEFAULT;
  strobo     = STROBE_BY_DEFAULT;

  /* initialize hardware */
  init();

  utx('I');

  /* main loop */
  while(1) {
    int event = waitevent();

    switch(event) {

    case EVENT_RING:
      utx('R');
      if(!lockdown) {
	/* if this is the first press, remember the time and enable signals */
	if(!presses) {
	  firstpress = gettick();
	}

        /* blink the led */
	ledoff();
	waitticks(BLINKTIME);
        ledon();

	/* if the number of presses reaches the limit, open the door */
	presses++;

        /* record time of last keypress */
        lastpress = gettick();

	/* if pressed often enough */
	if(presses >= PRESSES) {
          strobooff();
	  opendoor();
	  presses    = 0;
	  firstpress = 0;
          lastpress  = 0;
	}
      }
      break;

    case EVENT_TIMEOUT:
      utx('T');

      strobooff();

      /* reset the sequence detector */
      presses    = 0;
      firstpress = 0;
      lastpress  = 0;
      break;

    case EVENT_STROBE:
      if(strobo)
        stroboon();
      break;

    case EVENT_CMD_OPEN:
      opendoor();
      break;

    case EVENT_CMD_LOCK:
      utx('L');
      lockdown = TRUE;
      break;

    case EVENT_CMD_UNLOCK:
      utx('U');
      lockdown = FALSE;
      break;

    case EVENT_CMD_STROBO_ENABLE:
      utx('S');
      strobo = TRUE;
      break;

    case EVENT_CMD_STROBO_DISABLE:
      utx('s');
      strobooff();
      strobo = FALSE;
      break;
    }
  }
}

