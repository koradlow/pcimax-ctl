/*
 * Copyright 2012 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 * Author: Konke Radlow <koradlow@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335  USA
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <string.h>	/* String function definitions */
#include <unistd.h>	/* UNIX standard function definitions */
#include <fcntl.h>	/* File control definitions */
#include <errno.h>	/* Error number definitions */
#include <termios.h>	/* POSIX terminal control definitions */
#include <ctype.h>	/* Character classification routines */
#include <getopt.h>
#include <time.h>
#include <libudev.h>

#include "include/inih/ini.h"	/* ini file parsing lib */

/* define bits for bitmask that defines the settings that the user wants
 * to update */
#define PCIMAX_FM	0x01	/* FM related setting change requested */
#define PCIMAX_RDS	0x02	/* RDS related setting change requested */
#define PCIMAX_FREQ	0x10
#define PCIMAX_PWR	0x20
#define PCIMAX_STEREO	0x40
#define PCIMAX_AF	0x80
#define PCIMAX_RT	0x100
#define PCIMAX_PI	0x200
#define PCIMAX_PTY	0x400
#define PCIMAX_PTYT	0x800
#define PCIMAX_TP	0x1000
#define PCIMAX_TA	0x2000
#define PCIMAX_MS	0x4000
#define PCIMAX_PS	0x8000
#define PCIMAX_ECC	0x10000
#define PCIMAX_DI	0x20000

static struct termios old_settings;

/* short options */
enum Options{
	OptSetDevice = 'd',
	OptSetFreq = 'f',
	OptHelp = 'h',
	OptMonitor = 'm',
	OptFile = 64,
	OptSetAF,
	OptSetECC,
	OptSetMS,
	OptSetPower,
	OptSetPI,
	OptSetPS,
	OptSetPTY,
	OptSetRT,
	OptSetStereo,
	OptSetTA,
	OptSetTP,
	OptLast = 128
};

/* long options */
static struct option long_options[] = {
	{"device", required_argument, 0, OptSetDevice},
	{"file", required_argument, 0, OptFile},
	{"help", no_argument, 0, OptHelp},
	{"monitor", no_argument, 0, OptMonitor},
	{"set-af", required_argument, 0, OptSetAF},
	{"set-ecc", required_argument, 0, OptSetECC},
	{"set-freq", required_argument, 0, OptSetFreq},
	{"set-ms", required_argument, 0, OptSetMS},
	{"set-power", required_argument, 0, OptSetPower},
	{"set-pi", required_argument, 0 , OptSetPI},
	{"set-ps", required_argument, 0, OptSetPS},
	{"set-pty", required_argument, 0, OptSetPTY},
	{"set-rt", required_argument, 0, OptSetRT},
	{"set-stereo", required_argument, 0, OptSetStereo},
	{"set-ta", required_argument, 0 , OptSetTA},
	{"set-tp", required_argument, 0, OptSetTP},
	{0, 0, 0, 0}
};

/* struct containing all available settings for the device */
struct pcimax_settings {
	/** General settings **/
	uint32_t defined;	/* bitmask denoting all defined settings */
	char options[OptLast];	/* array with 1 field (true/false) for each option */
	char device[80];	/* path of the virtual com port of pcimax3000+ */
	char file[80];		/* path of the config file */
	bool monitor;		/* monitor config file for changes */
	
	/** FM-Transmitter settings **/
	uint32_t freq;	/* range 87500..108000 */
	uint8_t power; 	/* range 0..100 */
	char is_stereo; 
	
	/** RDS settings **/
	/* fields are stored as chars to ease transmission over serial line */
	uint8_t pi[2];
	uint32_t af[7];		/* Alternative Frequencies, range 87500..10800 */ 
	uint8_t af_size;	/* number of defined AFs */
	char rt[65];		/* Null-terminated string */
	char pty[3];		/* Null-terminated string */
	char ps[9];		/* Null-terminated string */
	char ecc;		/* Extended country code */
	char tp;		/* Traffic Program flag */
	char ta;		/* Traffic Announcement flag */
	char ms;		/* music / speech flag */
	/* decoder information fields */
	char di_artificial; 	/* artificial head */
	char di_compression;	/* compressed transmission flag */
	char di_dynamic_pty;	/* dynamic program type */
};

