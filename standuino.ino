/*
 * standuino - a stand-up meeting Arduino timer
 */
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include <IRremote.h>
#include <SoftPWM.h>

// IR Remote stuff
#define	REMOTE_LEFT		0xFF22DD
#define	REMOTE_UP		0xFF629D
#define	REMOTE_RIGHT		0xFFC23D
#define	REMOTE_DOWN		0xFFA857
#define	REMOTE_OK		0xFF02FD
/*
 * When the remote is not perfectly in front of the sensor, the decode_type will
 * be UNKNOWN (instead of the expected NEC) but the value will still be quite
 * consistent. the REMOTE_ALMOST_* are the "expected" (i.e. most likely) values
 * when the decode_type is UNKNOWN found by experimenting.
 */
#define	REMOTE_ALMOST_LEFT	0x52A3D41F
#define	REMOTE_ALMOST_UP	0x511DBB
#define	REMOTE_ALMOST_RIGHT	0x20FE4DBB
#define	REMOTE_ALMOST_DOWN	0xA3C8EDDB
#define	REMOTE_ALMOST_OK	0xD7E84B1B
IRrecv sensor(10);
decode_results remote;

// LEDs using SoftPWM
#define	LED_FADE_TIME		1000
#define	SPEAKER_COUNT		5
SOFTPWM_DEFINE_CHANNEL(0, DDRB, PORTB, PORTB1);  //Arduino pin 9
SOFTPWM_DEFINE_CHANNEL(1, DDRB, PORTB, PORTB0);  //Arduino pin 8
SOFTPWM_DEFINE_CHANNEL(2, DDRD, PORTD, PORTD7);  //Arduino pin 7
SOFTPWM_DEFINE_CHANNEL(3, DDRD, PORTD, PORTD6);  //Arduino pin 6
SOFTPWM_DEFINE_CHANNEL(4, DDRD, PORTD, PORTD5);  //Arduino pin 5
SOFTPWM_DEFINE_OBJECT(SPEAKER_COUNT);

// Display
Adafruit_7segment seven = Adafruit_7segment();

// Global state
int		current_talker = 0;
boolean		is_talking = false;
unsigned long	talking_since = 0;
unsigned long	talking_time[SPEAKER_COUNT];

// the current_talker stop talking.
void stop_talking()
{
	if (is_talking) {
		is_talking = false;
		talking_time[current_talker] += millis() - talking_since;
	}
}

// the current_talker (re)start talking.
void start_talking()
{
	if (!is_talking) {
		is_talking = true;
		talking_since = millis();
	}
}

// return the talker at current_talker's right
int next_talker()
{
	return ((current_talker + 1) % SPEAKER_COUNT);
}

// return the talker at current_talker's left
int prev_talker()
{
	return ((current_talker == 0 ? SPEAKER_COUNT : current_talker) - 1);
}

// return the talker having talking the least. If two (or more) talkers are
// tied, one of them is picked at random.
int next_random_talker()
{
	unsigned long least_talking_time = talking_time[0];
	int n = 1; // count of talker having talking the least.
	// find the minimum talking time, along with the talker count.
	for (int i = 1; i < SPEAKER_COUNT; i++) {
		if (talking_time[i] < least_talking_time) {
			least_talking_time = talking_time[i];
			n = 1;
		} else if (talking_time[i] == least_talking_time) {
			n++;
		}
	}
	int id = random(0, n); // pick someone
	// find our pick and return it.
	for (int i = 0; i < SPEAKER_COUNT; i++) {
		if (talking_time[i] == least_talking_time && id-- == 0)
			return (i);
	}
	/* NOTREACHED */
	return (-1);
}

// talker switch animation.
int talker_roulette(int from, int to)
{
	int distance = (to >= from ? 0 : SPEAKER_COUNT) + to - from;
	int n = 3 * SPEAKER_COUNT + distance;
	int rolling = current_talker;
	for (int i = 0; i < n; i++) {
		Palatis::SoftPWM.set(rolling, 0);
		rolling = (rolling + 1) % SPEAKER_COUNT;
		Palatis::SoftPWM.set(rolling, 255);
		delay(max(0, 80 - 4 * (n - i)));
	};
	return (to);
}

