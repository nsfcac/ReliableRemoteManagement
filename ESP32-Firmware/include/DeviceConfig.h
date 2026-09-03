#pragma once

/*
==========
Firmware Conf
==========
*/
#ifndef FIRMWARE_VERSION
    #define FIRMWARE_VERSION "0.0.0-local"
#endif

/*
==========
HEARTBEAT Conf
==========
*/
#ifndef HB_UART_NUM
    #define HB_UART_NUM 1
#endif

#ifndef HB_RX_PIN
    #define HB_RX_PIN 16
#endif

#ifndef HB_TX_PIN
    #define HB_TX_PIN 17
#endif

#ifndef HB_UART_BAUD
    #define HB_UART_BAUD 115200
#endif

#ifndef HB_SEND_MS
    #define HB_SEND_MS 500
#endif

#ifndef HB_TIMEOUT_MS
    #define HB_TIMEOUT_MS 2000
#endif

// Used in old RoleManager
#ifndef HB_TAKEOVER_HOLD_MS
    #define HB_TAKEOVER_HOLD_MS 5000
#endif

/*
==========
ONE WIRE BUS Conf
==========
*/

#ifndef ONE_WIRE_BUS_INTAKE
    #define ONE_WIRE_BUS_INTAKE 4
#endif

#ifndef ONE_WIRE_BUS_EXHAUST
    #define ONE_WIRE_BUS_EXHAUST 21
#endif

#ifndef TELEMETRY_SEND_MS
    #define TELEMETRY_SEND_MS 5000
#endif 



/*
==========
W5500 Conf
==========
*/
#ifndef W5500_CS_PIN
    #define W5500_CS_PIN 5
#endif

#ifndef W5500_SCK_PIN 
    #define W5500_SCK_PIN 18
#endif

#ifndef W5500_MISO_PIN 
    #define W5500_MISO_PIN 19
#endif

#ifndef W5500_MOSI_PIN
    #define W5500_MOSI_PIN 23
#endif

#ifndef W5500_RST_PIN
    #define W5500_RST_PIN 2
#endif