static void pcimax_set_settings(int fd, const struct termios *settings);

static void pcimax_usage_hint(void)
{
	fprintf(stderr, "Try 'rds-ctl --help' for more information.\n");
}

static void pcimax_usage(void)
{
	printf("\n General options: \n"
	       "  --file=<path>\n"
	       "                     load values from config file instead of cl\n"
	       "  -m, --monitor\n"
	       "                     monitor config file for changes and auto\n"
	       "                     update values when changes are detected\n"
	       );
}

static void pcimax_usage_fm(void)
{
	printf("\nFM related options: \n"
	       "  --device=<device>\n"
	       "                     set the target device\n"
	       "                     default: auto-detect\n"
	       "  --set-freq=<freq>\n"
	       "                     set the frequency for the FM transmitter\n"
	       "  --set-stereo=<true/false>\n"
	       "                     set the transmitter into stereo / mono mode\n"
	       "                     default = true => stereo\n"
	       "                     !doesn't seem to have any effect\n"
	       "  --set-power=<0..100>\n"
	       "                     set the transmitter output power\n"
	       "                     valid range: 0 .. 100\n"
	       );
}

static void pcimax_usage_rds(void)
{
		printf("\nRDS related options: \n"
	       "  --set-pi=<pi code>\n"
	       "                     set the Program Identification code\n"
	       "                     <pi code>: 0x0000 .. 0xFFFF or\n"
	       "                                0 .. 65535\n"
	       "  --set-pty=<pty code>\n"
	       "                     set the Program Type Code\n"
	       "                     <pty code> 0..31\n"
	       "  --set-ps=<station_name>\n"
	       "                     set the Program Station Name\n"
	       "                     length is limited to 8 chars\n"
	       "  --set-rt=<radio_text>\n"
	       "                     set the Radio Text\n"
	       "                     length is limited to 64 chars\n"
	       "  --set-ecc=<ecc>\n"
	       "                     set the Extended country code\n"
	       "                     <ecc> 0..4 or e0..e4 or E0..E4\n"
	       "  --set-af=<af list>\n"
	       "                     set the alternative frequencies for the station\n"
	       "                     <af list>: e.g. 88.9,101.2\n"
	       "                     max size of af list: 6\n"
	       "  --set-tp=<true/false>\n"
	       "                     set the Traffic Program flag\n"
	       "  --set-ta=<true/false>\n"
	       "                     set the Traffic Anouncement flag\n"
	       "  --set-ms=<true/false>\n"
	       "                     set the Music/Speech flag\n"
	       "                     <true> -> music, <false> -> speech\n"
	       );
}

/* try to auto-detect connected pcimax3000+ devices, by comparing the Vendor
 * and Product ID for the USB-to-Serial IC to all tty devices of the system
 * code inspired by: http://www.signal11.us/oss/udev/ */
static const char* pcimax_find_device(void)
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	const char *id_product = "ea60";
	const char *id_vendor = "10c4";
	const char *path;
	const char *product_buf;
	const char *vendor_buf;
	static char device[80];
	bool device_found = false;

	/* create the udev object */
	udev = udev_new();
	if (!udev) {
		fprintf(stderr, "Device auto-detection: Can't create udev\n");
		exit(1);
	}

	/* create a list of all devices in the 'tty' subsystem */
	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);

	/* check each item in the list, if it used the same USB to Serial
	 * IC as the the pcimax3000+ card */
	udev_list_entry_foreach(dev_list_entry, devices) {
		/* get the filename of the /sys entry for the device
		 * create a udev_device object (dev) representing it */
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);
		/* store the device path in the /dev/ filesystem */
		strncpy(device, udev_device_get_devnode(dev), 80); 
		/* to get information about the device, get the parent device 
		 * with the subsystem/devtype pair of "usb"/"usb_device" */
		dev = udev_device_get_parent_with_subsystem_devtype(
		       dev, "usb", "usb_device");
		if (!dev) {
			udev_device_unref(dev);
			continue;
		}

		/* check if the tty device matches the USB to Serial 
		 * converter that's used on the pcimax3000+ card */
		product_buf = udev_device_get_sysattr_value(dev, "idProduct");
		vendor_buf = udev_device_get_sysattr_value(dev, "idVendor");
		if (strncmp(id_product, product_buf, 4) == 0 &&
			strncmp(id_vendor, vendor_buf, 4) == 0) {
			printf("Found pcimax3000+ card at %s\n", device);
			device_found = true;
			break;
		}
		udev_device_unref(dev);
	}
	/* free the enumerator object */
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	/* end the program if no card could be detected */
	if (!device_found) {
		fprintf(stderr, "No pcimax3000+ card detected, exiting now\n");
		exit(1);
	}

	return device;
}

