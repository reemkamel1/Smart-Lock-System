
#define ATMEGA_RX 16
#define ATMEGA_TX 17

#define PC_BAUD 115200
#define ATMEGA_BAUD 9600

void setup()
{
    // USB communication with Python
    Serial.begin(PC_BAUD);

    // UART2 communication with ATmega32
    Serial2.begin(
        ATMEGA_BAUD,
        SERIAL_8N1,
        ATMEGA_RX,
        ATMEGA_TX
    );

    Serial.println("ESP32 READY");
}

void loop()
{
    // Check for request from ATmega32
    if (Serial2.available())
    {
        char request = Serial2.read();

        if (request == 'V')
        {
            Serial.println("V");

            Serial.println("Verification request forwarded to PC");

            char result = '0';
            bool received = false;

            unsigned long startTime = millis();

            // Wait up to 55 seconds for Python
            while (millis() - startTime < 55000)
            {
                if (Serial.available())
                {
                    char response = Serial.read();

                    if (response == '1' || response == '0')
                    {
                        result = response;
                        received = true;
                        break;
                    }
                }
            }

            // Fail-safe: no response means denial
            if (!received)
            {
                result = '0';
                Serial.println("Python response timeout");
            }

            // Send result to ATmega32
            Serial2.write(result);

            Serial.print("Result sent to ATmega32: ");
            Serial.println(result);
        }
    }
}
