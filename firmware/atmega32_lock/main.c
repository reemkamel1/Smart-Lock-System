

#define F_CPU 14745600UL

#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdint.h>
#include <avr/eeprom.h>

// ================= LCD I2C SETTINGS =================

#define LCD_ADDR       0x27

#define LCD_RS         0x01
#define LCD_EN         0x04
#define LCD_BACKLIGHT  0x08

// ================= OUTPUT PINS =================

#define GREEN_LED   PD2
#define RED_LED     PC7
#define BUZZER      PC6

// ================= UART SETTINGS =================

#define UART_BAUD 9600

// ATmega32:
// PD0 = RXD (Input)
// PD1 = TXD (Output)

// ================= GLOBAL VARIABLES =================

uint8_t wrong_attempts = 0;
uint8_t voice_wrong_attempts = 0;

// ================= EEPROM PASSWORD =================

uint8_t EEMEM ee_password[4];

void Password_Init(char *password)
{
	eeprom_read_block(password, ee_password, 4);

	uint8_t valid = 1;

	for (uint8_t i = 0; i < 4; i++)
	{
		if (password[i] < '0' || password[i] > '9')
		{
			valid = 0;
			break;
		}
	}

	// Default password = 1234

	if (!valid)
	{
		password[0] = '1';
		password[1] = '2';
		password[2] = '3';
		password[3] = '4';

		eeprom_update_block(password, ee_password, 4);
	}

	password[4] = '\0';
}

// Save new password permanently

void Password_Save(char *password)
{
	eeprom_update_block(password, ee_password, 4);
}

// ================= I2C FUNCTIONS =================

void I2C_Init(void)
{
	TWSR = 0x00;
	TWBR = 66;
	TWCR = (1 << TWEN);
}

void I2C_Start(void)
{
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);

	while (!(TWCR & (1 << TWINT)));
}