/* resores the terminal settings to the state they were before the program
 * made any changes */
static void pcimax_exit(int fd, bool reset)
{
	if (reset)
		pcimax_set_settings(fd, &old_settings);
	close(fd);
	exit(-1);
}

static int pcimax_open_serial(const char* device)
{
	int fd = -1; 
	/* O_NONBLOCK -> return immediately
	 * O_NOCTTY -> we're not the controlling terminal */
	fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0){
		fprintf(stderr, "Unable to open %s", device);
		perror(": ");
		pcimax_exit(fd, false);
	}
	return fd;
}

static void pcimax_set_settings(int fd, const struct termios *settings)
{
	/* TSCNOW -> change occurs immediately */
	tcflush(fd, TCIFLUSH);
	if (tcsetattr(fd, TCSANOW, settings) < 0) {
		perror("tcsetattr: ");
		pcimax_exit(fd, true);
	}
}

/* place the terminal 'fd' into PCIMAX3000+ compatible mode
 * @fd:		file descriptor for a terminal device
 * @ret_val:	0 on success, -1 on error */
/* TODO: figure out the minimal set of settings for proper communication */ 
static void pcimax_setup_serial(int fd)
{
	struct termios new_settings;
	uint32_t modem_ctl_ioctl;
	
	/* Get the current settings for the port */
	if (tcgetattr(fd, &old_settings)) {
		perror("tcgetattr: ");
		pcimax_exit(fd, false);
	}
	new_settings = old_settings;

	/* adopt the settings according to the requirements of the 
	 * PCIMAX3000+ card */
	new_settings.c_cflag = B9600 | CS8 | CREAD | CLOCAL;
	new_settings.c_cflag &= ~CRTSCTS;
	new_settings.c_iflag = IGNBRK;
	new_settings.c_oflag = ONLCR;
	new_settings.c_lflag = ECHOE | ECHOK | NOFLSH | ECHOCTL;
	new_settings.c_line = 0;
	new_settings.c_cc[VMIN] = 0;
	new_settings.c_cc[VTIME] = 0;
	new_settings.c_cc[VEOF] = 26; /* => ^Z */
	/* disable {SUSP,  REPRINT, WERASE, LNEXT} characters */ 
	new_settings.c_cc[VSUSP] = fpathconf(fd, _PC_VDISABLE);
	new_settings.c_cc[VREPRINT] = fpathconf(fd, _PC_VDISABLE);
	new_settings.c_cc[VWERASE] = fpathconf(fd, _PC_VDISABLE);
	new_settings.c_cc[VLNEXT] = fpathconf(fd, _PC_VDISABLE);

	/* Set the baud rates to 9600 */
	cfsetispeed(&new_settings, B9600);
	cfsetospeed(&new_settings, B9600);

	/* enable the Request to Send line & Data Terminal Ready*/
	ioctl(fd, TIOCMGET, &modem_ctl_ioctl);
	modem_ctl_ioctl |= TIOCM_RTS | TIOCM_DTR;
	ioctl(fd, TIOCMSET, &modem_ctl_ioctl);

	pcimax_set_settings(fd, &new_settings);
}

/* wrapper for write function that performs error checking, and
 * terminates the program if an error is detected */
static int pcimax_write(int fd, const void *buf, size_t count)
{
	int wr_count = 0;
	if ((wr_count = write(fd, buf, count)) == -1) {
		perror("write error: ");
		pcimax_exit(fd, true);
	}
	return wr_count;
}

