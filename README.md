# Tender Oni

ESP-32 Based Greenhouse Monitor

Starts a task which...
1. Connectes to wifi
2. Collects ambient sensor data from I2C
3. Enables an SSR via GPIO to power an analog soil pressure sensor/tensiometer
4. Grabs the soil moisture reading from the ADC
5. Sends all the sensor data to an IFTTT webhook, from which the data is sent to Google Sheets
6. Kicks into ultra low-power for a configured time or until restarted