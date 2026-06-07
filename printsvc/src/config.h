/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_CONFIG_H
#define DENEB_PRINTSVC_CONFIG_H

#define DENEB_PRINTSVC_STATUS_ENDPOINT "tcp://127.0.0.1:5555"
#define DENEB_PRINTSVC_COMMAND_ENDPOINT "tcp://127.0.0.1:5556"
#define DENEB_PRINTSVC_STATUS_TOPIC "10001"
#define DENEB_PRINTSVC_SERIAL_DEVICE "/dev/ttyS1"
#define DENEB_PRINTSVC_SERIAL_BAUD 250000
#define DENEB_PRINTSVC_MACRO_DIR "/home/cygnus/marlindriver/gcode"
#define DENEB_PRINTSVC_MAX_GCODE_LINE 256
#define DENEB_PRINTSVC_MAX_COMMANDS 16
#define DENEB_PRINTSVC_SERIAL_LINE 256
#define DENEB_PRINTSVC_STREAM_WINDOW 4
#define DENEB_MOTION_FW_HEX "/home/atmel_programmer/cygnus-marlin.hex"
#define DENEB_MOTION_FW_CACHE "/etc/deneb/motion-controller-firmware.sha256"
#define DENEB_MOTION_FW_PROGRAMMER "/home/atmel_programmer/prog.sh"
#define DENEB_PRINTSVC_DIAGNOSTIC_LOG "/var/log/ultimaker/deneb-printsvc.log"
#define DENEB_PRINTSVC_DIAGNOSTIC_LOG_FALLBACK "/tmp/deneb-printsvc.log"

#endif