/* @cmd:	c string or char array with terminating null byte
 * @data:	c string or char array 
 * @data_count:	number of data bytes to transmit */
static void pcimax_send_command(int fd, const char *cmd, const char *data, size_t data_count)
{
	static const char start = 0x00;		/* start of new command */
	static const char end_cmd = 0x01;	/* eof command, sof data */
	static const char finish = 0x02;	/* eof data */ 

	pcimax_write(fd, &start, 1); 
	pcimax_write(fd, cmd, strlen((char*)cmd));
	pcimax_write(fd, &end_cmd, 1);
	pcimax_write(fd, data, data_count);
	pcimax_write(fd, &finish, 1); 
	/* there has to be a delay after every command
	 * 200ms is used in official program */
	usleep(200 * 1000L);
}

/* encodes the integer frequency value into a string representation
 * @freq:	integer range 87500..108000 
 * @ret_val:	\0 terminated character array*/
static const char* pcimax_get_freq(uint32_t new_freq)
{
	static char freq_str[3] = {'0', '0', '\0'};
	char high_byte = 0;
	char low_byte = 0;
	uint32_t freq_fifth = (uint32_t)(new_freq / 5);
	
	/* chars 0x00, 0x01, 0x02 are reserved as special control characters
	 * by the device -> add 4 to results */
	high_byte = (char)((uint32_t)(freq_fifth / 128) + 4);
	low_byte = (char)((freq_fifth - (uint32_t)(freq_fifth / 128) * 128) + 4);
	freq_str[0] = low_byte;
	freq_str[1] = high_byte;
	return freq_str;
}

/* encodes the integer power value into a string representation
 * @power:	interger range 0..100
 * @ret_val:	\0 terminated array*/
static const char* pcimax_get_power(uint8_t power)
{
	static char power_str[2] = { 0x19, '\0'};

	/* valid range of values for RDS encoder 0x03..0x19 */
	if (power >= 0 && power <= 100)
		power_str[0] = (char)(power / 100.0f * 21) + 4;
	return power_str;
}

/* TODO: do the power & stereo settings have any effect? 
 * -> submitting a value for "F0" command yields in no detectable transmission */
/* Updates / Sets the FM Transmitter related settings
 * Frequency, Output Power and Stero/Mono mode */
static void pcimax_set_fm_settings(int fd, const struct pcimax_settings *settings)
{
	/* setting stereo / mono mode */
	if (settings->defined & PCIMAX_STEREO) {
		printf("Setting transmitter to %s mode\n", 
			(settings->is_stereo) ? "stereo" : "mono");
		pcimax_send_command(fd, "FS", &settings->is_stereo, 1);
	}
	/* setting transmitter frequency */
	if (settings->defined & PCIMAX_FREQ) {
		printf("Setting transmitter to %.1fMHz\n", (settings->freq / 1000.0f));
		const char *freq = pcimax_get_freq(settings->freq);
		pcimax_send_command(fd, "FF", freq, 2);
	}
	/* setting output power */
	if (settings->defined & PCIMAX_PWR) {
		printf("Setting transmitter power to %d%%\n", settings->power);
		const char *pow = pcimax_get_power(settings->power);
		pcimax_send_command(fd, "FO", pow, 1);
	}
	/* store the settings, commit changes */
	pcimax_send_command(fd, "FW", "0", 1);
}

/* pcimax3000+ protocol expects a trans-coded alternative frequency 
 * representation. The lowest possible frequency 87.6MHz maps to 1, and
 * the frequency is increased  by 0.1MHz per step, so that the
 * highest possible frequency 108MHz maps to 205 */ 
static char pcimax_get_af_code(uint32_t freq)
{
	return (freq - 87500) / 100;
}

/* TODO: add Country code and AreaCoverage fields to settings, or calculate them
 * from the given PI code */
