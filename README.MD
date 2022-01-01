# Etherlab Node.js

Bind etherlab process and use it directly in node.

## Prerequisites

Etherlab must be installed first. To install etherlab, you can follow the instruction from [etherlab repository](https://gitlab.com/etherlab.org/ethercat/-/blob/master/INSTALL)

This package needs the etherlab shared library and header to be installed in **/usr/local/**.

If etherlab is installed in different location, i.e. **/opt/etherlab**, then you can create symlink to **/usr/local/**

```bash
ln -s /opt/etherlab/lib/libethercat.so /usr/local/lib/libethercat.so
ln -s /opt/etherlab/include/ecrt.h /usr/local/include/ecrt.h
```

## Slaves Configuration

[How to create slave json configuration](https://github.com/STECHOQ/etherlab-nodejs/wiki/create-slaves-json)

Each slave is configured via json file. For example, if a master has 1 **Beckhoff EL2008** slave attached, then the configuration would look like this

```json
[
   {
      "alias":0,
      "position":0,
      "vendor_id":"0x00000002",
      "product_code":"0x07d83052",
      "syncs":[
         {
            "index":2,
            "watchdog_enabled":false,
            "pdos":[
               {
                  "index":"0x1600",
                  "entries":[
                     {
                        "index":"0x7000",
                        "subindex":"0x01",
                        "size":1,
                        "add_to_domain":true,
                        "swap_endian":false,
                        "signed":false
                     }
                  ]
               },
               {
                  "index":"0x1601",
                  "entries":[
                     {
                        "index":"0x7010",
                        "subindex":"0x01",
                        "size":1,
                        "add_to_domain":true,
                        "swap_endian":false,
                        "signed":false
                     }
                  ]
               },
               {
                  "index":"0x1602",
                  "entries":[
                     {
                        "index":"0x7020",
                        "subindex":"0x01",
                        "size":1,
                        "add_to_domain":true,
                        "swap_endian":false,
                        "signed":false
                     }
                  ]
               },
               {
                  "index":"0x1603",
                  "entries":[
                     {
                        "index":"0x7030",
                        "subindex":"0x01",
                        "size":1,
                        "add_to_domain":true,
                        "swap_endian":false,
                        "signed":false
                     }
                  ]
               },
               {
                  "index":"0x1604",
                  "entries":[
                     {
                        "index":"0x7040",
                        "subindex":"0x01",
                        "size":1,
                        "add_to_domain":true,
                        "swap_endian":false,
                        "signed":false
                     }
                  ]
               },
               {
                  "index":"0x1605",
                  "entries":[
                     {
                        "index":"0x7050",
                        "subindex":"0x01",
                        "size":1,
                        "add_to_domain":true,
                        "swap_endian":false,
                        "signed":false
                     }
                  ]
               },
               {
                  "index":"0x1606",
                  "entries":[
                     {
                        "index":"0x7060",
                        "subindex":"0x01",
                        "size":1,
                        "add_to_domain":true,
                        "swap_endian":false,
                        "signed":false
                     }
                  ]
               },
               {
                  "index":"0x1607",
                  "entries":[
                     {
                        "index":"0x7070",
                        "subindex":"0x01",
                        "size":1,
                        "add_to_domain":true,
                        "swap_endian":false,
                        "signed":false
                     }
                  ]
               }
            ]
         }
      ]
   }
]
```

## Example
```javascript
const __etherlab = require('etherlab-nodejs');

const config = './slaves.json';
const frequency = 5000; // in hertz

const etherlab = new __etherlab(config, frequency);

etherlab.on('data', (data, latency) => {
	console.log(data, latency);
	console.log(etherlab.getLatencyAndJitter());
});

etherlab.on('state', current => {
	console.log('master\'s current state ', current);
});

etherlab.on('ready', async () => {
	const domain = await etherlab.getDomain();
	console.log(domain);
});

etherlab.on('error', error => {
	console.error(error);
});

etherlab.start();
```