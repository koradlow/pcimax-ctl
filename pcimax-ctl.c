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
	OptHelp = 'h',
	OptLast = 128
};

/* long options */
static struct option long_options[] = {
	{"device", required_argument, 0, OptSetDevice},
	{"help", no_argument, 0, OptHelp},
	{0, 0, 0, 0}
};

static char options[OptLast];

void pcimax_set_settings(int fd, const struct termios *settings);

static void pcimax_usage_hint(void)
{
	fprintf(stderr, "Try 'rds-ctl --help' for more information.\n");
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
	
	/* Get the current settings for the port */
	if (tcgetattr(fd, &old_settings)) {
		perror("tcgetattr: ");
		pcimax_exit(fd, false);
	}

	new_settings.c_cflag = B9600 | CRTSCTS | CS8  | CLOCAL | CREAD;
	new_settings.c_iflag = IGNPAR;
	new_settings.c_oflag = 0;
	new_settings.c_lflag = 0;       //ICANON;
	new_settings.c_cc[VMIN]=1;
	new_settings.c_cc[VTIME]=0;

	/* Set the baud rates to 9600 */
	cfsetispeed(&new_settings, B9600);
	cfsetospeed(&new_settings, B9600);

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
 * @freq:	interger range 87500..108000 
 * @ret_val:	\0 terminated array*/
const char* pcimax_get_freq(int freq)
{
	static char freq_str[3] = {'0', '0', '\0'};
	char high_byte = 0;
	char low_byte = 0;
	uint32_t freq_fifth = (uint32_t)(freq / 5);
	
	/* chars 0x00, 0x01, 0x02 are reserved -> add 4 to results */
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

int main(int argc, char* argv[])
{
	int fd = -1;
	int idx = 0;
	int ch = 0;
	char device[80];	/* buffer for device name */
	char short_options[26 * 2 * 2 + 1]; 	/*TODO: don't use magic number */
	char str_buf[20] = { '\0' };

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
		case OptHelp:
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

	/* TODO: send the user specified options to the device 
	 * Right now only tries to change the frequency */
	/* setting stereo mode */
	//printf("Bytes written: %d\n",write(fd,"abeede",6));
	pcimax_send_command(fd, "FS", "1", 1);
	/* setting transmitter frequency */
	const char *freq = pcimax_get_freq(102000);
	pcimax_send_command(fd, "FF", freq, 2);
	/* setting output power */
	const char *pow = pcimax_get_power(80);
	pcimax_send_command(fd, "FO", pow, 0);
	/* store the settings, commit changes */
	str_buf[0]= '0';
	pcimax_send_command(fd, "FW", &str_buf[0], 1);

	/* restore settings & close the program  */
	pcimax_exit(fd, true);
	return 1;
};
