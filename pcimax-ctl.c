/*
 * Copyright 2012 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 * Author: Konke Radlow <koradlow@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; 
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
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <ctype.h>  /* Character classification routines */
#include <getopt.h>

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

static void usage_hint(void)
{
	fprintf(stderr, "Try 'rds-ctl --help' for more information.\n");
}

int open_serial(const char* device)
{
	int fd; 
	/* O_NDELAY -> ignore state of DCD signal line
	 * O_NOCTTY -> we're not the controlling terminal */
	fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd == -1){
		fprintf(stderr, "Unable to open %s\n", device);
	}
	return (fd);
}

/* TODO: implement variable baud rate support, for now fixed at 9600baud */
int set_baud(int fd, int baud)
{
	struct termios options;
	/* Get the current options for the port */
	if(tcgetattr(fd, &options)){
		fprintf(stderr, "Unable to get options for serial port\n");
		return -1;
	}

	/* Set the baud rates to 9600 */
	cfsetispeed(&options, B9600);
	cfsetospeed(&options, B9600);

	/* Enable the receiver and set local mode 
	 * CLOCAL -> ignore modem control lines 
	 * CREAD -> enable the receiver */
	options.c_cflag |= (CLOCAL | CREAD);
	/* TSCNOW -> change occurs immediately */
	tcsetattr(fd, TCSANOW, &options);
	
	return 0;
}

int main(int argc, char* argv[])
{
	int fd = -1;
	int idx = 0;
	int ch = 0;
	char device[80];	/* buffer for device name */
	char short_options[26 * 2 * 2 + 1]; 	/*TODO: don't use magic number */
	
	if (argc == 1) {
		usage_hint();
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
			case OptHelp:
			case ':':
				fprintf(stderr, "Option '%s' requires a value\n",
					argv[optind]);
				usage_hint();
				return 1;
			case '?':
				if (argv[optind])
					fprintf(stderr, "Unknown argument '%s'\n", argv[optind]);
				usage_hint();
				return 1;
		}
	}
	if (optind < argc) {
		printf("unknown arguments: ");
		while (optind < argc)
			printf("%s ", argv[optind++]);
		printf("\n");
		usage_hint();
		return 1;
	}
	
	/* open the serial device and set it up with the desired options */
	if((fd = open_serial(device) == -1))
		return -1;
	if(set_baud(fd, 9600) == -1)
		return -1;
	
	/* TODO: send the user specified options to the device 
	 * Right now only tries to change the frequency */
	
	return 1;
};
