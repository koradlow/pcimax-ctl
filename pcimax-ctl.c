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
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <ctype.h>  /* Character classification routines */
#include <getopt.h>
#include <time.h>

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

static struct termios old_settings;

/* short options */
enum Options{
	OptSetDevice = 'd',
	OptSetFreq = 'f',
	OptHelp = 'h',
	OptSetPower = 64,
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
	{"set-freq", required_argument, 0, OptSetFreq},
	{"set-power", required_argument, 0, OptSetPower},
	{"set-pi", required_argument, 0 , OptSetPI},
	{"set-ps", required_argument, 0, OptSetPS},
	{"set-pty", required_argument, 0, OptSetPTY},
	{"set-rt", required_argument, 0, OptSetRT},
	{"set-stereo", required_argument, 0, OptSetStereo},
	{"set-ta", required_argument, 0 , OptSetTA},
	{"set-tp", required_argument, 0, OptSetTP},
	{"help", no_argument, 0, OptHelp},
	{0, 0, 0, 0}
};

/* struct containing all available settings for the device */
struct pcimax_settings {
	uint32_t defined;
	char device[80];
	/** FM-Transmitter settings **/
	uint32_t freq;	/* range 87500..108000 */
	uint8_t power; 	/* range 0..100 */
	char is_stereo; 
	/** RDS settings **/
	char rt[65];
	char pi[3];
	char pty[2];
	char ps[9];
	char tp;
	char ta;
	char ms;
	uint8_t ecc;
};

static char options[OptLast];

static void pcimax_set_settings(int fd, const struct termios *settings);

static void pcimax_usage_hint(void)
{
	fprintf(stderr, "Try 'rds-ctl --help' for more information.\n");
}

static void pcimax_usage_fm(void)
{
	printf("\nFM related options: \n"
	       "  --device=<device>\n"
	       "                     set the target device\n"
	       "                     default: /dev/ttyUSB0\n"
	       "  --set-freq=<freq>\n"
	       "                     set the frequency for the FM transmitter\n"
	       "  --set-stereo=<true/false>\n"
	       "                     set the transmitter into stereo / mono mode\n"
	       "                     default = true => stereo\n"
	       "                     !doesn't seem to have any effect\n"
	       "  --set-power=<0..100>\n"
	       "                     set the transmitter output power\n"
	       "                     valid range: 0 .. 100\n"
	       "                     !doesn't seem to have any effect\n"
	       );
}

static void pcimax_usage_rds(void)
{
		printf("\nFM related options: \n"
	       "  --set-pi=<pi code>\n"
	       "                     set the Program Identification code\n"
	       "  --set-pty=<pty code>\n"
	       "                     set the Program Type Code\n"
	       "  --set-ps=<station_name>\n"
	       "                     set the Program Station Name\n"
	       "                     length is limited to 8 chars\n"
	       "  --set-rt=<radio_text>\n"
	       "                     set the Radio Text\n"
	       "                     length is limited to 64 chars\n"
	       "  --set-tp=<true/false>\n"
	       "                     set the Traffic Program flag\n"
	       "  --set-ta=<true/false>\n"
	       "                     set the Traffic Anouncement flag\n"
	       
	       );
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
	if (settings->defined & PCIMAX_STEREO)
		pcimax_send_command(fd, "FS", &settings->is_stereo, 1);
	/* setting transmitter frequency */
	if (settings->defined & PCIMAX_FREQ) {
		printf("Freq: %u\n", settings->freq);
		const char *freq = pcimax_get_freq(settings->freq);
		pcimax_send_command(fd, "FF", freq, 2);
	}
	/* setting output power */
	if (settings->defined & PCIMAX_PWR) {
		const char *pow = pcimax_get_power(settings->power);
		pcimax_send_command(fd, "FO", pow, 1);
	}
	/* store the settings, commit changes */
	pcimax_send_command(fd, "FW", "0", 1);
}

/* TODO: add Country code and AreaCoverage fields to settings, or calculate them
 * from the given PI code */
