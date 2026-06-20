# vehicle

Vehicle-domain state and its platform-independent CAN mapping.

## Prototype CAN frame

`VehicleCanCodec` currently uses standard frame `0x123` for the WSL/Jetson prototype:

| Byte | Field | Encoding |
| --- | --- | --- |
| 0-1 | speed | unsigned little-endian, 0.1 km/h per bit |
| 2 | gear | unsigned prototype gear value |
| 3 | SOC | integer percent, clamped to 0-100 |
| 4 bit 0 | cloud enabled | boolean |

This is a local test contract, not a production vehicle protocol. Replace the codec from an
approved DBC or signal specification when real chassis integration begins. SocketCAN transport and
service lifecycle do not depend on this byte layout.
