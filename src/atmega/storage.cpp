#include "storage.h"

static File    _file;
static bool    _ok        = false;
static uint8_t _rowCount  = 0;

bool Storage::init() {
  if (!SD.begin(SD_CS_PIN)) { _ok = false; return false; }
  _file = SD.open(F("log.csv"), FILE_WRITE);
  if (!_file) { _ok = false; return false; }
  if (_file.size() == 0) {
    _file.println(F("time,tempC,hum,water,mq2,flame,vib,panic,flags,battV,battSt,gps"));
    _file.flush();
  }
  _ok = true;
  return true;
}

bool Storage::available() { return _ok; }

bool Storage::logRow(const char *ts, const SensorData &d,
                     uint8_t flags, float battV, const char *battSt,
                     const char *gps) {
  if (!_ok || !_file) return false;

  // Build row with minimal String usage to save heap
  _file.print(ts);          _file.print(',');
  if (!isnan(d.tempC))    { _file.print(d.tempC,    1); } else { _file.print(F("ERR")); }
  _file.print(',');
  if (!isnan(d.humidity)) { _file.print(d.humidity, 1); } else { _file.print(F("ERR")); }
  _file.print(',');
  _file.print(d.water);     _file.print(',');
  _file.print(d.mq2);       _file.print(',');
  _file.print(d.flame);     _file.print(',');
  _file.print(d.vib);       _file.print(',');
  _file.print(d.panic ? 1 : 0); _file.print(',');
  _file.print(flags);       _file.print(',');
  _file.print(battV, 2);    _file.print(',');
  _file.print(battSt);      _file.print(',');
  _file.println(gps);

  if (++_rowCount >= SD_FLUSH_EVERY) { _file.flush(); _rowCount = 0; }
  return true;
}

void Storage::flush() {
  if (_ok && _file) { _file.flush(); _rowCount = 0; }
}