/* Updates / Sets RDS related settings */
static void pcimax_set_rds_settings(int fd, const struct pcimax_settings *settings)
{
	char buffer[20]; 

	/* enable RDS output */
	pcimax_send_command(fd, "PWR", "1", 1);

	/* setting PI code */
	if (settings->defined & PCIMAX_PI) {
		/* low byte of PI */
		pcimax_send_command(fd, "CCAC", "065", 3);
		/* Program reference, high byte of PI */
		pcimax_send_command(fd, "PREF", "128", 3);
	}
	/* setting PTY code */
	if (settings->defined & PCIMAX_PTY)
		pcimax_send_command(fd, "PTY", settings->pty, 2); 
	/* setting TP code */
	if (settings->defined & PCIMAX_TP)
		pcimax_send_command(fd, "TP", &settings->tp, 1);
	/* setting TA code */
	if (settings->defined & PCIMAX_TA)
		pcimax_send_command(fd, "TA", &settings->ta, 1);
	/* setting MS code */
	if (settings->defined & PCIMAX_MS)
		pcimax_send_command(fd, "MS", &settings->ms, 1);

	/* setting DI code (Decode Information) */
	pcimax_send_command(fd, "Did0", "1", 1); /* mono / stereo */
	pcimax_send_command(fd, "Did1", "1", 1); /* artificial head */
	pcimax_send_command(fd, "Did2", "1", 1); /* compression */
	pcimax_send_command(fd, "Did3", "1", 1); /* dynamic PTY */

	/* setting AF codes alternative frequencies */
	/* 0 AF + magic number + offset = number of defined AFs */
	buffer[0] = 0 + 224 + 4;
	pcimax_send_command(fd, "AF0", buffer, 1);  /* number of defined AFs */
	for (uint8_t i = 1; i <= 7;  i++) {
		/* set all 6 AFs to 0 */
		sprintf(buffer, "AF%u", i);
		pcimax_send_command(fd, buffer, "0", 1);
	}

	/* setting ECC code (country code, value range 1..5 + offset) */
	buffer[0] = settings->ecc + 4;
	pcimax_send_command(fd, "ECC", buffer, 1);

	/* setting the RT */
	/* TODO: Find out if there's a way to flush the rt buffer of the device
	 * right now values are only overwritten but if the new RT is shorter
	 * than the old one, parts of the old RT will still be visible */
	if (settings->defined & PCIMAX_RT) 
		pcimax_send_command(fd, "RT", settings->rt, strlen(settings->rt));

	/* setting PS name */
	/* TODO: right now only static station naming is supported, as the
	 * the RDS standard specifically specifies that the PS feature shouldn't
	 * be used dynamically */
	if (settings->defined & PCIMAX_PS) {
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

/* initializes the settings structure with sensefull default values */
static void pcimax_init_settings(struct pcimax_settings* settings)
{
	/* delimit char arrays for safety reasons (prevent overflows) */
	settings->ps[8] = '\0';
	settings->rt[64] = '\0';

	/* FM-Settings */
	strcpy(settings->device, "/dev/ttyUSB0");
	settings->freq = 88700;
	settings->power = 100;
	settings->is_stereo = true; 

	/* RDS-Settings */
	strcpy(settings->pty, "17");
	strcpy(settings->ps, "unknown");
	strcpy(settings->rt, "Radiotext undefined");
	settings->ta = '1';
	settings->tp = '1';
	settings->ms = '0';
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

int main(int argc, char* argv[])
{
	int fd = -1;
	int i = 0;
	int idx = 0;
	int ch = 0;
	double freq;
	char short_options[26 * 2 * 2 + 1]; 	/*TODO: don't use magic number */
	static struct pcimax_settings settings;
	
	pcimax_init_settings(&settings);
	if (argc == 1) {
		pcimax_usage_hint();
		return 0;
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

		options[(int)ch] = 1;
		switch (ch){
		case OptSetDevice:
			memset(settings.device, 0, 80);
			if(access(optarg, F_OK) != -1)
				strncpy(settings.device, optarg, 80);
			else {
				fprintf(stderr, "Unable to open device: %s\n", optarg);
				return -1;
			}
			break; 
		case OptSetFreq:
			settings.defined |= PCIMAX_FREQ | PCIMAX_FM;
			freq = strtod(optarg, NULL);
			settings.freq = freq * 1000;
			break;
		case OptSetStereo:
			settings.defined |= PCIMAX_STEREO | PCIMAX_FM;
			settings.is_stereo = strcmp(optarg, "false")? '1' : '0';
			break;
		case OptSetPower:
			settings.defined |= PCIMAX_PWR | PCIMAX_FM;
			i = strtol(optarg, 0L, 0);
			settings.power = (i >= 0 && i <= 100)? i : 100;
			break;
		case OptSetPI:
			settings.defined |= PCIMAX_PI | PCIMAX_RDS;
			strncpy(settings.pi, optarg, 3);
			break;
		case OptSetPTY:
			settings.defined |= PCIMAX_PTY | PCIMAX_RDS;
			strncpy(settings.pty, optarg, 2);
			break;
		case OptSetPS:
			settings.defined |= PCIMAX_PS | PCIMAX_RDS;
			strncpy(settings.ps, optarg, 8);
			pcimax_replace_terminating_null(settings.ps, 0x20, 8);
			break;
		case OptSetRT:
			settings.defined |= PCIMAX_RT | PCIMAX_RDS;
			strncpy(settings.rt, optarg, 64);
			pcimax_replace_terminating_null(settings.rt, 0x20, 64);
			break;
		case OptSetTP:
			settings.defined |= PCIMAX_TP | PCIMAX_RDS;
			settings.tp = strcmp(optarg, "false")? '1' : '0';
			break;
		case OptSetTA:
			settings.defined |= PCIMAX_TA | PCIMAX_RDS;
			settings.ta = strcmp(optarg, "false")? '1' : '0';
			break;
		case OptHelp:
			pcimax_usage_fm();
			pcimax_usage_rds();
			return 1;
			break;
		case ':':
			fprintf(stderr, "Option '%s' requires a value\n",
				argv[optind]);
			pcimax_usage_hint();
			return 1;
		case '?':
			if (argv[optind])
				fprintf(stderr, "Unknown argument '%s'\n", argv[optind]);
			pcimax_usage_hint();
			return 1;
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

	/* open the device(com port) and configure it */
	fd = pcimax_open_serial(settings.device);
	pcimax_setup_serial(fd);

	/* change all requested settings */
	if (settings.defined & PCIMAX_FM)
		pcimax_set_fm_settings(fd, &settings);
	if (settings.defined & PCIMAX_RDS)
		pcimax_set_rds_settings(fd, &settings);

	/* restore settings & close the program  */
	pcimax_exit(fd, true);
	return 1;
};
