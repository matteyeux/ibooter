/*
 * irecovery.c
 * Software frontend for iBoot/iBSS communication with iOS devices
 *
 * Copyright (c) 2012-2017 Nikias Bassen
 * Copyright (c) 2012-2015 Martin Szulecki <martin.szulecki@libimobiledevice.org>
 * Copyright (c) 2010-2011 Chronic-Dev Team
 * Copyright (c) 2010-2011 Joshua Hill
 * Copyright (c) 2008-2011 Nicolas Haunold
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <libusb-1.0/libusb.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>

#include <include/ibooter.h>
#include <include/irecovery.h>

#define dfu_hash_step(a,b) a = (dfu_hash_t1[(a & 0xFF) ^ ((unsigned char)b)] ^ (a >> 8))

static struct libusb_device_handle *device = NULL;
static int devicemode = -1;
static char *udid = NULL;


int enter_recovery() {

	idevice_t device = NULL;
	lockdownd_client_t client = NULL;
	lockdownd_error_t ldret = LOCKDOWN_E_UNKNOWN_ERROR;

	if (IDEVICE_E_SUCCESS != idevice_new(&device, NULL)) {
		fprintf(stdout, "No device found, is it plugged in?\n");
		return EXIT_FAILURE;
	}

	fprintf(stdout, "[INFO] Device with UDID %s will switch to recovery mode\n", udid);

	if (LOCKDOWN_E_SUCCESS != (ldret = lockdownd_client_new(device, &client, "ideviceenterrecovery"))) {
		fprintf(stdout, "[ERROR] Could not connect to lockdownd, error code %d\n", ldret);
		idevice_free(device);
		return -1;
	}

	if (lockdownd_enter_recovery(client) != LOCKDOWN_E_SUCCESS) {
		fprintf(stdout, "[ERROR] Failed to enter recovery mode : %s\n", strerror(errno));
		exit(1);
	}

	fprintf(stdout, "[INFO] Device is booting in recovery mode\n");

	lockdownd_client_free(client);
	idevice_free(device);    

	return 0;
}

void device_connect() {

	if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, devicemode = RECV_MODE)) == NULL) {
		if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, devicemode = WTF_MODE)) == NULL) {
			if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, devicemode = DFU_MODE)) == NULL) {
				devicemode = -1;
			}
		}
	}
}

void device_close() {

	if (device != NULL) {
		fprintf(stdout, "[X] Closing Connection.\n");
		libusb_close(device);
	}
}

void device_reset() {

	if (device != NULL) {
		printf("[Device] Reseting Connection.\n");
		libusb_reset_device(device);
	}

}

int device_sendcmd(char* argv[]) {

	char* command = argv[0];
	size_t length = strlen(command);

	if (length >= 0x200) {
		printf("[Device] Failed to send command (too long).\n");
		return -1;
	}

	if (! libusb_control_transfer(device, 0x40, 0, 0, 0, (unsigned char*)command, (length + 1), 1000)) {
		printf("[Device] Failed to send command.\n");
		return -1;
	}

	return 1;

}

int device_autoboot() {

	printf("[Device] Enabling auto-boot.\n");

	char* command[3];
	command[0] = "setenv auto-boot true";
	command[1] = "saveenv";
	command[2] = "reboot";

	device_sendcmd(&command[0]);
	device_sendcmd(&command[1]);
	device_sendcmd(&command[2]);

	return 1;

}

static int device_status(unsigned int* status) {
	unsigned char buffer[6];
	memset(buffer, '\0', 6);
	if (libusb_control_transfer(device, 0xA1, 3, 0, 0, buffer, 6, USB_TIMEOUT) != 6) {
		*status = 0;
		return -1;
	}
	*status = (unsigned int) buffer[4];
	return 0;
}

int device_send(char* filename, int dfu_notify_finished) {

	FILE* file = fopen(filename, "rb");

	if(file == NULL) {

		printf("[Program] Unable to find file. (%s)\n",filename);
		return 1;

	}

	fseek(file, 0, SEEK_END);
	unsigned int len = ftell(file);
	fseek(file, 0, SEEK_SET);

	unsigned char* buffer = malloc(len);

	if (buffer == NULL) {

		printf("[Program] Error allocating memory.\n");
		fclose(file);
		return 1;

	}

	fread(buffer, 1, len, file);
	fclose(file);

	int error = 0;
	int recovery_mode = (devicemode != DFU_MODE && devicemode != WTF_MODE);

	unsigned int h1 = 0xFFFFFFFF;
	unsigned char dfu_xbuf[12] = {0xff, 0xff, 0xff, 0xff, 0xac, 0x05, 0x00, 0x01, 0x55, 0x46, 0x44, 0x10};
	int packet_size = recovery_mode ? 0x8000 : 0x800;
	int last = len % packet_size;
	int packets = len / packet_size;

	if (last != 0) {
		packets++;
	} else {
		last = packet_size;
	}

	/* initiate transfer */
	if (recovery_mode) {
		error = libusb_control_transfer(device, 0x41, 0, 0, 0, NULL, 0, USB_TIMEOUT);
	} else {
		unsigned char dump[4];
		if (libusb_control_transfer(device, 0xa1, 5, 0, 0, dump, 1, USB_TIMEOUT) == 1) {
			error = 0;
		} else {
			error = -1;
		}
	}

	if (error) {
		printf("[Device] Error initializing transfer.\n");
		return error;
	}

	int i = 0;
	unsigned long count = 0;
	unsigned int status = 0;
	int bytes = 0;
	for (i = 0; i < packets; i++) {
		int size = (i + 1) < packets ? packet_size : last;

		/* Use bulk transfer for recovery mode and control transfer for DFU and WTF mode */
		if (recovery_mode) {
			error = libusb_bulk_transfer(device, 0x04, &buffer[i * packet_size], size, &bytes, USB_TIMEOUT);
		} else {
			int j;
			for (j = 0; j < size; j++) {
				dfu_hash_step(h1, buffer[i*packet_size + j]);
			}
			if (i+1 == packets) {
				for (j = 0; j < 2; j++) {
					dfu_hash_step(h1, dfu_xbuf[j*6 + 0]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 1]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 2]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 3]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 4]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 5]);
				}

				char* newbuf = (char*)malloc(size + 16);
				memcpy(newbuf, &buffer[i * packet_size], size);
				memcpy(newbuf+size, dfu_xbuf, 12);
				newbuf[size+12] = h1 & 0xFF;
				newbuf[size+13] = (h1 >> 8) & 0xFF;
				newbuf[size+14] = (h1 >> 16) & 0xFF;
				newbuf[size+15] = (h1 >> 24) & 0xFF;
				size += 16;
				bytes = libusb_control_transfer(device, 0x21, 1, i, 0, (unsigned char*)newbuf, size, USB_TIMEOUT);
				free(newbuf);
			} else {
				bytes = libusb_control_transfer(device, 0x21, 1, i, 0, &buffer[i * packet_size], size, USB_TIMEOUT);
			}
		}

		if (bytes != size) {
			printf("[Device] Error sending packet.\n");
			return -1;
		}

		if (!recovery_mode) {
			error = device_status(&status);
		}

		if (error) {
			printf("[Device] Error sending packet.\n");
			return error;
		}

		if (!recovery_mode && status != 5) {
			int retry = 0;

			while (retry < 20) {
				device_status(&status);
				if (status == 5) {
					break;
				}
				sleep(1);
				retry++;
			}

			if (status != 5) {
				printf("[Device] Invalid status error during file upload.\n");
				return -1;
			}
		}

		count += size;
		printf("Sent: %d bytes - %lu of %u\n", bytes, count, len);
	}

	if (dfu_notify_finished && !recovery_mode) {
		libusb_control_transfer(device, 0x21, 1, packets, 0, (unsigned char*) buffer, 0, USB_TIMEOUT);

		for (i = 0; i < 2; i++) {
			error = device_status(&status);
			if (error) {
				printf("[Device] Error receiving status while uploading file.\n");
				return error;
			}
		}

		if (dfu_notify_finished == 2) {
			/* we send a pseudo ZLP here just in case */
			libusb_control_transfer(device, 0x21, 1, 0, 0, 0, 0, USB_TIMEOUT);
		}

		device_reset();
	}

	printf("[Device] Successfully uploaded file.\n");

	free(buffer);
	return 0;
}