/* Updates / Sets RDS related settings */
static void pcimax_set_rds_settings(int fd, const struct pcimax_settings *settings)
{
	char buffer[65]; 
	/* the cards used the value 0x00, 0x01 and 0x02 as special control commands
	 * a offset has to be added to data guarantee that it is not interpreted as
	 * a control command */
	uint8_t offset = 4;

	/* enable RDS output */
	pcimax_send_command(fd, "PWR", "1", 1);

	/* setting PI code 
	 * RDS-Standard for short range transmitters: 
	 * (see IEC62106: Annex D table D.5)
	 * bits 0-7: Program reference number
	 * bits 8-11: Program in terms of area coverage
	 * 	1 (hex) when device uses an AF list
	 * 	0 (hex) when no AF list is used 
	 * bits 12-15: Country code - a fixed value between 0x01 and 0x0f
	 * atm the program will not enforce these values but let the user
	 * select the PI code freely (might be changed) */
	if (settings->defined & PCIMAX_PI) {
		printf("Setting RDS PI to 0x%02x%02x\n", settings->pi[0], 
			settings->pi[1]);
		/* low byte of PI */
		sprintf(buffer, "%03u", settings->pi[1]);
		pcimax_send_command(fd, "CCAC", buffer, 3);
		/* Program reference, high byte of PI */
		sprintf(buffer, "%03u", settings->pi[0]);
		pcimax_send_command(fd, "PREF", buffer, 3);
	}
	/* setting PTY code */
	if (settings->defined & PCIMAX_PTY) {
		printf("Setting RDS PTY to %s\n", settings->pty);
		pcimax_send_command(fd, "PTY", settings->pty, 2); 
	}
	/* setting TP code */
	if (settings->defined & PCIMAX_TP) {
		printf("Setting RDS TP flag to %s\n", (settings->tp) ? "true" : "false");
		pcimax_send_command(fd, "TP", &settings->tp, 1);
	}
	/* setting TA code */
	if (settings->defined & PCIMAX_TA) {
		printf("Setting RDS TA flag to %s\n", (settings->ta) ? "true" : "false");
		pcimax_send_command(fd, "TA", &settings->ta, 1);
	}
	/* setting MS code */
	if (settings->defined & PCIMAX_MS) {
		printf("Setting RDS m/s flag to %s\n", (settings->ms) ? "music" : "speech");
		pcimax_send_command(fd, "MS", &settings->ms, 1);
	}
	/* setting DI code (Decode Information) */
	if (settings->defined & PCIMAX_DI) {
		printf("Setting RDS Decoder Information flags\n");
		printf("  --> mode: %s, artificial head: %c, \n  --> compression: %c, dynamic PTY: %c\n",
			(settings->is_stereo)? "stereo" : "mono", settings->di_artificial,
			settings->di_compression, settings->di_dynamic_pty);
		/* use the FM-Transmitter setting for mono/stereo flag */
		pcimax_send_command(fd, "Did0", "1", settings->is_stereo);
		pcimax_send_command(fd, "Did1", "1", settings->di_artificial); /* artificial head */
		pcimax_send_command(fd, "Did2", "1", settings->di_compression); /* compression */
		pcimax_send_command(fd, "Did3", "1", settings->di_dynamic_pty); /* dynamic PTY */
	}
	/* setting AF codes alternative frequencies */
	/* n AF + magic number + offset = number of defined AFs 
	 * maximal AFs = 7 */
	buffer[0] = settings->af_size + 224 + offset;
	pcimax_send_command(fd, "AF0", buffer, 1);  /* number of defined AFs */
	for (uint8_t i = 1; i <= 7;  i++) {
		char af;
		/* set all defined AFs to the desired frequency and the
		 * rest to 0 */
		sprintf(buffer, "AF%u", i);
		if (i > settings->af_size) {
			pcimax_send_command(fd, buffer, "0", 1);
			continue;
		}
		printf("Setting %s to %0.1f\n", buffer, 
			settings->af[i-1] / 1000.0f);
		af = pcimax_get_af_code(settings->af[i-1]);
		pcimax_send_command(fd, buffer, &af, 1);
	}

	/* setting ECC code (country code, value range 1..5 + offset) */
	buffer[0] = settings->ecc + offset;
	if (settings->defined & PCIMAX_ECC) {
		printf("Setting RDS ECC code to E%u\n", settings->ecc - 1);
		pcimax_send_command(fd, "ECC", buffer, 1);
	}
	/* setting the RT */
	/* a) when setting a new RT the old value is not flushed but over-
	 * written. If the new RT is shorter than the old one, parts of
	 * the old RT will still be transmitted. To solve this problem the
	 * buffer is filled with space characters before setting the new RT 
	 * b) RDS standard features an RDS RT a/b flag to notify the receiver
	 * the receiver that new RT will be transmitted. Pcimax3000+ does not
	 * support this */
	if (settings->defined & PCIMAX_RT) {
		printf("Setting RDS RT to: %s\n", settings->rt); 
		/* overwrite the old RT with space characters */
		memset(buffer, 0x20, 64);
		pcimax_send_command(fd, "RT", buffer, 64);
		pcimax_send_command(fd, "RT", settings->rt, strlen(settings->rt));
	}
	/* setting PS name */
	/* Even though pcimax3000+ features dynamic station names this
	 * program only supports static station naming, as the the RDS
	 * standard specifically states that the PS feature shouldn't
	 * be used dynamically */
	if (settings->defined & PCIMAX_PS) {
		printf("Setting RDS PS to: %s\n", settings->ps);
		/* overwrite the old PS with space characters */
		memset(buffer, 0x20, 64);
		pcimax_send_command(fd, "PS00", buffer, 8);
		pcimax_send_command(fd, "PS00", settings->ps, 8);
		for (uint8_t i = 1; i < 40; i++) {
			sprintf(buffer, "PS%02u", i);
			pcimax_send_command(fd, buffer, "NULL", 4);
		}
		/* setting the delays for dynamic PS (disabeling the dynamic PS feature)
		 * there are 40 fields available, all of them have to be set */
		pcimax_send_command(fd, "PD00", "1", 1);
		for (uint8_t i = 1; i < 40; i++) {
			sprintf(buffer, "PD%02u", i);
			pcimax_send_command(fd, buffer, "0", 1);
		}
	}
}

