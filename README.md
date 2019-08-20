# mm2xtgeoip
Converts the MaxMind geoip CSV database to the format used by IPTables/XTgeoip

# Compiling and installing
Download, extract, run `make` to compile and finally `make install` as root. Add `mm2xtgeoip_dl` to cron to update the databases periodically. `mm2xtgeoip_dl` is a shell script that requires `wget` and `unzip`.

# Usage
Run `mm2xtgeoip --help` to see all available options. 
