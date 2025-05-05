#pragma once

// Describe your device and sensors in json format below.
// Make sure to update when you change sensors or recalibrate.
// A reference to this configuration is stored with every upload.
// This allows to link uploaded data to specific device and sensor configurations.
//
// Example:
// {
//     "device_guid": "", // Generate one at https://www.guidgenerator.com/ and paste here
//     "buoy_version": "v0.1", // Version of the Project SeaSense buoy that you are using. If you created a custom version, come up with a unique name and version.
//     "owner_name": "Zoran Kovačević", // Your name
//     "owner_email": "zoran@kovacevic.com", // Your email
//     "owner_phone": "+31648104284", // Your international phone number
//     "notes": "Deployed mostly in Amsterdam and Markermeer, Netherlands!", // Any notes, where do you typically sail? What boat? Anything you want to share.
//     "sensors": [
//       {
//         "name": "Arduino TDS sensor",
//         "type": "TDS",
//         "unit": "ppm",
//         "data_column": "tds",
//         "note": "https://aliexpress.com/item/1005006291597020.html",
//         "calibration": [
//             {
//                 "date": "2024-05-10T12:00:00Z",
//                 "value": 1413,
//                 "measured_voltage": 1.8216,
//                 "note": "Calibrated with 1413 µS/cm solution"
//               },
//               {
//                 "date": "2024-05-10T12:00:00Z",
//                 "value": 25,
//                 "measured_voltage": 0.0023,
//                 "note": "Calibrated with demineralized water"
//               }
//         ]
//       },
//       {
//         "name": "Turbidity sensor",
//         "type": "Turbidity",
//         "unit": "%",
//         "data_column": "turbidity",
//         "note": "https://aliexpress.com/item/1005005911851361.html",
//         "calibration": [
//           {
//             "date": "2024-05-10T12:00:00Z",
//             "value": 0,
//             "measured_voltage": 1.12,
//             "note": "Calibrated 0% with tap water"
//           },
//           {
//             "date": "2024-05-10T12:00:00Z",
//             "value": 100,
//             "measured_voltage": 2.11,
//             "note": "Calibrated 100% with coffee"
//           }
//         ]
//       },
//       {
//         "name": "EC 0-44000µS/cm sensor",
//         "type": "EC",
//         "unit": "µS/cm",
//         "data_column": "ec",
//         "note": "https://aliexpress.com/item/32965992320.html",
//         "calibration": [
//           {
//             "date": "2024-05-10T12:00:00Z",
//             "value": 1413,
//             "measured_voltage": 0.1696,
//             "note": "Calibrated with 1413 µS/cm solution"
//           },
//           {
//             "date": "2024-05-10T12:00:00Z",
//             "value": 25,
//             "measured_voltage": 0.0452,
//             "note": "Calibrated with demineralized water"
//           }
//         ]
//       },
//       {
//         "name": "DS18B20 temperature sensor",
//         "type": "Temperature",
//         "unit": "°C",
//         "data_column": "watertemp",
//         "note": "https://aliexpress.com/item/1005001601986600.html",
//         "calibration": [
//           {
//             "date": "2024-05-10T12:00:00Z",
//             "note": "Factory calibration"
//           }
//         ]
//       },
//       {
//         "name": "NEO-8M GPS",
//         "type": "GPS",
//         "unit": "",
//         "data_column": "lat/lon/hdop",
//         "note": "https://aliexpress.com/item/1005008226016736.html"
//       }
//     ]
//   }

// Your device configuration:
const char deviceJson[] PROGMEM = R"rawliteral(
{
    "device_guid": "",
    "buoy_version": "",
    "owner_name": "",
    "owner_email": "",
    "owner_phone": "",
    "notes": "",
    "sensors": [
      {
        "name": "Arduino TDS sensor",
        "type": "TDS",
        "unit": "ppm",
        "data_column": "tds",
        "note": "https://aliexpress.com/item/1005006291597020.html",
        "calibration": [
            {
                "date": "2024-05-10T12:00:00Z",
                "value": 1413,
                "measured_voltage": 1.8216,
                "note": "Calibrated with 1413 µS/cm solution"
              },
              {
                "date": "2024-05-10T12:00:00Z",
                "value": 25,
                "measured_voltage": 0.0023,
                "note": "Calibrated with demineralized water"
              }
        ]
      },
      {
        "name": "Turbidity sensor",
        "type": "Turbidity",
        "unit": "%",
        "data_column": "turbidity",
        "note": "https://aliexpress.com/item/1005005911851361.html",
        "calibration": [
          {
            "date": "2024-05-10T12:00:00Z",
            "value": 0,
            "measured_voltage": 1.12,
            "note": "Calibrated 0% with tap water"
          },
          {
            "date": "2024-05-10T12:00:00Z",
            "value": 100,
            "measured_voltage": 2.11,
            "note": "Calibrated 100% with coffee"
          }
        ]
      },
      {
        "name": "EC 0-44000µS/cm sensor",
        "type": "EC",
        "unit": "µS/cm",
        "data_column": "ec",
        "note": "https://aliexpress.com/item/32965992320.html",
        "calibration": [
          {
            "date": "2024-05-10T12:00:00Z",
            "value": 1413,
            "measured_voltage": 0.1696,
            "note": "Calibrated with 1413 µS/cm solution"
          },
          {
            "date": "2024-05-10T12:00:00Z",
            "value": 25,
            "measured_voltage": 0.0452,
            "note": "Calibrated with demineralized water"
          }
        ]
      },
      {
        "name": "DS18B20 temperature sensor",
        "type": "Temperature",
        "unit": "°C",
        "data_column": "watertemp",
        "note": "https://aliexpress.com/item/1005001601986600.html",
        "calibration": [
          {
            "date": "2024-05-10T12:00:00Z",
            "note": "Factory calibration"
          }
        ]
      },
      {
        "name": "NEO-8M GPS",
        "type": "GPS",
        "unit": "",
        "data_column": "lat/lon/hdop",
        "note": "https://aliexpress.com/item/1005008226016736.html"
      }
    ]
  }
)rawliteral";