The example provided here, is used with the following slaves attached to the master

```
$ ethercat slaves
0  0:0  PREOP  +  EK1100 EtherCAT-Koppler (2A E-Bus)
1  0:1  PREOP  +  EL4004 4Ch. Ana. Output 0-10V, 12bit
2  0:2  PREOP  +  EL6224 (IO Link Master)
3  0:3  PREOP  +  EL3174 4Ch. Ana. Input +/-10V Diff., +/-20mA SingleEnded, 16 Bi
4  0:4  PREOP  +  EL2004 4K. Dig. Ausgang 24V, 0.5A
5  0:5  PREOP  +  EL1004 4K. Dig. Eingang 24V, 3ms
```

The PDO configuration is like this

```
$ ethercat pdos
=== Master 0, Slave 1 ===
SM0: PhysAddr 0x1000, DefaultSize  128, ControlRegister 0x26, Enable 1
SM1: PhysAddr 0x1080, DefaultSize  128, ControlRegister 0x22, Enable 1
SM2: PhysAddr 0x1100, DefaultSize    8, ControlRegister 0x24, Enable 1
  RxPDO 0x1600 "AO RxPDO-Map Outputs Ch.1"
    PDO entry 0x7000:01, 16 bit, "Analog output"
  RxPDO 0x1601 "AO RxPDO-Map Outputs Ch.2"
    PDO entry 0x7010:01, 16 bit, "Analog output"
  RxPDO 0x1602 "AO RxPDO-Map Outputs Ch.3"
    PDO entry 0x7020:01, 16 bit, "Analog output"
  RxPDO 0x1603 "AO RxPDO-Map Outputs Ch.4"
    PDO entry 0x7030:01, 16 bit, "Analog output"
SM3: PhysAddr 0x1180, DefaultSize    0, ControlRegister 0x00, Enable 0
=== Master 0, Slave 2 ===
SM0: PhysAddr 0x1000, DefaultSize  256, ControlRegister 0x26, Enable 1
SM1: PhysAddr 0x1100, DefaultSize  256, ControlRegister 0x22, Enable 1
SM2: PhysAddr 0x1200, DefaultSize    0, ControlRegister 0x24, Enable 0
  RxPDO 0x1600 "IO RxPDO-Map Outputs Ch.1"
  RxPDO 0x1601 "IO RxPDO-Map Outputs Ch.2"
  RxPDO 0x1602 "IO RxPDO-Map Outputs Ch.3"
  RxPDO 0x1603 "IO RxPDO-Map Outputs Ch.4"
SM3: PhysAddr 0x1300, DefaultSize    6, ControlRegister 0x20, Enable 1
  TxPDO 0x1a00 "IO TxPDO-Map Inputs Ch.1"
    PDO entry 0x6000:01, 16 bit, ""
  TxPDO 0x1a01 ""
    PDO entry 0x6010:01, 16 bit, ""
  TxPDO 0x1a02 ""
  TxPDO 0x1a03 ""
  TxPDO 0x1a80 "DeviceState TxPDO-Map Inputs"
    PDO entry 0xf100:01,  8 bit, ""
    PDO entry 0xf100:02,  8 bit, ""
    PDO entry 0xf100:03,  8 bit, ""
    PDO entry 0xf100:04,  8 bit, ""
  TxPDO 0x1a81 "DeviceState TxPDO-Map Inputs"
    PDO entry 0x0000:00, 12 bit, ""
    PDO entry 0xf101:0d,  1 bit, ""
    PDO entry 0x0000:00,  2 bit, ""
    PDO entry 0xf101:10,  1 bit, ""
=== Master 0, Slave 3 ===
SM0: PhysAddr 0x1000, DefaultSize  128, ControlRegister 0x26, Enable 1
SM1: PhysAddr 0x1080, DefaultSize  128, ControlRegister 0x22, Enable 1
SM2: PhysAddr 0x1100, DefaultSize    0, ControlRegister 0x04, Enable 0
SM3: PhysAddr 0x1180, DefaultSize   16, ControlRegister 0x20, Enable 1
  TxPDO 0x1a00 "AI TxPDO-Map Standard Ch.1"
    PDO entry 0x6000:01,  1 bit, ""
    PDO entry 0x6000:02,  1 bit, ""
    PDO entry 0x6000:03,  2 bit, ""
    PDO entry 0x6000:05,  2 bit, ""
    PDO entry 0x6000:07,  1 bit, ""
    PDO entry 0x0000:00,  1 bit, ""
    PDO entry 0x0000:00,  5 bit, ""
    PDO entry 0x6000:0e,  1 bit, ""
    PDO entry 0x6000:0f,  1 bit, ""
    PDO entry 0x6000:10,  1 bit, ""
    PDO entry 0x6000:11, 16 bit, ""
  TxPDO 0x1a02 "AI TxPDO-Map Standard Ch.2"
    PDO entry 0x6010:01,  1 bit, ""
    PDO entry 0x6010:02,  1 bit, ""
    PDO entry 0x6010:03,  2 bit, ""
    PDO entry 0x6010:05,  2 bit, ""
    PDO entry 0x6010:07,  1 bit, ""
    PDO entry 0x0000:00,  1 bit, ""
    PDO entry 0x0000:00,  5 bit, ""
    PDO entry 0x6010:0e,  1 bit, ""
    PDO entry 0x6010:0f,  1 bit, ""
    PDO entry 0x6010:10,  1 bit, ""
    PDO entry 0x6010:11, 16 bit, ""
  TxPDO 0x1a04 "AI TxPDO-Map Standard Ch.3"
    PDO entry 0x6020:01,  1 bit, ""
    PDO entry 0x6020:02,  1 bit, ""
    PDO entry 0x6020:03,  2 bit, ""
    PDO entry 0x6020:05,  2 bit, ""
    PDO entry 0x6020:07,  1 bit, ""
    PDO entry 0x0000:00,  1 bit, ""
    PDO entry 0x0000:00,  5 bit, ""
    PDO entry 0x6020:0e,  1 bit, ""
    PDO entry 0x6020:0f,  1 bit, ""
    PDO entry 0x6020:10,  1 bit, ""
    PDO entry 0x6020:11, 16 bit, ""
  TxPDO 0x1a06 "AI TxPDO-Map Standard Ch.4"
    PDO entry 0x6030:01,  1 bit, ""
    PDO entry 0x6030:02,  1 bit, ""
    PDO entry 0x6030:03,  2 bit, ""
    PDO entry 0x6030:05,  2 bit, ""
    PDO entry 0x6030:07,  1 bit, ""
    PDO entry 0x0000:00,  1 bit, ""
    PDO entry 0x0000:00,  5 bit, ""
    PDO entry 0x6030:0e,  1 bit, ""
    PDO entry 0x6030:0f,  1 bit, ""
    PDO entry 0x6030:10,  1 bit, ""
    PDO entry 0x6030:11, 16 bit, ""
=== Master 0, Slave 4 ===
SM0: PhysAddr 0x0f00, DefaultSize    0, ControlRegister 0x44, Enable 9
  RxPDO 0x1600 "Channel 1"
    PDO entry 0x7000:01,  1 bit, "Output"
  RxPDO 0x1601 "Channel 2"
    PDO entry 0x7010:01,  1 bit, "Output"
  RxPDO 0x1602 "Channel 3"
    PDO entry 0x7020:01,  1 bit, "Output"
  RxPDO 0x1603 "Channel 4"
    PDO entry 0x7030:01,  1 bit, "Output"
=== Master 0, Slave 5 ===
SM0: PhysAddr 0x1000, DefaultSize    1, ControlRegister 0x00, Enable 1
  TxPDO 0x1a00 "Channel 1"
    PDO entry 0x6000:01,  1 bit, "Input"
  TxPDO 0x1a01 "Channel 2"
    PDO entry 0x6010:01,  1 bit, "Input"
  TxPDO 0x1a02 "Channel 3"
    PDO entry 0x6020:01,  1 bit, "Input"
  TxPDO 0x1a03 "Channel 4"
    PDO entry 0x6030:01,  1 bit, "Input"
```