void I2C_Stop(void)
{
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

void I2C_Write(uint8_t data)
{
	TWDR = data;

	TWCR = (1 << TWINT) | (1 << TWEN);

	while (!(TWCR & (1 << TWINT)));
}

// ================= LCD FUNCTIONS =================

void LCD_Expander(uint8_t data)
{
	I2C_Start();

	I2C_Write(LCD_ADDR << 1);
	I2C_Write(data | LCD_BACKLIGHT);

	I2C_Stop();
}

void LCD_Enable(uint8_t data)
{
	LCD_Expander(data | LCD_EN);

	_delay_us(1);

	LCD_Expander(data & ~LCD_EN);

	_delay_us(50);
}

void LCD_Send4Bits(uint8_t data)
{
	LCD_Expander(data);
	LCD_Enable(data);
}

void LCD_Send(uint8_t data, uint8_t mode)
{
	LCD_Send4Bits((data & 0xF0) | mode);

	LCD_Send4Bits(((data << 4) & 0xF0) | mode);
}

void LCD_Command(uint8_t cmd)
{
	LCD_Send(cmd, 0);

	if (cmd == 0x01 || cmd == 0x02)
	_delay_ms(2);
}

void LCD_Char(uint8_t data)
{
	LCD_Send(data, LCD_RS);
}

void LCD_Init(void)
{
	_delay_ms(50);

	LCD_Send4Bits(0x30);
	_delay_ms(5);

	LCD_Send4Bits(0x30);
	_delay_us(150);

	LCD_Send4Bits(0x30);
	LCD_Send4Bits(0x20);

	LCD_Command(0x28);
	LCD_Command(0x0C);
	LCD_Command(0x06);
	LCD_Command(0x01);
}

void LCD_Clear(void)
{
	LCD_Command(0x01);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
	if (row == 0)
	LCD_Command(0x80 + col);
	else
	LCD_Command(0xC0 + col);
}

void LCD_String(const char *str)
{
	while (*str)
	LCD_Char(*str++);
}

// ================= OUTPUT FUNCTIONS =================

void Output_Init(void)
{
	// Green LED on PD2

	DDRD |= (1 << GREEN_LED);

	// Red LED and buzzer

	DDRC |= (1 << RED_LED) | (1 << BUZZER);

	PORTD &= ~(1 << GREEN_LED);

	PORTC &= ~((1 << RED_LED) | (1 << BUZZER));
}

void Green_LED_ON(void)
{
	PORTD |= (1 << GREEN_LED);
}

void Green_LED_OFF(void)
{
	PORTD &= ~(1 << GREEN_LED);
}

void Red_LED_ON(void)
{
	PORTC |= (1 << RED_LED);
}

void Red_LED_OFF(void)
{
	PORTC &= ~(1 << RED_LED);
}

void Buzzer_ON(void)
{
	PORTC |= (1 << BUZZER);
}

void Buzzer_OFF(void)
{
	PORTC &= ~(1 << BUZZER);
}

// ================= UART COMMUNICATION =================

void UART_Init(void)
{
	DDRD &= ~(1 << PD0);
	DDRD |= (1 << PD1);

	// 9600 baud at F_CPU = 14.7456 MHz

	UBRRH = 0;
	UBRRL = 95;

	UCSRA = 0;

	UCSRB = (1 << RXEN) | (1 << TXEN);

	UCSRC = (1 << URSEL) |
	(1 << UCSZ1) |
	(1 << UCSZ0);
}

void UART_SendChar(char data)
{
	while (!(UCSRA & (1 << UDRE)));

	UDR = data;
}

uint8_t UART_Available(void)
{
	return (UCSRA & (1 << RXC));
}

char UART_Read(void)
{
	while (!(UCSRA & (1 << RXC)));

	return UDR;
}

// Remove old received data before a new verification request

void UART_FlushRx(void)
{
	while (UART_Available())
	{
		volatile char dummy = UDR;
		(void)dummy;
	}
}

// ================= VOICE VERIFICATION =================

// ATmega32 sends 'V' to ESP32.
//
// ESP32 asks Python to start voice verification.
//
// ESP32 sends:
// '1' = Voice verification successful
// '0' = Voice verification failed
//
// Timeout = 60 seconds.

uint8_t Voice_Verification(void)
{
	uint16_t elapsed = 0;

	uint8_t remaining;

	UART_FlushRx();

	LCD_Clear();

	LCD_SetCursor(0, 0);
	LCD_String("VOICE VERIFY");

	LCD_SetCursor(1, 0);
	LCD_String("Waiting from PC");

	// Request voice verification from ESP32

	UART_SendChar('V');

	// Wait up to 60 seconds

	while (elapsed < 6000)
	{
		if (UART_Available())
		{
			char received = UART_Read();

			if (received == '1')
			{
				return 1;
			}
			else if (received == '0')
			{
				return 0;
			}
		}

		// Update countdown every second

		if (elapsed % 100 == 0)
		{
			remaining = 60 - (elapsed / 100);

			LCD_SetCursor(1, 0);
			LCD_String("Wait: ");

			LCD_Char('0' + (remaining / 10));
			LCD_Char('0' + (remaining % 10));

			LCD_String(" sec       ");
		}

		_delay_ms(10);

		elapsed++;
	}

	// Timeout = verification failed

	return 0;
}

// ================= VOICE FAILURE HANDLER =================

void Voice_Failure_Handler(void)
{
	voice_wrong_attempts++;

	Green_LED_OFF();
	Red_LED_ON();

	LCD_Clear();

	LCD_SetCursor(0, 0);
	LCD_String("VOICE CHECK FAIL");

	LCD_SetCursor(1, 0);
	LCD_String("TRY AGAIN");

	// Changed from 2 seconds to 5 seconds

	_delay_ms(5000);

	Red_LED_OFF();

	// Three failed voice verification sessions

	if (voice_wrong_attempts >= 3)
	{
		LCD_Clear();

		LCD_SetCursor(0, 0);
		LCD_String("VOICE LOCKOUT");

		LCD_SetCursor(1, 0);
		LCD_String("ALARM!");

		Buzzer_ON();

		_delay_ms(2000);

		Buzzer_OFF();

		voice_wrong_attempts = 0;

		_delay_ms(1000);
	}
}

// ================= KEYPAD FUNCTIONS =================

// PA0-PA3 = Rows
// PA4-PA7 = Columns

char keypad_map[4][4] =
{
	{'1','2','3','A'},
	{'4','5','6','B'},
	{'7','8','9','C'},
	{'*','0','#','D'}
};

void Keypad_Init(void)
{
	DDRA = 0x0F;

	PORTA = 0xFF;
}

char Keypad_GetKey(void)
{
	for (uint8_t row = 0; row < 4; row++)
	{
		PORTA = 0xFF;

		PORTA &= ~(1 << row);

		_delay_us(10);

		for (uint8_t col = 0; col < 4; col++)
		{
			if (!(PINA & (1 << (col + 4))))
			{
				_delay_ms(25);

				if (!(PINA & (1 << (col + 4))))
				{
					while (!(PINA & (1 << (col + 4))))
					{
					}

					_delay_ms(20);

					return keypad_map[row][col];
				}
			}
		}
	}

	return 0;
}

// ================= CHANGE PASSWORD =================

// Read 4 digits from keypad
// Press * to clear current input

void Read_New_Password(char *buffer, uint8_t line)
{
	uint8_t index = 0;

	LCD_SetCursor(line, 0);
	LCD_String("                ");

	LCD_SetCursor(line, 0);

	while (index < 4)
	{
		char key = Keypad_GetKey();

		if (key >= '0' && key <= '9')
		{
			buffer[index] = key;

			LCD_Char('*');

			index++;
		}
		else if (key == '*')
		{
			index = 0;

			LCD_SetCursor(line, 0);
			LCD_String("                ");

			LCD_SetCursor(line, 0);
		}
	}

	buffer[4] = '\0';
}

// Change and confirm new password

void Change_Password(char *password)
{
	char new_password[5];

	char confirm_password[5];

	while (1)
	{
		// Enter new password

		LCD_Clear();

		LCD_SetCursor(0, 0);
		LCD_String("Enter New Pass:");

		Read_New_Password(new_password, 1);

		// Confirm new password

		LCD_Clear();

		LCD_SetCursor(0, 0);
		LCD_String("Confirm Pass:");

		Read_New_Password(confirm_password, 1);

		// Check if both passwords match

		if (strcmp(new_password, confirm_password) == 0)
		{
			// Save password in EEPROM

			Password_Save(new_password);

			// Update RAM password

			strcpy(password, new_password);

			LCD_Clear();

			LCD_SetCursor(0, 0);
			LCD_String("PASSWORD SAVED");

			LCD_SetCursor(1, 0);
			LCD_String("Successfully!");

			_delay_ms(2000);

			return;
		}
		else
		{
			LCD_Clear();

			LCD_SetCursor(0, 0);
			LCD_String("Not Matching!");

			LCD_SetCursor(1, 0);
			LCD_String("Try Again");

			_delay_ms(2000);
		}
	}
}

// ================= MAIN PROGRAM =================

int main(void)
{
	char password[5];

	char entered[5];

	uint8_t index = 0;

	// Initialize hardware

	Keypad_Init();

	Output_Init();

	UART_Init();

	I2C_Init();

	LCD_Init();

	// Load password from EEPROM

	Password_Init(password);

	// Welcome screen

	LCD_SetCursor(0, 0);
	LCD_String("Digital Lock");

	LCD_SetCursor(1, 0);
	LCD_String("Initializing");

	_delay_ms(1500);

	LCD_Clear();

	LCD_SetCursor(0, 0);
	LCD_String("Enter Password:");

	LCD_SetCursor(1, 0);

	// ================= MAIN LOOP =================

	while (1)
	{
		char key = Keypad_GetKey();

		if (key != 0)
		{
			// ENTER DIGITS

			if (key >= '0' && key <= '9')
			{
				if (index < 4)
				{
					entered[index] = key;

					LCD_Char('*');

					index++;
				}
			}

			// CLEAR PASSWORD

			else if (key == '*')
			{
				index = 0;

				Green_LED_OFF();
				Red_LED_OFF();
				Buzzer_OFF();

				LCD_Clear();

				LCD_SetCursor(0, 0);
				LCD_String("Enter Password:");

				LCD_SetCursor(1, 0);
			}

			// CONFIRM PASSWORD

			else if (key == '#')
			{
				entered[index] = '\0';

				LCD_Clear();

				// ================= CORRECT PASSWORD =================

				if (index == 4 &&
				strcmp(entered, password) == 0)
				{
					wrong_attempts = 0;

					Green_LED_OFF();
					Red_LED_OFF();
					Buzzer_OFF();

					LCD_SetCursor(0, 0);
					LCD_String("PASSWORD CORRECT");

					LCD_SetCursor(1, 0);
					LCD_String("Verify Voice");

					_delay_ms(1000);

					// VOICE VERIFICATION THROUGH ESP32

					if (Voice_Verification())
					{
						// Voice verification successful

						voice_wrong_attempts = 0;

						LCD_Clear();

						LCD_SetCursor(0, 0);
						LCD_String("ACCESS GRANTED");

						LCD_SetCursor(1, 0);
						LCD_String("DOOR OPENED");

						Green_LED_ON();

						Red_LED_OFF();
						Buzzer_OFF();

						// Changed from 2 seconds to 5 seconds

						_delay_ms(5000);

						Green_LED_OFF();
					}
					else
					{
						// Voice verification failed

						Voice_Failure_Handler();
					}
				}

				// ================= WRONG PASSWORD =================

				else
				{
					wrong_attempts++;

					Green_LED_OFF();

					Red_LED_ON();

					LCD_SetCursor(0, 0);
					LCD_String("Wrong Password");

					LCD_SetCursor(1, 0);

					// THREE WRONG PIN ATTEMPTS

					if (wrong_attempts >= 3)
					{
						LCD_String("ALARM!");

						Buzzer_ON();

						_delay_ms(2000);

						Buzzer_OFF();

						wrong_attempts = 0;

						// ================= IDENTITY VERIFICATION =================

						LCD_Clear();

						LCD_SetCursor(0, 0);
						LCD_String("VERIFY IDENTITY");

						LCD_SetCursor(1, 0);
						LCD_String("Voice Required");

						_delay_ms(1500);

						// Verify owner's voice before changing password

						if (Voice_Verification())
						{
							// Identity confirmed

							voice_wrong_attempts = 0;

							LCD_Clear();

							LCD_SetCursor(0, 0);
							LCD_String("IDENTITY OK");

							LCD_SetCursor(1, 0);
							LCD_String("CHANGE PASSWORD");

							_delay_ms(1500);

							Change_Password(password);
						}
						else
						{
							// Identity rejected
							// Password change is not allowed

							Voice_Failure_Handler();
						}

						Green_LED_OFF();
						Red_LED_OFF();
						Buzzer_OFF();
					}
					else
					{
						LCD_String("Try Again");

						_delay_ms(2000);
					}

					Red_LED_OFF();
				}

				// ================= RESET INPUT =================

				index = 0;

				LCD_Clear();

				LCD_SetCursor(0, 0);
				LCD_String("Enter Password:");

				LCD_SetCursor(1, 0);
			}
		}
	}
}