void setup()
{
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, HIGH);
	// we use pin 2 always high to provide 5v
	pinMode(2, OUTPUT);
	digitalWrite(2, HIGH);
	// IR sensor
	sensor.enableIRIn();
	// LEDs
	Palatis::SoftPWM.begin(60);
	for (int i = 0; i < SPEAKER_COUNT; i++)
		Palatis::SoftPWM.set(i, 255);
	// Display
	seven.begin(0x70);
	seven.writeDigitNum(0, 8, true);
	seven.writeDigitNum(1, 8, true);
	seven.drawColon(true);
	seven.writeDigitNum(3, 8, true);
	seven.writeDigitNum(4, 8, true);
	seven.writeDisplay();
	// Serial
	Serial.begin(9600);
	// print interrupt load for diagnostic purposes
	Palatis::SoftPWM.printInterruptLoad();
	// see  https://www.arduino.cc/en/Reference/Random
	randomSeed(analogRead(0));
	// Wait with everything lit for a bit.
	delay(600);
	digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
	// IR sensor
	if (sensor.decode(&remote)) {
		switch (remote.value) {
		case REMOTE_ALMOST_LEFT: // FALLTHROUGH
		case REMOTE_LEFT:
			stop_talking();
			current_talker = prev_talker();
			break;
		case REMOTE_ALMOST_RIGHT: // FALLTHROUGH
		case REMOTE_RIGHT:
			stop_talking();
			current_talker = next_talker();
			break;
		case REMOTE_ALMOST_OK: // FALLTHROUGH
		case REMOTE_OK:
			if (is_talking)
				stop_talking();
			else
				start_talking();
			break;
		case REMOTE_ALMOST_UP: // FALLTHROUGH
		case REMOTE_UP:
		case REMOTE_ALMOST_DOWN: // FALLTHROUGH
		case REMOTE_DOWN:
			stop_talking();
			current_talker = talker_roulette(current_talker, next_random_talker());
			break;
		default:
			/* ignored */;
		}
		if (remote.decode_type == NEC)
			Serial.print("NEC");
		else
			Serial.print(remote.decode_type);
		Serial.print(": ");
		Serial.println(remote.value, HEX);
		sensor.resume(); // Receive the next value
	}

	// LEDs state
	int led_value = 255; // used for the current talker_id LED
	if (is_talking) {
		// a round-trip (fade-out, fade-in) is twice the fading time.
		int ms = (millis() - talking_since) % (2 * LED_FADE_TIME);
		if (ms < LED_FADE_TIME) {
			// first half of the round-trip, fading out. Thus, we
			// map() to [255, 0]
			led_value = map(ms, 0, LED_FADE_TIME, 255, 0);
		} else {
			// second half of the round-trip, fading in. Thus, we
			// map() to [0, 255]
			led_value = map(ms, LED_FADE_TIME, 2 * LED_FADE_TIME, 0, 255);
		}
	}
	for (int i = 0; i < SPEAKER_COUNT; i++)
		Palatis::SoftPWM.set(i, (i == current_talker ? led_value : 0));

	// Display
	unsigned long total_talking_time = talking_time[current_talker];
	if (is_talking)
		total_talking_time += millis() - talking_since;
	// we want to show until 99s with 4 digits, thus we discard the millis.
	total_talking_time /= 10;
	if (total_talking_time > 9999) { // overflow the 4 digit display
		seven.println(10000); // this will show "-- --"
	} else {
		seven.writeDigitNum(0, (total_talking_time / 1000) % 10, true);
		seven.writeDigitNum(1, (total_talking_time /  100) % 10, true);
		seven.drawColon(true);
		seven.writeDigitNum(3, (total_talking_time /   10) % 10, true);
		seven.writeDigitNum(4, (total_talking_time       ) % 10, true);
	}
	seven.writeDisplay();
}