int device_buffer(char* data, int len) {

	int packets = len / 0x800;

	if(len % 0x800) {
		packets++;
	}

	int last = len % 0x800;

	if(!last) {
		last = 0x800;
	}

	int i = 0;

	unsigned char response[6];

	for(i = 0; i < packets; i++) {

		int size = i + 1 < packets ? 0x800 : last;

		if(! libusb_control_transfer(device, 0x21, 1, i, 0, (unsigned char*)&data[i * 0x800], size, 1000)) {

			fprintf(stdout, "[Device] Error sending packet from buffer.\n");
			return -1;
		}

		if( libusb_control_transfer(device, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {

			fprintf(stdout, "[Device] Error receiving status from buffer.\n");
			return -1;
		}

		if(response[4] != 5) {

			fprintf(stdout, "[Device] Invalid status error from buffer.\n");
			return -1;
		}
	}

	libusb_control_transfer(device, 0x21, 1, i, 0, (unsigned char*)data, 0, 1000);

	for(i = 6; i <= 8; i++) {

		if( libusb_control_transfer(device, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {

			fprintf(stdout, "[Device] Error receiving execution status from buffer.\n");
			return -1;
		}

		if(response[4] != i) {

			fprintf(stdout, "[Device] Invalid execution status from buffer.\n");
			return -1;
		}
	}

	return 0;
}

int device_receive(char *buffer, int size) {
	int bytes = 0;
	#if 0
		memset(buffer, 0, size);
		libusb_bulk_transfer(device, 0x81, buffer, size, &bytes, 500);
		return bytes;
	#else
		int total = size;
		while (libusb_bulk_transfer(device, 0x81, (unsigned char*)buffer, size, &bytes, 100) == 0 && bytes) {
			buffer += bytes;
			size -= bytes;
		}
		return total - size;
	#endif
}

void prog_init() {

	libusb_init(NULL);
	device_connect();
}

void prog_exit() {

	device_close();
	libusb_exit(NULL);
	exit(0);
}


void recovery_cmd_help()
{
	fprintf(stdout, "Commands:\n");
	fprintf(stdout, "\t/exit\t\t\texit from recovery console.\n");
	fprintf(stdout, "\t/upload <file>\t\tupload file to device.\n");
	fprintf(stdout, "\t/exploit [payload]\tsend usb exploit packet.\n");
	fprintf(stdout, "\t/batch <file>\t\texecute commands from a batch file.\n");
	fprintf(stdout, "\t/auto-boot\t\tenable auto-boot (exit recovery loop).\n"); // or up
}

int prog_parse(char *command) {

	char* action = strtok(strdup(command), " ");
	if(! strcmp(action, "help")) {
		recovery_cmd_help();
	} else if(! strcmp(action, "exit") || ! strcmp(action, "e")) {

		free(action);
		return -1;
	} else if (! strcmp(action, "batch")) {

		char* filename = strtok(NULL, " ");
		if (filename != NULL)
			prog_batch(filename);
	
	} else if (! strcmp(action, "auto-boot") ||! strcmp(action, "up")) {
		device_autoboot();
	} else {
		fprintf(stdout, "Command not found\n");
		recovery_cmd_help();
	}

	free(action);
	return 0;
}

int prog_batch(char *filename) {

	//max command length
	char line[0x200];
	FILE* script = fopen(filename, "rb");

	if (script == NULL) {
		fprintf(stdout, "[Program] Unable to find batch file.\n");
		return -1;
	}

	fprintf(stdout, "\n");

	while (fgets(line, 0x200, script) != NULL) {

		if(!((line[0]=='/') && (line[1]=='/'))) {

			if(line[0] == '/') {

				fprintf(stdout, "[Program] Running command: %s", line);

				int offset = (strlen(line) - 1);

				while(offset > 0) {

					if (line[offset] == 0x0D || line[offset] == 0x0A) line[offset--] = 0x00;
					else break;
				};

				prog_parse(&line[1]);
			}
			else {

				char *command[1];
				command[0] = line;
				device_sendcmd(command);
			}
		}
	}

	fclose(script);
	return 0;
}


int prog_console(char* logfile) {

	if(libusb_set_configuration(device, 1) < 0) {

		fprintf(stdout, "[Program] Error setting configuration.\n");
		return -1;
	}

	if(libusb_claim_interface(device, 0) < 0) {

		fprintf(stdout, "[Program] Error claiming interface.\n");
		return -1;
	}

	if(libusb_claim_interface(device, 1) < 0) {

		fprintf(stdout, "[Program] Error claiming interface.\n");
		return -1;
	}

	if(libusb_set_interface_alt_setting(device, 1, 1) < 0) {

		fprintf(stdout, "[Program] Error claiming alt interface.\n");
		return -1;
	}

	char* buffer = malloc(BUF_SIZE);
	if(buffer == NULL) {

		fprintf(stdout, "[Program] Error allocating memory.\n");
		return -1;
	}

	FILE* fd = NULL;
	if(logfile != NULL) {

		fd = fopen(logfile, "w");
		if(fd == NULL) {
			fprintf(stdout, "[Program] Unable to open log file.\n");
			free(buffer);
			return -1;
		}
	}

	fprintf(stdout, "[Program] Attached to Recovery Console.\n");

	if (logfile)
		fprintf(stdout, "[Program] Output being logged to: %s.\n", logfile);

	while(1) {

		int bytes = device_receive(buffer, BUF_SIZE);

		if (bytes>0) {
			int i;

			for(i = 0; i < bytes; ++i) {

				fprintf(stdout, "%c", buffer[i]);
				if(fd) fprintf(fd, "%c", buffer[i]);
			}
		}

		char *command = readline("ibooter> ");

		if (command != NULL) {

			add_history(command);

			if(fd) fprintf(fd, ">%s\n", command);

			if (command[0] == '/') {

				if (prog_parse(&command[1]) < 0) {

					free(command);
					break;
				}

			} else {

				device_sendcmd(&command);

				char* action = strtok(strdup(command), " ");

				if (! strcmp(action, "getenv")) {
					char response[0x200];
					memset(response, 0, sizeof(response));
					libusb_control_transfer(device, 0xC0, 0, 0, 0, (unsigned char*)response, 0x200, 1000);
					fprintf(stdout, "Env: %s\n", response);

				}

				if (! strcmp(action, "reboot"))
					return -1;
			}
		}
		free(command);
	}

	free(buffer);
	if(fd) fclose(fd);
	libusb_release_interface(device, 1);
	return 0;
}

int init_recovery() {

	prog_init();

	(void) signal(SIGTERM, prog_exit);
	(void) signal(SIGQUIT, prog_exit);
	(void) signal(SIGINT, prog_exit);

	if (device == NULL) {
		fprintf(stdout, "[Device] Connection failed.\n");
		exit(1);
		return -1;
	}

	fprintf(stdout, "[X] Connected.\n");
	return 0;
}

int close_recovery ()
{
	prog_exit();
	exit(0);
}