/* replaces terminating null characters in char arrays (strings) with spaces
 * @string:	ptr to a char array
 * @replacement:character used for replacement
 * @length:	length of the array
 * used to modify strings so that their whole length will be sent over the
 * serial line, in order to overwrite remainings of old strings */
void pcimax_replace_terminating_null(char *string, char replacement, uint32_t length)
{
	for(uint32_t i = 0; i < length; i++) {
		if (string[i] == '\0')
			string[i] = replacement;
	}
}

void pcimax_parse_ecc(struct pcimax_settings *settings, const char *value)
{
	settings->defined |= PCIMAX_RDS | PCIMAX_ECC;
	if (value[0] == 'e' || value[0] == 'E')
		value++;
	if (value[0] >= '0' && value[0] <= '4')
		/* the card expects values from 1 to 5, but the codes 
		 * are specified as E0..E4 --> add +1 */
		settings->ecc = (char)strtol(value, NULL, 10) + 1;
	else {
		fprintf(stderr, "Unsupported ECC given: %c\n", value[0]);
		exit(1);
	}
}

void pcimax_parse_pi(struct pcimax_settings *settings, const char *value)
{
	int tmp = 0;
	
	settings->defined |= PCIMAX_PI | PCIMAX_RDS;
	if (value[0] == '0' && value[1] == 'x')
		tmp = strtol(value, NULL, 16);
	else
		tmp = strtol(value, NULL, 10);
	settings->pi[1] = (uint8_t) tmp & 0Xff;
	settings->pi[0] = (uint8_t) (tmp >> 8) & 0x0ff;
}

void pcimax_parse_af(struct pcimax_settings *settings, const char *value)
{
	char buffer[10];
	int length = strlen(value);
	int pos = 0;
	int start = 0; 
	
	/* find sub-strings, delimited by ',' or ' ' and convert them into
	 * integer values */ 
	while(pos <= length) {
		if (value[pos] == ',' || value[pos] == ' ' || pos == length) {
			settings->defined |= PCIMAX_PI | PCIMAX_RDS;
			memset(buffer, 0, 10*sizeof(char));
			strncpy(buffer, &value[start], pos-start);
			settings->af[settings->af_size++] = strtof(buffer, NULL) * 1000.0f;
			start = pos + 1 ;
			/* pcimax3000+ supports only 7 AFs */	
			if (settings->af_size >= 7)
				break;
		}
		pos++;
	}
}

