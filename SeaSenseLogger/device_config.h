#pragma once

// Describe your device and sensors in json format below.
// Make sure to update when you change sensors or recalibrate.
// A reference to this configuration is stored with every upload.
// This allows to link uploaded data to specific device and sensor configurations.
//
// Note:
// - generate a device_guid using https://www.uuidgenerator.net/
// - sensorheight and depth in meters
// - GPS accuracy in meters
// - Temperature accuracy in degrees Celcius
// - TDS, Turbidity, EC accuracy in %

// Your device configuration:
const char deviceJson[] PROGMEM = R"rawliteral(
{
    "device_guid": "f00c1844-42db-4309-847b-8fbe0b46bec1",
    "buoy_version": "v0.1",
    "owner": {
        "name": "Zoran Kovačević",
        "email": "zoran@kovacevic.nl",
        "phone": "+31648104284",
        "notes": "Deployed mostly in Amsterdam and Markermeer, Netherlands!"
    },
    "vessel": {
        "name": "SV Pusu",
        "length_waterline_m": 9.34,
        "draft_m": 1.65,
        "beam_m": 2.60,
        "displacement_kg": 3800,
        "type": "Sunwind 31",
        "call_sign": "PH7956",
        "vessel_type": "sailboat",
        "attachment_point": "port",
        "tow_line_length_m": 4
    },
    "sensors": [
      {
        "name": "Arduino TDS sensor",
        "type": "TDS",
        "unit": "ppm",
        "depth": 0.1,
        "data_column": "tds",
        "accuracy": "10",
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
        "depth": 0.1,
        "data_column": "turbidity",
        "accuracy": "10",
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
        "depth": 0.1,
        "data_column": "ec",
        "accuracy": "5",
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
        "depth": 0.1,
        "data_column": "water_temp",
        "accuracy": "0.5",
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
        "unit": "degrees",
        "height": 0.1,
        "data_column": "lat,lon,hdop",
        "accuracy": "2.5",
        "note": "https://aliexpress.com/item/1005008226016736.html"
      },
      {
        "name": "BME280 air temperature",
        "type": "AirTemperature",
        "unit": "°C",
        "data_column": "air_temperature",
        "note": "https://nl.aliexpress.com/item/1005006067716183.html",
        "voltage_range": "3.3-5.5V",
        "temperature_range_c": "-40 to 85",
        "temperature_accuracy_c": "+/-0.5°C (25°C)",
        "calibration": [
          {
            "date": "2024-05-10T12:00:00Z",
            "note": "Not calibrated, factory default."
          }
        ]
      },
      {
        "name": "BME280 air humidity",
        "type": "AirHumidity",
        "unit": "%RH",
        "data_column": "air_humidity",
        "note": "https://nl.aliexpress.com/item/1005006067716183.html",
        "voltage_range": "3.3-5.5V",
        "humidity_range_percent": "0-100",
        "humidity_accuracy_percent": "+/-3%RH (25°C)",
        "calibration": [
          {
            "date": "2024-05-10T12:00:00Z",
            "note": "Not calibrated, factory default."
          }
        ]
      },
      {
        "name": "BME280 air pressure",
        "type": "AirPressure",
        "unit": "hPa",
        "data_column": "air_pressure",
        "note": "https://nl.aliexpress.com/item/1005006067716183.html",
        "voltage_range": "3.3-5.5V",
        "pressure_range_hpa": "300-1100",
        "calibration": [
          {
            "date": "2024-05-10T12:00:00Z",
            "note": "Not calibrated, factory default."
          }
        ]
      }
    ]
}
)rawliteral";