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

static struct termios old_settings;

/* short options */
enum Options{
	OptSetDevice = 'd',
	OptSetFreq = 'f',
	OptHelp = 'h',
	OptLast = 128
};

/* long options */
static struct option long_options[] = {
	{"device", required_argument, 0, OptSetDevice},
	{"set-freq", required_argument, 0 , OptSetFreq},
	{"help", no_argument, 0, OptHelp},
	{0, 0, 0, 0}
};

static char options[OptLast];

void pcimax_set_settings(int fd, const struct termios *settings);

static void pcimax_usage_hint(void)
{
	fprintf(stderr, "Try 'rds-ctl --help' for more information.\n");
}

static void pcimax_usage_common(void)
{
	printf("\navailable options: \n"
	       "  --device=<device>\n"
	       "                     set the target device, defaults to /dev/ttyUSB0\n"
	       "  --set-freq=<freq>\n"
	       "                     set the frequency for the FM transmitter\n"
	       "                     defaults to freq = 88.7\nS"
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

int pcimax_open_serial(const char* device)
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

void pcimax_set_settings(int fd, const struct termios *settings)
{
	/* TSCNOW -> change occurs immediately */
	tcflush(fd, TCIFLUSH);
	if (tcsetattr(fd, TCSANOW, settings) < 0) {
		perror("tcsetattr: ");
		pcimax_exit(fd, true);
	}
}

/* place the terminal 'fd' into raw mode (noncanonical mode with all
 * input and output processing disabled)
 * @fd:		file descriptor for a terminal device
 * @ret_val:	0 on success, -1 on error */
void pcimax_setup_serial(int fd)
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
int pcimax_write(int fd, const void *buf, size_t count)
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
void pcimax_send_command(int fd, const char *cmd, const char *data, size_t data_count)
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
 * @freq:	float range 87.500..108.000 
 * @ret_val:	\0 terminated character array*/
const char* pcimax_get_freq(double new_freq)
{
	int freq = new_freq * 1000;
	static char freq_str[3] = {'0', '0', '\0'};
	char high_byte = 0;
	char low_byte = 0;
	uint32_t freq_fifth = (uint32_t)(freq / 5);
	
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
const char* pcimax_get_power(int power)
{
	static char power_str[2] = { 0x19, '\0'};

	/* valid range of values for RDS encoder 0x03..0x19 */
	if (power >= 0 && power <= 100)
		power_str[0] = (char)(power / 100.0f * 21) + 4;
	return power_str;
}

/* Updates / Sets the frequency of the FM Transmitter, sets the output
 * power and the transmission mode */
void pcimax_set_freq(int fd, double new_freq)
{
	/* TODO: send the user specified options to the device 
	 * Right now only tries to change the frequency */
	/* setting stereo mode */
	pcimax_send_command(fd, "FS", "1", 1);
	/* setting transmitter frequency */
	const char *freq = pcimax_get_freq(new_freq);
	pcimax_send_command(fd, "FF", freq, 2);
	/* setting output power */
	const char *pow = pcimax_get_power(80);
	pcimax_send_command(fd, "FO", pow, 0);
	/* store the settings, commit changes */
	pcimax_send_command(fd, "FW", "0", 1);
}

int main(int argc, char* argv[])
{
	int fd = -1;
	int idx = 0;
	int ch = 0;
	double freq = 87.5;
	char device[80];	/* buffer for device name */
	char short_options[26 * 2 * 2 + 1]; 	/*TODO: don't use magic number */
	static const char *default_device = "/dev/ttyUSB0";
	strcpy(device, default_device);
	
	if (argc == 1) {
		pcimax_usage_hint();
		return 0;
	}
	/* parse command line options */
	for (int i = 0; long_options[i].name; i++) {
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
			strncpy(device, optarg, 80);
			if(access(optarg, F_OK) != -1)
			for(int i=0; optarg[i]!='\0' && i<80; ++i)
				device[i] = optarg[i];
			else {
				fprintf(stderr, "Unable to open device: %s\n", device);
				return -1;
			}
			break; 
		case OptSetFreq:
			freq = strtod(optarg, NULL);
			break;
		case OptHelp:
			pcimax_usage_common();
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
	fd = pcimax_open_serial(device);
	pcimax_setup_serial(fd);

	pcimax_set_freq(fd, freq);

	/* restore settings & close the program  */
	pcimax_exit(fd, true);
	return 1;
};