/* callback function for the ini file parsing library */
int pcimax_ini_cb(void* buffer, const char* section, const char* name, const char* value)
{
	struct pcimax_settings *settings = (struct pcimax_settings*) buffer;
	int i;
	
	#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	/* FM Settings */
	if (MATCH("FM", "freq")) {
		settings->defined |= PCIMAX_FREQ | PCIMAX_FM;
		settings->freq = strtod(value, NULL) * 1000;
	} else if (MATCH("FM", "stereo")) {
		settings->defined |= PCIMAX_STEREO | PCIMAX_FM;
		settings->is_stereo = strcmp(value, "false")? '1' : '0';
	} else if (MATCH("FM", "power")) {
		settings->defined |= PCIMAX_PWR | PCIMAX_FM;
		i = strtol(value, 0L, 0);
		settings->power = (i >= 0 && i <= 100)? i : 100;
	} 
	
	/* RDS Settings */
	if (MATCH("RDS", "pi")) {
		pcimax_parse_pi(settings, value);
	} else if (MATCH("RDS", "pty")) {
		settings->defined |= PCIMAX_PTY | PCIMAX_RDS;
		strncpy(settings->pty, value, 2);
	} else if (MATCH("RDS", "ps")) {
		settings->defined |= PCIMAX_PS | PCIMAX_RDS;
		strncpy(settings->ps, value, 8);
	} else if (MATCH("RDS", "rt")) {
		settings->defined |= PCIMAX_RT | PCIMAX_RDS;
		strncpy(settings->rt, value, 64);
	} else if (MATCH("RDS", "ecc")) {
		pcimax_parse_ecc(settings, value);
	} else if (MATCH("RDS", "tp")) {
		settings->defined |= PCIMAX_TP | PCIMAX_RDS;
		settings->tp = strcmp(value, "false")? '1' : '0';
	} else if (MATCH("RDS", "ta")) {
		settings->defined |= PCIMAX_TA | PCIMAX_RDS;
		settings->ta = strcmp(value, "false")? '1' : '0';
	} else if (MATCH("RDS", "ms")) {
		settings->defined |= PCIMAX_MS | PCIMAX_RDS;
		settings->ms = strcmp(value, "false")? '1' : '0';
	} else if (MATCH("RDS", "af")) {
		pcimax_parse_af(settings, value);
	} else if (MATCH("RDS", "di_artificial")) {
		settings->defined |= PCIMAX_DI | PCIMAX_RDS;
		settings->di_artificial = strcmp(value, "false")? '1' : '0';
	} else if (MATCH("RDS", "di_compression")) {
		settings->defined |= PCIMAX_DI | PCIMAX_RDS;
		settings->di_compression = strcmp(value, "false")? '1' : '0';
	} else if (MATCH("RDS", "di_dynamic_pty")) {
		settings->defined |= PCIMAX_DI | PCIMAX_RDS;
		settings->di_dynamic_pty = strcmp(value, "false")? '1' : '0';
	}

	return 0;
}

/* parse the command line into the settings struct */
uint32_t pcimax_parse_cl(int argc, char **argv,
			struct pcimax_settings *settings)
{
	int i = 0;
	int idx = 0;
	int ch = 0;
	double freq;
	/* 26 letters in the alphabet, case sensitive = 26 * 2 possible
	 * short options, where each option requires at most two chars
	 * {option, optional argument} */
	char short_options[26 * 2 * 2 + 1];

	if (argc == 1) {
		pcimax_usage_hint();
		exit(1);
	}
	/* parse command line options */
	for (i = 0; long_options[i].name; i++) {
		if (!isalpha(long_options[i].val))
			continue;
		short_options[idx++] = long_options[i].val;
		if (long_options[i].has_arg == required_argument)
			short_options[idx++] = ':';
	}

	while(1){
		int option_index = 0;

		short_options[idx] = 0;
		ch = getopt_long(argc, argv, short_options,
				 long_options, &option_index);
		if (ch == -1)
			break;

		settings->options[(int)ch] = 1;
		switch (ch){
		case OptFile:
			if (access(optarg, F_OK) != -1)
				strncpy(settings->file, optarg, 80);
			else {
				fprintf(stderr, "Unable to open ini file: %s\n", optarg);
				exit(1);
			}
			break;
		case OptSetDevice:
			memset(settings->device, 0, 80);
			if (access(optarg, F_OK) != -1)
				strncpy(settings->device, optarg, 80);
			else {
				fprintf(stderr, "Unable to open device: %s\n", optarg);
				exit(1);
			}
			break; 
		case OptSetFreq:
			settings->defined |= PCIMAX_FREQ | PCIMAX_FM;
			freq = strtod(optarg, NULL);
			settings->freq = freq * 1000;
			break;
		case OptSetAF:
			pcimax_parse_af(settings, optarg);
			break;
		case OptSetECC:
			pcimax_parse_ecc(settings, optarg);
			break;
		case OptSetStereo:
			settings->defined |= PCIMAX_STEREO | PCIMAX_FM;
			settings->is_stereo = strcmp(optarg, "false")? '1' : '0';
			break;
		case OptSetPower:
			settings->defined |= PCIMAX_PWR | PCIMAX_FM;
			i = strtol(optarg, 0L, 0);
			settings->power = (i >= 0 && i <= 100)? i : 100;
			break;
		case OptSetPI:
			pcimax_parse_pi(settings, optarg);
			break;
		case OptSetPTY:
			settings->defined |= PCIMAX_PTY | PCIMAX_RDS;
			strncpy(settings->pty, optarg, 2);
			break;
		case OptSetPS:
			settings->defined |= PCIMAX_PS | PCIMAX_RDS;
			strncpy(settings->ps, optarg, 8);
			break;
		case OptSetRT:
			settings->defined |= PCIMAX_RT | PCIMAX_RDS;
			strncpy(settings->rt, optarg, 64);
			break;
		case OptSetTP:
			settings->defined |= PCIMAX_TP | PCIMAX_RDS;
			settings->tp = strcmp(optarg, "false")? '1' : '0';
			break;
		case OptSetTA:
			settings->defined |= PCIMAX_TA | PCIMAX_RDS;
			settings->ta = strcmp(optarg, "false")? '1' : '0';
			break;
		case OptSetMS:
			settings->defined |= PCIMAX_MS | PCIMAX_RDS;
			settings->ms = strcmp(optarg, "false")? '1' : '0';
			break;
		case OptHelp:
			pcimax_usage();
			pcimax_usage_fm();
			pcimax_usage_rds();
			exit(1);
			break;
		case ':':
			fprintf(stderr, "Option '%s' requires a value\n",
				argv[optind]);
			pcimax_usage_hint();
			exit(1);
		case '?':
			if (argv[optind])
				fprintf(stderr, "Unknown argument '%s'\n", argv[optind]);
			pcimax_usage_hint();
			exit(1);
		}
	}
	if (optind < argc) {
		printf("unknown arguments: ");
		while (optind < argc)
			printf("%s ", argv[optind++]);
		printf("\n");
		pcimax_usage_hint();
		return 1;
	}

	return 0;
}

int main(int argc, char* argv[])
{
	int fd = -1;
	static struct pcimax_settings settings;
	memset(&settings, 0, sizeof(settings));
	
	/* set up the program settings */
	pcimax_parse_cl(argc, argv, &settings);

	/* if a ini file was specified, load the values from the file */
	if (settings.options[OptFile]) {
		ini_parse(settings.file, pcimax_ini_cb, &settings);
	}

	/* if no device was specified, try to auto-detect the card */
	if (!settings.options[OptSetDevice]) {
		strncpy(settings.device, pcimax_find_device(), 80);
	}

	/* open the device(com port) and configure it */
	fd = pcimax_open_serial(settings.device);
	pcimax_setup_serial(fd);

	/* update all defined RDS values */
	if (settings.defined & PCIMAX_FM)
		pcimax_set_fm_settings(fd, &settings);
	if (settings.defined & PCIMAX_RDS)
		pcimax_set_rds_settings(fd, &settings);

	/* restore com port settings & close the program  */
	pcimax_exit(fd, true);
	return 1;
};
